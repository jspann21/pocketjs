# Phase 1 runtime reliability gate

This batch extends the qualified package-lineage path without adding package
writes or FAT metadata mutation. **The firmware validates the package plan,
runtime memory reserve, heap structure, and stack/heap guard bands before or
during execution.** A live QuickJS frame failure records the crash and boots
the embedded recovery package without waiting for a reboot.

## Admission and memory contract

The native package bridge now parses the fixed target plan without allocation.
It requires target `ipod-photo`, host ABI 1, logical and physical viewport
220x176, fixed/native presentation, raster density 1, and only supported
feature keys. Container integrity remains a separate earlier check.

Before QuickJS boot, the runtime requires a valid heap, intact guard bands, and
at least 8 MiB in the largest free heap block. The kernel rechecks heap and
guard integrity once per second. **Guard corruption stops execution with the
`MEMG` panic instead of continuing with damaged allocator or stack state.**

## One-install hardware batch

Use `phase1-reliability-simple`. Its verified transaction stages:

- a container-valid pending package whose plan deliberately claims a 221x176
  logical viewport;
- the previously qualified Beta crash app as active;
- the previously qualified Alpha app as last-good and embedded recovery;
- two fixed 512-byte lineage slots seeded at generation 1.

The expected sequence is:

```text
Boot 1 / Beta:       SAFE A G00003
Center / recovery:   RECV E G00004
Boot 2 / Alpha:      ROLL L G00006
Boot 3 / Alpha:      KEEP L G00009
```

`SAFE A` proves the malformed pending plan was rejected before evaluation and
the valid active app passed memory admission. `RECV E` proves a running guest
fault reached embedded recovery without reboot. `ROLL L` proves the persisted
crash record selected `LASTGOOD.PKT`; `KEEP L` proves that result survived a
clean reboot while the malformed pending file remained rejected.

This batch does not qualify arbitrary writable files, host-side package
promotion, physical power interruption, disk-mode entry, or PMU standby.
