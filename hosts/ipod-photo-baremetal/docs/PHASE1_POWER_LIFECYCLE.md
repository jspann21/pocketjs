# Campaign 3 — power and complete lifecycle

Campaign 3 is the next campaign after the physically passed native-kernel
handoff candidate. The prior candidate proved LCD device ownership, filtered
source pins, panel sleep/wake, shutdown preflight, and the read-only disk-mode
handoff. It intentionally stopped before ATA flush/standby, disk-rail control,
charger policy, PMU standby, and lifecycle wake accounting. This campaign adds
those boundaries in one separately named candidate:

```text
pocketjs-a1099-phase1-power-lifecycle.ipod
bytes: 1107776
sha256: b9d9a4a0a162419adc36dd6474b45aaa772b6dd703f554e8da7edf6514e971b3
```

This exact candidate is hardware-qualified for Campaign 3. It does not install
directly into OSOS and it does not add Campaign 4 audio.

## Safety contract

- PCF50605 transactions wait at most 5 ms for the PP5020 I2C controller. Each
  read/write gets three attempts; a failed attempt resets the I2C block and
  reapplies the known clock. Recovery count is exposed in telemetry, and an
  invalid ADC sample never authorizes a shutdown.
- Battery percentage uses the iPod Photo measured discharge points
  `3450, 3660, 3700, 3730, 3770, 3820, 3870, 3920, 4040, 4100, 4170 mV`,
  with piecewise interpolation. Low state requires three valid samples at or
  below 3450 mV. Critical state requires five valid samples at or below
  3300 mV. Recovery requires three valid samples at or above 3550 mV.
- USB (D3), FireWire (C2), and charger indication (GPO32 bit 0) retain their
  board-specific active levels. All observable source pins must agree for
  three samples before a source transition is accepted. A stable external
  source blocks automatic battery shutoff.
- Charger output is conservative: USB selects the 100 mA LTC4066 limit;
  absence of USB selects suspend. The 500 mA mode exists in the HAL for
  explicit future qualification but is not selected by this image. Charger
  writes are deferred while source state is unstable.
- Panel sleep always turns the backlight off first. Resume restores the disk,
  panel, framebuffer, and then the backlight. LCD sleep/wake snapshots the
  inherited `CLOCK_SOURCE` and `CLCD_CLOCK_SRC` values and refuses a lifecycle
  transition if either shared clock changes.
- Disk power has explicit `ON -> FLUSHED -> STANDBY -> OFF` states. `OFF` is
  reachable only after a successful ATA `FLUSH CACHE` and `STANDBY IMMEDIATE`.
  The IDE0 rail and pin mux are disabled only after that sequence; any command
  or verification failure leaves power on and reports `KERN E##`.
- Terminal restart, shutdown, and disk-mode paths commit the running lineage
  record clean before stopping QuickJS. They flush and standby the disk before
  releasing the package, stopping Timer1, disabling IRQs, and sleeping the
  panel. PMU standby writes PCF50605 OOCC1 with USB and FireWire wake bits.
  A PMU/I2C failure leaves the already-safe unit inert instead of rebooting.
- Source transitions from no external power to USB or FireWire generate a
  one-shot wake event, reset the inactivity timer, and increment total and
  per-source counters. An external-power wake resumes an already-suspended
  unit. The `WAKE USB`/`WAKE FIRE` evidence bits remain latched for the boot.

## Device actions

The sparse status strip uses these additional labels:

```text
BAT LOW     BAT CRIT     CHG ONLY
WAKE USB    WAKE FIRE
SUSPEND     RESUME
SUSP EXT
DISK FLUSH  DISK OFF
BAT OFF
```

With the native-kernel and lineage gates enabled:

| Action | Required ordering | Refusal behavior |
| --- | --- | --- |
| Menu + Right, held 2 s on battery | show `DISK FLUSH`, then flush, standby, rail off, backlight/LCD sleep | external power shows `SUSP EXT`; disk and display stay on |
| any button or external-power insertion while suspended | rail on, LCD wake, full present, backlight on | remains suspended and dark |
| Menu + Left, held 2 s | clean lineage, flush/standby/off, stop runtime, LCD sleep, PP5020 reboot | no reboot if storage refuses |
| Menu + Play, held 2 s | existing idle ATA check, clean lineage, flush/standby/off, stop runtime, disk marker reboot | no marker/reset if any check refuses |
| 10 s without activity on stable battery-only power | same suspend sequence as Menu + Right | external or unstable power blocks automatic suspend; retries are rate-limited after failure |
| automatic critical-battery shutdown | clean lineage, flush/standby/off, charger suspend, PMU standby | invalid/unstable samples or external power block shutdown |

