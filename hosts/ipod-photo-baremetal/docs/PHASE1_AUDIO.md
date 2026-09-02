# Campaign 4: audio hardware ownership

**This candidate passed its physical procedure, with a startup crackle noted.** It follows the qualified
Campaign 3 power/lifecycle image and keeps the reversible Rockbox file handoff.
It does not authorize direct OSOS installation.

## Boundary

The first audio gate owns the A1099 WM8975 codec, headphone output routing,
24 MHz external codec clock, and PP5020 I2S transmitter. A user-triggered,
short, low-amplitude PCM tone exercises that path without guest callbacks,
DMA, FIQ, recording, or line-out. Codec transactions and FIFO waits are
bounded. Suspend, restart, and disk-mode handoff quiesce audio before storage
or display power changes.

Hold Left+Right for one second to request the diagnostic. The sample sequence
is 250 ms left-only, 100 ms silence, and 250 ms right-only at 44.1 kHz. Each
channel tone is 440 Hz with a short amplitude ramp. Headphone volume uses
WM8975 code `0x51` (-40 dB); PCM amplitude is 6.25% of full scale. The tone's
observed FIFO-feed duration must be within 10% of 600 ms. `AUD OFF` means
the bounded feed and teardown completed, not that audible output is proven.

Build with `AUDIO_GATE=1 POWER_LIFECYCLE_GATE=1 NATIVE_KERNEL_GATE=1
LINEAGE_GATE=1`. The operator procedure and fixed-hash artifacts are in
`phase1-audio-simple`. The gate does not add or change portable capabilities.

Candidate SHA-256:
`e3dd2ee1e51667b17f10abbe943d86cd439c35d104d0f63215c2de800cfbaa45`
(1,110,360 bytes). Native Rust/QuickJS and stub image checks, existing host
tests, payload hashes, and the PowerShell mock install/restore round trip
passed. The operator reported correct audio and all requested behavior on
2026-09-02, with a slight crackle at audio start. Startup transient reduction
remains open; this pass does not claim artifact-free audio. The saved restore
receipt matches the pre-install Rockbox SHA-256
`451a7ba76fc95a073d3cbbe7857724623401d430adee4b457b66c4035bf4609e`,
with no remaining handoff or package transaction. The iPod is not currently
mounted, so restore verification uses that receipt and the operator report.

**The `audio.pcm` capability remains absent.** This diagnostic is not the
portable audio module: it does not provide PCM rings, credit events,
pause/resume stream semantics, mixing, or the native-clock contract in
`contracts/spec/audio.ts`. Those require a subsequent streaming candidate
after the codec and clock path passes on hardware.

## References

The register and board-routing references are the local Rockbox PP5020 and
WM8975 definitions (`firmware/export/pp5020.h`, `wm8975.h`, and the PP target
`i2s-pp.c`, `wmcodec-pp.c`, `audio-pp.c`, `pcm-pp.c` implementations).
PocketJS does not call Rockbox code at runtime.

The user's RetailOS reference at
`C:\Users\ajrty\Desktop\Projects\ipod\.work\retailos-5.1.2.1` remains
read-only. It contains firmware binaries rather than source; no RetailOS
bytes or executable routines are copied into this candidate.

## Qualification limit

A successful build verifies the ARM image and its linked code, not audible
output, codec acknowledgment, channel routing, analog volume, noise, or
hardware clock rate. Those observations must be returned from the exact
candidate's physical procedure. An audio fault, lost display, unexpected
sound, disk error, or failed restore stops the campaign.
