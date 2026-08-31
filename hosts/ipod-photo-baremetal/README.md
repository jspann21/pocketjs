# PocketJS iPod Photo A1099 — standalone firmware, hardware stage 1

This directory is the first **no-Rockbox** artifact for the PocketJS A1099
port. It is a flat PP5020 ARMv4T OS image plus an `ipco` transport wrapper.
There is no Rockbox plugin ABI, scheduler, framebuffer, filesystem, allocator,
or driver dependency.

This is intentionally a hardware bring-up image, not the JavaScript runtime
yet. It establishes the boot contract that every later PocketJS layer depends
on:

- image entry at offset zero;
- CPU/COP discrimination and COP parking;
- SDRAM remap from native `0x10000000` to link address zero;
- separate exception-mode stacks and a small `.noinit` crash record;
- interrupts and cache held off for deterministic first hardware tests;
- A1099 color-LCD bridge submission from a 220×176 swapped-RGB565 buffer;
- backlight ownership;
- click-wheel/buttons/hold polling;
- Menu + Play held for two seconds to reboot;
- a deterministic red/green/blue diagnostic screen;
- standard `ipco` additive-checksum packaging;
- a read-only tool for comparing Apple firmware images and ZIP archives.

## Build

The build is freestanding and works with LLVM 17 or a compatible Clang/LLD
installation:

```sh
make -C hosts/ipod-photo-baremetal
```

Outputs:

```text
build/pocketjs-a1099-bringup.elf
build/pocketjs-a1099-bringup.bin
build/pocketjs-a1099-bringup.ipod
build/pocketjs-a1099-bringup.map
```

`make check` verifies the ELF architecture/entry/load segments, all eight ARM
vector branches, absence of dynamic/relocation sections, exact flat-image
contents, transport model/checksum, zero padding, and host-side tool tests.

## What the screen means

- Upper, middle and lower bands are red, green and blue.
- Five boxes across the top represent Menu, Left, Select, Right and Play.
- White means pressed.
- The center box flashes yellow/cyan for clockwise/counter-clockwise wheel
  steps.
- The lower-left box is orange while Hold is active.
- The lower-right box toggles black/white every 500 ms as a liveness marker.

## Flashing policy

Do **not** make this your only boot image yet. The first hardware run should be
chainloaded or installed through a reversible dual-boot setup with a verified
firmware-partition backup. The code deliberately adopts the Apple loader's
known-safe clock and panel state rather than claiming complete cold-init,
power, ATA, USB or battery ownership.

The direct-OSOS gate is documented in `docs/HARDWARE_GATES.md`.

## Firmware archive inspection

The included tool works locally and emits only hashes/metadata—never extracted
or decrypted Apple code:

```sh
python3 hosts/ipod-photo-baremetal/tools/inspect_apple_firmware.py \
  ipod-photo-firmware.zip -o firmware-report.json
```

That JSON is sufficient to compare every file in an archive when binary
attachments are unavailable.