`CHG ONLY` is a low/critical-battery indication while an external source is
stable; it never bypasses the low-voltage debounce and never permits automatic
shutdown while external power is present.

## Superseded USB-idle candidate

Candidate `83bc3b2b285126057a2c409797ac2890b95da379566c48005de4c2298ca9a7b5`
reported `PWR USB` on physical hardware and then entered the unconditional
10-second idle-suspend path. The display turned off and did not recover during
the test, so the operator stopped. USB detection did not request PMU standby;
the failure was that source transitions neither reset inactivity nor requested
resume. That candidate is rejected. Automatic idle suspend is now restricted
to stable battery-only operation, and USB/FireWire transitions reset inactivity
and wake a suspended unit.

Candidate `a3a7b50710ded8f4d3f8205631b1ecda7bc2c923ff00a5fa0e2833f714400dc6`
is also rejected. Physical testing reached manual suspend, but the queued
`SUSPEND` diagnostic then drove the normal presenter against an asleep panel;
the presenter entered its fatal LCD path before another input edge could be
handled. The same run reported `KERN E74` during a later shutdown attempt.
The replacement keeps input/source/battery polling active while suspended but
blocks guest stepping and LCD transfers until resume. An already-clean
`ACTIVE` record is accepted on retry so a later flush or standby failure cannot permanently poison the
shutdown path. `KERN E04` remains the intentional result of pressing Play
before completing the center-button `LCD WAKE` prerequisite on that boot.

Candidate `531be59d0cb1576a00716f099c635d99ef59df6fb6264c183e0520cc40e08d4a`
physically passed battery suspend/resume and safe restart. The observed
`RESUME` label is intentionally latched as the last lifecycle event. Testing
also confirmed that Menu+Select retains the iPod's native hard-reset behavior,
so it cannot safely serve as a software shutdown chord and is no longer part
of this gate. Manual suspend is now explicitly battery-only, reports
`SUSP EXT` instead of appearing inert on external power, and presents
`DISK FLUSH` before the panel sleeps. True power-off qualification remains the
debounced automatic critical-battery path.

## Synthetic and hardware gate

Run the self-contained threshold cases before installing the image:

```text
python .\power_cases.py
```

It covers noise, three-sample low, five-sample critical, invalid-sample
retention, and external-power shutdown blocking. The expected final line is
`cases=PASS count=6`.

The matching `phase1-power-lifecycle-simple` kit then stages the candidate and
the already-qualified Alpha package/state pair through the existing bounded
backup/restore transaction. On hardware, collect repeated warm/cold boots,
initial battery/USB/FireWire source labels, cable insertion/removal, low
battery USB charge-only behavior, manual and idle suspend/resume, safe restart,
automatic critical-battery shutdown, and disk-mode reboot. Keep
`INSTALL-STATUS.json` and `RESTORE-STATUS.json` with the results. Restore as
soon as the volume mounts and verify normal Rockbox boot.

True physical power interruption, measured charger current, PMU rail removal,
and wake from a powered-off unit remain hardware-only observations for this
candidate. Stop on a false low-battery shutdown, disk activity after `DISK OFF`,
reboot loop, lost panel, or any unverified filesystem change.

## Qualification result

Candidate `b9d9a4a0a162419adc36dd6474b45aaa772b6dd703f554e8da7edf6514e971b3`
passed the revised physical procedure on 2026-09-02. Battery suspend showed
`DISK FLUSH`, center-button wake returned `RESUME`, external-power suspend was
refused without dropping the display, idle suspend/resume and safe restart
behaved as specified, disk mode mounted, and the verified restore returned the
original Rockbox image. The retained install/restore evidence is in
`phase1-power-lifecycle-qualified-evidence`.
