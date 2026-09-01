# Phase 1 hardware results

## Initial core candidate — scheduler starvation

```text
candidate wrapper SHA-256:
ca1a9c8e13531d42c6f7c1c722be6ee7c02716d53e867d07a249db2c5baad1d1

payload SHA-256:
b8ae2bb62ace0cae9c602cdae69bc745a0e0d6a5e1bac46ece59720b8a3786b7

device: A1099 P98/M9829, LCD type 0, iFlash/SD
loader handoff: passed
real Rust pocketjs-core init/render: passed
initial USB indicator: blue
rightmost dropped-tick indicator: red about 3 seconds after main screen
heartbeat/input/wheel/Hold: static after backlog developed
installer and backup hashes: correct
```

Diagnosis: the main loop used an open-ended `while
(scheduler_take_fixed_tick())` and performed a full retained-UI raster plus LCD
presentation inside every iteration. Rendering took slightly longer than the
60 Hz interval, so new ticks continually replenished the queue. The corrected
build claims a finite batch and coalesces presentation.

## Bounded-batch responsive candidate — alive but full-frame bound

```text
candidate wrapper SHA-256:
cdec0e021f84674195d8922e355850f0f3dc4705d757c9961bf8a94676b80658

payload SHA-256:
383cadca17dcc0d95068223d4c2b45c8bce220f18c83200370ece2f6d108c127

boot: passed
first full frame: about 3 seconds
heartbeat: continued, but extremely slowly
buttons/wheel: only marginally observable because frames were very slow
Hold: passed
USB and telemetry-disabled markers: correct
dropped-tick indicator: red after about 3 seconds
Menu+Play reset: passed
```

Diagnosis: finite scheduler batching fixed the open-ended starvation bug, but
every visible update still rebuilt, rasterized, byte-swapped, and transferred
the complete 220x176 frame with the cache disabled. The next gate uses retained
damage plus partial LCD transfer and starts Timer1 only after the boot frame.

## Incremental uncached candidate — correct damage, still over-scheduled

```text
candidate wrapper SHA-256:
dec65aac4b191dc9c95f9599629cbae89bd501e9ddedf6e445046b24259495fb

boot: passed
heartbeat: slow
buttons/wheel/Hold: delayed but observably better
wheel-touch cyan state: passed
partial-update artifacts: none
power-telemetry-disabled markers: correct
dropped-tick indicator: red after about one second
Menu+Play reset: passed
```

This qualifies the retained damage rectangles and compact LCD updates on the
physical type-0 panel. The remaining avoidable load was that the host requested
render planning after every fixed simulation tick even when no visible property
changed. The paced candidate renders only on visual mutation, carries a longer
bounded queue, catches simulation up before presentation, and reports measured
frame cost in the fourth status chip.
## Paced uncached candidate — rendering remains CPU-bound

```text
candidate wrapper SHA-256:
768a3bd5da0be9725671a5ff9853f994fe22e62c5c9ae476eaa5b2d0f0edbe4c

first screen: about 1-2 seconds
fourth chip: usually orange (changed frame >= 300 ms)
heartbeat: slow and inconsistent
buttons/wheel/Hold: delayed, but more responsive than the incremental build
wheel-touch cyan state: passed
partial-update artifacts: none
dropped-tick indicator: red after about one second
Menu+Play reset: passed
```

Event-gated damage presentation reduced unnecessary work and preserved correct
partial updates, but an actual changed PocketJS frame still costs at least 300
ms with the PP5020 cache disabled. The next gate enables the 8 KiB cache with a
writeback test through the uncached SDRAM alias and uses measured microseconds
per timer interrupt instead of assuming the timer cadence.

## Cache-owned candidate — performance gate passed

```text
candidate wrapper SHA-256:
a2499e1f9903ad2fa85d0e3352a3b313255dd477698f573131af2ba4d72cdb19

first screen: about 1 second
cache status: green; uncached-alias writeback self-test passed
changed-frame status: usually green (under 100 ms)
heartbeat: normal
buttons/wheel/Hold: prompt
wheel-touch cyan state: passed
partial-update artifacts: none
dropped-tick indicator: never appeared
Menu+Play reset: passed
```

This qualifies standalone PP5020 cache ownership, cached execution and data,
PocketJS retained damage rendering, compact LCD updates, timer accounting, and
input responsiveness on the physical P98/M9829 type-0 panel. The next isolated
gate enables PCF50605 battery ADC and LTC4066 charging-state telemetry without
changing the qualified cache, scheduler, renderer, LCD, or input paths.
