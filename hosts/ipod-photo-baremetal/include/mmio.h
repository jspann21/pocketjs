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

static inline void cpu_nop(void) {
    __asm__ volatile("nop");
}

#endif
