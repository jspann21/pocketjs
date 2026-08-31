from pathlib import Path
import importlib.util
import struct

MODULE = Path(__file__).parents[1] / "tools" / "pack_ipod.py"
spec = importlib.util.spec_from_file_location("pack_ipod", MODULE)
assert spec and spec.loader
pack_ipod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack_ipod)


def test_round_trip() -> None:
    image = struct.pack("<8I", *([0xEA000000] * 8)) + bytes(range(256))
    payload = pack_ipod.encode(image)
    assert payload[4:8] == b"ipco"
    assert pack_ipod.decode(payload) == image


def test_checksum_rejects_damage() -> None:
    image = struct.pack("<8I", *([0xEA000000] * 8))
    payload = bytearray(pack_ipod.encode(image))
    payload[-1] ^= 1
    try:
        pack_ipod.decode(bytes(payload))
    except ValueError as error:
        assert "checksum" in str(error)
    else:
        raise AssertionError("damaged payload was admitted")


if __name__ == "__main__":
    test_round_trip()
    test_checksum_rejects_damage()
    print("ipco packer: OK")
