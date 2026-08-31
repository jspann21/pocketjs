#include "a1099.h"
#include "mmio.h"

#define LCD_POLL_LIMIT 1000000u
#define LCD_CMD_RAM_ADDRESS       0x21u
#define LCD_CMD_WRITE_GRAM        0x22u
#define LCD_CMD_HORIZONTAL_WINDOW 0x44u
#define LCD_CMD_VERTICAL_WINDOW   0x45u

static uint32_t panel_type;

static bool wait_port_idle(void) {
    for (uint32_t count = 0; count < LCD_POLL_LIMIT; ++count) {
        if ((mmio_read32(PP_LCD2_PORT) & PP_LCD2_BUSY) == 0u) {
            return true;
        }
    }
    a1099_crash_record.lcd_error = 1u;
    return false;
}

static bool wait_block(uint32_t mask, uint32_t error) {
    for (uint32_t count = 0; count < LCD_POLL_LIMIT; ++count) {
        if ((mmio_read32(PP_LCD2_BLOCK_CTRL) & mask) != 0u) {
            return true;
        }
    }
    a1099_crash_record.lcd_error = error;
    return false;
}

static bool command_data(uint32_t command, uint32_t data) {
    if ((panel_type & 1u) == 0u) {
        if (!wait_port_idle()) {
            return false;
        }
        mmio_write32(PP_LCD2_PORT, PP_LCD2_COMMAND | command);
        if (!wait_port_idle()) {
            return false;
        }
        mmio_write32(PP_LCD2_PORT, PP_LCD2_COMMAND | data);
        return true;
    }

    if (!wait_port_idle()) {
        return false;
    }
    mmio_write32(PP_LCD2_PORT, PP_LCD2_COMMAND);
    mmio_write32(PP_LCD2_PORT, PP_LCD2_COMMAND | command);
    if (!wait_port_idle()) {
        return false;
    }
    mmio_write32(PP_LCD2_PORT, PP_LCD2_DATA | ((data >> 8) & 0xffu));
    mmio_write32(PP_LCD2_PORT, PP_LCD2_DATA | (data & 0xffu));
    return true;
}

static bool setup_region(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    uint32_t vertical_start = y;
    uint32_t vertical_end = y + height - 1u;
    uint32_t horizontal_end = (A1099_LCD_WIDTH - 1u) - x;
    uint32_t horizontal_start = horizontal_end - width + 1u;

    if ((panel_type & 1u) == 0u) {
        return command_data(0x12u, vertical_start) &&
               command_data(0x13u, horizontal_end) &&
               command_data(0x15u, vertical_end) &&
               command_data(0x16u, horizontal_start);
    }

    if (!command_data(LCD_CMD_HORIZONTAL_WINDOW,
                      (vertical_end << 8) | vertical_start) ||
        !command_data(LCD_CMD_VERTICAL_WINDOW,
                      (horizontal_end << 8) | horizontal_start) ||
        !command_data(LCD_CMD_RAM_ADDRESS,
                      (horizontal_end << 8) | vertical_start) ||
        !wait_port_idle()) {
        return false;
    }

    mmio_write32(PP_LCD2_PORT, PP_LCD2_COMMAND);
    mmio_write32(PP_LCD2_PORT, PP_LCD2_COMMAND | LCD_CMD_WRITE_GRAM);
    return true;
}

static bool write_pairs(const uint32_t *pairs, uint32_t pixel_count) {
    while (pixel_count != 0u) {
        if (!wait_block(PP_LCD2_BLOCK_TX_OK, 2u)) {
            return false;
        }
        mmio_write32(PP_LCD2_BLOCK_DATA, *pairs++);
        pixel_count -= 2u;
    }
    return true;
}

void a1099_backlight_set(uint8_t brightness) {
    /* The loader already qualified B02/B03 direction. Touch only the known
     * enable/value bits and leave neighbouring click-wheel pins unchanged. */
    gpio_set32(PP_GPIOB_ENABLE, 0x0cu);
    mmio_clear32(PP_GPO32_ENABLE, 0x02000000u);
    mmio_set32(PP_DEVICE_ENABLE, PP_DEVICE_PWM);

    if (brightness == 0u) {
        mmio_write32(PP_PWM0_DUTY, 0x80000000u);
        gpio_clear32(PP_GPIOB_OUTPUT_VALUE, 0x08u);
        return;
    }

    mmio_write32(PP_PWM0_DUTY, 0x80000000u | ((uint32_t)brightness << 16));
    gpio_set32(PP_GPIOB_OUTPUT_VALUE, 0x08u);
}

