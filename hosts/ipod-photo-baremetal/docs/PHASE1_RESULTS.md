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

## Expanded HostOps candidate — Hold edge exceeds initial JS budget

```text
candidate wrapper SHA-256:
498e49fcb60adcfde8bad52b6583dfe5c7050241923a41167bfbcde9f8c823dc

APP.PKT SHA-256:
4e072db93b1bf4b31c9b2a6ced764c78ed14a16ca842406ad7b1d11bea14ab15

first screen: about 1 second
runtime chip initially: cyan
embedded guest lane/pulse/wheel marker: passed
wheel touch and Select/Menu/Play guest states: passed
native input and heartbeat: prompt/normal
battery/cache/USB indicators: plausible/green/blue
changed-frame status: orange in the worst case
partial-update artifacts: none
runtime chip after toggling Hold: orange
Menu+Play reset: passed
Rockbox restore SHA-256:
e1735b38b0c261a3c0bb65f513568ebc608cb1b44fc9da092b398d37f0907cbd
```

The expanded bridge and disk guest remained functional, but the physical
PP5020 exceeded the initial 100 ms QuickJS frame watchdog while processing the
Hold mutation edge. The follow-up candidate retains a finite watchdog at 250 ms
and adds visible texture, property-batch, and core-animation probes so the next
device cycle qualifies more of the bridge while retesting Hold.

## Expanded HostOps + Hold watchdog candidate — passed

```text
candidate wrapper SHA-256:
6babe891ac53f09a0a3c71da1e14849713f6bc60136bad73571862e4bdcdb3f7

APP.PKT SHA-256:
fc5cc221f6c6c1a951d3657eabf832ec5c0b906c703d5b73dfac0a0cfe3ff579

first screen: about 1 second
runtime chip initially: cyan
cyan/magenta checker texture and alternating blocks: passed
yellow animation runner and rail: visible
Select animation and reverse/restart: prompt start, slow/choppy motion
guest pulse/lane/wheel/touch/buttons/Hold states: passed
runtime chip after five Hold transitions: cyan
native Hold strip, controls and heartbeat: prompt/prompt/normal
battery/cache/USB indicators: plausible/green/blue
changed-frame status: orange only on a Hold edge
partial-update artifacts: none
Menu+Play reset: passed
Rockbox restore SHA-256:
e1735b38b0c261a3c0bb65f513568ebc608cb1b44fc9da092b398d37f0907cbd
backup/state/stale transaction files after restore: absent
```

This candidate passes the Hold regression that failed at 100 ms and physically
qualifies the texture, image, typed-array property-batch, animation,
cancellation/restart, focus, and active bridge paths exercised by the guest.
The slow/choppy animated traversal is retained as performance evidence: it did
not coincide with a runtime fault, input delay, stopped heartbeat, reported
dropped tick, partial-update artifact, or failed restoration.

## Ordinary generated application candidate — passed

```text
candidate wrapper SHA-256:
3695faaab21e2632a20dcb1305b92fb0a7f4007cc4106f185bd509e5560395d4

APP.PKT and embedded fallback SHA-256:
d2a412b62f62ba7ce64ee65aa62ff7abfe700259ee0df66da6a9daa759d89d1b

labelled ordinary-app screen within 30 seconds: passed
APP status: OK
center Select changed WAIT to OK: passed
wheel changed WAIT to OK: passed
readability and visible artifacts: passed / none observed
Rockbox restore SHA-256:
e1735b38b0c261a3c0bb65f513568ebc608cb1b44fc9da092b398d37f0907cbd
backup/state/stale transaction files after restore: absent
```

This closes the Campaign 1 ordinary framework-application gate. The device
executed the canonical target's generated Solid bundle and baked asset pack
through the standard frame/input contract, including reactive text updates.
Because the disk and embedded packages were identical, this result does not
claim the separate large multi-cluster FAT32 source-selection gate.

## Ordinary generated app disk-source candidate — passed

```text
candidate wrapper SHA-256:
25e7f8767c1b5118eadd2bcb148f6b03d6a5a2842bed46d8beb1774a1c771577

APP.PKT and embedded fallback SHA-256:
d2a412b62f62ba7ce64ee65aa62ff7abfe700259ee0df66da6a9daa759d89d1b

ordinary app within 30 seconds: passed
source strip: DISK R00376
center Select and wheel response: passed
unexpected behavior: none
Rockbox restore SHA-256:
e1735b38b0c261a3c0bb65f513568ebc608cb1b44fc9da092b398d37f0907cbd
backup/state/stale transaction files after restore: absent
```

The explicit `DISK` source and 376-sector count qualify physical FAT32 loading
of the 174,792-byte ordinary package. This closes the source ambiguity in the
previous byte-identical disk/embedded candidate without restoring the crowded
diagnostic screen.
