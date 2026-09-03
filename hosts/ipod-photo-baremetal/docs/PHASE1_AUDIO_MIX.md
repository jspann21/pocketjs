# Campaign 4: native format conversion and four-stream mixing

**Revision 7 passed its physical procedure on 2026-09-02.** It follows streaming
checkpoint `0d3546b`, whose full physical procedure passed with a small
first-tone-only crackle. That passed image remains unchanged.

## Native scope

The mixer owns four fixed-capacity source rings of 16,384 frames each.
Each source accepts 44.1, 22.05, or 11.025 kHz signed 16-bit PCM, in mono
or interleaved stereo. Writes copy the input before returning. Mono is
duplicated to stereo; lower rates use exact integer sample repetition.
Per-source Q15 gain is applied before summation and signed output saturation.

Mixing runs in the main context at the qualified cooperative refill points,
not in the DMA interrupt. Output is queued to the passed native 44.1-kHz
transport. A bounded ledger distinguishes speculative mixed source positions
from source frames retired by the native output clock. Source capacity is
released on retirement, not when a producer prepares audio ahead of time.
Source end is reported once after final output and the FIFO have drained.

The diagnostic checks invalid formats, four-stream admission, fifth-stream
refusal, exact source-ring capacity, stale handles after reset, borrowed
buffer copying, all six rate/channel combinations, four simultaneous sources,
volume changes, global pause/resume, one deliberately starved source while
the others continue, exact retirement/end counts, and lifecycle cleanup.
The hardware headphone level remains -40 dB; generated peaks are 1,024 per
source before gain. The first-tone transient remains open.

## Contract boundary

**The portable `audio.pcm` capability remains absent.** This batch qualifies
the native mixer, not `globalThis.audio`. Pause, stop, and reset are global
batch controls. Per-stream control changes in this diagnostic apply to newly
prepared audio and can lag by the output reserve (about 0.3 seconds).
Independent stream destruction/flush while other streams continue, guest
marshalling, tick-boundary event delivery, and the complete portable method
surface remain the next integration boundary. No recording, line-out, new
filesystem writes, or direct OSOS installation is introduced.

Build with `AUDIO_MIX_GATE=1 AUDIO_STREAM_GATE=1 AUDIO_GATE=1
POWER_LIFECYCLE_GATE=1 NATIVE_KERNEL_GATE=1 LINEAGE_GATE=1`.
The self-contained kit is `phase1-audio-mix7-simple`; its build report records
the exact image hash, checks, and qualification status.

## Revision 1 failure and revision 2 correction

Revision 1 (`01bc725278ac5f60a441dfe5a436556fa766a9ccc68822efec02319772af7058`)
produced a beep then `MIX E06`. Its kit is preserved, not qualified. The mixer
could prepare up to twelve output blocks after only one source refill. DMA
advancing during that catch-up created more output space without a matching
producer turn, allowing the mixer to run past the supplied source data.

Revision 2 alternates source refill and one mixed output block, at most twelve
times per cooperative callback. It keeps the original reserve, native DMA
transport, retirement ledger, deliberate starvation, and strict underrun checks.
Revision 2 reached one complete pass but intermittently failed with `MIX E05`,
including after `44K STER`. It is not qualified. Result reports are treated
as restored under the operator's standing instruction.

## Revision 3: cooperative refill during guest execution and raster work

Whole raster renders and guest-frame/core-step pairs previously had no internal
refill points. Revision 3 services native audio from QuickJS's main-thread
interrupt callback, between guest execution and core stepping, and between
eight-row raster strips. The Rust `cooperative-audio` feature is selected only
for this mixer gate. It retains the damage plan, painter order, clipping, and
framebuffer format; the native callback must not re-enter Rust UI or JavaScript.
The guest execution deadline remains unchanged, including time spent refilling.

