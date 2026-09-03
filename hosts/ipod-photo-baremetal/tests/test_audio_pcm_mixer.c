#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_dma.h"
#include "audio_pcm_mixer.h"
#include "timer.h"

static PjsAudioDmaSnapshot dma;
static int16_t captured[PJS_AUDIO_PCM_OUTPUT_CHUNK * 2u];
static uint32_t now_us;

uint32_t timer_now_us(void) { return now_us; }
void timer_delay_us(uint32_t us) { now_us += us; }
int pjs_audio_dma_init(void) { memset(&dma, 0, sizeof dma); return 0; }
uint32_t pjs_audio_dma_write(const int16_t *p, uint32_t frames) {
    if (frames > dma.free_frames && dma.free_frames != 0u) frames = dma.free_frames;
    if (dma.queued_frames + frames > PJS_AUDIO_DMA_RING_FRAMES)
        frames = PJS_AUDIO_DMA_RING_FRAMES - dma.queued_frames;
    memcpy(captured, p, (size_t)frames * 2u * sizeof(int16_t));
    dma.queued_frames += frames;
    dma.free_frames = PJS_AUDIO_DMA_RING_FRAMES - dma.queued_frames;
    return frames;
}
int pjs_audio_dma_play(void) { dma.playing = 1u; return 0; }
int pjs_audio_dma_pause(void) { dma.playing = 0u; dma.paused = 1u; return 0; }
void pjs_audio_dma_end(void) { dma.ended = 1u; }
int pjs_audio_dma_stop(void) { memset(&dma, 0, sizeof dma); return 0; }
void pjs_audio_dma_service(uint32_t now) { (void)now; }
void pjs_audio_dma_snapshot(PjsAudioDmaSnapshot *out) { *out = dma; }
static void consume(uint32_t frames) {
    assert(frames <= dma.queued_frames);
    dma.queued_frames -= frames;
    dma.free_frames = PJS_AUDIO_DMA_RING_FRAMES - dma.queued_frames;
    dma.consumed_frames += frames;
    now_us += frames * 1000000u / 44100u;
}

static void reset_all(void) {
    memset(&dma, 0, sizeof dma);
    dma.free_frames = PJS_AUDIO_DMA_RING_FRAMES;
    now_us = 1u;
    assert(pjs_audio_pcm_mixer_init() == 0);
}

