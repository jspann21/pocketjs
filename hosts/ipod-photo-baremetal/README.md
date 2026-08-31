# PocketJS iPod Photo A1099 — standalone Phase 0

This directory contains the first **no-Rockbox-runtime** layer of the PocketJS
port for the 220×176 iPod Photo/Color A1099 (PortalPlayer PP5020). It builds an
original freestanding ARMv4T board probe and an `ipco` transport wrapper for a
reversible file-level bootloader handoff.

The default build is intentionally the same type-0 handoff path that produced
the successful physical probe:

```text
pocketjs-a1099-probe.ipod
reference bytes:   5,360
reference SHA-256: 652f4c86030a02f010603a015fb78bd18f3cbbd657e8313dd365cef1f45af141
```

The reference hash is a reproducibility sentinel, not permission to direct-
flash the firmware partition. The exact CI artifact selected for testing must
still pass the reversible hardware checklist.

## What Phase 0 owns

- eight ARM vectors at image offset zero;
- the `Rockbox\x01` compatibility tag at payload offset `0x20` for the installed
  file loader, with reset code at `0x100`;
- CPU/COP discrimination and a bounded COP-sleep gate;
- bounded inherited-cache clean, cache disable, and IRAM-resident SDRAM remap;
- separate exception stacks, `.noinit` crash evidence, and a guarded main stack;
- type-0 A1099 LCD transfer with bounded FIFO waits and 65,536-byte splitting;
- backlight, click-wheel, buttons, Hold, heartbeat, and reset-chord diagnostics;
- exact `ipco` packaging, ELF/image validation, and atomic install/restore tools.

It does not link Rockbox, call Rockbox services, mount storage, manage charging,
or run QuickJS yet. The installed bootloader is used only to load the file and
jump to offset zero.

## Build and verify

The CI workflow pins LLVM and builds the default P98/M9829 type-0 handoff image.
A local LLVM 17 toolchain can run the same gates:

```sh
make clean
make all
make test
```

Outputs are written under `build/`:

```text
pocketjs-a1099-probe.elf
pocketjs-a1099-probe.bin
pocketjs-a1099-probe.ipod
pocketjs-a1099-probe.map
pocketjs-a1099-probe.dis
```

The verifier checks vector targets, reset and handoff offsets, the copied remap
stub, cache-clean ordering, relocation/undefined-symbol absence, stored-image
and RAM bounds, wrapper checksum/model, and exact wrapper/body identity. Host
tests cover the packer, click-wheel decoder, Windows-safe transaction state,
interrupted-install recovery, and restore.

Other compile paths remain gated but are not first-test artifacts:

```sh
make clean LCD_TYPE=-1 all test       # GPIO-detected fallback
make clean HANDOFF_SIGNATURE=0 all test  # direct-image compile path only
```

## Reversible device test

Use `tools/handoff.py`; do not replace `rockbox.ipod` manually:

```sh
python3 tools/handoff.py install --mount /path/to/IPOD \
  --probe build/pocketjs-a1099-probe.ipod
python3 tools/handoff.py status --mount /path/to/IPOD
```

After the test, enter boot-ROM disk mode and restore:

```sh
python3 tools/handoff.py restore --mount /path/to/IPOD
```

Read `docs/HARDWARE_TEST.md`, `docs/REVERSIBLE_BOOT.md`, and
`docs/HARDWARE_GATES.md` before copying an artifact to the device.
