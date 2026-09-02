# Full PocketJS firmware plan

The hardware probe is not the final runtime. The production architecture keeps
PocketJS intact and replaces only the host operating-system layer.

## Runtime ownership

```text
Apple boot ROM
  -> PocketJS A1099 crt0 and kernel
  -> QuickJS runtime
  -> standard globalThis.ui HostOps bridge
  -> standard pocketjs-core retained tree/layout/text/animation
  -> standard DrawList
  -> PocketJS RGB565 renderer and A1099 LCD presenter
```

No JavaScript-to-Rockbox translation layer and no patched RetailOS functions are
part of the design.

## Gates

### 0. Board probe — implemented here

Boot/remap, faults, display, backlight, buttons, wheel, hold, reset, image
packaging, and reproducible firmware evidence.

### 1. Native kernel

- qualified PP5020 cache enable plus runtime clean/invalidate policy;
- timer IRQ and fixed-step scheduler;
- bounded SDRAM allocator with guard regions;
- PCF50605 power and battery state;
- deterministic shutdown and reboot-to-disk-mode path;
- cold-boot LCD clock/device ownership.

### 2. PocketJS core and QuickJS

- compile the existing `pocketjs-core` as an ARMv4T `no_std` static library;
- use a target-neutral version of the existing C ABI;
- compile the pinned QuickJS sources freestanding;
- preserve the normal HostOps contract and host ABI;
- render through PocketJS's RGB565 software backend;
- embed one target-thinned recovery `.pocket` package.

### 3. Dynamic packages

- ATA PIO read-only driver;
- MBR/FAT32 read-only filesystem;
- package selection: pending -> active -> last-good -> embedded recovery;
- verify footer, target, host ABI, identity, plan, JavaScript terminator, and
  package hash before evaluating code.

### 4. Persistence and production recovery

- confined preallocated settings/state files with alternating CRC slots;
- independent firmware backup/installer/read-back verifier;
- first-retired-frame acceptance before committing an updated package;
- crash lineage and rollback.

The confined state writer is hardware-qualified in `PHASE1_PERSISTENCE.md`.
The first-presented-frame acceptance, crash lineage, rejected-hash quarantine,
and rollback campaign is documented in `PHASE1_LINEAGE.md`. It uses those same
two exact preallocated sectors and does not write packages or FAT metadata.
Strict fixed-plan admission, memory reserve/guard enforcement, live embedded
recovery, and the combined rollback batch are documented in
`PHASE1_RELIABILITY.md`.
LCD device-gate ownership, filtered power transitions, reversible panel
sleep/wake, shutdown preflight, and the terminal disk-mode handoff are
documented in `PHASE1_POWER_DISK_HANDOFF.md`. Shared LCD clock selection, ATA
standby, and PMU standby remain outside that gate.
Physical power interruption remains a separate, unqualified boundary.

### 5. Optional capabilities

Audio, writable per-app filesystem, USB transport, and networking remain absent
from the target profile until their complete observable contracts are shipped
and tested. Capability declarations will never be aspirational.
