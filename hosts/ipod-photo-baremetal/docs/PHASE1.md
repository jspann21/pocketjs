# Phase 1: standalone kernel substrate and real PocketJS core

## Responsive-scheduler correction

The first physical Phase-1 candidate (`ca1a9c8e...`) rendered once for every
pending simulation tick. On the uncached A1099, each render was just slower
than the 16.67 ms simulation interval, so the interrupt producer replenished
the queue while the consumer was draining it. The red dropped-tick indicator
appeared about three seconds after handoff and input/liveness stopped updating.

The corrected candidate atomically claims at most four pending ticks, advances
the retained UI for that finite batch, and presents only one coalesced frame.
Ticks arriving during rendering remain for the next outer pass, guaranteeing
that input polling regains control. PCF50605 transactions are disabled for this
retest; direct USB/FireWire GPIO indication remains active and the purple
battery/third chip explicitly marks the telemetry-disabled build.


## Incremental-render correction

The bounded-batch retest remained alive but was still extremely slow. The first
full PocketJS raster took about three seconds, immediately filled the four-tick
queue, and lit the red dropped-tick indicator. Interaction then advanced only
at the full-frame raster rate.

This candidate renders the initial complete frame before enabling Timer1. It
then uses PocketJS's retained DrawList `DamageTracker`, repaints only changed
regions, expands them to the LCD bridge's two-pixel packing alignment, and
submits compact partial updates. It also avoids writing unchanged diagnostic
properties on every 60 Hz step. Simulation remains 60 Hz and presentation is
capped at 30 Hz. The cache remains disabled so this test isolates incremental
rendering from the later cache-ownership gate.

Phase 1 is a new reversible `/rockbox.ipod` candidate layered on the exact
hardware-qualified Phase-0 startup/display/input path. It still uses the
installed bootloader only as a file loader; no Rockbox runtime code or API is
linked or called after handoff.

## Implemented

- context-preserving ARM IRQ entry and return;
- Timer1 periodic interrupt at 1 kHz;
- deterministic 60 Hz fixed-step scheduler with a four-tick catch-up cap and
  explicit dropped-tick accounting;
- bounded first-fit SDRAM allocator with arbitrary power-of-two alignment,
  split/coalesce, validation, stats, and host stress tests;
- explicit ownership of a disabled PP5020 cache during qualification;
- bounded/recoverable PP5020 I2C transactions;
- read-only PCF50605 battery ADC plus USB, FireWire, and charging telemetry;
- the actual `pocketjs-core` crate built `no_std + alloc` for
  `armv4t-none-eabi` and linked as a static library;
- a retained PocketJS `Ui`, DrawList generation, and the core RGB565 software
  rasterizer; only the final 16-bit byte swap is target-specific;
- continued Menu+Play reset and boot-ROM disk-mode recovery.

## Deliberate limits

- The cache remains disabled until this exact IRQ/core image passes on the
  physical P98. This favors correctness over performance for the first gate.
- Power support is telemetry-only. Phase 1 does not alter charger settings,
  request standby, or enforce low-battery shutdown.
- QuickJS is not linked yet. The retained UI core and memory/runtime substrate
  are established first; the next phase adds the QuickJS HostOps bridge and an
  embedded `.pocket` recovery application.
- No ATA, FAT, audio, or direct OSOS installation is enabled.

## Hardware gate

Install only through `tools/handoff.py`, record the artifact commit and
SHA-256, and verify:

1. repeated boot and display correctness;
2. all Phase-0 controls and recovery behavior;
3. heartbeat remains steady while rotating the wheel rapidly;
4. battery bar is plausible and USB/charging chips react to cable state;
5. the dropped-tick chip remains dark during ordinary use;
6. Menu+Play reset and byte-exact restoration both succeed.

The Phase-1 artifact is unqualified until that exact CI-produced file passes.
