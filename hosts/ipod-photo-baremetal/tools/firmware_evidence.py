#!/usr/bin/env python3
"""Emit non-proprietary metadata and checksums from an iPod updater image."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

ENTRY = struct.Struct("<4s4s8I")
DIRECTORY_OFFSET = 0x3A00
DEVICE_TO_FILE_BIAS = 0x600


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inspect(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    entries: list[dict[str, object]] = []
    offset = DIRECTORY_OFFSET
    while offset + ENTRY.size <= len(data):
        dev, kind, image_id, device_offset, length, load_address, entry_offset, checksum, version, unknown = ENTRY.unpack_from(data, offset)
        if dev != b"!ATA":
            break
        file_offset = device_offset - DEVICE_TO_FILE_BIAS
        if file_offset < 0 or file_offset + length > len(data):
            raise ValueError(f"entry at 0x{offset:x} escapes the file")
        payload = data[file_offset : file_offset + length]
        entries.append(
            {
                "directory_offset": offset,
                "device": dev.decode("ascii"),
                "raw_type": kind.decode("ascii"),
                "display_type": kind[::-1].decode("ascii"),
                "id": image_id,
                "device_offset": device_offset,
                "file_offset": file_offset,
                "length": length,
                "load_address": load_address,
                "entry_offset": entry_offset,
                "expected_checksum": checksum,
                "cipher_or_plain_additive_checksum": sum(payload) & 0xFFFFFFFF,
                "version": version,
                "unknown": unknown,
                "sha256": digest(payload),
            }
        )
        offset += ENTRY.size
    if not entries:
        raise ValueError("no !ATA firmware directory found at 0x3a00")
    return {"file": path.name, "size": len(data), "sha256": digest(data), "entries": entries}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()
    report = inspect(args.firmware)
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
