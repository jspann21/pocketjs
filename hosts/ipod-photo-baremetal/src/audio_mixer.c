#include "audio_mixer.h"
#include "audio_dma.h"
#include "timer.h"

#include <stdbool.h>
#include <limits.h>

#define SOURCE_MASK (PJS_AUDIO_MIXER_RING_FRAMES - 1u)
#define LEDGER_COUNT 32u
#define LEDGER_MASK (LEDGER_COUNT - 1u)
#define MIX_STRIP_FRAMES 64u

/* Only the main context accesses source PCM and ledgers. DMA reads the
 * transport's separate uncached output ring, never these source arrays. */
static int16_t pcm[PJS_AUDIO_MIXER_MAX_STREAMS][PJS_AUDIO_MIXER_RING_FRAMES][2];
static int16_t output[PJS_AUDIO_MIXER_OUTPUT_CHUNK * 2u];
/* Keep the accumulation working set small on the PP5020's 8 KiB cache. */
static int32_t accumulation[MIX_STRIP_FRAMES * 2u];
static uint32_t next_handle = 1u;

typedef struct {
    int handle;
    uint32_t rate, channels, ratio, gain;
    uint32_t written, mixed, retired, repeat;
    uint32_t underruns, end_events;
    bool end_requested, planned_starved, ended;
} Source;

typedef struct {
    uint32_t output_end;
    uint32_t source_end[4];
    uint32_t underruns[4];
} Ledger;

typedef struct {
    Source source[4];
    Ledger ledger[LEDGER_COUNT];
    uint32_t count, head, pending, output_written;
    bool initialized, paused, tail_pending, ending, ended;
    uint8_t fault;
} Mixer;
static Mixer mixer;

static Source *find_source(int handle)
{
    if (!mixer.initialized || handle <= 0) return 0;
    for (uint32_t i = 0u; i < mixer.count; ++i)
        if (mixer.source[i].handle == handle) return &mixer.source[i];
    return 0;
}

static void latch_fault(uint8_t error)
{
    if (mixer.fault == 0u) mixer.fault = error;
}

int pjs_audio_mixer_stop(void)
{
    int result = pjs_audio_dma_stop();
    if (result != 0) return result;
    mixer.initialized = false;
    mixer.paused = false;
    return 0;
}

int pjs_audio_mixer_init(void)
{
    /* No source or ledger lifetime can be reset while output may own data. */
    int result = pjs_audio_mixer_stop();
    if (result != 0) return result;
    mixer = (Mixer){0};
    result = pjs_audio_dma_init();
    if (result != 0) return result;
    mixer.initialized = true;
    return 0;
}

int pjs_audio_mixer_create(uint32_t rate, uint32_t channels)
{
    if (!mixer.initialized || mixer.fault != 0u || mixer.output_written != 0u ||
        mixer.count == PJS_AUDIO_MIXER_MAX_STREAMS ||
        (rate != 44100u && rate != 22050u && rate != 11025u) ||
        (channels != 1u && channels != 2u) || next_handle > INT_MAX) return -1;
    Source *source = &mixer.source[mixer.count++];
    *source = (Source){
        .handle = (int)next_handle++, .rate = rate, .channels = channels,
        .ratio = rate == 44100u ? 1u : rate == 22050u ? 2u : 4u,
        .gain = PJS_AUDIO_MIXER_GAIN_ONE,
    };
    return source->handle;
}

uint32_t pjs_audio_mixer_write(int handle, const int16_t *interleaved,
                               uint32_t frames)
{
    Source *source = find_source(handle);
    if (source == 0 || interleaved == 0 || mixer.fault != 0u ||
        source->end_requested || mixer.ending) return 0u;
    uint32_t queued = source->written - source->retired;
    if (queued > PJS_AUDIO_MIXER_RING_FRAMES) { latch_fault(1u); return 0u; }
    uint32_t available = PJS_AUDIO_MIXER_RING_FRAMES - queued;
    if (frames > available) frames = available;
    uint32_t slot = (uint32_t)(source - mixer.source);
    for (uint32_t i = 0u; i < frames; ++i) {
        uint32_t at = (source->written + i) & SOURCE_MASK;
        int16_t left = interleaved[i * source->channels];
        pcm[slot][at][0] = left;
        pcm[slot][at][1] = source->channels == 1u ? left : interleaved[2u * i + 1u];
    }
    source->written += frames;
    return frames;
}

