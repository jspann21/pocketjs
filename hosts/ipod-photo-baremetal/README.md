# PocketJS iPod Photo A1099 — standalone firmware stage 1

This tree is the first **no-Rockbox runtime** artifact for PocketJS on the
220×176 iPod Photo/Color A1099 (PortalPlayer PP5020). It builds a flat ARMv4T
OS image and a standard `ipco` transport wrapper. It does not link Rockbox,
call the Rockbox plugin ABI, use a Rockbox scheduler, or depend on a Rockbox
framebuffer, filesystem, allocator, or driver at runtime.

Stage 1 is deliberately a board bring-up image, not QuickJS yet. It proves the
boot and display/input substrate that the complete PocketJS host will sit on:

- entry at image offset zero;
- CPU/COP discrimination and a bounded COP-sleep gate;
- SDRAM remap from native `0x10000000` to link address zero;
- eight live ARM vectors and separate exception-mode IRAM stacks;
- `.noinit` crash evidence across warm reboot when the loader preserves RAM;
- cache and IRQs held off until their ownership is qualified;
- both A1099 LCD command families selected from panel straps;
- synchronous 220×176 RGB565-swapped submission;
- click-wheel, buttons, and Hold polling;
- Menu + Play held for two seconds requests a hardware reboot;
- exact `ipco` checksum packaging and independent ELF/image verification.

## Build

LLVM 17 or a compatible Clang/LLD installation is sufficient:

```sh
make
```

Outputs are written under `build/`:

```text
pocketjs-a1099-bringup.elf
pocketjs-a1099-bringup.bin
pocketjs-a1099-bringup.ipod
pocketjs-a1099-bringup.map
pocketjs-a1099-bringup.dis
SHA256SUMS.txt
```

`make check` verifies the ELF architecture, entry, load bounds, absence of
dynamic/relocation sections, exact flat-image reproduction, all eight vector
branches, and the `ipco` model/checksum/padding contract. Host-side tooling is
covered by standard-library unit tests.

## Diagnostic screen

- The screen is divided into red, green, and blue bands.
- A white outer frame and black crosshair expose orientation and edge loss.
- Five top boxes are Menu, Left, Select, Right, and Play.
- The center box flashes yellow for clockwise and cyan for counter-clockwise.
- The lower-left box is orange while Hold is active.
- The lower-right box toggles every 500 ms as a liveness marker.
- Two lower-center boxes show the detected panel strap value in binary.

## Safety status

**Not hardware-tested. Do not install this as the only OSOS image.**

The first run must use the already-confirmed reversible boot path and a verified
off-device firmware-partition backup. This image intentionally preserves the
loader-qualified clock/panel state. Cold LCD power-up, battery/charging,
ATA/FAT32, USB/disk mode, cache, interrupts, shutdown, and recovery-package
ownership are later gates.

The runtime-independent path is:

```text
Apple boot ROM or reversible loader
    -> this crt0
    -> PP5020/A1099 HAL
    -> diagnostic main loop
```

The final path will be:

```text
Apple boot ROM -> PocketJS A1099 firmware -> embedded/recoverable .pocket app
```

See `docs/HARDWARE_GATES.md` and `docs/HARDWARE_TEST.md` before using the
binary on a device.
