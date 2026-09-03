# Phase 1 Campaign 5: portable `audio.pcm`

This campaign exposes the hardware-qualified A1099 four-stream PCM engine through the portable PocketJS audio contract in `contracts/spec/audio.ts`.

## Architecture

- `globalThis.audio` implements `createStream`, `destroyStream`, `writePcm`, `play`, `pause`, `stop`, `setVolume`, `endStream`, and `poll`.
- Four fixed stream slots use generation-tagged handles so stale guest handles cannot affect a reused slot.
- Accepted source formats remain the Campaign-4 qualified set: signed 16-bit PCM, mono/stereo, 44.1/22.05/11.025 kHz.
- Guest PCM is synchronously copied before `writePcm` returns. Typed-array offsets do not need ARM alignment because the bridge decodes little-endian s16 bytes through an aligned staging strip.
- Per-stream pause/stop/destroy rebuild the shared prepared queue at a completed DMA boundary. Other streams rewind only speculative `mixed` cursors to their audio-clock-retired positions, so they continue without losing source frames.
- The portable queue targets 4096 prepared output frames and gives native service priority below 2048 frames. This is intentionally shorter than the Campaign-4 diagnostic reserve.
- `ended`, `underrun`, and `credit` facts are frozen at the guest tick boundary and drained one JSON event per `poll()` call.
- When the last live stream is destroyed or the guest is replaced, DMA is stopped, the FIFO is cleared, the WM8975 is muted/powered down, and the scoped PP5020 clock owner is released in the already-qualified order.

## Rockbox / RetailOS evidence

No new codec, I2S, DMA, or clock MMIO sequence is introduced here. The native transport remains the qualified Campaign-4 implementation. Rockbox `pcm-pp.c` independently supports the boundary used for queue rebuild: stop the DMA transfer, then wait for the IIS TX FIFO to become empty before teardown. RetailOS evidence continues to be used only as binary register-sequence corroboration for the scoped PP5020 clock transition.

## Contract details

- `stop(handle)` pauses at a completed DMA boundary and flushes only that source ring.
- `endStream(handle)` emits one `ended` after that stream's final source frame reaches the hardware clock, then auto-pauses that stream.
- Starvation emits one `underrun` per starved episode while other streams keep mixing.
- Credit mirrors source-ring free space and is corrected only at tick boundaries.
- Destroyed stream events are purged; generation checks also prevent old retirement ledgers from touching a new stream.
- Guest replacement resets every portable stream before QuickJS state is released.

## Still outside this gate

- recording / microphone input
- line-out routing
- new audio codec, I2S, DMA, or clock sequences
- direct OSOS installation

Hardware qualification remains false until a physical A1099 runs a real framework `createWavPlayer()` package through the portable namespace.
