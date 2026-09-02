#include "audio_stream_gate.h"
#include "audio_dma.h"
#include "irq.h"
#include "timer.h"

#define RATE 44100u
#define CHUNK 512u
#define RESERVE 12288u
#define LEAD_FRAMES 4410u
#define SILENT_TAIL 4410u

enum { IDLE, PRIME, FIRST, PAUSE, SECOND, STARVE, RECOVER, DRAIN };
static struct {
    PjsAudioState *codec;
    uint32_t stage, since, started, produced, drain_target;
    uint32_t pause_consumed, pause_queued;
    uint32_t mode, error;
    bool dirty, pause_checked, end_sent, cleanup_pending;
} gate;

static const int16_t sine[32] = {
    0,399,785,1138,1448,1703,1892,2009,
    2048,2009,1892,1703,1448,1138,785,399,
    0,-399,-785,-1138,-1448,-1703,-1892,-2009,
    -2048,-2009,-1892,-1703,-1448,-1138,-785,-399
};
static int16_t samples[CHUNK * 2u];

static void status(uint32_t mode, uint32_t error)
{
    gate.mode = mode;
    gate.error = error;
    gate.dirty = true;
}

static void generate(uint32_t frames, bool silence)
{
    for (uint32_t i = 0; i < frames; ++i) {
        uint32_t frame = gate.produced + i;
        int32_t sample = 0;
        uint32_t channel = 0;
        uint32_t tail_frame = silence ? frame - (gate.drain_target - SILENT_TAIL) : 0u;
        if ((!silence || tail_frame < 128u) && frame >= LEAD_FRAMES) {
            frame -= LEAD_FRAMES;
            uint32_t note_frame = frame % (RATE / 2u);
            channel = (frame / (RATE / 2u)) % 3u;
            uint32_t gain = 128u;
            if (note_frame < 128u) gain = note_frame;
            else if (RATE / 2u - note_frame < 128u) gain = RATE / 2u - note_frame;
            sample = sine[((note_frame * 20924u) >> 16) & 31u];
            sample = sample * (int32_t)gain / 128;
            if (silence) sample = sample * (int32_t)(128u - tail_frame) / 128;
        }
        samples[2u * i] = channel != 1u ? (int16_t)sample : 0;
        samples[2u * i + 1u] = channel != 0u ? (int16_t)sample : 0;
    }
}

static void fill(uint32_t chunks, bool silence)
{
    for (uint32_t i = 0; i < chunks; ++i) {
        PjsAudioDmaSnapshot state;
        pjs_audio_dma_snapshot(&state);
        if (state.fault != 0u || state.queued_frames >= RESERVE) break;
        uint32_t count = CHUNK;
        if (count > state.free_frames) count = state.free_frames;
        if (gate.stage == DRAIN && count > gate.drain_target - gate.produced)
            count = gate.drain_target - gate.produced;
        if (count == 0u) break;
        generate(count, silence);
        uint32_t accepted = pjs_audio_dma_write(samples, count);
        gate.produced += accepted;
        if (accepted != count) break;
    }
}

void pjs_audio_stream_gate_refill(void)
{
    if (gate.stage == IDLE || gate.stage == PAUSE || gate.stage == STARVE ||
        gate.end_sent) return;
    /* Restore the high-water mark, not a fixed 46-ms allowance per outer
     * loop. The loop stays bounded even if DMA consumes data while we fill. */
    fill(RESERVE / CHUNK, gate.stage == DRAIN);
    if (gate.stage == DRAIN && gate.produced == gate.drain_target) {
        pjs_audio_dma_end();
        gate.end_sent = true;
    }
}

int pjs_audio_stream_gate_cancel(void)
{
    int codec_result = 0;
    if (gate.codec != 0 && gate.codec->state == PJS_AUDIO_READY) {
        codec_result = pjs_audio_pcm_mute(gate.codec, true);
        timer_delay_us(10000u);
    }
    int result = pjs_audio_dma_stop();
    gate.cleanup_pending = result != 0;
    if (gate.codec != 0) {
        int close_result = pjs_audio_stop(gate.codec);
        if (codec_result == 0) codec_result = close_result;
    }
    gate.codec = 0;
    gate.stage = IDLE;
    if (result == 0) result = codec_result;
    status(result == 0 ? 32u : 33u, result == 0 ? 0u : 8u);
    return result;
}

static void fail(uint32_t error)
{
    (void)pjs_audio_stream_gate_cancel();
    status(33u, error);
}

