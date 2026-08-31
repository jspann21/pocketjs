#ifndef POCKETJS_IPOD_PHOTO_PANIC_H
#define POCKETJS_IPOD_PHOTO_PANIC_H

#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint32_t reason;
    uint32_t pc;
    uint32_t spsr;
    uint32_t regs[14];
} PjsCrashRecord;

void panic_fault(uint32_t reason, uint32_t pc, uint32_t spsr,
                 const uint32_t *registers) __attribute__((noreturn));
void panic_code(uint32_t reason) __attribute__((noreturn));

#endif
