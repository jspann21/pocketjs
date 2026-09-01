# Phase 1 QuickJS + embedded `.pocket` hardware test

This candidate preserves the hardware-qualified A1099 cache, retained PocketJS
renderer, partial LCD updates, 60 Hz scheduler, controls, and power telemetry.
It adds the pinned QuickJS engine and boots an embedded, target-admitted
`ipod-photo` `.pocket` recovery guest.

The reversible `/rockbox.ipod` handoff remains the only installation method for
this test. The image is not a direct firmware-partition installer.

## Visible proof chain

A successful boot must show all of the following:

1. The small runtime chip between the battery bar and cache chip is **magenta**.
   Yellow means the package was admitted but QuickJS did not finish booting.
   Red means package admission, evaluation, HostOps, or a later guest frame
   failed.
2. A separate magenta pulse appears in the upper bar near the middle of the
   screen. It alternates brightness independently from the native white
   heartbeat.
3. A thin guest-owned lane appears below the battery row. Its marker follows
   wheel position.
4. The guest marker becomes cyan while the wheel is touched, red while Hold is
   active, green for Select, red-pink for Menu, yellow for Play, and blue while
   wheel deltas are consumed.
5. The existing native controls, battery bar, cache chip, USB chip, charging
   chip, performance chip, heartbeat, and Menu+Play reset remain functional.

The first full screen may take slightly longer because QuickJS initializes and
parses the embedded guest before the initial retained-mode render. Normal idle
and input operation should remain prompt after that screen appears.

## Result block

```text
first screen appeared after: ___ seconds
runtime chip: magenta / yellow / red / other
magenta guest pulse visible and changing: yes / no
guest lane visible: yes / no
guest marker followed wheel: prompt / delayed / not working
guest marker cyan on wheel touch: yes / no
guest marker changed for Select/Menu/Play: yes / no / partly
guest marker reacted to Hold: yes / no
native heartbeat: normal / slow / stopped
native buttons and wheel: prompt / delayed / not working
battery bar plausible: yes / no
cache chip green: yes / no
USB chip blue while connected: yes / no
performance chip: green / yellow / orange / red
partial-update artifacts: none / describe
runtime chip ever became red: never / after ___ seconds
Menu+Play reset: yes / no
anything unexpected:
```
