#!/usr/bin/env python3
"""Structural verifier for the A1099 stage-one ELF, flat image, and ipco file."""
from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIIIIIII")
SECTION_HEADER = struct.Struct("<IIIIIIIIII")
PT_LOAD = 1
PT_DYNAMIC = 2
PT_INTERP = 3
SHT_RELA = 4
SHT_DYNAMIC = 6
SHT_REL = 9
PF_X = 1
EM_ARM = 40
ET_EXEC = 2
MODEL = b"ipco"
SEED = 3
SDRAM_LIMIT = 32 * 1024 * 1024
BOOTLOADER_LIMIT = 8 * 1024 * 1024


@dataclass(frozen=True)
class LoadSegment:
    offset: int
    vaddr: int
    paddr: int
    filesz: int
    memsz: int
    flags: int


def parse_elf(data: bytes) -> tuple[int, list[LoadSegment]]:
    if len(data) < ELF_HEADER.size:
        raise ValueError("ELF header is truncated")
    fields = ELF_HEADER.unpack_from(data)
    ident = fields[0]
    if ident[:4] != b"\x7fELF" or ident[4] != 1 or ident[5] != 1:
        raise ValueError("expected ELF32 little-endian input")
    e_type, e_machine, e_version, e_entry = fields[1:5]
    e_phoff, e_shoff = fields[5], fields[6]
    e_ehsize, e_phentsize, e_phnum = fields[8], fields[9], fields[10]
    e_shentsize, e_shnum = fields[11], fields[12]
    if e_type != ET_EXEC or e_machine != EM_ARM or e_version != 1:
        raise ValueError("expected an ARM ELF executable")
    if e_entry != 0:
        raise ValueError(f"entry is 0x{e_entry:x}, expected zero")
    if e_ehsize != ELF_HEADER.size or e_phentsize != PROGRAM_HEADER.size:
        raise ValueError("unexpected ELF/program-header size")

    loads: list[LoadSegment] = []
    for index in range(e_phnum):
        at = e_phoff + index * e_phentsize
        if at + PROGRAM_HEADER.size > len(data):
            raise ValueError("program-header table is truncated")
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, flags, _align = (
            PROGRAM_HEADER.unpack_from(data, at)
        )
        if p_type in (PT_DYNAMIC, PT_INTERP):
            raise ValueError("dynamic/interpreter program header is forbidden")
        if p_type == PT_LOAD:
            if p_offset + p_filesz > len(data) or p_filesz > p_memsz:
                raise ValueError("invalid PT_LOAD extent")
            if p_vaddr != p_paddr:
                raise ValueError("stage-one PT_LOAD VMA and physical address must match")
            if p_paddr + p_memsz > SDRAM_LIMIT:
                raise ValueError("PT_LOAD escapes low 32 MiB SDRAM")
            loads.append(LoadSegment(p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, flags))

    if not loads or min(segment.paddr for segment in loads) != 0:
        raise ValueError("no PT_LOAD starts at physical address zero")
    if not any(
        segment.paddr <= e_entry < segment.paddr + segment.memsz and
        (segment.flags & PF_X) != 0
        for segment in loads
    ):
        raise ValueError("entry address is not inside an executable PT_LOAD")

    if e_shnum:
        if e_shentsize != SECTION_HEADER.size:
            raise ValueError("unexpected section-header size")
        for index in range(e_shnum):
            at = e_shoff + index * e_shentsize
            if at + SECTION_HEADER.size > len(data):
                raise ValueError("section-header table is truncated")
            section_type = SECTION_HEADER.unpack_from(data, at)[1]
            if section_type in (SHT_RELA, SHT_DYNAMIC, SHT_REL):
                raise ValueError("relocation/dynamic section is forbidden")

    return e_entry, loads


def expected_flat(elf: bytes, loads: list[LoadSegment]) -> bytes:
    end = max(segment.paddr + segment.filesz for segment in loads)
    result = bytearray(end)
    written = bytearray(end)
    for segment in loads:
        payload = elf[segment.offset : segment.offset + segment.filesz]
        start = segment.paddr
        for index, value in enumerate(payload, start):
            if written[index] and result[index] != value:
                raise ValueError("overlapping PT_LOAD file ranges disagree")
            result[index] = value
            written[index] = 1
    return bytes(result)


def decode_arm_b(word: int, address: int) -> int | None:
    """Decode an unconditional ARM-state B (not BL) target, or return None."""
    if (word & 0xFF000000) != 0xEA000000:
        return None
    immediate = word & 0x00FFFFFF
    if immediate & 0x00800000:
        immediate -= 0x01000000
    return address + 8 + (immediate << 2)


def verify(elf_path: Path, bin_path: Path, ipod_path: Path) -> None:
    elf = elf_path.read_bytes()
    image = bin_path.read_bytes()
    transport = ipod_path.read_bytes()
    _entry, loads = parse_elf(elf)

    reconstructed = expected_flat(elf, loads)
    if image != reconstructed:
        raise ValueError("flat image does not exactly reproduce file-backed PT_LOAD bytes")
    if len(image) < 32:
        raise ValueError("flat image is too small for vectors")
    if len(image) > BOOTLOADER_LIMIT:
        raise ValueError("flat image exceeds the installed bootloader's 8 MiB limit")
    for index in range(8):
        address = index * 4
        word = struct.unpack_from("<I", image, address)[0]
        target = decode_arm_b(word, address)
        if target is None:
            raise ValueError(
                f"vector {index} is not an unconditional ARM B instruction: 0x{word:08x}"
            )
        if target < 32 or target >= len(image) or (target & 3) != 0:
            raise ValueError(
                f"vector {index} branches outside aligned image code: 0x{target:08x}"
            )

    if len(transport) < 8:
        raise ValueError("ipco transport is truncated")
    checksum, model = struct.unpack_from(">I4s", transport)
    payload = transport[8:]
    if model != MODEL:
        raise ValueError(f"transport model is {model!r}, expected {MODEL!r}")
    if checksum != (SEED + sum(payload)) & 0xFFFFFFFF:
        raise ValueError("transport additive checksum does not match")
    padded = image + b"\0" * ((-len(image)) & 3)
    if payload != padded:
        raise ValueError("transport payload is not the exact image plus zero alignment padding")

    print(
        f"verified: ARM ELF entry=0, {len(loads)} PT_LOAD segment(s), "
        f"{len(image)}-byte image, 8 in-range unconditional vector branches, "
        f"valid ipco checksum 0x{checksum:08x}"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("image", type=Path)
    parser.add_argument("ipod", type=Path)
    args = parser.parse_args()
    verify(args.elf, args.image, args.ipod)


if __name__ == "__main__":
    main()
