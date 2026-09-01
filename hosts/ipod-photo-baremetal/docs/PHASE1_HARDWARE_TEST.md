# Phase-1 paced incremental-render hardware test

This candidate keeps the PP5020 cache and PCF50605 telemetry disabled. It tests
only event-gated rendering, a longer bounded scheduler queue, wheel-delta
latching, and measured frame pacing on the already-qualified partial-LCD path.

Expected after the first screen:

- idle rendering occurs only when the heartbeat changes;
- buttons, wheel, wheel touch, and Hold respond more promptly;
- battery bar and third chip remain purple;
- no stale trails, shifted rectangles, clipping, or color corruption;
- fourth chip reports the most recent changed frame: green under 100 ms, yellow
  100-299 ms, orange 300 ms or slower, red only after a genuine dropped tick.

Report the first-screen time, fourth-chip color, heartbeat speed, input latency,
any partial-update artifact, and whether red ever appears during 30 seconds.
