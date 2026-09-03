#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "audio_clock.h"
#include "audio_dma.h"
#include "audio_pcm.h"
#include "audio_pcm_mixer.h"
#include "timer.h"

static PjsAudioDmaSnapshot dma;
static uint32_t now_us;
static bool dma_end_requested;
static unsigned clock_acquires, clock_releases, codec_prepares, codec_stops;
static bool codec_muted;

uint32_t timer_now_us(void) { return ++now_us; }
void timer_delay_us(uint32_t us) { now_us += us; }
void pjs_audio_state_init(PjsAudioState *a) { memset(a,0,sizeof *a); }
int pjs_audio_pcm_prepare(PjsAudioState *a) { a->state=PJS_AUDIO_READY; codec_muted=true; codec_prepares++; return 0; }
int pjs_audio_pcm_mute(PjsAudioState *a, bool muted) { assert(a->state==PJS_AUDIO_READY); codec_muted=muted; return 0; }
int pjs_audio_stop(PjsAudioState *a) { a->state=PJS_AUDIO_OFF; codec_stops++; return 0; }
int pjs_audio_clock_acquire(void) { clock_acquires++; return 0; }
int pjs_audio_clock_release(void) { clock_releases++; return 0; }
int pjs_audio_dma_init(void) { memset(&dma,0,sizeof dma); dma.free_frames=PJS_AUDIO_DMA_RING_FRAMES; dma_end_requested=false; return 0; }
uint32_t pjs_audio_dma_write(const int16_t *p, uint32_t frames) { (void)p; if (dma.queued_frames+frames>PJS_AUDIO_DMA_RING_FRAMES) frames=PJS_AUDIO_DMA_RING_FRAMES-dma.queued_frames; dma.queued_frames+=frames; dma.free_frames=PJS_AUDIO_DMA_RING_FRAMES-dma.queued_frames; return frames; }
int pjs_audio_dma_play(void) { if (dma.ended) return -2; dma.playing=1; dma.paused=0; return 0; }
int pjs_audio_dma_pause(void) {
    /* Model the real boundary pause: the in-flight 1024-frame command retires
     * before the pause flag becomes observable. */
    if (dma.playing && dma.queued_frames != 0u) {
        uint32_t retired = dma.queued_frames > PJS_AUDIO_DMA_CHUNK_FRAMES ?
                           PJS_AUDIO_DMA_CHUNK_FRAMES : dma.queued_frames;
        dma.queued_frames -= retired;
        dma.free_frames = PJS_AUDIO_DMA_RING_FRAMES - dma.queued_frames;
        dma.consumed_frames += retired;
    }
    dma.playing=0; dma.paused=1; return 0;
}
void pjs_audio_dma_end(void) { dma_end_requested=true; if (dma.queued_frames==0) { dma.ended=1; dma.playing=0; } }
int pjs_audio_dma_stop(void) { memset(&dma,0,sizeof dma); dma.free_frames=PJS_AUDIO_DMA_RING_FRAMES; dma_end_requested=false; return 0; }
void pjs_audio_dma_service(uint32_t now) { (void)now; if (dma_end_requested && dma.queued_frames==0) { dma.ended=1; dma.playing=0; } }
void pjs_audio_dma_snapshot(PjsAudioDmaSnapshot *out) { *out=dma; }
static void consume(uint32_t frames) { assert(frames<=dma.queued_frames); dma.queued_frames-=frames; dma.free_frames=PJS_AUDIO_DMA_RING_FRAMES-dma.queued_frames; dma.consumed_frames+=frames; if (dma_end_requested && dma.queued_frames==0) { dma.ended=1; dma.playing=0; } }

static const char *poll_all(char seen[][64], int *count) {
    const char *last=0, *e;
    while ((e=pjs_audio_pcm_poll()) != 0) { assert(*count<16); strncpy(seen[*count],e,63); seen[*count][63]=0; (*count)++; last=e; }
    return last;
}

