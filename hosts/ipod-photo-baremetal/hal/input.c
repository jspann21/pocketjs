#include "a1099.h"
#include "mmio.h"

static int32_t last_wheel_position = -1;
static uint32_t retained_buttons;

static void wheel_rearm(void) {
    mmio_clear32(PP_WHEEL_CONTROL0, 0x60000000u);
    mmio_set32(PP_WHEEL_CONTROL1, 0x04000000u);
    mmio_set32(PP_WHEEL_CONTROL0, 0x60000000u);
}

void a1099_input_init(void) {
    mmio_set32(PP_DEVICE_ENABLE, PP_DEVICE_OPTO);
    mmio_set32(PP_DEVICE_RESET, PP_DEVICE_OPTO);
    a1099_delay_us(5u);
    mmio_clear32(PP_DEVICE_RESET, PP_DEVICE_OPTO);
    mmio_set32(PP_DEVICE_INIT1, PP_INIT_BUTTONS);

    mmio_write32(PP_WHEEL_CONTROL0, 0xc00a1f00u);
    mmio_write32(PP_WHEEL_CONTROL1, 0x01000000u);
    wheel_rearm();

    mmio_set32(PP_GPIOA_ENABLE, 0x20u);
    mmio_clear32(PP_GPIOA_OUTPUT_EN, 0x20u);
    a1099_crash_record.last_phase = 3u;
}

A1099InputState a1099_input_poll(void) {
    A1099InputState state = {0};
    state.buttons = retained_buttons;

    if ((mmio_read32(PP_GPIOA_INPUT_VALUE) & 0x20u) == 0u) {
        state.buttons |= A1099_BUTTON_HOLD;
    }

    if ((mmio_read32(PP_WHEEL_CONTROL1) & 0x04000000u) == 0u) {
        return state;
    }

    uint32_t raw = mmio_read32(PP_WHEEL_DATA);
    state.raw = raw;
    a1099_crash_record.input_raw = raw;

    if ((raw & 0x800000ffu) == 0x8000001au) {
        uint32_t buttons = 0u;
        if ((raw & 0x00000100u) != 0u) buttons |= A1099_BUTTON_SELECT;
        if ((raw & 0x00000200u) != 0u) buttons |= A1099_BUTTON_RIGHT;
        if ((raw & 0x00000400u) != 0u) buttons |= A1099_BUTTON_LEFT;
        if ((raw & 0x00000800u) != 0u) buttons |= A1099_BUTTON_PLAY;
        if ((raw & 0x00001000u) != 0u) buttons |= A1099_BUTTON_MENU;
        retained_buttons = buttons;
        state.buttons = buttons | (state.buttons & A1099_BUTTON_HOLD);
        state.valid = true;

        state.wheel_touched = (raw & 0x40000000u) != 0u;
        if (state.wheel_touched) {
            int32_t position = (int32_t)((raw >> 16) & 0x7fu);
            state.wheel_position = (uint8_t)position;
            if (last_wheel_position >= 0) {
                int32_t delta = position - last_wheel_position;
                if (delta < -48) delta += 96;
                if (delta > 48) delta -= 96;
                if (delta >= 2) {
                    state.wheel_delta = (int8_t)delta;
                    state.buttons |= A1099_WHEEL_CW;
                } else if (delta <= -2) {
                    state.wheel_delta = (int8_t)delta;
                    state.buttons |= A1099_WHEEL_CCW;
                }
            }
            last_wheel_position = position;
        } else {
            last_wheel_position = -1;
        }
    } else if ((raw & 0x800000ffu) != 0x8000003au && raw != 0xffffffffu) {
        a1099_delay_us(2000u);
        wheel_rearm();
    }

    mmio_set32(PP_WHEEL_CONTROL1, 0x0c000000u);
    mmio_write32(PP_WHEEL_CONTROL0, 0x400a1f00u);
    return state;
}