int pjs_audio_mixer_volume(int handle, uint32_t q15)
{
    Source *source = find_source(handle);
    if (source == 0 || q15 > PJS_AUDIO_MIXER_GAIN_ONE || mixer.fault != 0u) return -1;
    source->gain = q15;
    return 0;
}

int pjs_audio_mixer_end(int handle)
{
    Source *source = find_source(handle);
    if (source == 0 || mixer.fault != 0u) return -1;
    source->end_requested = true;
    return 0;
}

void pjs_audio_mixer_service(uint32_t now)
{
    if (!mixer.initialized) return;
    pjs_audio_dma_service(now);
    PjsAudioDmaSnapshot dma;
    pjs_audio_dma_snapshot(&dma);
    if (dma.fault != 0u) { latch_fault(2u); return; }
    while (mixer.pending != 0u) {
        Ledger *entry = &mixer.ledger[mixer.head];
        if ((int32_t)(dma.consumed_frames - entry->output_end) < 0) break;
        for (uint32_t i = 0u; i < mixer.count; ++i) {
            Source *source = &mixer.source[i];
            uint32_t end = entry->source_end[i];
            if (end - source->retired > source->written - source->retired) {
                latch_fault(1u); return;
            }
            source->retired = end;
            source->underruns += entry->underruns[i];
        }
        mixer.head = (mixer.head + 1u) & LEDGER_MASK;
        --mixer.pending;
    }
    if (dma.ended && !mixer.ended) {
        if (!mixer.ending || mixer.pending != 0u) { latch_fault(3u); return; }
        for (uint32_t i = 0u; i < mixer.count; ++i) {
            Source *source = &mixer.source[i];
            if (!source->end_requested || source->retired != source->written) {
                latch_fault(3u); return;
            }
            source->ended = true;
            ++source->end_events;
        }
        mixer.ended = true;
    }
}

static int16_t saturate(int32_t sample)
{
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    return (int16_t)sample;
}

void pjs_audio_mixer_refill(void)
{
    if (!mixer.initialized || mixer.paused || mixer.ending ||
        mixer.fault != 0u || mixer.count == 0u) return;
    /* Prepare at most ONE block per producer turn. DMA can advance during
     * mixing; chasing its moving high-water mark here would outrun the source
     * refill that happened before this call and manufacture queued silence.
     * The caller refills sources again before requesting the next block. */
    for (uint32_t block = 0u; block < 1u; ++block) {
        pjs_audio_mixer_service(timer_now_us());
        if (mixer.fault != 0u) return;
        PjsAudioDmaSnapshot dma;
        pjs_audio_dma_snapshot(&dma);
        if (dma.queued_frames >= PJS_AUDIO_MIXER_OUTPUT_HIGHWATER ||
            dma.free_frames < PJS_AUDIO_MIXER_OUTPUT_CHUNK) return;
        if (mixer.pending == LEDGER_COUNT) { latch_fault(4u); return; }

        uint32_t position[4], repeat[4];
        bool starved[4];
        Ledger entry = {0};
        for (uint32_t i = 0u; i < mixer.count; ++i) {
            position[i] = mixer.source[i].mixed;
            repeat[i] = mixer.source[i].repeat;
            starved[i] = mixer.source[i].planned_starved;
        }
        for (uint32_t base = 0u; base < PJS_AUDIO_MIXER_OUTPUT_CHUNK;
             base += MIX_STRIP_FRAMES) {
            for (uint32_t sample = 0u; sample < MIX_STRIP_FRAMES * 2u; ++sample)
                accumulation[sample] = 0;
            /* Walk one source sequentially instead of switching between
             * four 64 KiB-separated rings on every output frame. Hoist its
             * metadata and reuse each scaled sample for its 1/2/4 repeats. */
            for (uint32_t i = 0u; i < mixer.count; ++i) {
                const Source *source = &mixer.source[i];
                uint32_t at = position[i], phase = repeat[i];
                uint32_t written = source->written, ratio = source->ratio;
                uint32_t gain = source->gain;
                bool empty = starved[i];
                uint32_t frame = 0u;
                while (frame < MIX_STRIP_FRAMES) {
                    if (at == written) {
                        if (!source->end_requested && !empty) {
                            empty = true;
                            ++entry.underruns[i];
                        }
                        break; /* Remaining accumulation already contains silence. */
                    }
                    empty = false;
                    uint32_t run = ratio - phase;
                    if (run > MIX_STRIP_FRAMES - frame) run = MIX_STRIP_FRAMES - frame;
                    if (gain != 0u) {
                        int32_t left = pcm[i][at & SOURCE_MASK][0];
                        int32_t right = pcm[i][at & SOURCE_MASK][1];
                        if (gain != PJS_AUDIO_MIXER_GAIN_ONE) {
                            left = left * (int32_t)gain / 32768;
                            right = right * (int32_t)gain / 32768;
                        }
                        for (uint32_t n = 0u; n < run; ++n) {
                            accumulation[2u * (frame + n)] += left;
                            accumulation[2u * (frame + n) + 1u] += right;
                        }
                    }
                    frame += run;
                    phase += run;
                    if (phase == ratio) { phase = 0u; ++at; }
                }
                position[i] = at;
                repeat[i] = phase;
                starved[i] = empty;
            }
            /* Four scaled int16 sources fit int32; saturate only after sum. */
            for (uint32_t sample = 0u; sample < MIX_STRIP_FRAMES * 2u; ++sample)
                output[2u * base + sample] = saturate(accumulation[sample]);
        }

        bool all_done = true;
        for (uint32_t i = 0u; i < mixer.count; ++i) {
            Source *source = &mixer.source[i];
            entry.source_end[i] = position[i];
            if (!source->end_requested || position[i] != source->written ||
                repeat[i] != 0u) all_done = false;
        }
        uint32_t accepted = pjs_audio_dma_write(output, PJS_AUDIO_MIXER_OUTPUT_CHUNK);
        if (accepted != PJS_AUDIO_MIXER_OUTPUT_CHUNK) { latch_fault(5u); return; }
        mixer.output_written += accepted;
        entry.output_end = mixer.output_written;
        mixer.ledger[(mixer.head + mixer.pending) & LEDGER_MASK] = entry;
        ++mixer.pending;
        for (uint32_t i = 0u; i < mixer.count; ++i) {
            mixer.source[i].mixed = position[i];
            mixer.source[i].repeat = repeat[i];
            mixer.source[i].planned_starved = starved[i];
        }
        if (mixer.tail_pending) {
            /* One entire zero block follows the last source sample, then the
             * transport proves FIFO drain before publishing ended. */
            mixer.ending = true;
            pjs_audio_dma_end();
            return;
        }
        if (all_done) mixer.tail_pending = true;
    }
}

