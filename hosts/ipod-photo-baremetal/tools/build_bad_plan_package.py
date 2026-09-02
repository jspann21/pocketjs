#!/usr/bin/env python3
"""Build a container-valid package with one deliberately inadmissible plan."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

MAGIC = 0x544B4350
VERSION = 1
ALIGN = 16
HEADER_SIZE = 16
VARIANT_SIZE = 40
SECTION_SIZE = 16
SECTION_PLAN = 2
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


def mutate_plan(package: bytes) -> bytes:
    output = bytearray(package)
    if len(output) < HEADER_SIZE + VARIANT_SIZE + 8:
        raise ValueError("package is truncated")
    magic, version, manifest_len, variants = struct.unpack_from("<4I", output)
    if (magic, version, variants) != (MAGIC, VERSION, 1):
        raise ValueError("package header mismatch")
    if struct.unpack_from("<Q", output, len(output) - 8)[0] != fnv1a64(output[:-8]):
        raise ValueError("input package footer hash mismatch")

    variant = align(HEADER_SIZE + manifest_len)
    if variant + VARIANT_SIZE > len(output) - 8:
        raise ValueError("package variant is truncated")
    target = bytes(output[variant:variant + 16]).split(b"\0", 1)[0]
    host_abi, section_count, section_table = struct.unpack_from(
        "<3I", output, variant + 16
    )
    if target != b"ipod-photo" or host_abi != 1:
        raise ValueError("input package target mismatch")
    if section_table + section_count * SECTION_SIZE > len(output) - 8:
        raise ValueError("package section table is truncated")

    payloads: list[bytes] = []
    original_payloads: list[bytes] = []
    plan_count = 0
    old = b'"logical":[220,176]'
    new = b'"logical":[221,176]'
    for index in range(section_count):
        kind, _reserved, offset, length = struct.unpack_from(
            "<4I", output, section_table + index * SECTION_SIZE
        )
        if offset + length > len(output) - 8:
            raise ValueError("package section is truncated")
        original_payloads.append(bytes(output[offset:offset + length]))
        if kind == SECTION_PLAN:
            plan_count += 1
            plan = bytes(output[offset:offset + length])
            if plan.count(old) != 1:
                raise ValueError("expected one 220x176 logical viewport")
            output[offset:offset + length] = plan.replace(old, new, 1)
        payloads.append(bytes(output[offset:offset + length]))

    if plan_count != 1:
        raise ValueError("expected exactly one plan section")
    if struct.unpack_from("<Q", output, variant + 32)[0] != fnv1a64(*original_payloads):
        raise ValueError("input package variant hash mismatch")
    struct.pack_into("<Q", output, variant + 32, fnv1a64(*payloads))
    struct.pack_into("<Q", output, len(output) - 8, fnv1a64(output[:-8]))
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    package = mutate_plan(args.input.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    print(f"bad-plan package: {len(package)} bytes, FNV-1a64 0x{fnv1a64(package[:-8]):016x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
