from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


pack_ipod = load("pack_ipod", ROOT / "tools" / "pack_ipod.py")
firmware_evidence = load("firmware_evidence", ROOT / "tools" / "firmware_evidence.py")
verify_image = load("verify_image", ROOT / "tools" / "verify_image.py")


class PackTests(unittest.TestCase):
    def test_ipco_padding_and_checksum(self):
        image = b"abcde"
        result = pack_ipod.pack(image)
        checksum, model = struct.unpack_from(">I4s", result)
        self.assertEqual(model, b"ipco")
        self.assertEqual(result[8:], b"abcde\0\0\0")
        self.assertEqual(checksum, (3 + sum(result[8:])) & 0xFFFFFFFF)

    def test_empty_image_rejected(self):
        with self.assertRaises(ValueError):
            pack_ipod.pack(b"")


class VectorTests(unittest.TestCase):
    def test_forward_arm_branch(self):
        # At address zero, PC is 8; immediate 6 reaches 0x20.
        self.assertEqual(verify_image.decode_arm_b(0xEA000006, 0), 0x20)

    def test_backward_arm_branch_sign_extension(self):
        address = 0x100
        target = 0x20
        immediate = ((target - (address + 8)) >> 2) & 0x00FFFFFF
        self.assertEqual(verify_image.decode_arm_b(0xEA000000 | immediate, address), target)

    def test_branch_with_link_rejected(self):
        self.assertIsNone(verify_image.decode_arm_b(0xEB000006, 0))

    def test_conditional_branch_rejected(self):
        self.assertIsNone(verify_image.decode_arm_b(0x1A000006, 0))

    def test_non_branch_rejected(self):
        self.assertIsNone(verify_image.decode_arm_b(0xE1A00000, 0))


class FirmwareEvidenceTests(unittest.TestCase):
    def test_directory_metadata(self):
        image = bytearray(0x5000)
        payload = b"hello"
        device_offset = 0x4400
        file_offset = device_offset - 0x600
        image[file_offset : file_offset + len(payload)] = payload
        struct.pack_into(
            "<4s4s8I", image, 0x3A00,
            b"!ATA", b"soso", 0, device_offset, len(payload),
            0x10000000, 0, sum(payload), 0x6013, 0x048D0040,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            path.write_bytes(image)
            report = firmware_evidence.inspect(path)
        entry = report["entries"][0]
        self.assertEqual(entry["display_type"], "osos")
        self.assertEqual(entry["file_offset"], file_offset)
        self.assertEqual(entry["cipher_or_plain_additive_checksum"], sum(payload))


if __name__ == "__main__":
    unittest.main()