int pjs_audio_stream_gate_start(PjsAudioState *audio)
{
    if (pjs_audio_stream_gate_active()) return -1;
    /* Do not reset the codec/I2S block while a previously faulted transfer
     * could still own it. Reinitialization follows confirmed DMA quiescence. */
    int stopped = pjs_audio_dma_stop();
    if (stopped != 0) {
        gate.cleanup_pending = true;
        status(33u, 8u);
        return stopped;
    }
    gate.codec = audio;
    gate.produced = 0u;
    gate.end_sent = false;
    gate.pause_checked = false;
    int result = pjs_audio_pcm_prepare(audio);
    if (result != 0) { fail(1u); return result; }
    gate.cleanup_pending = true;
    result = pjs_audio_dma_init();
    if (result != 0) { fail(2u); return result; }
    gate.stage = PRIME;
    fill(RESERVE / CHUNK, false);
    if (gate.produced < RESERVE) { fail(3u); return -1; }
    result = pjs_audio_dma_play();
    if (result != 0) { fail(2u); return result; }
    gate.started = gate.since = timer_now_us();
    status(27u, 0u);
    return 0;
}

void pjs_audio_stream_gate_tick(uint32_t now)
{
    if (gate.stage == IDLE) return;
    pjs_audio_dma_service(now);
    PjsAudioDmaSnapshot state;
    pjs_audio_dma_snapshot(&state);
    if (state.fault != 0u) { fail(10u + state.fault); return; }
    if ((uint32_t)(now - gate.started) > 20000000u) { fail(4u); return; }
    if (irq_unexpected_low() != 0u || irq_unexpected_high() != 0u) {
        fail(5u); return;
    }
    if (gate.stage != STARVE && gate.stage != RECOVER && gate.stage != DRAIN &&
        state.underruns != 0u) { fail(6u); return; }

    uint32_t elapsed = now - gate.since;
    switch (gate.stage) {
    case PRIME:
        if (elapsed >= 50000u) {
            if (state.irq_count == 0u || state.consumed_frames == 0u) {
                fail(7u); return;
            }
            if (pjs_audio_pcm_mute(gate.codec, false) != 0) { fail(1u); return; }
            gate.stage = FIRST;
            gate.since = timer_now_us();
        }
        break;
    case FIRST:
        if (elapsed >= 3000000u) {
            uint32_t expected = ((now - gate.started) / 1000u) * 441u / 10u;
            if (state.consumed_frames < expected - expected / 10u ||
                state.consumed_frames > expected + expected / 10u) {
                fail(7u); return;
            }
            if (pjs_audio_pcm_mute(gate.codec, true) != 0) { fail(1u); return; }
            if (pjs_audio_dma_pause() != 0) { fail(9u); return; }
            gate.stage = PAUSE;
            gate.since = now;
            status(28u, 0u);
        }
        break;
    case PAUSE:
        if (elapsed >= 100000u && !gate.pause_checked) {
            if (!state.paused || state.playing) { fail(9u); return; }
            gate.pause_consumed = state.consumed_frames;
            gate.pause_queued = state.queued_frames;
            gate.pause_checked = true;
        }
        if (elapsed >= 1000000u) {
            if (state.consumed_frames != gate.pause_consumed ||
                state.queued_frames != gate.pause_queued ||
                state.queued_frames == 0u) { fail(9u); return; }
            if (pjs_audio_dma_play() != 0) { fail(9u); return; }
            if (pjs_audio_pcm_mute(gate.codec, false) != 0) { fail(1u); return; }
            gate.stage = SECOND;
            gate.since = now;
            status(27u, 0u);
        }
        break;
    case SECOND:
        if (elapsed >= 3000000u) {
            gate.stage = STARVE;
            gate.since = now;
            status(29u, 0u);
        }
        break;
    case STARVE:
        if (elapsed >= 1000000u) {
            if (state.underruns != 1u) { fail(6u); return; }
            gate.stage = RECOVER;
            gate.since = now;
            status(27u, 0u);
        }
        break;
    case RECOVER:
        if (state.underruns != 1u) { fail(6u); return; }
        if (elapsed >= 2000000u) {
            gate.stage = DRAIN;
            gate.since = now;
            gate.drain_target = gate.produced + SILENT_TAIL;
            status(30u, 0u);
        }
        break;
    case DRAIN:
        if (state.underruns != 1u) { fail(6u); return; }
        if (state.ended) {
            if (!gate.end_sent || state.queued_frames != 0u ||
                state.consumed_frames != gate.produced ||
                state.submitted_frames != state.consumed_frames ||
                state.end_requests != 1u || state.end_events != 1u ||
                state.silence_frames == 0u || state.irq_count < 32u) {
                fail(7u); return;
            }
            if (pjs_audio_stream_gate_cancel() != 0) { fail(8u); return; }
            status(31u, 0u);
            return;
        }
        break;
    default: break;
    }
    pjs_audio_stream_gate_refill();
}

bool pjs_audio_stream_gate_active(void)
{
    return gate.stage != IDLE || gate.cleanup_pending;
}

bool pjs_audio_stream_gate_status(uint32_t *mode, uint32_t *error)
{
    if (!gate.dirty) return false;
    *mode = gate.mode;
    *error = gate.error;
    gate.dirty = false;
    return true;
}
