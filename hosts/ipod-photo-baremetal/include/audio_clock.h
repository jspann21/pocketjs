#ifndef PJS_AUDIO_CLOCK_H
#define PJS_AUDIO_CLOCK_H

/* Main-context only, with audio DMA stopped and COP parked. MIX gate only. */
int pjs_audio_clock_acquire(void);
int pjs_audio_clock_release(void);

#endif
