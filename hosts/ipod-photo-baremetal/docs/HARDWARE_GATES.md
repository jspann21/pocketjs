# Hardware gates before direct OSOS installation

## Stage 1: reversible bring-up

1. Verify a complete firmware-partition backup and SHA-256 off-device.
2. Confirm boot-ROM disk mode works before modifying the firmware partition.
3. Chainload the image or retain a known-good alternate OSOS path.
4. Cold/warm boot at least 20 times.
5. Confirm all four known panel-type strap values either render correctly or
   fail without a destructive register loop.
6. Validate red/green/blue ordering and every row/column edge.
7. Validate all buttons, both wheel directions and Hold.
8. Hold Menu + Play for two seconds and confirm a clean reboot.
9. Inject undefined and data-abort test builds; confirm repeatable reboot and a
   valid `.noinit` crash record under a debugger/instrumented follow-up image.

## Stage 2: full board support

Required before PocketJS replaces OSOS directly:

- cold LCD/panel wake and sleep independent of inherited loader state;
- timer IRQ and bounded scheduler;
- cache enable/clean/invalidate ownership tests;
- PCF50605 battery, charge, USB-power and low-voltage state machine;
- disk-safe shutdown;
- ATA PIO plus partition and FAT32 read-only support;
- embedded recovery `.pocket` package;
- package failure lineage: pending → active → last-good → embedded;
- installer backup, bounds checks, sector-aligned write and full read-back;
- at least 100 recovery cycles without loss of disk mode.

## Stage 3: PocketJS runtime

Only after Stage 2:

- native allocator with hard image/stack/framebuffer bounds;
- target-thinned package admission;
- QuickJS with explicit stack and memory limits;
- retained PocketJS core and RGB565 damage renderer;
- fixed 60 Hz simulation with independently paced LCD presentation;
- fault/JS exception recovery to the embedded package.