int main(void) {
    reset_all();
    assert(pjs_audio_pcm_mixer_create(48000, 2) == -1);
    assert(pjs_audio_pcm_mixer_create(44100, 0) == -1);
    int h[4];
    h[0] = pjs_audio_pcm_mixer_create(44100, 2);
    h[1] = pjs_audio_pcm_mixer_create(22050, 1);
    h[2] = pjs_audio_pcm_mixer_create(11025, 2);
    h[3] = pjs_audio_pcm_mixer_create(44100, 1);
    for (int i=0;i<4;i++) assert(h[i] > 0);
    assert(pjs_audio_pcm_mixer_create(44100, 2) == -1);
    int old = h[1];
    assert(pjs_audio_pcm_mixer_destroy(old) == 0);
    int replacement = pjs_audio_pcm_mixer_create(22050, 1);
    assert(replacement > 0 && replacement != old);
    PjsAudioPcmMixerStream snap;
    assert(pjs_audio_pcm_mixer_snapshot(old, &snap) == -1);

    reset_all();
    int a = pjs_audio_pcm_mixer_create(44100, 2);
    int b = pjs_audio_pcm_mixer_create(44100, 2);
    int16_t av[8] = {1000,-1000, 1000,-1000, 1000,-1000, 1000,-1000};
    int16_t bv[8] = {500,500, 500,500, 500,500, 500,500};
    assert(pjs_audio_pcm_mixer_write(a, av, 4) == 4);
    memset(av, 0, sizeof av); /* synchronous-copy contract */
    assert(pjs_audio_pcm_mixer_write(b, bv, 4) == 4);
    assert(pjs_audio_pcm_mixer_play(a) == 0);
    assert(pjs_audio_pcm_mixer_refill());
    assert(captured[0] == 1000 && captured[1] == -1000);
    assert(pjs_audio_pcm_mixer_play(b) == 0);
    pjs_audio_pcm_mixer_discard_prepared();
    memset(&dma, 0, sizeof dma); dma.free_frames = PJS_AUDIO_DMA_RING_FRAMES;
    assert(pjs_audio_pcm_mixer_refill());
    assert(captured[0] == 1500 && captured[1] == -500);
    assert(pjs_audio_pcm_mixer_pause(b) == 0);
    pjs_audio_pcm_mixer_discard_prepared();
    memset(&dma, 0, sizeof dma); dma.free_frames = PJS_AUDIO_DMA_RING_FRAMES;
    assert(pjs_audio_pcm_mixer_refill());
    assert(captured[0] == 1000 && captured[1] == -1000);

    reset_all();
    int e = pjs_audio_pcm_mixer_create(11025, 1);
    int16_t tone[256]; for (unsigned i=0;i<256;i++) tone[i]=1234;
    assert(pjs_audio_pcm_mixer_write(e, tone, 256) == 256);
    assert(pjs_audio_pcm_mixer_play(e) == 0);
    assert(pjs_audio_pcm_mixer_end(e) == 0);
    assert(pjs_audio_pcm_mixer_refill());
    assert(captured[0] == 1234 && captured[1] == 1234);
    consume(1024);
    pjs_audio_pcm_mixer_service(now_us);
    assert(pjs_audio_pcm_mixer_snapshot(e, &snap) == 0);
    assert(snap.ended == 1 && snap.playing == 0 && snap.end_events == 1);
    assert(snap.free_frames == PJS_AUDIO_PCM_RING_FRAMES);

    reset_all();
    int u = pjs_audio_pcm_mixer_create(44100, 2);
    assert(pjs_audio_pcm_mixer_play(u) == 0);
    assert(pjs_audio_pcm_mixer_refill());
    consume(1024); pjs_audio_pcm_mixer_service(now_us);
    assert(pjs_audio_pcm_mixer_snapshot(u, &snap) == 0 && snap.underruns == 1);
    assert(pjs_audio_pcm_mixer_refill());
    consume(1024); pjs_audio_pcm_mixer_service(now_us);
    assert(pjs_audio_pcm_mixer_snapshot(u, &snap) == 0 && snap.underruns == 1);
    int16_t one[2] = {1,1};
    assert(pjs_audio_pcm_mixer_write(u, one, 1) == 1);
    assert(pjs_audio_pcm_mixer_refill());
    consume(1024); pjs_audio_pcm_mixer_service(now_us);
    assert(pjs_audio_pcm_mixer_refill());
    consume(1024); pjs_audio_pcm_mixer_service(now_us);
    assert(pjs_audio_pcm_mixer_snapshot(u, &snap) == 0 && snap.underruns == 2);

    reset_all();
    int stale = pjs_audio_pcm_mixer_create(44100, 1);
    int16_t block[1024]; for (unsigned i=0;i<1024;i++) block[i]=9;
    assert(pjs_audio_pcm_mixer_write(stale, block, 1024) == 1024);
    assert(pjs_audio_pcm_mixer_play(stale) == 0);
    assert(pjs_audio_pcm_mixer_refill());
    assert(pjs_audio_pcm_mixer_destroy(stale) == 0);
    int fresh = pjs_audio_pcm_mixer_create(44100, 1);
    assert(fresh != stale);
    assert(pjs_audio_pcm_mixer_write(fresh, block, 10) == 10);
    consume(1024); pjs_audio_pcm_mixer_service(now_us);
    assert(pjs_audio_pcm_mixer_snapshot(fresh, &snap) == 0);
    assert(snap.consumed_frames == 0 && snap.queued_frames == 10);

    reset_all();
    int s = pjs_audio_pcm_mixer_create(44100,1);
    assert(pjs_audio_pcm_mixer_write(s, block, 100) == 100);
    assert(pjs_audio_pcm_mixer_stop(s) == 0);
    assert(pjs_audio_pcm_mixer_snapshot(s, &snap) == 0);
    assert(snap.queued_frames == 0 && snap.free_frames == PJS_AUDIO_PCM_RING_FRAMES);

    puts("audio pcm mixer tests: ok");
    return 0;
}
