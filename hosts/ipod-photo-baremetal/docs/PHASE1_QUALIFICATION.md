# iPod Photo A1099 Phase 1 hardware qualification

Status: hardware-qualified on a physical iPod Photo/Color A1099 for the scope listed below.

Qualification date: 2026-09-01
Boot path: reversible Rockbox bootloader `/rockbox.ipod` handoff only.

## Final qualified runtime candidate

- File: `pocketjs-a1099-phase1-runtime-hold.ipod`
- SHA-256: `c9a1d513ad2e440c054ceb2f116d26e334c5737071c8d5e87460cfc9f8251152`
- Valid disk guest: `/POCKETJS/APP.PKT`
- APP.PKT SHA-256: `0a85c16910a804faf0b5a1d486eea1b70a18e749dedd539a693bce8962781074`

## Qualified scope

- Rockbox bootloader handoff and standalone ARMv4T startup
- LCD, backlight, five buttons, click wheel, wheel touch, Hold and Menu+Play reset
- Timer1 IRQ, fixed-step scheduler and responsive input polling
- Rust `pocketjs-core` backend and retained UI renderer
- Damage tracking and partial LCD updates without observed corruption
- PP5020 cache ownership/self-test
- Read-only PCF50605 battery ADC plus USB/charging telemetry
- QuickJS runtime and generic PocketJS HostOps bridge
- Target-admitted `ipod-photo` `.pocket` package execution
- Read-only PIO ATA + FAT32 loading of `/POCKETJS/APP.PKT`
- Invalid/corrupt disk package rejection with embedded recovery `.pocket` fallback
- Runtime Hold transition with no QuickJS fault or scheduler drop

## Observed performance

Input and heartbeat remained prompt. The diagnostic performance chip commonly remained yellow and briefly reached orange during a Hold transition. No red scheduler-drop indication was observed in the final candidate. This is an optimization target, not a correctness failure.

## Still outside this qualification

- ATA/FAT writes and filesystem mutation
- ATA power/sleep ownership changes
- audio output and codec control
- USB device stack
- charger-control writes
- automatic low-battery shutdown / PMU standby writes
- direct firmware-partition/OSOS installation

This branch contains normal source files. Building it does not require the historical Phase 1 reconstruction patch stack.
