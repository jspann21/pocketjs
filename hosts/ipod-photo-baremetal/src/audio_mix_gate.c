#include "audio_stream_gate.h"
#include "audio_mixer.h"
#include "audio_dma.h"
#include "audio_clock.h"
#include "irq.h"
#include "timer.h"

#define CHUNK 512u
#define OUTPUT_RESERVE PJS_AUDIO_MIXER_OUTPUT_HIGHWATER
#define UNLIMITED UINT32_MAX

enum { IDLE, PREPARE, PRIME, FORMAT, FIRST, PAUSE, SECOND, STARVE, RECOVER, DRAIN };
typedef struct {
    int handle;
    uint32_t rate, channels, step, produced, target, route;
    uint32_t pause_consumed, pause_queued;
    bool end_sent;
} Source;
static struct {
    PjsAudioState *codec;
    Source source[4];
    uint32_t stage, format, count, started, since, play_started, mode, error, pending_error;
    uint32_t refill_finished, max_refill_gap, max_mix_us;
    uint32_t producer_work_us, max_producer_us;
    bool dirty, cleanup_pending, pause_checked, volume_changed, recovering;
} gate;
static int16_t samples[CHUNK * 2u];
static const int16_t sine[32] = {
    0,200,393,569,724,852,946,1005,1024,1005,946,852,724,569,393,200,
    0,-200,-393,-569,-724,-852,-946,-1005,-1024,-1005,-946,-852,-724,-569,-393,-200
};

static void status(uint32_t mode, uint32_t error)
{
    gate.mode = mode;
    gate.error = error;
    gate.dirty = true;
}

static void generate(const Source *source, uint32_t count)
{
    uint32_t lead = source->rate / 10u;
    uint32_t tone_end = source->target == UNLIMITED ? UNLIMITED :
                        source->target - source->rate / 20u;
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t frame = source->produced + i;
        int32_t value = 0;
        if (frame >= lead && frame < tone_end) {
            uint32_t position = frame - lead;
            uint32_t gain = position < 128u ? position : 128u;
            if (tone_end - frame < gain) gain = tone_end - frame;
            value = sine[((position * source->step) >> 16) & 31u];
            value = value * (int32_t)gain / 128;
        }
        if (source->channels == 1u) samples[i] = (int16_t)value;
        else {
            uint32_t route = source->route;
            if (route == 3u) route = frame < source->rate / 2u ? 0u : 1u;
            samples[2u * i] = route != 1u ? (int16_t)value : 0;
            samples[2u * i + 1u] = route != 0u ? (int16_t)value : 0;
        }
    }
}

/* This function is also called from LCD transfer boundaries. Do not add
 * codec, UI, or gate-stage transitions here. Faults are presented by tick. */
