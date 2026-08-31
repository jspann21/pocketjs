#!/usr/bin/env python3
"""Reproduce the A1099 firmware facts used by the standalone PocketJS port.

The script reads user-supplied firmware files and emits facts only. It never
modifies an input and does not package Apple bytes into PocketJS artifacts.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import plistlib
import struct
import zipfile
from pathlib import Path
from typing import Any

SECTOR = 0x200
STRIPPED_PREFIX = 0x800
DIRECTORY_OFFSET = 0x3A00
SECURITY_OFFSETS = (0x5, 0x25, 0x6F, 0x69, 0x15, 0x4D, 0x40, 0x34)
SECURITY_CONSTANT = 0x54C3A298


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_fact(path: Path, data: bytes) -> dict[str, Any]:
    return {"path": path.name, "bytes": len(data), "sha256": sha256(data)}


def u32le(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def image_file_offset(device_offset: int) -> int:
    return SECTOR + device_offset - STRIPPED_PREFIX


def parse_directory(firmware: bytes) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    offset = DIRECTORY_OFFSET
    while offset + 40 <= len(firmware) and firmware[offset : offset + 4] == b"!ATA":
        raw = firmware[offset : offset + 40]
        identifier = raw[4:8].decode("ascii")
        flash, device, length, address, entry, checksum, version, load = struct.unpack_from(
            "<8I", raw, 8
        )
        file_offset = image_file_offset(device)
        payload = firmware[file_offset : file_offset + length]
        entries.append(
            {
                "directoryOffset": offset,
                "type": "!ATA",
                "id": identifier,
                "flash": flash,
                "deviceOffset": device,
                "fileOffset": file_offset,
                "length": length,
                "address": address,
                "entryOffset": entry,
                "checksum": checksum,
                "version": version,
                "loadAddress": load,
                "payloadSha256": sha256(payload),
                "encryptedByteSum": sum(payload) & 0xFFFFFFFF,
            }
        )
        offset += 40
    return entries


def test_marker(marker: int) -> bool:
    mask = (marker & 0xFF) * 0x01010101
    decrypted = (marker ^ mask) & 0xFFFFFFFF
    first = (decrypted >> 24) & 0xFF
    second = (decrypted >> 16) & 0xFF
    third = (decrypted >> 8) & 0xFF
    if first == 0 or not first < second < third:
        return False
    first &= 0xF
    second &= 0xF
    third &= 0xF
    return first > second > third and third != 0


def security_keys(block: bytes) -> list[dict[str, Any]]:
    found: list[dict[str, Any]] = []
    for index, word_offset in enumerate(SECURITY_OFFSETS):
        marker = u32le(block, word_offset * 4)
        if not test_marker(marker):
            continue
        position = (
            SECURITY_OFFSETS[index + 1] * 4 + 4
            if index < len(SECURITY_OFFSETS) - 1
            else SECURITY_OFFSETS[0] * 4 + 4
        )
        key = 0
        for _ in range(2):
            key = (marker ^ u32le(block, position) ^ SECURITY_CONSTANT) & 0xFFFFFFFF
            position += 4
        accumulator = 0x6F
        for count in range(2, 128, 2):
            left = u32le(block, count * 4)
            right = u32le(block, count * 4 + 4)
            combined = (left | (right >> 16)) & 0xFFFFFFFF
            merged = ((left & 0xFFFF) | right) & 0xFFFFFFFF
            accumulator = ((accumulator ^ combined) + merged) & 0xFFFFFFFF
        key = (key ^ accumulator) & 0xFFFFFFFF
        found.append(
            {
                "markerIndex": index,
                "marker": marker,
                "keyHex": struct.pack("<I", key).hex(),
            }
        )
    return found


def rc4(data: bytes, key: bytes) -> bytes:
    state = list(range(256))
    j = 0
    for i in range(256):
        j = (j + state[i] + key[i % len(key)]) & 0xFF
        state[i], state[j] = state[j], state[i]
    output = bytearray(len(data))
    i = 0
    j = 0
    for index, value in enumerate(data):
        i = (i + 1) & 0xFF
        j = (j + state[i]) & 0xFF
        state[i], state[j] = state[j], state[i]
        output[index] = value ^ state[(state[i] + state[j]) & 0xFF]
    return bytes(output)


def branch_target(word: int, address: int) -> int | None:
    if (word & 0x0E000000) != 0x0A000000 or (word >> 28) == 0xF:
        return None
    displacement = word & 0x00FFFFFF
    if displacement & 0x00800000:
        displacement -= 0x01000000
    return (address + 8 + displacement * 4) & 0xFFFFFFFF


def inspect_ipsw(path: Path, firmware: bytes) -> dict[str, Any]:
    data = path.read_bytes()
    result: dict[str, Any] = file_fact(path, data)
    with zipfile.ZipFile(path) as archive:
        result["members"] = [
            {"name": info.filename, "bytes": info.file_size} for info in archive.infolist()
        ]
        candidates = [info for info in archive.infolist() if info.file_size >= len(firmware)]
        matches = []
        for info in candidates:
            member = archive.read(info)
            location = member.find(firmware)
            if location >= 0:
                matches.append(
                    {
                        "member": info.filename,
                        "firmwareOffset": location,
                        "prefixSha256": sha256(member[:location]),
                    }
                )
        result["firmwareMatches"] = matches
        if "manifest.plist" in archive.namelist():
            result["manifest"] = plistlib.loads(archive.read("manifest.plist"))
    return result


def inspect_live_ipod(path: Path) -> dict[str, Any]:
    payload = path.read_bytes()
    result: dict[str, Any] = file_fact(path, payload)
    if len(payload) < 8:
        result["valid"] = False
        return result
    declared, model = struct.unpack(">I4s", payload[:8])
    image = payload[8:]
    actual = (3 + sum(image)) & 0xFFFFFFFF
    result.update(
        {
            "model": model.decode("ascii", "replace"),
            "declaredChecksum": declared,
            "calculatedChecksum": actual,
            "valid": model == b"ipco" and declared == actual,
            "imageBytes": len(image),
            "imageSha256": sha256(image),
            "firstWord": u32le(image, 0) if len(image) >= 4 else None,
        }
    )
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--soso", type=Path)
    parser.add_argument("--ipsw", type=Path)
    parser.add_argument("--live-ipod", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--decrypted-updater", type=Path)
    args = parser.parse_args()

    firmware = args.firmware.read_bytes()
    report: dict[str, Any] = {
        "firmware": file_fact(args.firmware, firmware),
        "directoryOffset": DIRECTORY_OFFSET,
        "entries": parse_directory(firmware),
    }

    entries = {entry["id"]: entry for entry in report["entries"]}
    soso_entry = entries.get("soso")
    if soso_entry:
        start = soso_entry["fileOffset"]
        soso = firmware[start : start + soso_entry["length"]]
        soso_entry["checksumMatches"] = (sum(soso) & 0xFFFFFFFF) == soso_entry["checksum"]
        vectors = [u32le(soso, index * 4) for index in range(8)]
        soso_entry["vectors"] = [
            {
                "address": 0x10000000 + index * 4,
                "isArmBranch": branch_target(word, 0x10000000 + index * 4) is not None,
                "branchTarget": branch_target(word, 0x10000000 + index * 4),
            }
            for index, word in enumerate(vectors)
        ]
        constants = (0xF000F000, 0x00003E00, 0x10000F84, 0x60000000, 0x70008A00)
        soso_entry["selectedLiteralOffsets"] = {
            f"0x{value:08x}": [
                offset
                for offset in range(0, len(soso) - 3, 4)
                if u32le(soso, offset) == value
            ][:32]
            for value in constants
        }
        if args.soso:
            extracted = args.soso.read_bytes()
            report["extractedSoso"] = file_fact(args.soso, extracted)
            report["extractedSoso"]["exactMatch"] = extracted == soso

    updater = entries.get("dpua")
    if updater:
        start = updater["fileOffset"]
        encrypted = firmware[start : start + updater["length"]]
        security_start = start - SECTOR
        security = firmware[security_start:start]
        keys = security_keys(security)
        updater["securityBlockOffset"] = security_start
        updater["securityBlockSha256"] = sha256(security)
        updater["keys"] = keys
        if len(keys) == 1:
            key = bytes.fromhex(keys[0]["keyHex"])
            decrypted = rc4(encrypted, key)
            updater["decryptedSha256"] = sha256(decrypted)
            updater["decryptedByteSum"] = sum(decrypted) & 0xFFFFFFFF
            updater["checksumMatches"] = updater["decryptedByteSum"] == updater["checksum"]
            if args.decrypted_updater:
                args.decrypted_updater.parent.mkdir(parents=True, exist_ok=True)
                args.decrypted_updater.write_bytes(decrypted)

    if args.ipsw:
        report["ipsw"] = inspect_ipsw(args.ipsw, firmware)
    if args.live_ipod:
        report["liveIpod"] = inspect_live_ipod(args.live_ipod)

    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