int pjs_audio_mixer_play(void)
{
    if (!mixer.initialized || mixer.fault != 0u || mixer.ended || mixer.count == 0u)
        return -1;
    /* The caller primes source data and output before first play. On resume
     * keep the prepared queue intact; mixing here, before the producer gets
     * its next refill turn, could invent an underrun at the pause boundary. */
    PjsAudioDmaSnapshot dma;
    pjs_audio_dma_snapshot(&dma);
    if (dma.queued_frames == 0u) return -1;
    mixer.paused = false;
    return pjs_audio_dma_play();
}

int pjs_audio_mixer_pause(void)
{
    if (!mixer.initialized || mixer.fault != 0u) return -1;
    int result = pjs_audio_dma_pause();
    if (result == 0) mixer.paused = true;
    return result;
}

int pjs_audio_mixer_snapshot(int handle, PjsAudioMixerStream *out)
{
    if (out == 0) return -1;
    pjs_audio_mixer_service(timer_now_us());
    Source *source = find_source(handle);
    if (source == 0) return -1;
    uint32_t queued = source->written - source->retired;
    if (queued > PJS_AUDIO_MIXER_RING_FRAMES) { latch_fault(1u); return -1; }
    *out = (PjsAudioMixerStream){
        .rate = source->rate, .channels = (uint8_t)source->channels,
        .ended = source->ended ? 1u : 0u,
        .queued_frames = queued, .free_frames = PJS_AUDIO_MIXER_RING_FRAMES - queued,
        .unmixed_frames = source->written - source->mixed,
        .accepted_frames = source->written, .consumed_frames = source->retired,
        .underruns = source->underruns, .end_events = source->end_events,
    };
    return 0;
}

void pjs_audio_mixer_aggregate(PjsAudioMixerSnapshot *out)
{
    if (out == 0) return;
    pjs_audio_mixer_service(timer_now_us());
    PjsAudioDmaSnapshot dma;
    pjs_audio_dma_snapshot(&dma);
    *out = (PjsAudioMixerSnapshot){
        .queued_output_frames = dma.queued_frames, .free_output_frames = dma.free_frames,
        .accepted_output_frames = mixer.output_written,
        .retired_output_frames = dma.consumed_frames,
        .underruns = dma.underruns, .end_events = dma.end_events,
        .playing = dma.playing, .paused = dma.paused,
        .ended = mixer.ended ? 1u : 0u, .fault = mixer.fault,
    };
}
