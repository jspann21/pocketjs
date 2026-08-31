#include "a1099.h"

static uint16_t framebuffer[A1099_FB_PIXELS] __attribute__((aligned(16)));

static uint16_t swap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    uint16_t native = (uint16_t)(((uint16_t)(red & 0xf8u) << 8) |
                                 ((uint16_t)(green & 0xfcu) << 3) |
                                 ((uint16_t)blue >> 3));
    return swap16(native);
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                      uint16_t color) {
    if (x >= A1099_LCD_WIDTH || y >= A1099_LCD_HEIGHT) {
        return;
    }
    if (width > A1099_LCD_WIDTH - x) width = A1099_LCD_WIDTH - x;
    if (height > A1099_LCD_HEIGHT - y) height = A1099_LCD_HEIGHT - y;
    for (uint32_t row = 0; row < height; ++row) {
        uint16_t *target = framebuffer + (y + row) * A1099_LCD_WIDTH + x;
        for (uint32_t column = 0; column < width; ++column) {
            target[column] = color;
        }
    }
}

static void frame_base(void) {
    const uint16_t red = rgb565(255u, 0u, 0u);
    const uint16_t green = rgb565(0u, 255u, 0u);
    const uint16_t blue = rgb565(0u, 0u, 255u);
    const uint16_t white = rgb565(255u, 255u, 255u);
    const uint16_t black = rgb565(0u, 0u, 0u);

    fill_rect(0u, 0u, A1099_LCD_WIDTH, 58u, red);
    fill_rect(0u, 58u, A1099_LCD_WIDTH, 59u, green);
    fill_rect(0u, 117u, A1099_LCD_WIDTH, 59u, blue);

    /* A one-pixel frame and crosshair expose clipping, orientation and edge
     * loss without requiring a font renderer. */
    fill_rect(0u, 0u, A1099_LCD_WIDTH, 1u, white);
    fill_rect(0u, A1099_LCD_HEIGHT - 1u, A1099_LCD_WIDTH, 1u, white);
    fill_rect(0u, 0u, 1u, A1099_LCD_HEIGHT, white);
    fill_rect(A1099_LCD_WIDTH - 1u, 0u, 1u, A1099_LCD_HEIGHT, white);
    fill_rect(109u, 0u, 2u, A1099_LCD_HEIGHT, black);
    fill_rect(0u, 87u, A1099_LCD_WIDTH, 2u, black);

    /* Panel strap value as a 2-bit marker in the lower center. */
    uint32_t type = a1099_lcd_type();
    fill_rect(96u, 150u, 12u, 12u, (type & 1u) != 0u ? white : black);
    fill_rect(112u, 150u, 12u, 12u, (type & 2u) != 0u ? white : black);
}

static void draw_inputs(uint32_t buttons, bool alive, uint32_t wheel_until,
                        uint32_t wheel_direction) {
    const uint16_t white = rgb565(255u, 255u, 255u);
    const uint16_t black = rgb565(0u, 0u, 0u);
    const uint16_t yellow = rgb565(255u, 255u, 0u);
    const uint16_t cyan = rgb565(0u, 255u, 255u);
    const uint16_t orange = rgb565(255u, 128u, 0u);

    const uint32_t flags[5] = {
        A1099_BUTTON_MENU,
        A1099_BUTTON_LEFT,
        A1099_BUTTON_SELECT,
        A1099_BUTTON_RIGHT,
        A1099_BUTTON_PLAY,
    };
    for (uint32_t index = 0; index < 5u; ++index) {
        fill_rect(12u + index * 40u, 10u, 28u, 20u,
                  (buttons & flags[index]) != 0u ? white : black);
    }

    uint16_t wheel_color = black;
    if ((int32_t)(wheel_until - a1099_usec()) > 0) {
        wheel_color = wheel_direction == A1099_WHEEL_CW ? yellow : cyan;
    }
    fill_rect(96u, 67u, 28u, 20u, wheel_color);

    fill_rect(12u, 143u, 28u, 20u,
              (buttons & A1099_BUTTON_HOLD) != 0u ? orange : black);
    fill_rect(180u, 143u, 28u, 20u, alive ? white : black);
}

static void fatal_blink(uint32_t code) {
    a1099_crash_record.last_phase = 0xe0000000u | code;
    for (;;) {
        a1099_backlight_set(220u);
        a1099_delay_us(150000u);
        a1099_backlight_set(0u);
        a1099_delay_us(150000u);
    }
}

void kernel_main(void) {
    a1099_system_init();
    a1099_backlight_set(0u);
    if (!a1099_lcd_init()) {
        fatal_blink(1u);
    }
    a1099_input_init();

    frame_base();
    draw_inputs(0u, false, 0u, 0u);
    if (!a1099_lcd_present(framebuffer, A1099_LCD_WIDTH,
                           0u, 0u, A1099_LCD_WIDTH, A1099_LCD_HEIGHT)) {
        fatal_blink(2u);
    }
    a1099_backlight_set(180u);
    a1099_crash_record.last_phase = 4u;

    uint32_t last_buttons = 0xffffffffu;
    uint32_t last_alive_flip = a1099_usec();
    uint32_t chord_start = 0u;
    bool chord_active = false;
    uint32_t wheel_until = 0u;
    uint32_t wheel_direction = 0u;
    bool alive = false;
    bool redraw = true;

    for (;;) {
        A1099InputState input = a1099_input_poll();
        uint32_t now = a1099_usec();

        if ((input.buttons & A1099_WHEEL_CW) != 0u) {
            wheel_direction = A1099_WHEEL_CW;
            wheel_until = now + 180000u;
            redraw = true;
        } else if ((input.buttons & A1099_WHEEL_CCW) != 0u) {
            wheel_direction = A1099_WHEEL_CCW;
            wheel_until = now + 180000u;
            redraw = true;
        }

        uint32_t display_buttons = input.buttons &
            ~(A1099_WHEEL_CW | A1099_WHEEL_CCW);
        if (display_buttons != last_buttons) {
            last_buttons = display_buttons;
            redraw = true;
        }

        if ((uint32_t)(now - last_alive_flip) >= 500000u) {
            last_alive_flip = now;
            alive = !alive;
            redraw = true;
        }

        uint32_t chord = A1099_BUTTON_MENU | A1099_BUTTON_PLAY;
        if ((display_buttons & chord) == chord &&
            (display_buttons & A1099_BUTTON_HOLD) == 0u) {
            if (!chord_active) {
                chord_start = now;
                chord_active = true;
            } else if ((uint32_t)(now - chord_start) >= 2000000u) {
                a1099_crash_record.last_phase = 5u;
                a1099_reboot();
            }
        } else {
            chord_active = false;
        }

        if (redraw) {
            frame_base();
            draw_inputs(display_buttons, alive, wheel_until, wheel_direction);
            if (!a1099_lcd_present(framebuffer, A1099_LCD_WIDTH,
                                   0u, 0u, A1099_LCD_WIDTH, A1099_LCD_HEIGHT)) {
                fatal_blink(3u);
            }
            redraw = false;
        }

        a1099_delay_us(2000u);
    }
}
