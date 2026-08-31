#include "input.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    PjsInputState state = {0};
    uint32_t packet = 0x8000001au | 0x40000000u | (0x2au << 16) |
                      0x00000100u | 0x00000800u;
    assert(input_decode_packet(packet, &state));
    assert((state.buttons & PJS_BUTTON_SELECT) != 0u);
    assert((state.buttons & PJS_BUTTON_PLAY) != 0u);
    assert(state.wheel_touched);
    assert(state.wheel_position == 0x2au);
    assert(!input_decode_packet(0xffffffffu, &state));
    puts("input decoder: OK");
    return 0;
}
