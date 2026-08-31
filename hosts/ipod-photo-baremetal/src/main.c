#include "backlight.h"
#include "input.h"
#include "lcd.h"
#include "panic.h"
#include "platform.h"
#include "pp5020.h"
#include "timer.h"

static uint16_t framebuffer[PJS_FRAME_PIXELS] __attribute__((aligned(4)));

static void disable_interrupt_sources(void)
{
    PP_CPU_INT_DIS = 0xffffffffu;
    PP_COP_INT_DIS = 0xffffffffu;
    PP_INT_FORCED_CLR = 0xffffffffu;
    PP_CPU_HI_INT_DIS = 0xffffffffu;
    PP_COP_HI_INT_DIS = 0xffffffffu;
    PP_HI_INT_FORCED_CLR = 0xffffffffu;
}

static void clear(uint16_t color)
{
    for (size_t index = 0; index < PJS_FRAME_PIXELS; ++index) {
        framebuffer[index] = color;
    }
}

static void rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                 uint16_t color)
{
    if (x >= PJS_LCD_WIDTH || y >= PJS_LCD_HEIGHT) return;
    if (width > PJS_LCD_WIDTH - x) width = PJS_LCD_WIDTH - x;
    if (height > PJS_LCD_HEIGHT - y) height = PJS_LCD_HEIGHT - y;
    for (uint32_t yy = y; yy < y + height; ++yy) {
        size_t row = (size_t)yy * PJS_LCD_WIDTH + x;
        for (uint32_t xx = 0u; xx < width; ++xx) framebuffer[row + xx] = color;
    }
}

static void render_color_bars(uint32_t phase)
{
    static const uint8_t colors[8][3] = {
        {255u, 255u, 255u}, {255u, 255u, 0u}, {0u, 255u, 255u}, {0u, 255u, 0u},
        {255u, 0u, 255u}, {255u, 0u, 0u}, {0u, 0u, 255u}, {0u, 0u, 0u},
    };
    for (uint32_t bar = 0u; bar < 8u; ++bar) {
        uint32_t shifted = (bar + phase) & 7u;
        uint16_t color = lcd_rgb565_swapped(colors[shifted][0], colors[shifted][1],
                                            colors[shifted][2]);
        rect(bar * 28u, 0u, bar == 7u ? 24u : 28u, PJS_LCD_HEIGHT, color);
    }
}

static void render_input(const PjsInputState *input, uint32_t heartbeat,
                         uint32_t pattern)
{
    uint16_t black = lcd_rgb565_swapped(0u, 0u, 0u);
    uint16_t grey = lcd_rgb565_swapped(48u, 48u, 48u);
    uint16_t white = lcd_rgb565_swapped(255u, 255u, 255u);
    uint16_t green = lcd_rgb565_swapped(0u, 230u, 50u);
    uint16_t red = lcd_rgb565_swapped(230u, 0u, 30u);
    uint16_t cyan = lcd_rgb565_swapped(0u, 220u, 255u);
    uint16_t yellow = lcd_rgb565_swapped(255u, 220u, 0u);

    if ((pattern & 1u) == 0u) clear(black);
    else render_color_bars(pattern >> 1);

    rect(0u, 0u, PJS_LCD_WIDTH, 12u, grey);
    rect(heartbeat != 0u ? 207u : 199u, 2u, 8u, 8u, white);

    const uint32_t bit[5] = {
        PJS_BUTTON_MENU, PJS_BUTTON_LEFT, PJS_BUTTON_SELECT,
        PJS_BUTTON_RIGHT, PJS_BUTTON_PLAY,
    };
    const uint16_t active[5] = { red, cyan, green, cyan, yellow };
    for (uint32_t index = 0u; index < 5u; ++index) {
        uint16_t color = (input->buttons & bit[index]) != 0u ? active[index] : grey;
        rect(12u + index * 41u, 132u, 32u, 32u, color);
    }

    uint32_t wheel_position = input->wheel_position > 95u ? 95u : input->wheel_position;
    uint32_t wheel_x = 4u + ((wheel_position * 35u) >> 4);
    rect(4u, 114u, 208u, 4u, grey);
    rect(wheel_x, 110u, 4u, 12u, input->wheel_touched ? cyan : white);

    /* Panel type and packet validity are encoded as diagnostic chips. */
    rect(4u, 18u, 12u, 12u, input->valid_packet ? green : red);
    for (uint32_t bit_index = 0u; bit_index < 2u; ++bit_index) {
        uint16_t color = ((lcd_panel_type() >> bit_index) & 1u) != 0u ? yellow : grey;
        rect(22u + bit_index * 16u, 18u, 12u, 12u, color);
    }

    if (input->hold) {
        rect(0u, 46u, PJS_LCD_WIDTH, 44u, red);
        rect(12u, 58u, PJS_LCD_WIDTH - 24u, 20u, black);
    }
}

void kernel_main(void)
{
    disable_interrupt_sources();
    input_init();
    if (!lcd_init()) panic_code(0x4c434401u); /* LCD1 */
    backlight_init();

    PjsInputState previous = {0};
    uint32_t pattern = 0u;
    uint32_t next_frame = timer_now_us();
    uint32_t exit_chord_start = 0u;

    for (;;) {
        PjsInputState input;
        input_poll(&input);

        if ((input.buttons & PJS_BUTTON_SELECT) != 0u &&
            (previous.buttons & PJS_BUTTON_SELECT) == 0u) {
            pattern = (pattern + 1u) & 15u;
        }

        uint32_t chord = PJS_BUTTON_MENU | PJS_BUTTON_PLAY;
        if ((input.buttons & chord) == chord) {
            if (exit_chord_start == 0u) exit_chord_start = timer_now_us();
            if ((uint32_t)(timer_now_us() - exit_chord_start) >= 2000000u) {
                pp_reboot();
            }
        } else {
            exit_chord_start = 0u;
        }

        uint32_t now = timer_now_us();
        render_input(&input, (now >> 19) & 1u, pattern);
        if (!lcd_present(framebuffer, PJS_FRAME_PIXELS)) panic_code(0x4c434402u); /* LCD2 */

        previous = input;
        next_frame += PJS_PROBE_FRAME_US;
        if ((int32_t)(timer_now_us() - next_frame) >= 0) {
            next_frame = timer_now_us();
        } else {
            timer_wait_until(next_frame);
        }
    }
}
