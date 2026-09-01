#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def symbols(elf: Path, nm: str) -> dict[str, int]:
    result: dict[str, int] = {}
    output = subprocess.check_output([nm, "-n", str(elf)], text=True)
    for line in output.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3:
            try:
                result[fields[2]] = int(fields[0], 16)
            except ValueError:
                pass
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--ipod", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--nm", default="nm")
    args = parser.parse_args()

    image = args.bin.read_bytes()
    wrapper = args.ipod.read_bytes()
    if wrapper[4:8] != b"ipco" or wrapper[8:] != image:
        raise SystemExit("invalid Phase-1 ipco wrapper")
    syms = symbols(args.elf, args.nm)
    required = [
        "_start", "reset_handler", "vector_irq", "irq_dispatch",
        "timer_irq_init", "pjs_heap_init", "PJS_CORE_BACKEND_RUST",
        "pjs_core_init", "pjs_core_step",
        "pjs_core_render_damage", "lcd_present_damage", "kernel_main_phase1", "__image_end", "__heap_start", "__ram_end",
    ]
    missing = [name for name in required if name not in syms]
    if missing:
        raise SystemExit("missing report symbols: " + ", ".join(missing))

    report = {
        "schema": 1,
        "target": "ipod-photo-a1099-phase1",
        "commit": args.commit,
        "coreBackend": "pocketjs-core-rust",
        "image": {
            "bytes": len(image),
            "sha256": sha(args.bin),
            "firstWord": f"0x{struct.unpack_from('<I', image, 0)[0]:08x}",
        },
        "ipod": {
            "bytes": len(wrapper),
            "sha256": sha(args.ipod),
            "model": wrapper[4:8].decode("ascii"),
            "checksum": f"0x{struct.unpack_from('>I', wrapper, 0)[0]:08x}",
        },
        "symbols": {name: f"0x{syms[name]:08x}" for name in required},
        "memory": {
            "storedBytes": syms["__image_end"],
            "heapStart": f"0x{syms['__heap_start']:08x}",
            "ramEnd": f"0x{syms['__ram_end']:08x}",
        },
        "hardwareQualified": False,
        "installation": "reversible /rockbox.ipod handoff only",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
