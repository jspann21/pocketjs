#!/usr/bin/env python3
"""Create a machine-readable report for verified A1099 probe builds."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path

from pack_ipod import MODEL, checksum, decode
from verify_build import symbols_from_nm


@dataclass(frozen=True)
class Variant:
    name: str
    lcd_type: int
    handoff: bool
    build: Path


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def symbols(elf: Path) -> dict[str, int]:
    completed = subprocess.run(
        ["nm", "-n", str(elf)],
        check=True,
        text=True,
        capture_output=True,
        timeout=15,
    )
    return symbols_from_nm(completed.stdout)


def parse_spec(value: str, *, matrix: bool) -> Variant:
    fields = value.split(",", 3)
    if matrix:
        if len(fields) != 2:
            raise argparse.ArgumentTypeError("matrix must be LCD_TYPE,BUILD_DIR")
        lcd, build = fields
        return Variant(name=lcd, lcd_type=int(lcd), handoff=True, build=Path(build))
    if len(fields) != 4:
        raise argparse.ArgumentTypeError(
            "variant must be NAME,LCD_TYPE,HANDOFF(0|1),BUILD_DIR"
        )
    name, lcd, handoff, build = fields
    if handoff not in {"0", "1"}:
        raise argparse.ArgumentTypeError("handoff must be 0 or 1")
    return Variant(name=name, lcd_type=int(lcd), handoff=handoff == "1", build=Path(build))


def build_facts(spec: Variant) -> dict[str, object]:
    flat_path = spec.build / "pocketjs-a1099-probe.bin"
    ipod_path = spec.build / "pocketjs-a1099-probe.ipod"
    elf_path = spec.build / "pocketjs-a1099-probe.elf"
    flat = flat_path.read_bytes()
    wrapped = ipod_path.read_bytes()
    if decode(wrapped) != flat:
        raise SystemExit(f"{spec.name}: wrapper does not contain the flat image")
    sym = symbols(elf_path)
    required = {
        "_start",
        "reset_handler",
        "cache_clean_wait",
        "cache_disable",
        "remap_stub_start",
        "remap_target",
        "remap_stub_end",
        "post_remap",
        "__image_end",
        "__bss_start",
        "__bss_end",
        "__stack_top",
        "__heap_start",
        "__ram_end",
    }
    missing = sorted(required - sym.keys())
    if missing:
        raise SystemExit(f"{spec.name}: missing symbols: {', '.join(missing)}")
    vectors = struct.unpack_from("<8I", flat)
    return {
        "lcdType": spec.lcd_type,
        "handoffSignature": spec.handoff,
        "flatBytes": len(flat),
        "flatSha256": sha256(flat),
        "ipodBytes": len(wrapped),
        "ipodSha256": sha256(wrapped),
        "ipodModel": MODEL.decode("ascii"),
        "ipodChecksum": f"0x{checksum(flat):08X}",
        "vectors": [f"0x{word:08X}" for word in vectors],
        "resetOffset": sym["reset_handler"],
        "cacheCleanWaitOffset": sym["cache_clean_wait"],
        "cacheDisableOffset": sym["cache_disable"],
        "remapStubOffset": sym["remap_stub_start"],
        "remapStubBytes": sym["remap_stub_end"] - sym["remap_stub_start"],
        "storedImageEnd": sym["__image_end"],
        "bssStart": sym["__bss_start"],
        "bssEnd": sym["__bss_end"],
        "stackTop": sym["__stack_top"],
        "heapStart": sym["__heap_start"],
        "ramEnd": sym["__ram_end"],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--variant", action="append", default=[])
    parser.add_argument("--matrix", action="append", default=[])
    args = parser.parse_args()
    variants = [parse_spec(value, matrix=False) for value in args.variant]
    matrix = [parse_spec(value, matrix=True) for value in args.matrix]
    if not variants:
        raise SystemExit("at least one --variant is required")

    report = {
        "formatVersion": 2,
        "target": "ipod-photo-a1099",
        "hostAbi": 8,
        "bootstrap": {
            "copSleepWaitBounded": True,
            "cacheCleanBeforeDisable": True,
            "cacheCleanWaitBounded": True,
            "sdramRemapStubCopiedToIram": True,
        },
        "variants": {spec.name: build_facts(spec) for spec in variants},
        "compileMatrix": {
            spec.name: {
                key: value
                for key, value in build_facts(spec).items()
                if key in {"flatBytes", "flatSha256", "ipodBytes", "ipodSha256"}
            }
            for spec in matrix
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
