#include "a1099.h"
#include "mmio.h"

volatile A1099CrashRecord a1099_crash_record
    __attribute__((section(".noinit"), aligned(16)));

static void record_initialize(void) {
    volatile uint32_t *words = (volatile uint32_t *)&a1099_crash_record;
    for (size_t index = 0; index < sizeof(a1099_crash_record) / sizeof(uint32_t); ++index) {
        words[index] = 0;
    }
    a1099_crash_record.magic = A1099_CRASH_MAGIC;
    a1099_crash_record.version = A1099_CRASH_VERSION;
}

uint32_t a1099_usec(void) {
    return mmio_read32(PP_USEC_TIMER);
}

void a1099_delay_us(uint32_t microseconds) {
    uint32_t start = a1099_usec();
    uint32_t probe = start;
    for (uint32_t spin = 0; spin < 256u && probe == start; ++spin) {
        probe = a1099_usec();
        cpu_nop();
    }
    if (probe != start) {
        while ((uint32_t)(a1099_usec() - start) < microseconds) cpu_nop();
        return;
    }
    volatile uint32_t loops = microseconds * 12u + 1u;
    while (loops-- != 0u) cpu_nop();
}

void a1099_system_init(void) {
    if (a1099_crash_record.magic != A1099_CRASH_MAGIC ||
        a1099_crash_record.version != A1099_CRASH_VERSION) {
        record_initialize();
    }
    a1099_crash_record.boot_count += 1u;
    a1099_crash_record.last_phase = 1u;
    a1099_crash_record.lcd_error = 0u;
    a1099_crash_record.input_raw = 0u;

    mmio_write32(PP_CPU_INT_DISABLE, 0xffffffffu);
    mmio_write32(PP_COP_INT_DISABLE, 0xffffffffu);
    mmio_write32(PP_CPU_HI_INT_DISABLE, 0xffffffffu);
    mmio_write32(PP_COP_HI_INT_DISABLE, 0xffffffffu);
    mmio_write32(PP_CACHE_CONTROL, 0u);

    mmio_set32(PP_DEVICE_ENABLE, PP_DEVICE_LCD | PP_DEVICE_PWM | PP_DEVICE_OPTO);
    mmio_clear32(PP_DEVICE_RESET, PP_DEVICE_LCD | PP_DEVICE_PWM | PP_DEVICE_OPTO);
}

void a1099_reboot(void) {
    mmio_set32(PP_DEVICE_RESET, PP_DEVICE_SYSTEM);
    for (;;) cpu_nop();
}

void a1099_fault(uint32_t cause, uint32_t lr, uint32_t spsr) {
    if (a1099_crash_record.magic != A1099_CRASH_MAGIC ||
        a1099_crash_record.version != A1099_CRASH_VERSION) {
        record_initialize();
    }
    a1099_crash_record.fault_count += 1u;
    a1099_crash_record.fault_cause = cause;
    a1099_crash_record.fault_lr = lr;
    a1099_crash_record.fault_spsr = spsr;
    a1099_crash_record.fault_usec = a1099_usec();
    a1099_crash_record.last_phase = 0xf0000000u | cause;
    a1099_reboot();
}