static void refill_blocks(void)
{
    if (gate.stage == IDLE) return;
    pjs_audio_mixer_service(timer_now_us());
    if (gate.stage == PAUSE || gate.pending_error != 0u) return;
    /* Replenish sources before EVERY mixed block. The output consumer keeps
     * advancing during these calls, so one source fill cannot fund an entire
     * mixer catch-up loop. Fourteen turns bound one cooperative callback. */
    uint32_t callback_started = timer_now_us();
    uint32_t block_limit = gate.stage == PREPARE ? 1u : OUTPUT_RESERVE / 1024u;
    for (uint32_t block = 0u; block < block_limit; ++block) {
        uint32_t piece_started = timer_now_us();
        PjsAudioDmaSnapshot dma;
        pjs_audio_dma_snapshot(&dma);
        if (dma.fault != 0u || dma.ended ||
            dma.queued_frames >= OUTPUT_RESERVE) return;
        for (uint32_t index = 0u; index < gate.count; ++index) {
            Source *source = &gate.source[index];
            if (source->end_sent || (gate.stage == STARVE && index == 1u)) continue;
            uint32_t needed = source->rate == 44100u ? 1024u :
                              source->rate == 22050u ? 512u : 256u;
            /* Fund only the NEXT output block, not an entire source reserve.
             * queued_frames includes already mixed/in-flight PCM, so it is
             * not a safe readiness test while DMA advances. All supported
             * ratios divide the native block, leaving no partial repetition. */
            for (uint32_t chunk = 0u; chunk < 2u; ++chunk) {
                PjsAudioMixerStream state;
                if (pjs_audio_mixer_snapshot(source->handle, &state) != 0) {
                    gate.pending_error = 3u; return;
                }
                if (state.unmixed_frames >= needed) break;
                uint32_t count = CHUNK;
                if (count > needed - state.unmixed_frames)
                    count = needed - state.unmixed_frames;
                if (count > state.free_frames) count = state.free_frames;
                if (count > source->target - source->produced)
                    count = source->target - source->produced;
                if (count == 0u) break;
                generate(source, count);
                uint32_t accepted = pjs_audio_mixer_write(source->handle, samples, count);
                source->produced += accepted;
                /* Reuse immediately: audible output depends on synchronous copying. */
                for (uint32_t i = 0u; i < count * source->channels; ++i) samples[i] = 0;
                if (accepted != count) { gate.pending_error = 4u; return; }
                /* Keep partially prepared sources for the next callback;
                 * do not mix until every non-starved source has been visited. */
                uint32_t chunk_finished = timer_now_us();
                if ((uint32_t)(chunk_finished - callback_started) >= 6000u) {
                    gate.producer_work_us += chunk_finished - piece_started;
                    return;
                }
            }
            if (source->produced == source->target) {
                if (pjs_audio_mixer_end(source->handle) != 0) {
                    gate.pending_error = 3u; return;
                }
                source->end_sent = true;
            }
        }
        uint32_t mix_started = timer_now_us();
        pjs_audio_mixer_refill();
        uint32_t finished = timer_now_us();
        uint32_t mix_us = finished - mix_started;
        if (gate.stage != PREPARE && mix_us > gate.max_mix_us)
            gate.max_mix_us = mix_us;
        uint32_t producer_us = gate.producer_work_us + finished - piece_started;
        gate.producer_work_us = 0u;
        if (gate.stage != PREPARE && producer_us > gate.max_producer_us)
            gate.max_producer_us = producer_us;
        /* Do not keep the main loop inside a fourteen-block catch-up while
         * unmute, input, and stage transitions need their next turn. */
        if ((uint32_t)(finished - callback_started) >= 6000u) return;
    }
}

void pjs_audio_stream_gate_refill(void)
{
    uint32_t now = timer_now_us();
    if (gate.stage != IDLE && gate.stage != PREPARE && gate.stage != PAUSE) {
        uint32_t gap = now - gate.refill_finished;
        if (gap > gate.max_refill_gap) gate.max_refill_gap = gap;
    }
    refill_blocks();
    gate.refill_finished = timer_now_us();
}

bool pjs_audio_stream_gate_needs_service(void)
{
    if (gate.stage == IDLE || gate.stage == PREPARE || gate.stage == PAUSE ||
        gate.pending_error != 0u) {
        gate.recovering = false;
        return false;
    }
    PjsAudioDmaSnapshot dma;
    pjs_audio_dma_snapshot(&dma);
    if (dma.fault != 0u || dma.underruns != 0u || !dma.playing ||
        dma.end_requests != 0u) {
        gate.recovering = false;
        return false;
    }
    /* A bounded callback is not a promise that the queue is full. When
     * reserve is low, give subsequent main-loop turns to audio until it
     * recovers. The main loop still polls controls and advances the gate;
     * guest ticks remain pending, subject to the existing scheduler cap. */
    if (dma.queued_frames < 8192u) gate.recovering = true;
    if (dma.queued_frames >= 12288u) gate.recovering = false;
    return gate.recovering;
}

