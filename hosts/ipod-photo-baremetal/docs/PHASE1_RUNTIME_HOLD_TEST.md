# Phase 1 QuickJS Hold-transition test

This gate preserves the hardware-qualified FAT32 disk guest, QuickJS HostOps,
cache, read-only power telemetry, input, and partial LCD paths. It changes only
two stress points observed during physical storage qualification:

- the QuickJS per-frame watchdog is 100 ms instead of 20 ms; a watchdog expiry
  is reported as an orange runtime chip rather than the generic red error chip;
- the native Hold diagnostic damage is an 8-pixel strip instead of a 44-pixel
  overlay, reducing a Hold transition from 9,680 to 1,760 diagnostic pixels.

Expected with `/POCKETJS/APP.PKT` present:

- runtime chip stays cyan, including while toggling Hold;
- Hold strip appears/disappears promptly;
- guest marker still reacts to Hold;
- performance chip should remain green/yellow and never latch red;
- no partial-update artifacts;
- Menu+Play reset remains available.

Orange runtime means the 100 ms QuickJS watchdog still expired. Red runtime means
a non-budget QuickJS/HostOps failure. Neither result qualifies this gate.