int main(void) {
    assert(pjs_audio_pcm_create_stream(48000,2)==-1);
    assert(clock_acquires==0);
    int a=pjs_audio_pcm_create_stream(44100,2);
    int b=pjs_audio_pcm_create_stream(22050,1);
    assert(a>0 && b>0 && a!=b);
    assert(clock_acquires==1 && codec_prepares==1 && codec_muted);
    uint8_t bytes[16];
    for (unsigned i=0;i<sizeof bytes;i+=2) { bytes[i]=0x34; bytes[i+1]=0x12; }
    assert(pjs_audio_pcm_write_bytes(a,bytes,sizeof bytes)==4);
    assert(pjs_audio_pcm_write_bytes(a,bytes,sizeof bytes-1)==0);
    /* b is mono: 16 bytes = 8 frames. */
    assert(pjs_audio_pcm_write_bytes(b,bytes,sizeof bytes)==8);
    pjs_audio_pcm_play(a);
    assert(dma.playing && !codec_muted && dma.queued_frames>0);
    pjs_audio_pcm_play(b);
    assert(dma.playing);
    pjs_audio_pcm_pause(a);
    assert(dma.playing); /* b continues after shared queue rebuild */
    pjs_audio_pcm_stop(a);
    pjs_audio_pcm_begin_tick();
    char events[16][64]; int n=0; poll_all(events,&n);
    bool credit=false;
    for(int i=0;i<n;i++) if (strstr(events[i],"\"t\":\"credit\"") && strstr(events[i],"\"h\":")) credit=true;
    assert(credit);

    /* Tick freeze: native retirement after begin_tick is invisible until the
     * following boundary. */
    n=0; pjs_audio_pcm_begin_tick(); poll_all(events,&n);
    if (dma.queued_frames>=1024) consume(1024);
    pjs_audio_pcm_service();
    assert(pjs_audio_pcm_poll()==0);
    pjs_audio_pcm_begin_tick();
    n=0; poll_all(events,&n);
    assert(n>0); /* credit and/or underrun from the retired block */

    /* stop() on an actively mixed stream must retire the in-flight output
     * block before truncating the source writer cursor. */
    uint8_t active[4096]; memset(active, 0x22, sizeof active);
    assert(pjs_audio_pcm_write_bytes(a, active, sizeof active) == 1024);
    pjs_audio_pcm_play(a);
    assert(dma.playing && dma.queued_frames != 0u);
    pjs_audio_pcm_stop(a);
    assert(pjs_audio_pcm_last_error() == 0u);

    int old=b;
    pjs_audio_pcm_destroy_stream(old);
    int fresh=pjs_audio_pcm_create_stream(22050,1);
    assert(fresh>0 && fresh!=old);

    /* End one stream and observe exactly one ended edge after its final
     * source data reaches the native clock. */
    uint8_t mono[512]; memset(mono,0x11,sizeof mono); /* 256 @11.025 below */
    int e=pjs_audio_pcm_create_stream(11025,1);
    assert(e>0);
    assert(pjs_audio_pcm_write_bytes(e,mono,sizeof mono)==256);
    pjs_audio_pcm_play(e);
    pjs_audio_pcm_end_stream(e);
    for (int guard=0; guard<16 && !dma.ended; guard++) {
        if (dma.queued_frames>=1024) consume(1024);
        else if (dma.queued_frames) consume(dma.queued_frames);
        pjs_audio_pcm_service();
    }
    pjs_audio_pcm_begin_tick();
    n=0; poll_all(events,&n);
    int ended=0;
    for(int i=0;i<n;i++) if (strstr(events[i],"\"t\":\"ended\"") && strstr(events[i],"\"h\":")) ended++;
    assert(ended==1);

    pjs_audio_pcm_destroy_stream(a);
    pjs_audio_pcm_destroy_stream(fresh);
    pjs_audio_pcm_destroy_stream(e);
    assert(!pjs_audio_pcm_active());
    assert(clock_releases==1 && codec_stops==1);
    assert(pjs_audio_pcm_reset()==0);
    puts("audio pcm facade tests: ok");
    return 0;
}
