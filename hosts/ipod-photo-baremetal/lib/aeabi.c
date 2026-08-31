#include <stdint.h>

/* Deliberately small, deterministic ARM EABI division support. Stage one has
 * no hot-path divides, but Clang may synthesize helpers during optimization. */
static uint32_t divide_unsigned(uint32_t numerator, uint32_t denominator) {
    if (denominator == 0u) return 0xffffffffu;
    uint32_t quotient = 0u;
    uint32_t bit = 1u;
    while (denominator <= 0x7fffffffu && denominator <= numerator >> 1) {
        denominator <<= 1;
        bit <<= 1;
    }
    while (bit != 0u) {
        if (numerator >= denominator) {
            numerator -= denominator;
            quotient |= bit;
        }
        denominator >>= 1;
        bit >>= 1;
    }
    return quotient;
}

uint32_t __aeabi_uidiv(uint32_t numerator, uint32_t denominator) {
    return divide_unsigned(numerator, denominator);
}

int32_t __aeabi_idiv(int32_t numerator, int32_t denominator) {
    if (denominator == 0) return numerator < 0 ? (int32_t)0x80000000u : 0x7fffffff;
    uint32_t negative = ((uint32_t)numerator ^ (uint32_t)denominator) >> 31;
    uint32_t un = numerator < 0 ? 0u - (uint32_t)numerator : (uint32_t)numerator;
    uint32_t ud = denominator < 0 ? 0u - (uint32_t)denominator : (uint32_t)denominator;
    uint32_t quotient = divide_unsigned(un, ud);
    return negative != 0u ? -(int32_t)quotient : (int32_t)quotient;
}
