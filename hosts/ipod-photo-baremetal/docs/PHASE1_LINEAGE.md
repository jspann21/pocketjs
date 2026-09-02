# Phase 1 package lineage and rollback gate

This batch adds bounded package lineage on top of the qualified confined state
slots. It does not add package-file writes or a general writable filesystem.
Firmware still writes only the data sectors of the two exact, preallocated
512-byte state files. Package files, FAT tables, directories, allocation
chains, and the firmware image remain read-only.

Lineage records use separate `PJSLIFE2` version-2 records, so the persistence
gate's version-1 records cannot be mistaken for package lifecycle state. Each
committed CRC record contains its generation and phase plus source identifiers
and full 64-bit package hashes for active, last-good, trial, and rejected
packages. The four phases are `ACTIVE`, `TRIAL`, `RUNNING`, and `CRASHED`.

Before evaluating a package, firmware durably records `TRIAL`. Acceptance is
not written merely because parsing or QuickJS boot succeeds. It requires the
initial QuickJS frame, a core step, a non-empty damage region, and successful
LCD presentation. A runtime frame failure records `CRASHED` and quarantines
that exact source/hash. On reboot, stale trial/running/crashed state selects
last-good then embedded recovery. The rejected pending hash is retained so the
unchanged file is not retried.

## One-install hardware gate

Use `phase1-lineage-simple`. The PowerShell transaction backs up Rockbox and
the exact prior contents or absence of `ACTIVE.PKT`, `PENDING.PKT`,
`LASTGOOD.PKT`, `APP.PKT`, `STATE0.BIN`, and `STATE1.BIN`. It stages a stable
Alpha active app, a distinct Beta pending app, and seeded lineage slots.

The expected sequence is:

```text
Boot 1 / Beta:  ACPT P G00003
Center:         CRSH P G00004
Boot 2 / Alpha: ROLL A G00006
Boot 3 / Alpha: KEEP A G00009
```

`P` is pending and `A` is active. Center deliberately runs the Beta guest past
the bounded QuickJS frame budget. Menu+Play uses the already-qualified reboot
path. The second reboot proves the unchanged rejected pending package is not
retried. `LINE E##` is a gate failure.

This batch does not claim physical power-loss qualification, low-battery
writes, arbitrary writable files, package promotion/copying, FAT metadata
mutation, or general host-side package rotation.
