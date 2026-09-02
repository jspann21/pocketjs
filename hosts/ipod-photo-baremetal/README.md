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

## Phase 1: kernel substrate and real retained core

Phase 1 is a separate artifact. It preserves the exact Phase-0 default target
while adding a context-preserving Timer1 IRQ path, deterministic 60 Hz
scheduler, bounded SDRAM allocator, telemetry-only PCF50605 access, and the
actual `pocketjs-core` retained UI plus RGB565 rasterizer compiled `no_std` for
ARMv4T.

```sh
make phase1-check
```

The output is:

```text
build-phase1/pocketjs-a1099-phase1-core.ipod
```

It is not hardware-qualified merely because it builds. Install only the exact
CI artifact through `tools/handoff.py`, after restoring and verifying the
original `rockbox.ipod`. Follow `docs/PHASE1_HARDWARE_TEST.md`. QuickJS, ATA,
FAT, audio, charger control, standby, and direct OSOS installation remain out
of scope for this gate.


## Phase 1 incremental renderer

`Makefile.phase1` builds `pocketjs-a1099-phase1-paced.ipod`. It runs the
real no-std Rust PocketJS core, keeps 60 Hz fixed simulation, limits DrawList
planning to 30 Hz, rasterizes only retained damage rectangles, and transfers
only those rectangles to the LCD. The expensive first full frame completes
before Timer1 starts. PCF50605/I2C telemetry remains disabled for this gate.

## Campaign 3: power and complete lifecycle

The next standalone hardware candidate is documented in
`docs/PHASE1_POWER_LIFECYCLE.md` and packaged in
`phase1-power-lifecycle-simple`. Build it with
`Makefile.phase1 POWER_LIFECYCLE_GATE=1 NATIVE_KERNEL_GATE=1
LINEAGE_GATE=1`. This candidate adds bounded PCF50605 recovery, calibrated
and debounced battery policy, filtered USB/FireWire/charging state,
conservative charger control, ATA flush/standby/rail-off ordering, LCD and
backlight sleep, safe restart/shutdown/disk mode, suspend/resume, and
wake-source accounting. Candidate
`b9d9a4a0a162419adc36dd6474b45aaa772b6dd703f554e8da7edf6514e971b3`
passed its revised physical procedure and verified restore on 2026-09-02. It
does not install directly into OSOS or add Campaign 4 audio.

## Campaign 4: audio hardware ownership

`docs/PHASE1_AUDIO.md` describes the passed audio ownership candidate in
`phase1-audio-simple`. `AUDIO_GATE=1` adds bounded WM8975 headphone output and
a user-triggered left/right I2S test tone, with teardown before lifecycle
transitions. It requires the Campaign 3 build gates. **The portable
`audio.pcm` capability is not advertised**; streaming and its complete event
contract remain a later gate. The operator passed this procedure on
2026-09-02 with correct audio and a slight startup crackle; the saved restore
receipt verifies the original Rockbox image. Startup transient reduction
remains open.
