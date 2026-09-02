#!/usr/bin/env python3
"""Build the fixed PocketJS persistence-gate seed slots."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

SECTOR_BYTES = 512
MAGIC = b"PJSSTATE"
COMMITTED = 0x434F4D54


def record(generation: int, payload: int, committed: bool) -> bytes:
    output = bytearray(SECTOR_BYTES)
    output[:8] = MAGIC
    struct.pack_into("<IIII", output, 8, 1, generation, payload,
                     COMMITTED if committed else 0)
    struct.pack_into("<I", output, SECTOR_BYTES - 4,
                     zlib.crc32(output[:-4]) & 0xFFFFFFFF)
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "STATE0.BIN").write_bytes(record(1, 1, True))
    (args.output / "STATE1.BIN").write_bytes(record(0, 0, False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
