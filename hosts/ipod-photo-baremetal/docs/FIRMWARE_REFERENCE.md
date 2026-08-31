# Verified A1099 firmware reference

The source tree contains no Apple firmware bytes. The following metadata was
re-derived from the user-owned private artifact on 2026-08-31.

## Complete updater image

```text
file:     iPod_5.1.2.1.bin
size:     6,514,176 bytes
SHA-256:  55845b4694263be104e8bfded72f11d1b1d5b9cbeec64f9ffaced80b0bcdc2f5
```

The firmware directory begins at file offset `0x3A00` and contains two `!ATA`
entries.

| Display type | Raw type | File offset | Length | Load address | Entry | Checksum |
|---|---|---:|---:|---:|---:|---:|
| `osos` | `soso` | `0x003E00` | `0x523514` | `0x10000000` | `0` | `0x1DE43EC5` |
| `aupd` | `dpua` | `0x527600` | `0x10EED4` | `0x10000000` | `0` | `0x0BC3A366` plaintext |

The OSOS additive checksum matches and its SHA-256 is:

```text
9321189b846a7317f4f575075696056e9a18c79644886a00055a402259c6fadc
```

The updater's 512-byte security sector yields one four-byte RC4 key. In-memory
decryption reproduced the directory plaintext checksum and produced SHA-256:

```text
9a54779a693a16e556d798bcd70d9333d8d75f39b690d6c921986a8be6b6fdde
```

## Boot evidence used by stage 1

The RetailOS reset vector is an ARM branch from offset `0` to `0x1EC`. Early
startup:

- executes in SDRAM's native `0x10000000` window;
- writes the MMAP table through `0xF000F000`;
- enters linked low memory at offset `0x238`;
- distinguishes CPU `0x55` and COP `0xAA` at `0x60000000`;
- assigns initial IRAM stacks near `0x40003FFC` and `0x40005FFC`;
- uses PP5020 physical-map access flags `0x0F84`.

The standalone implementation uses only those hardware/format facts and
independently written code. It does not copy or link Apple code.
