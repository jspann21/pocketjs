# A1099 firmware evidence

This report records facts extracted from the private, unmodified artifacts in
GitHub Actions run `33420820506`, artifact `9768806560`. No Apple firmware bytes
are included in this source tree.

## Artifact integrity

| File | Bytes | SHA-256 |
|---|---:|---|
| `iPod_5.1.2.1.bin` | 6,514,176 | `55845b4694263be104e8bfded72f11d1b1d5b9cbeec64f9ffaced80b0bcdc2f5` |
| `iPod_5.1.2.1.ipsw` | 3,831,893 | `03643928fd4b5d180f92396382680c48e745826b8b170bf2e298bef2bbe26464` |
| `RetailOS_1.2.1_soso.bin` | 5,387,540 | `9321189b846a7317f4f575075696056e9a18c79644886a00055a402259c6fadc` |
| `rockbox-current-live-20260831-131920.ipod` | 1,165,744 | `8dc29b572f0eeee69dfc9471fe6fae6beb2bf9ec35f15d6ffa3fb9b67e26f3d7` |

The downloaded Actions artifact ZIP is 16,902,972 bytes with SHA-256
`669c510fd5ca730c8ed8200983540bb45de2cc71f3fbc82c55e818781e915d09`.

## IPSW relation

The IPSW member `Firmware-5.4.2.1` is 6,516,224 bytes. The complete
`iPod_5.1.2.1.bin` occurs byte-for-byte at offset `0x800`; the prefix is the
Apple firmware-partition stop-sign/header area.

## Firmware directory

The stripped `.bin` directory begins at `0x3A00`; each entry is 40 bytes. Image
file offsets obey:

```text
file_offset = 0x200 + devOffset - 0x800
```

| ID | File offset | Length | Address | Entry | Checksum |
|---|---:|---:|---:|---:|---:|
| `soso` | `0x003E00` | `0x523514` | `0x10000000` | `0` | `0x1DE43EC5` |
| `dpua` | `0x527600` | `0x10EED4` | `0x10000000` | `0` | `0x0BC3A366` plaintext |

The additive checksum of `soso` is exactly `0x1DE43EC5`, and the extracted
`RetailOS_1.2.1_soso.bin` is byte-identical to that payload.

## Updater decryption

The 512-byte security block precedes `dpua` at `0x527400`. Its SHA-256 is
`1a4f781885c151c907c768a81e6404828d53301af1b057eedfa03065adc81790`.
The standard AUPD key derivation finds exactly one marker:

```text
marker index: 6
marker:       0x75B7DD0E
RC4 key:      74 e4 05 a3
```

The decrypted updater has SHA-256
`9a54779a693a16e556d798bcd70d9333d8d75f39b690d6c921986a8be6b6fdde`,
and its additive checksum is exactly the directory value `0x0BC3A366`.

## Boot facts

The `soso` directory entry's `addr` field is `0x10000000` and its entry
offset is zero. Interpreted in that native alias, the first vector branches to
reset code at `0x100001EC`. Early startup references the PP5020
MMAP registers at `0xF000F000`, the physical mapping value `0x10000F84`, the
processor identity register at `0x60000000`, and then transfers execution to a
low link-time address after remapping.

The standalone probe follows that verified contract without copying Apple code:
it uses its own offset-zero vectors and its own IRAM-resident remap stub.

## Hardware evidence selected for the first probe

- Color LCD bridge base: `0x70008A00`.
- Display: 220×176, 16-bit RGB565 with byte-swapped words.
- Click-wheel packet source: `0x7000C140`.
- Hold switch: GPIO A5, active low.
- Backlight PWM: `0x7000A010`; B3 gates the light.
- Device: A1099 / P98 / M9829, 60 GB model, currently iFlash + 64 GB SD,
  Windows/MBR/FAT32 formatting.
- Boot-ROM Select + Play disk mode is confirmed on the physical unit.

`tools/analyze_reference_firmware.py` reproduces the machine-readable
`FIRMWARE_EVIDENCE.json` from user-owned inputs.
