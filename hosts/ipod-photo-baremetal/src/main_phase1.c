#include "backlight.h"
#include "cache.h"
#include "core_bridge.h"
#include "heap.h"
#include "input.h"
#include "irq.h"
#include "lcd.h"
#include "panic.h"
#include "platform.h"
#include "power.h"
#include "qjs_runtime.h"
#include "pp5020.h"
#include "scheduler.h"
#include "storage.h"
#include "timer.h"
#include "timer_irq.h"

#ifndef PJS_PHASE1_POWER_TELEMETRY
#define PJS_PHASE1_POWER_TELEMETRY 1
#endif

#ifndef PJS_PHASE1_CACHE_ENABLE
#define PJS_PHASE1_CACHE_ENABLE 1
#endif

#define PJS_PHASE1_PLACEHOLDER_BATTERY_MV 3800u
#define PJS_WHEEL_DELTA_LIMIT 127

extern unsigned char __heap_start;
extern unsigned char __ram_end;
extern const uint8_t pjs_embedded_package[];
extern const uint32_t pjs_embedded_package_length;

static uint16_t framebuffer[PJS_FRAME_PIXELS] __attribute__((aligned(16)));

static void disable_interrupt_sources(void)
{
    PP_CPU_INT_DIS = 0xffffffffu;
    PP_COP_INT_DIS = 0xffffffffu;
    PP_INT_FORCED_CLR = 0xffffffffu;
    PP_CPU_HI_INT_DIS = 0xffffffffu;
    PP_COP_HI_INT_DIS = 0xffffffffu;
    PP_HI_INT_FORCED_CLR = 0xffffffffu;
}

static void initialize_heap(void)
{
    uintptr_t start = (uintptr_t)&__heap_start;
    uintptr_t end = (uintptr_t)&__ram_end;
    if (end <= start + PJS_HEAP_TOP_GUARD) panic_code(0x48454131u); /* HEA1 */
    end -= PJS_HEAP_TOP_GUARD;
    if (!pjs_heap_init((void *)start, end - start) || !pjs_heap_validate()) {
        panic_code(0x48454132u); /* HEA2 */
    }
}

static int32_t clamp_wheel_delta(int32_t value)
{
    if (value < -PJS_WHEEL_DELTA_LIMIT) return -PJS_WHEEL_DELTA_LIMIT;
    if (value > PJS_WHEEL_DELTA_LIMIT) return PJS_WHEEL_DELTA_LIMIT;
    return value;
}

static uint32_t runtime_failure_status(void)
{
    return qjs_runtime_error_code() == PJS_QJS_ERROR_FRAME_BUDGET ?
        PJS_RUNTIME_ERROR_BUDGET : PJS_RUNTIME_ERROR;
}

static PjsCoreInput core_input(const PjsInputState *input,
                               const PjsPowerTelemetry *power,
                               const PjsScheduler *scheduler,
                               int32_t wheel_delta,
                               bool cache_enabled,
                               uint32_t last_frame_us,
                               uint32_t runtime_status)
{
    return (PjsCoreInput){
        .buttons = input->buttons,
        .wheel_delta = wheel_delta,
        .wheel_position = input->wheel_position,
        .wheel_touched = input->wheel_touched ? 1u : 0u,
        .hold = input->hold ? 1u : 0u,
        .battery_mv = power->battery_mv,
        .power_flags = power->flags,
        .dropped_ticks = scheduler->dropped_ticks,
        .cache_enabled = cache_enabled ? 1u : 0u,
        .last_frame_us = last_frame_us,
        .runtime_status = runtime_status,
    };
}

static uint32_t render_and_present(void)
{
    uint32_t started = timer_now_us();
    PjsCoreDamagePlan damage = {0};
    if (pjs_core_render_damage(framebuffer, (uint32_t)PJS_FRAME_PIXELS, &damage) != 0) {
        panic_code(0x50314333u); /* P1C3 */
    }
    if (damage.count != 0u) {
        /* LCD transfer is programmed CPU I/O, not memory DMA. The CPU reads
         * cached framebuffer pixels and copies them into the bridge, so no
         * cache writeback is required for this presentation path. */
        if (!lcd_present_damage(framebuffer, PJS_FRAME_PIXELS, &damage)) {
            panic_code(0x50314c32u); /* P1L2 */
        }
    }
    return timer_now_us() - started;
}

