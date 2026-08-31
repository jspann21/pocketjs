#include "lcd.h"
#include "platform.h"
#include "pp5020.h"
#include "timer.h"

#ifndef PJS_A1099_LCD_TYPE
/* M9829/P98 60 GB is the old Color/Photo panel family. Use -1 to probe GPIO. */
#define PJS_A1099_LCD_TYPE 0
#endif

#define LCD_STATE_COLD  0x4c434400u
#define LCD_STATE_READY 0x4c43444fu

static uint32_t lcd_state = LCD_STATE_COLD;
static uint8_t panel_type;

static bool wait_port(void)
{
    uint32_t left = PJS_LCD_POLL_LIMIT;
    while ((PP_LCD2_PORT & PP_LCD2_BUSY_MASK) != 0u) {
        if (--left == 0u) return false;
    }
    return true;
}

static bool wait_block(uint32_t mask)
{
    uint32_t left = PJS_LCD_POLL_LIMIT;
    while ((PP_LCD2_BLOCK_CTRL & mask) == 0u) {
        if (--left == 0u) return false;
    }
    return true;
}

static bool command_data(uint16_t command, uint16_t data)
{
    if ((panel_type & 1u) == 0u) {
        if (!wait_port()) return false;
        PP_LCD2_PORT = PP_LCD2_CMD_MASK | command;
        if (!wait_port()) return false;
        PP_LCD2_PORT = PP_LCD2_CMD_MASK | data;
    } else {
        if (!wait_port()) return false;
        PP_LCD2_PORT = PP_LCD2_CMD_MASK;
        PP_LCD2_PORT = PP_LCD2_CMD_MASK | command;
        if (!wait_port()) return false;
        PP_LCD2_PORT = PP_LCD2_DATA_MASK | (data >> 8);
        PP_LCD2_PORT = PP_LCD2_DATA_MASK | (data & 0xffu);
    }
    return true;
}

static bool setup_full_region(void)
{
    const uint16_t y0 = 0u;
    const uint16_t y1 = (uint16_t)(PJS_LCD_HEIGHT - 1u);
    const uint16_t x1 = (uint16_t)(PJS_LCD_WIDTH - 1u);
    const uint16_t x0 = 0u;

    if ((panel_type & 1u) == 0u) {
        return command_data(0x12u, y0) &&
               command_data(0x13u, x1) &&
               command_data(0x15u, y1) &&
               command_data(0x16u, x0);
    }

    if (!command_data(0x44u, (uint16_t)((y1 << 8) | y0))) return false;
    if (!command_data(0x45u, (uint16_t)((x1 << 8) | x0))) return false;
    if (!command_data(0x21u, (uint16_t)((x1 << 8) | y0))) return false;
    if (!wait_port()) return false;
    PP_LCD2_PORT = PP_LCD2_CMD_MASK;
    PP_LCD2_PORT = PP_LCD2_CMD_MASK | 0x22u;
    return true;
}

bool lcd_init(void)
{
#if PJS_A1099_LCD_TYPE < 0
    panel_type = (uint8_t)((PP_GPIOA_INPUT_VAL & 0x02u) |
                           ((PP_GPIOA_INPUT_VAL & 0x10u) >> 4));
#else
    panel_type = (uint8_t)PJS_A1099_LCD_TYPE;
#endif

    /* The installed Apple-compatible bootloader already supplies the LCD
     * bridge clock for the first hardware probe. We only issue the panel's
     * qualified controller sequence here; cold-boot clock ownership is the
     * next board-support gate. */
    if ((panel_type & 1u) == 0u) {
        if (!command_data(0xefu, 0x0000u) ||
            !command_data(0x01u, 0x0000u) ||
            !command_data(0x80u, 0x0001u) ||
            !command_data(0x10u, 0x000cu) ||
            !command_data(0x18u, 0x0006u) ||
            !command_data(0x7eu, 0x0004u) ||
            !command_data(0x7eu, 0x0005u) ||
            !command_data(0x7fu, 0x0001u)) {
            return false;
        }
    }

    lcd_state = LCD_STATE_READY;
    return true;
}

bool lcd_present(const uint16_t *pixels, size_t pixel_count)
{
    if (lcd_state != LCD_STATE_READY || pixels == 0 || pixel_count != PJS_FRAME_PIXELS ||
        (((uintptr_t)pixels) & 3u) != 0u) {
        return false;
    }
    if (!setup_full_region()) return false;

    const uint32_t *words = (const uint32_t *)(const void *)pixels;
    uint32_t rows_left = PJS_LCD_HEIGHT;

    /* The bridge's transfer length field is limited to 0x10000 bytes. A
     * 220x176 frame is 77,440 bytes, so submit 148 rows and then 28 rows while
     * the panel's full-screen GRAM window remains active. */
    while (rows_left != 0u) {
        uint32_t rows = rows_left > 148u ? 148u : rows_left;
        uint32_t byte_count = PJS_LCD_WIDTH * rows * 2u;
        size_t word_count = ((size_t)PJS_LCD_WIDTH * rows) / 2u;

        PP_LCD2_BLOCK_CTRL = 0x10000080u;
        PP_LCD2_BLOCK_CONFIG = 0xc0010000u | (byte_count - 1u);
        PP_LCD2_BLOCK_CTRL = 0x34000000u;

        for (size_t index = 0; index < word_count; ++index) {
            if (!wait_block(PP_LCD2_BLOCK_TXOK)) {
                PP_LCD2_BLOCK_CONFIG = 0u;
                return false;
            }
            PP_LCD2_BLOCK_DATA = *words++;
        }

        if (!wait_block(PP_LCD2_BLOCK_READY)) {
            PP_LCD2_BLOCK_CONFIG = 0u;
            return false;
        }
        PP_LCD2_BLOCK_CONFIG = 0u;
        rows_left -= rows;
    }
    return true;
}

bool lcd_ready(void)
{
    return lcd_state == LCD_STATE_READY;
}

uint8_t lcd_panel_type(void)
{
    return panel_type;
}

uint16_t lcd_rgb565_swapped(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t native = (uint16_t)(((uint16_t)(r & 0xf8u) << 8) |
                                 ((uint16_t)(g & 0xfcu) << 3) |
                                 ((uint16_t)b >> 3));
    return (uint16_t)((native << 8) | (native >> 8));
}
