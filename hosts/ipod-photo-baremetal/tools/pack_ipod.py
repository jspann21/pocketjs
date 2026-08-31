#!/usr/bin/env python3
"""Pack a flat A1099 image into the standard ipco transport wrapper."""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

MODEL = b"ipco"
SEED = 3


def pack(image: bytes) -> bytes:
    if not image:
        raise ValueError("image is empty")
    padded = image + b"\0" * ((-len(image)) & 3)
    checksum = (SEED + sum(padded)) & 0xFFFFFFFF
    return struct.pack(">I4s", checksum, MODEL) + padded


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = pack(args.image.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    print(f"wrote {args.output} ({len(result)} bytes)")


if __name__ == "__main__":
    main()
