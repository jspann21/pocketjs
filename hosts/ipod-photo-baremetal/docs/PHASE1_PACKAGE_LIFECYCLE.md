# Phase 1 read-only package lifecycle

The iPod Photo runtime checks fixed FAT32 package slots in this order:

1. `/POCKETJS/PENDING.PKT`
2. `/POCKETJS/ACTIVE.PKT`
3. `/POCKETJS/LASTGOOD.PKT`
4. `/POCKETJS/APP.PKT`
5. the package embedded in `rockbox.ipod`

Each disk package must pass the existing package hash, target, host ABI, and
required-section checks. A package is selected only after QuickJS boot and its
first frame both succeed. A rejected slot is released before the next slot is
tried. **The device performs no filesystem writes in this phase.** Promotion
between pending, active, and last-good remains an operator-controlled disk-mode
operation.

The native source line is position-stable and text-only. `PEND`, `ACTV`,
`LAST`, `APP`, and `EMBD` identify the selected slot. A following failure code
records the first rejected higher-priority slot; `P02` means package admission
failed. `R#####` is the cumulative sector-read count and can vary with FAT32
layout.

Use `phase1-package-lifecycle-simple` for the physical gate. One install stages
a deliberately corrupt pending package and a valid active package. The first
boot should report `ACTV P02`. `STAGE-FALLBACK.ps1` removes only the staged
active package; the second boot should report `EMBD P02`. `RESTORE.ps1`
restores the original Rockbox image and the exact prior contents or absence of
all four package slots.
