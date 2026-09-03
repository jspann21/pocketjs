#ifndef POCKETJS_IPOD_PHOTO_AUDIO_MIXER_H
#define POCKETJS_IPOD_PHOTO_AUDIO_MIXER_H

#include <stdint.h>

/* Bounded native mixer qualification only. This is deliberately not the
 * portable globalThis.audio surface: the caller owns all guest marshalling and
 * invokes the producer from the main context. */
#define PJS_AUDIO_MIXER_MAX_STREAMS 4u
#define PJS_AUDIO_MIXER_RING_FRAMES 16384u
#define PJS_AUDIO_MIXER_OUTPUT_RATE 44100u
#define PJS_AUDIO_MIXER_OUTPUT_CHUNK 1024u
#define PJS_AUDIO_MIXER_OUTPUT_HIGHWATER 14336u
#define PJS_AUDIO_MIXER_GAIN_ONE 32768u

typedef struct {
    uint32_t rate;
    uint8_t channels;
    uint8_t ended;
    uint32_t queued_frames;
    uint32_t free_frames;
    /* Source frames not yet represented in the prepared DMA output. */
    uint32_t unmixed_frames;
    uint32_t accepted_frames;
    uint32_t consumed_frames;
    uint32_t underruns;
    uint32_t end_events;
} PjsAudioMixerStream;

typedef struct {
    uint32_t queued_output_frames;
    uint32_t free_output_frames;
    uint32_t accepted_output_frames;
    uint32_t retired_output_frames;
    uint32_t underruns;
    uint32_t end_events;
    uint8_t playing;
    uint8_t paused;
    uint8_t ended;
    uint8_t fault;
} PjsAudioMixerSnapshot;

/* Main-context lifecycle and producer API. A positive stream handle contains
 * a generation, so a stale handle from before init() cannot address a reused
 * slot. All stream controls are intentionally batch/global for this gate. */
int pjs_audio_mixer_init(void);
int pjs_audio_mixer_create(uint32_t rate, uint32_t channels);
uint32_t pjs_audio_mixer_write(int handle, const int16_t *interleaved,
                               uint32_t frames);
int pjs_audio_mixer_volume(int handle, uint32_t q15);
int pjs_audio_mixer_end(int handle);
/* Prepare at most one output block; replenish source rings between calls. */
void pjs_audio_mixer_refill(void);
int pjs_audio_mixer_play(void);
int pjs_audio_mixer_pause(void);
int pjs_audio_mixer_stop(void);
void pjs_audio_mixer_service(uint32_t now);
int pjs_audio_mixer_snapshot(int handle, PjsAudioMixerStream *out);
void pjs_audio_mixer_aggregate(PjsAudioMixerSnapshot *out);

#endif
