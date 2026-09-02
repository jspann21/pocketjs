# Campaign 4: native-clock PCM streaming

**Revision 2 passed its complete physical procedure.** It follows the audio
ownership checkpoint `7db0bb8`. The passed polling-tone image and its saved
install/restore evidence remain unchanged in `phase1-audio-simple`.

## Scope

The native sink adds a fixed-capacity stereo 44.1 kHz PCM ring and PP5020 DMA
channel 0 output. Completion interrupts retire queued frames and schedule
the next transfer independently of the guest and display loop. The IRQ path
does not allocate, invoke JavaScript, perform I2C operations, or flush caches.
The main context owns codec operations and producer writes.

The diagnostic producer runs an approximately eleven-second batch:

1. Settle the muted codec and feed a silence lead-in before unmute.
2. Play alternating left, right, and both-channel half-second notes while
   the normal PocketJS app continues stepping and rendering.
3. Pause for one second and verify the queued PCM and consumed-frame count
   remain stable, then resume the same queue.
4. Stop producing for one second, require exactly one underrun episode,
   feed again, and verify recovery without another underrun.
5. Fade into a silence tail, signal end, and require all submitted source
   frames to retire before reporting completion and shutting audio down.

`STR PASS` proves the instrumented software checks completed. Audible timing,
noise, channel routing, concurrent screen/input behavior, and lifecycle
transitions still require the physical procedure. A repeat chord cancels the
batch. Suspend, restart, disk handoff, and guest failure stop the native sink.
Active playback suppresses inactivity sleep; explicit suspend remains available.

## Startup transient

The prior candidate produced correct audio with a slight startup crackle.
This candidate adds muted analog settling and a clocked silence lead-in,
and avoids codec power cycling for the pause/resume step. **Reduction of the
crackle is not yet proven.** The operator must report whether it persists,
improves, or worsens, including any transient on pause, resume, or stop.

## Capability boundary

This is a native sink qualification, not `globalThis.audio`. The portable
`audio.pcm` capability remains absent until all stream formats, multiple
stream handling, volume, borrowed-buffer copying, credit/underrun/ended
events, and tick-boundary delivery in `contracts/spec/audio.ts` are shipped
and validated. The diagnostic's single fixed-format ring does not claim that
contract. No recording, line-out, writable app filesystem, or direct OSOS
installation is introduced.

Build flags: `AUDIO_STREAM_GATE=1 AUDIO_GATE=1 POWER_LIFECYCLE_GATE=1
NATIVE_KERNEL_GATE=1 LINEAGE_GATE=1`. The self-contained operator kit is
`phase1-audio-stream2-simple`. The firmware references remain read-only.

## Revision 1 result and refill correction

Revision 1 (`5a70b549c4ae30702c129a865b511c40101a7a38d210f5687c581e7e1eb07554`)
failed with `STR PLAY -> STR E06`, after the left note and a brief right note.
Other behavior was reported correct. Result reports are treated as restored.
The original kit remains intact for identification; it is not qualified.

Revision 2 replaces the fixed 2,048-frame (46-ms) refill allowance with a
bounded refill to a 12,288-frame (279-ms) high-water mark. The producer runs
between individual guest frames, before and after core rendering, and every
2 KiB during synchronous LCD transfers. These main-context refill calls do
not invoke codec, guest, rendering, or diagnostic-state transitions. Pause
and deliberate starvation suppress refills; end still closes publication
exactly once. **Unexpected underruns still fail the gate.**

## Build evidence

The revision 2 candidate is 1,116,680 bytes with SHA-256
`c3f0b9a549707ceeeb791e1f3e2be815ab0ab02e47db25f96e2a2713ac1dcabd`.
Native Rust/QuickJS and stream-stub builds passed the existing host checks
and both image verifiers. The gate-off stub regression build also passed.
Disassembly confirms the 64-byte IRQ save frame; DMA handler relocations
call only native scheduling and the microsecond timer. The ring and silence
buffers are 32-byte aligned in BSS and use uncached SDRAM accesses.
On 2026-09-02 the operator reported everything worked exactly as specified.
The small crackle may have improved and occurs only at the first tone of
each sequence. Restore is treated as complete under the operator's standing
instruction. **The first-tone transient remains open.** This qualifies the
fixed-format native sink, not the complete portable audio namespace.
