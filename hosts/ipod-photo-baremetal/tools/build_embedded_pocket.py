#!/usr/bin/env python3
"""Build the deterministic embedded A1099 recovery `.pocket` package.

The format authority is contracts/spec/pocket-package.ts. This script emits a
single `ipod-photo` variant and an aligned C byte array, then parses and hashes
the result again before returning success.
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

MAGIC = 0x544B4350
VERSION = 1
ALIGN = 16
HEADER_SIZE = 16
VARIANT_SIZE = 40
SECTION_SIZE = 16
TARGET_BYTES = 16
TARGET = "ipod-photo"
HOST_ABI = 1

SECTION_IDENTITY = 1
SECTION_PLAN = 2
SECTION_JS = 3

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
FNV_MASK = 0xFFFFFFFFFFFFFFFF


def align(value: int) -> int:
    return (value + ALIGN - 1) & ~(ALIGN - 1)


def fnv1a64(*chunks: bytes) -> int:
    value = FNV_OFFSET
    for chunk in chunks:
        for byte in chunk:
            value ^= byte
            value = (value * FNV_PRIME) & FNV_MASK
    return value


def identity_bytes(output: str, app_id: str, title: str) -> bytes:
    result = bytearray()
    for value in (output, app_id, title):
        encoded = value.encode("utf-8")
        if len(encoded) > 0xFFFF:
            raise ValueError("identity value exceeds u16 length")
        result += struct.pack("<H", len(encoded))
        result += encoded
    return bytes(result)


def build_package(source: bytes) -> bytes:
    if b"\0" in source:
        raise ValueError("JavaScript source contains an interior NUL")

    manifest = json.dumps(
        {
            "id": "a1099-runtime-smoke",
            "title": "PocketJS A1099 Runtime",
            "version": "0.1.0",
        },
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    plan = json.dumps(
        {
            "output": "a1099-runtime-smoke",
            "features": {
                "input.buttons": True,
                "text.glyphs.baked": True,
            },
            "target": {"hostAbi": HOST_ABI, "id": TARGET},
            "viewport": {
                "logical": [220, 176],
                "physical": [220, 176],
                "policy": "fixed",
                "presentation": "native",
                "rasterDensity": 1,
            },
        },
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    sections = [
        (SECTION_IDENTITY, identity_bytes(
            "a1099-runtime-smoke",
            "a1099-runtime-smoke",
            "PocketJS A1099 Runtime",
        )),
        (SECTION_PLAN, plan),
        (SECTION_JS, source + b"\0"),
    ]

    manifest_offset = HEADER_SIZE
    variant_offset = align(manifest_offset + len(manifest))
    section_table_offset = variant_offset + VARIANT_SIZE
    cursor = align(section_table_offset + len(sections) * SECTION_SIZE)
    payload_offsets: list[int] = []
    for _, payload in sections:
        payload_offsets.append(cursor)
        cursor = align(cursor + len(payload))
    total = cursor + 8

    output = bytearray(total)
    struct.pack_into("<4I", output, 0, MAGIC, VERSION, len(manifest), 1)
    output[manifest_offset : manifest_offset + len(manifest)] = manifest

    target = TARGET.encode("utf-8")
    if len(target) >= TARGET_BYTES:
        raise ValueError("target id does not fit package table")
    output[variant_offset : variant_offset + len(target)] = target
    struct.pack_into("<3I", output, variant_offset + 16, HOST_ABI, len(sections), section_table_offset)
    struct.pack_into("<Q", output, variant_offset + 32, fnv1a64(*(p for _, p in sections)))

    for index, ((kind, payload), payload_offset) in enumerate(zip(sections, payload_offsets, strict=True)):
        entry = section_table_offset + index * SECTION_SIZE
        struct.pack_into("<4I", output, entry, kind, 0, payload_offset, len(payload))
        output[payload_offset : payload_offset + len(payload)] = payload

    struct.pack_into("<Q", output, total - 8, fnv1a64(bytes(output[:-8])))
    return bytes(output)


def verify_container(package: bytes) -> bytes:
    if len(package) < HEADER_SIZE + VARIANT_SIZE + 8:
        raise ValueError("package is truncated")
    magic, version, manifest_len, variants = struct.unpack_from("<4I", package, 0)
    if (magic, version, variants) != (MAGIC, VERSION, 1):
        raise ValueError("package header mismatch")
    if struct.unpack_from("<Q", package, len(package) - 8)[0] != fnv1a64(package[:-8]):
        raise ValueError("package footer hash mismatch")
    variant = align(HEADER_SIZE + manifest_len)
    if variant + VARIANT_SIZE > len(package) - 8:
        raise ValueError("package variant is truncated")
    raw_target = package[variant : variant + TARGET_BYTES].split(b"\0", 1)[0].decode("utf-8")
    host_abi, section_count, section_table = struct.unpack_from("<3I", package, variant + 16)
    if raw_target != TARGET or host_abi != HOST_ABI or section_count == 0:
        raise ValueError("package variant mismatch")
    if section_table + section_count * SECTION_SIZE > len(package) - 8:
        raise ValueError("package section table is truncated")
    payloads: list[bytes] = []
    javascript = None
    for index in range(section_count):
        kind, _reserved, offset, length = struct.unpack_from(
            "<4I", package, section_table + index * SECTION_SIZE
        )
        payload = package[offset : offset + length]
        if len(payload) != length:
            raise ValueError("package section is truncated")
        payloads.append(payload)
        if kind == SECTION_JS:
            javascript = payload
    if struct.unpack_from("<Q", package, variant + 32)[0] != fnv1a64(*payloads):
        raise ValueError("variant hash mismatch")
    if javascript is None or len(javascript) < 2 or javascript[-1] != 0:
        raise ValueError("package JavaScript is missing or not NUL-terminated")
    return javascript


def verify_package(package: bytes, source: bytes) -> None:
    javascript = verify_container(package)
    if javascript != source + b"\0":
        raise ValueError("JavaScript section mismatch")


def emit_c(package: bytes, output: Path) -> None:
    lines = [
        "/* Generated by tools/build_embedded_pocket.py; do not edit. */",
        "#include <stdint.h>",
        "",
        "const uint8_t pjs_embedded_package[]",
        "    __attribute__((aligned(16), section(\".rodata.pjs_package\"))) = {",
    ]
    for offset in range(0, len(package), 12):
        chunk = package[offset : offset + 12]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    lines += [
        "};",
        f"const uint32_t pjs_embedded_package_length = {len(package)}u;",
        "",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--source", type=Path)
    inputs.add_argument("--input-package", type=Path)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--c-output", type=Path, required=True)
    args = parser.parse_args()

    if args.input_package is not None:
        package = args.input_package.read_bytes()
        verify_container(package)
    else:
        source = args.source.read_bytes()
        package = build_package(source)
        verify_package(package, source)
    args.package.parent.mkdir(parents=True, exist_ok=True)
    args.package.write_bytes(package)
    emit_c(package, args.c_output)
    print(f"embedded package: {len(package)} bytes, FNV-1a64 0x{fnv1a64(package[:-8]):016x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