The output reserve increases from 12,288 to 14,336 native frames (about 325 ms),
with source refill before each output block and a fourteen-block callback bound.
The DMA ring, IRQ implementation, retirement ledger, and strict underrun checks
are unchanged. Existing renderer tests and native/stub checks pass; physical
qualification requires three consecutive complete battery sequences to expose
the previously intermittent output starvation, followed by lifecycle checks.

## Revision 4: presentation fairness and incremental preparation

Revision 3 remained at `11K STER`; the operator reported that `MIX FOUR` did
not start. The wheel no longer moved the app and Left+Right did not display
`MIX STOP`. This is the last visible label, not proof of the internal gate stage.
The main loop could skip presentation indefinitely whenever scheduler debt
remained after a 32-step guest batch. Mixer work adds to that debt.

The mixer gate now takes at most four guest steps before returning to controls,
native stage advancement, and a presentation opportunity, even with pending
ticks. It retains pending work and the existing scheduler overflow policy.
Other builds keep their previous catch-up behavior.

Native preparation now queues one output block per callback, funding it with
at most two source chunks per source. DMA starts only after the full output
reserve is ready. `MIX PRIME` identifies four-source preparation, followed
automatically by `MIX FOUR`. A two-second preparation deadline reports `MIX E14`;
a two-second missing format completion reports `MIX E15`. Rate, underrun,
retirement, and end-count assertions remain strict. Hardware confirmation is
pending; this revision does not claim the reported stall has been reproduced.

## Revision 5: source-major mixer and bounded refill catch-up

Revision 4 reached `MIX PRIME`, then `MIX FOUR`, then `MIX E05`, without audible
mixed sound. It is not qualified. The transition now appears on the display,
but the native output queue still starves.

The mixer now accumulates 64-frame strips, walking each source sequentially
instead of switching between four rings 64 KiB apart on every output sample.
Its 512-byte int32 accumulator keeps the working set small. Source metadata is
hoisted out of the inner loop; lower-rate repeated samples reuse their scaled
value. Zero gain avoids sample loads while retaining source advancement and
underrun accounting. Unity gain avoids multiplication. Scaling still truncates
toward zero and saturation still occurs only after all sources are summed.
Only the mixer and diagnostic producer compile with `-O2` rather than `-Os`.

The feeder funds only the next output block from `unmixed_frames` (written minus
mixed), rather than treating source frames already represented in DMA output as
ready input. Each source needs 1,024, 512, or 256 frames depending on its rate;
at most two 512-frame writes are needed. This also avoids a readiness race when
DMA retires frames between the producer and mixer snapshots.

Refill callbacks check elapsed time after every source chunk and mixed block,
returning after 6 ms and retaining the fourteen-block hard cap. Partial source
preparation survives a yield; mixing starts only after all non-starved sources
have been visited. This bounds producer catch-up as well as mixing, keeping
codec unmute, stage advancement, and controls from waiting on a large source
reserve refill. The transport and reserve sizes are unchanged. Actual device
throughput remains to be measured by the gate.

