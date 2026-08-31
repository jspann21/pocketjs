#ifndef POCKETJS_A1099_MMIO_H
#define POCKETJS_A1099_MMIO_H

#include <stdint.h>

static inline uint32_t mmio_read32(uintptr_t address) {
    return *(volatile uint32_t *)address;
}

static inline void mmio_write32(uintptr_t address, uint32_t value) {
    *(volatile uint32_t *)address = value;
}

static inline void mmio_set32(uintptr_t address, uint32_t mask) {
    mmio_write32(address, mmio_read32(address) | mask);
}

static inline void mmio_clear32(uintptr_t address, uint32_t mask) {
    mmio_write32(address, mmio_read32(address) & ~mask);
}

/* PP502x GPIO registers have an atomic alias at +0x800: bits 8..15 select
 * which low-byte pins change, while bits 0..7 provide their new values. */
static inline void gpio_write32(uintptr_t address, uint32_t value, uint32_t mask) {
    uint32_t low_mask = mask & 0xffu;
    mmio_write32(address + 0x800u, (low_mask << 8) | (value & low_mask));
}

static inline void gpio_set32(uintptr_t address, uint32_t mask) {
    gpio_write32(address, mask, mask);
}

static inline void gpio_clear32(uintptr_t address, uint32_t mask) {
    gpio_write32(address, 0u, mask);
}

static inline void cpu_nop(void) {
    __asm__ volatile("nop");
}

#endif
