from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).parents[1]


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


pack = load("pack_ipod", ROOT / "tools" / "pack_ipod.py")
sys.modules["pack_ipod"] = pack
handoff = load("handoff", ROOT / "tools" / "handoff.py")


def image(signature: bool, fill: int) -> bytes:
    vectors = struct.pack("<8I", *([0xEA000000] * 8))
    tag = b"Rockbox\x01" if signature else bytes(8)
    body = vectors + tag + bytes([fill]) * 24
    return body + bytes((-len(body)) & 3)


def ordinary_transaction() -> None:
    with tempfile.TemporaryDirectory() as directory:
        mount = Path(directory)
        original = pack.encode(image(False, 0x11))
        probe = pack.encode(image(True, 0x22))
        target = mount / "rockbox.ipod"
        probe_path = mount / "probe.ipod"
        target.write_bytes(original)
        probe_path.write_bytes(probe)
        handoff.install(mount, probe_path)
        assert target.read_bytes() == probe
        assert (mount / "rockbox.ipod.pocketjs-backup").read_bytes() == original
        handoff.restore(mount)
        assert target.read_bytes() == original
        assert not (mount / "rockbox.ipod.pocketjs-backup").exists()
        assert not (mount / ".pocketjs-a1099-handoff.json").exists()


def interrupted_windows_preinstall() -> None:
    """Pin recovery from v1's read-only fsync failure on Windows."""

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        mount = root / ".rockbox"
        mount.mkdir()
        original = pack.encode(image(False, 0x33))
        probe = pack.encode(image(True, 0x44))
        target = mount / "rockbox.ipod"
        backup = mount / "rockbox.ipod.pocketjs-backup"
        stale_state = mount / "..pocketjs-a1099-handoff.json.new"
        probe_path = root / "probe.ipod"

        target.write_bytes(original)
        backup.write_bytes(original)
        stale_state.write_text("{}\n", encoding="utf-8")
        probe_path.write_bytes(probe)

        # Passing the volume root must discover .rockbox, prove the active and
        # backup hashes are identical, clean the interrupted transaction, and
        # proceed with a normal install.
        handoff.install(root, probe_path)
        assert target.read_bytes() == probe
        assert backup.read_bytes() == original
        assert not stale_state.exists()
        handoff.restore(root)
        assert target.read_bytes() == original


def main() -> None:
    ordinary_transaction()
    interrupted_windows_preinstall()
    print("handoff transactions: OK")


if __name__ == "__main__":
    main()