Output starvation remains a strict failure. `MIX E52` means starvation occurred
and a mixer call took at least 23.22 ms (one output block's duration). Otherwise,
`MIX E51` identifies starvation with a recorded unserviced refill gap of at least
300 ms; remaining cases report `MIX E05`. These are observations during the
current phase, not a claim that timing alone proves a unique root cause.

## Revision 6: queue-aware main-loop priority

Revision 5 produced audible mixed sound at `MIX FOUR`, then `MIX E05`, without
either timing qualifier. Neither measured threshold was crossed, but repeated
short service deficits could still drain the queue: a bounded refill callback
was followed by more guest or render work even when output reserve remained low.

The main loop now latches audio recovery below 8,192 native frames and releases
it at 12,288 frames. During recovery, each loop still polls input, handles power,
advances native stages, and refills audio; it defers guest and render work.
It takes at most one guest tick per pass and checks recovery before taking that
tick and again before rendering. Pending ticks remain subject to the existing
scheduler capacity/drop policy. Preparation, pause, requested end/drain, and
failures bypass this recovery rule. Hardware UI responsiveness remains part of
qualification; scheduling priority does not prove adequate producer throughput.

Producer timing now includes source generation, copy/clear, and mixing for a
complete output block, accumulated across partial callbacks without guest gaps.
On output starvation, `MIX E53` reports total producer work of at least 23.22 ms
when mixer-only `MIX E52` does not apply. `MIX E51` and unclassified `MIX E05`
remain. No underrun is accepted, and the transport, mixer math, and ring sizes
are unchanged. Revision 6 produced audible `MIX FOUR`, then `MIX E53`.

## Revision 7: scoped PP5020 audio CPU boost

The E53 report establishes an output underrun with at least one complete
producer block over its 23.22 ms budget. PocketJS previously inherited its
CPU clock without acquiring a throughput floor for audio. The actual inherited
frequency was not measured on the device; clock inheritance is a supported
performance hypothesis, not a uniquely proven cause of every earlier underrun.

The local Rockbox tree (`ipod`, main at `a0e9e0c700`) implements its PP5020
80 MHz clock in `firmware/target/arm/pp/system-pp502x.c:set_cpu_frequency`:
24 MHz intermediate, memory timing `0x303`, PLL `0x8a020a03`, unlock `0xd19b`,
repeat PLL write, 500 us settling, memory timing `0x808`, PLL clock selection.
The audio branch `origin/feature/ipvf-pcm-audio` at `0593f4f4e0` explicitly
acquires `rb->cpu_boost(true)` in `apps/plugins/ipodnative_player.inc:112`
and releases it in cleanup at line 300. This supports a scoped playback owner,
not an assumption that merely running the Rockbox bootloader boosts PocketJS.
The local RetailOS `RetailOS_1.2.1_soso.bin` SHA-256 is
`9321189b846a7317f4f575075696056e9a18c79644886a00055a402259c6fadc`.
Disassembly at file offsets `0x6a8..0x7c8`, especially `0x78c..0x7a8`,
independently confirms the PP5020 unlock/rewrite/fixed-delay sequence. This is
binary register-sequence evidence, not RetailOS source or a reused code blob.

Only `AUDIO_MIX_GATE=1` builds include the new clock owner. Before codec setup,
it checks that COP remains parked and DMA is stopped, snapshots the inherited
PLL/source/memory timing/PLL power bit, and enters 80 MHz. The position-independent
transition is copied and read back at reserved IRAM `0x40010000` (512-byte limit).
It masks IRQ/FIQ, accesses only registers and its IRAM literal pool while clocks
change, uses the independent microsecond timer, restores CPU control and interrupt
state, and never wakes COP. No SDRAM stack access occurs during clock changes.
Other DEV_INIT2 bits and the codec/LCD-specific clocks are not changed by the owner.

After successful DMA and codec teardown, the saved configuration is restored
and read back before reporting `MIX PASS`, `MIX STOP`, or completing lifecycle
cancellation. Failed teardown retains ownership for retry; it cannot silently
drop the boost while DMA may still be active. E54 identifies boost admission or
readback failure; E55 identifies restore failure. Existing E05/E51/E52/E53 remain
strict failures. Mixing, rates, buffers, gains, and transport are unchanged.

Native and stub builds, existing image/host checks, gate-off binary identity,
and the self-contained kit roundtrip passed before handoff. The operator then
reported "all was successful. please commit" on 2026-09-02, qualifying the
revision 7 procedure: six formats, three full battery batches, responsive app,
pause/starvation/drain, cancel/restart, suspend/wake, and USB playback.
The exact qualified image is 1,123,964 bytes, SHA-256
`f39611516b7c1602628f4e86820ad8147a3e4dc633f985d9aeb2c1b557007a8b`.
Restoration is accepted under the operator's standing instruction; the kit's
device install/restore receipts are retained. No separate first-tone-noise
measurement was supplied, so the known transient is not claimed resolved.