int pjs_audio_stream_gate_cancel(void)
{
    int codec_result = 0;
    if (gate.codec != 0 && gate.codec->state == PJS_AUDIO_READY) {
        codec_result = pjs_audio_pcm_mute(gate.codec, true);
        timer_delay_us(10000u);
    }
    int result = pjs_audio_mixer_stop();
    if (gate.codec != 0) {
        int closed = pjs_audio_stop(gate.codec);
        if (codec_result == 0) codec_result = closed;
    }
    if (codec_result == 0) gate.codec = 0;
    gate.stage = IDLE;
    if (result == 0) result = codec_result;
    uint32_t error = result == 0 ? 0u : 8u;
    /* Do not restore clocks while DMA or codec teardown is incomplete. */
    if (result == 0 && pjs_audio_clock_release() != 0) {
        result = -1; error = 55u;
    }
    gate.cleanup_pending = result != 0;
    status(result == 0 ? 45u : 46u, error);
    return result;
}

static void fail(uint32_t error)
{
    if (pjs_audio_stream_gate_cancel() == 0) status(46u, error);
}

static int add_source(uint32_t index, uint32_t rate, uint32_t channels,
                      uint32_t hz, uint32_t route, uint32_t target)
{
    int handle = pjs_audio_mixer_create(rate, channels);
    if (handle < 1) return -1;
    /* Rounded Q16 phase steps for this diagnostic's four pitches. Keeping
     * the rate conversion to shifts also supports the freestanding stub. */
    uint32_t phase_step = hz == 330u ? 15693u : hz == 440u ? 20924u :
                          hz == 550u ? 26155u : 31386u;
    if (rate == 22050u) phase_step *= 2u;
    else if (rate == 11025u) phase_step *= 4u;
    gate.source[index] = (Source){
        .handle = handle, .rate = rate, .channels = channels,
        .step = phase_step, .route = route, .target = target,
    };
    return 0;
}

static int admission_check(void)
{
    if (pjs_audio_mixer_create(48000u, 2u) != -1 ||
        pjs_audio_mixer_create(44100u, 0u) != -1 ||
        pjs_audio_mixer_create(44100u, 3u) != -1) return -1;
    int first = 0;
    for (uint32_t i = 0u; i < 4u; ++i) {
        int handle = pjs_audio_mixer_create(44100u, 2u);
        if (handle < 1) return -1;
        if (i == 0u) first = handle;
    }
    if (pjs_audio_mixer_create(11025u, 1u) != -1) return -1;
    for (uint32_t i = 0u; i < 32u; ++i)
        if (pjs_audio_mixer_write(first, samples, CHUNK) != CHUNK) return -1;
    if (pjs_audio_mixer_write(first, samples, 1u) != 0u) return -1;
    PjsAudioMixerStream full;
    if (pjs_audio_mixer_snapshot(first, &full) != 0 ||
        full.queued_frames != 16384u || full.free_frames != 0u) return -1;
    if (pjs_audio_mixer_init() != 0 ||
        pjs_audio_mixer_write(first, samples, 1u) != 0u) return -1;
    return 0;
}

static int begin_phase(void)
{
    if (pjs_audio_pcm_mute(gate.codec, true) != 0 ||
        pjs_audio_mixer_init() != 0) return -1;
    gate.count = gate.format < 6u ? 1u : 4u;
    if (gate.count == 1u) {
        static const uint32_t rates[3] = {44100u, 22050u, 11025u};
        uint32_t rate = rates[gate.format / 2u];
        if (add_source(0u, rate, 1u + gate.format % 2u, 440u, 3u, rate) != 0)
            return -1;
    } else {
        if (add_source(0u, 44100u, 2u, 330u, 0u, UNLIMITED) != 0 ||
            add_source(1u, 22050u, 2u, 440u, 1u, UNLIMITED) != 0 ||
            add_source(2u, 11025u, 1u, 550u, 2u, UNLIMITED) != 0 ||
            add_source(3u, 44100u, 1u, 660u, 2u, UNLIMITED) != 0) return -1;
        for (uint32_t i = 0u; i < 4u; ++i)
            if (pjs_audio_mixer_volume(gate.source[i].handle,
                                       i == 3u ? 0u : 8192u * (i + 1u)) != 0)
                return -1;
    }
    gate.stage = PREPARE;
    gate.since = timer_now_us();
    gate.refill_finished = gate.since;
    gate.max_refill_gap = gate.max_mix_us = 0u;
    gate.producer_work_us = gate.max_producer_us = 0u;
    gate.recovering = false;
    status(gate.count == 1u ? 34u + gate.format : 47u, 0u);
    return 0;
}