void kernel_main_phase1(void)
{
    disable_interrupt_sources();
#if PJS_PHASE1_CACHE_ENABLE
    bool cache_enabled = cache_take_ownership_enabled();
#else
    cache_take_ownership_disabled();
    bool cache_enabled = false;
#endif
    initialize_heap();

    input_init();
    if (!lcd_init()) panic_code(0x50314c31u); /* P1L1 */
    backlight_init();

    PjsPowerTelemetry power = {
        .battery_mv = PJS_PHASE1_PLACEHOLDER_BATTERY_MV,
        .flags = (uint8_t)(power_source_flags_read() | PJS_POWER_TELEMETRY_DISABLED),
    };
    if (pjs_core_backend_marker() != PJS_CORE_BACKEND_RUST_MAGIC) {
        panic_code(0x50314245u); /* P1BE */
    }
    if (pjs_core_init() != 0) panic_code(0x50314331u); /* P1C1 */

    uint32_t runtime_status = PJS_RUNTIME_DISABLED;
    bool runtime_active = false;
    bool runtime_from_disk = false;
    PjsStorageFile disk_package = {0};
    PjsGuestPackage guest = {0};
    if (pjs_storage_load_guest(&disk_package) == PJS_STORAGE_OK &&
        pjs_package_open_ipod_photo(disk_package.bytes, disk_package.length, &guest) == 0) {
        runtime_status = PJS_RUNTIME_PACKAGE_ADMITTED;
        runtime_active = qjs_runtime_boot(&guest);
        runtime_from_disk = runtime_active;
    }
    if (!runtime_active) {
        pjs_storage_release(&disk_package);
        guest = (PjsGuestPackage){0};
        if (pjs_package_open_ipod_photo(pjs_embedded_package,
                                        pjs_embedded_package_length,
                                        &guest) == 0) {
            runtime_status = PJS_RUNTIME_PACKAGE_ADMITTED;
            runtime_active = qjs_runtime_boot(&guest);
        }
    }
    runtime_status = runtime_active ?
        (runtime_from_disk ? PJS_RUNTIME_READY_DISK : PJS_RUNTIME_READY) :
        PJS_RUNTIME_ERROR;

    PjsInputState input = {0};
    input_poll(&input);
    PjsScheduler initial_scheduler = {0};
    PjsCoreInput initial_input = core_input(&input, &power, &initial_scheduler, 0,
                                            cache_enabled, 0u, runtime_status);
    if (runtime_active && !qjs_runtime_frame(&initial_input)) {
        qjs_runtime_shutdown();
        runtime_active = false;
        runtime_status = runtime_failure_status();
        initial_input.runtime_status = runtime_status;
    }
    if (pjs_core_step(&initial_input) < 0) panic_code(0x50314332u); /* P1C2 */
    uint32_t last_frame_us = render_and_present();

    /* Start the 60 Hz clock only after the expensive initial frame. */
    scheduler_global_reset();
    timer_irq_init();
    irq_enable_global();

#if PJS_PHASE1_POWER_TELEMETRY
    /* Keep the already-qualified boot frame independent from I2C. Telemetry
     * starts only after the UI, cache, timer and input paths are alive. */
    power_telemetry_init();
#endif

    int32_t pending_wheel_delta = 0;
    uint32_t exit_chord_start = 0u;
    uint32_t now = timer_now_us();
#if PJS_PHASE1_POWER_TELEMETRY
    uint32_t next_power_sample = now + 250000u;
#else
    uint32_t next_power_sample = now + 1000000u;
#endif
    uint32_t next_present = now;

    for (;;) {
        /* This path remains hot while no frame is required. It samples input
         * continuously and preserves wheel motion until the next fixed step. */
        input_poll(&input);
        pending_wheel_delta = clamp_wheel_delta(
            pending_wheel_delta + (int32_t)input.wheel_delta);
        now = timer_now_us();

        uint32_t chord = PJS_BUTTON_MENU | PJS_BUTTON_PLAY;
        if ((input.buttons & chord) == chord) {
            if (exit_chord_start == 0u) exit_chord_start = now;
            if ((uint32_t)(now - exit_chord_start) >= 2000000u) {
                timer_irq_stop();
                irq_disable_global();
                pp_reboot();
            }
        } else {
            exit_chord_start = 0u;
        }

        if ((int32_t)(now - next_power_sample) >= 0) {
#if PJS_PHASE1_POWER_TELEMETRY
            power_telemetry_sample(&power);
#else
            power.flags = (uint8_t)(power_source_flags_read() |
                                    PJS_POWER_TELEMETRY_DISABLED);
            power.battery_mv = PJS_PHASE1_PLACEHOLDER_BATTERY_MV;
#endif
            next_power_sample = now + 1000000u;
        }

        uint32_t steps = scheduler_take_fixed_batch(PJS_SCHEDULER_MAX_STEPS_PER_PASS);
        if (steps != 0u) {
            PjsScheduler scheduler;
            scheduler_snapshot(&scheduler);
            PjsCoreInput frame_input = core_input(&input, &power, &scheduler,
                                                  pending_wheel_delta,
                                                  cache_enabled,
                                                  last_frame_us,
                                                  runtime_status);
            for (uint32_t step = 0u; step < steps; ++step) {
                if (runtime_active && !qjs_runtime_frame(&frame_input)) {
                    qjs_runtime_shutdown();
                    runtime_active = false;
                    runtime_status = runtime_failure_status();
                    frame_input.runtime_status = runtime_status;
                }
                if (pjs_core_step(&frame_input) < 0) {
                    panic_code(0x50314332u); /* P1C2 */
                }
                /* Wheel motion is an edge-like event. Apply the accumulated
                 * delta once, never once per catch-up simulation step. */
                frame_input.wheel_delta = 0;
                pending_wheel_delta = 0;
            }

            /* Drain bounded catch-up work before starting another expensive
             * render. Input is polled again at the top of every batch. */
            scheduler_snapshot(&scheduler);
            if (scheduler.pending != 0u) continue;
        }

        now = timer_now_us();
        if (pjs_core_needs_render() == 0u ||
            (int32_t)(now - next_present) < 0) {
            continue;
        }

        last_frame_us = render_and_present();
        next_present = timer_now_us() + PJS_PHASE1_RENDER_GAP_US;
    }
}
