#ifndef POCKETJS_IPOD_PHOTO_BACKLIGHT_H
#define POCKETJS_IPOD_PHOTO_BACKLIGHT_H

#include <stdbool.h>
#include <stdint.h>

void backlight_init(void);
void backlight_set(uint8_t brightness);
void backlight_enable(bool enabled);

#endif