int pjs_audio_stream_gate_start(PjsAudioState *audio)
{
    if (pjs_audio_stream_gate_active()) return -1;
    if (pjs_audio_mixer_stop() != 0) {
        gate.cleanup_pending = true; status(46u, 8u); return -1;
    }
    gate.codec = audio;
    gate.pending_error = 0u;
    gate.format = 0u;
    gate.pause_checked = false;
    gate.volume_changed = false;
    gate.cleanup_pending = true;
    if (pjs_audio_clock_acquire() != 0) { fail(54u); return -1; }
    if (pjs_audio_pcm_prepare(audio) != 0) { fail(1u); return -1; }
    if (pjs_audio_mixer_init() != 0 || admission_check() != 0) {
        fail(2u); return -1;
    }
    gate.started = timer_now_us();
    if (begin_phase() != 0) { fail(3u); return -1; }
    return 0;
}

void pjs_audio_stream_gate_tick(uint32_t now)
{
    if (gate.stage == IDLE) return;
    pjs_audio_mixer_service(now);
    PjsAudioDmaSnapshot dma;
    pjs_audio_dma_snapshot(&dma);
    PjsAudioMixerSnapshot mixed;
    pjs_audio_mixer_aggregate(&mixed);
    if (gate.pending_error != 0u) { fail(gate.pending_error); return; }
    if (dma.fault != 0u) { fail(20u + dma.fault); return; }
    if (mixed.fault != 0u) { fail(30u + mixed.fault); return; }
    if (dma.underruns != 0u) {
        /* All three remain strict output-starvation failures. Distinguish
         * an over-budget mixer block from a long unserviced main-loop gap. */
        uint32_t gap = now - gate.refill_finished;
        if (gap > gate.max_refill_gap) gate.max_refill_gap = gap;
        fail(gate.max_mix_us >= 23220u ? 52u :
             gate.max_producer_us >= 23220u ? 53u :
             gate.max_refill_gap >= 300000u ? 51u : 5u);
        return;
    }
    if ((uint32_t)(now - gate.started) > 40000000u) { fail(10u); return; }
    if (irq_unexpected_low() != 0u || irq_unexpected_high() != 0u) {
        fail(11u); return;
    }
    PjsAudioMixerStream state[4];
    for (uint32_t i = 0u; i < gate.count; ++i) {
        if (pjs_audio_mixer_snapshot(gate.source[i].handle, &state[i]) != 0) {
            fail(3u); return;
        }
        uint32_t wanted = gate.format == 6u && i == 1u &&
                          (gate.stage == RECOVER || gate.stage == DRAIN) ? 1u : 0u;
        if (gate.stage != STARVE && state[i].underruns != wanted) {
            fail(6u); return;
        }
    }
    uint32_t elapsed = now - gate.since;
    if (gate.stage == PREPARE) {
        if (dma.queued_frames >= OUTPUT_RESERVE) {
            if (pjs_audio_mixer_play() != 0) { fail(3u); return; }
            gate.stage = PRIME;
            gate.play_started = gate.since = timer_now_us();
            gate.refill_finished = gate.since;
            status(gate.count == 1u ? 34u + gate.format : 40u, 0u);
        } else if (elapsed >= 2000000u) {
            fail(14u); return;
        }
    } else if (gate.stage == PRIME && elapsed >= 50000u) {
        if (dma.irq_count == 0u || pjs_audio_pcm_mute(gate.codec, false) != 0) {
            fail(1u); return;
        }
        gate.stage = gate.count == 1u ? FORMAT : FIRST;
        gate.since = timer_now_us();
    } else if ((gate.stage == FORMAT || gate.stage == DRAIN) && dma.ended) {
        /* Each single-source format contains exactly one source-second.
         * Conversion must produce one native second plus bounded padding. */
        if (gate.stage == FORMAT &&
            (dma.consumed_frames < 44100u || dma.consumed_frames > 46148u ||
             (uint32_t)(now - gate.play_started) < 900000u ||
             (uint32_t)(now - gate.play_started) > 1800000u)) {
            fail(13u); return;
        }
        for (uint32_t i = 0u; i < gate.count; ++i)
            if (!state[i].ended || state[i].end_events != 1u ||
                state[i].queued_frames != 0u ||
                state[i].accepted_frames != gate.source[i].target ||
                state[i].consumed_frames != gate.source[i].target) {
                fail(7u); return;
            }
        if (gate.stage == FORMAT) {
            ++gate.format;
            if (begin_phase() != 0) fail(3u);
            return;
        }
        if (pjs_audio_stream_gate_cancel() != 0) return;
        status(44u, 0u);
        return;
    } else if (gate.stage == FIRST && elapsed >= 2000000u) {
        if (pjs_audio_pcm_mute(gate.codec, true) != 0 ||
            pjs_audio_mixer_pause() != 0) { fail(9u); return; }
        gate.stage = PAUSE; gate.since = now;
        status(41u, 0u);
    } else if (gate.stage == PAUSE) {
        if (elapsed >= 100000u && !gate.pause_checked) {
            if (!dma.paused || dma.playing) { fail(9u); return; }
            for (uint32_t i = 0u; i < gate.count; ++i) {
                gate.source[i].pause_consumed = state[i].consumed_frames;
                gate.source[i].pause_queued = state[i].queued_frames;
            }
            gate.pause_checked = true;
        }
        if (elapsed >= 1000000u) {
            for (uint32_t i = 0u; i < gate.count; ++i)
                if (state[i].consumed_frames != gate.source[i].pause_consumed ||
                    state[i].queued_frames != gate.source[i].pause_queued ||
                    state[i].queued_frames == 0u) { fail(9u); return; }
            if (pjs_audio_mixer_play() != 0 ||
                pjs_audio_pcm_mute(gate.codec, false) != 0) { fail(9u); return; }
            gate.stage = SECOND; gate.since = now;
            gate.refill_finished = timer_now_us();
            status(40u, 0u);
        }
    } else if (gate.stage == SECOND) {
        if (elapsed >= 500000u && !gate.volume_changed) {
            if (pjs_audio_mixer_volume(gate.source[0].handle, 0u) != 0 ||
                pjs_audio_mixer_volume(gate.source[3].handle, 32768u) != 0) {
                fail(12u); return;
            }
            gate.volume_changed = true;
        }
        if (elapsed >= 2000000u) {
            gate.stage = STARVE; gate.since = now;
            status(42u, 0u);
        }
    } else if (gate.stage == STARVE && elapsed >= 1000000u) {
        for (uint32_t i = 0u; i < gate.count; ++i)
            if (state[i].underruns != (i == 1u ? 1u : 0u)) { fail(6u); return; }
        gate.stage = RECOVER; gate.since = now;
        status(40u, 0u);
    } else if (gate.stage == RECOVER && elapsed >= 2000000u) {
        for (uint32_t i = 0u; i < gate.count; ++i)
            gate.source[i].target = gate.source[i].produced + gate.source[i].rate / 5u;
        gate.stage = DRAIN; gate.since = now;
        status(43u, 0u);
    }
    /* A missed completion must become a visible bounded failure, not an
     * indefinitely stale final-format label. Normal rate checks still apply. */
    if (gate.stage == FORMAT &&
        (uint32_t)(now - gate.play_started) >= 2000000u) {
        fail(15u); return;
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
    *mode = gate.mode; *error = gate.error; gate.dirty = false;
    return true;
}