uint32_t a1099_lcd_type(void) {
    return panel_type;
}

bool a1099_lcd_init(void) {
    mmio_set32(PP_DEVICE_ENABLE, PP_DEVICE_LCD);
    mmio_clear32(PP_DEVICE_RESET, PP_DEVICE_LCD);

    uint32_t revision = mmio_read32(PP_IPOD_HW_REVISION);
    a1099_crash_record.reserved[0] = revision;
    if (revision == 0x00060000u) {
        /* The P98/M9829 interface revision is the original Photo panel and
         * must not be classified from the later GPIO strap scheme. */
        panel_type = 0u;
    } else {
        uint32_t straps = mmio_read32(PP_GPIOA_INPUT_VALUE);
        panel_type = (straps & 0x02u) | ((straps & 0x10u) >> 4);
    }
    a1099_crash_record.lcd_type = panel_type;
    a1099_crash_record.last_phase = 2u;

    /* The even controller family needs this short, idempotent qualification
     * sequence. The odd family retains the Apple/bootloader-qualified power
     * state in stage one; cold panel ownership is a later hardware gate. */
    if ((panel_type & 1u) == 0u) {
        return command_data(0xefu, 0x0000u) &&
               command_data(0x01u, 0x0000u) &&
               command_data(0x80u, 0x0001u) &&
               command_data(0x10u, 0x000cu) &&
               command_data(0x18u, 0x0006u) &&
               command_data(0x7eu, 0x0004u) &&
               command_data(0x7eu, 0x0005u) &&
               command_data(0x7fu, 0x0001u);
    }
    return true;
}

bool a1099_lcd_present(const uint16_t *pixels, uint32_t stride,
                       uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height) {
    if (pixels == NULL || width == 0u || height == 0u || stride < width ||
        x >= A1099_LCD_WIDTH || y >= A1099_LCD_HEIGHT ||
        width > A1099_LCD_WIDTH - x || height > A1099_LCD_HEIGHT - y ||
        (x & 1u) != 0u || (width & 1u) != 0u || (stride & 1u) != 0u ||
        (((uintptr_t)pixels) & 3u) != 0u) {
        a1099_crash_record.lcd_error = 3u;
        return false;
    }

    if (!setup_region(x, y, width, height)) {
        return false;
    }

    const uint16_t *row = pixels;
    uint32_t rows_left = height;
    while (rows_left != 0u) {
        uint32_t rows = 1u;
        while (rows < rows_left && (rows + 1u) * width * 2u <= 0x10000u) {
            ++rows;
        }
        if ((rows & 1u) != 0u && rows > 1u && rows < rows_left) {
            --rows;
        }

        uint32_t bytes = rows * width * 2u;
        mmio_write32(PP_LCD2_BLOCK_CTRL, 0x10000080u);
        mmio_write32(PP_LCD2_BLOCK_CONFIG, 0xc0010000u | (bytes - 1u));
        mmio_write32(PP_LCD2_BLOCK_CTRL, 0x34000000u);

        if (stride == width) {
            if (!write_pairs((const uint32_t *)row, rows * width)) {
                mmio_write32(PP_LCD2_BLOCK_CONFIG, 0u);
                return false;
            }
            row += rows * stride;
        } else {
            for (uint32_t current = 0; current < rows; ++current) {
                if (!write_pairs((const uint32_t *)row, width)) {
                    mmio_write32(PP_LCD2_BLOCK_CONFIG, 0u);
                    return false;
                }
                row += stride;
            }
        }

        if (!wait_block(PP_LCD2_BLOCK_READY, 4u)) {
            mmio_write32(PP_LCD2_BLOCK_CONFIG, 0u);
            return false;
        }
        mmio_write32(PP_LCD2_BLOCK_CONFIG, 0u);
        rows_left -= rows;
    }

    return true;
}
