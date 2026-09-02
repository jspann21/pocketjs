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

#ifndef PJS_PHASE1_PERSISTENCE_GATE
#define PJS_PHASE1_PERSISTENCE_GATE 0
#endif

#ifndef PJS_PHASE1_LINEAGE_GATE
#define PJS_PHASE1_LINEAGE_GATE 0
#endif

#ifndef PJS_PHASE1_RELIABILITY_GATE
#define PJS_PHASE1_RELIABILITY_GATE 0
#endif

#ifndef PJS_PHASE1_NATIVE_KERNEL_GATE
#define PJS_PHASE1_NATIVE_KERNEL_GATE 0
#endif

#if PJS_PHASE1_RELIABILITY_GATE && !PJS_PHASE1_LINEAGE_GATE
#error "The reliability gate requires the lineage gate"
#endif

#if PJS_PHASE1_NATIVE_KERNEL_GATE && !PJS_PHASE1_LINEAGE_GATE
#error "The native kernel gate requires the lineage gate"
#endif

#define PJS_PHASE1_PLACEHOLDER_BATTERY_MV 3800u
#define PJS_WHEEL_DELTA_LIMIT 127
#define PJS_LAUNCHER_PACKAGE_HASH_LOW 0x901119b9u
#define PJS_LAUNCHER_PACKAGE_HASH_HIGH 0x1868f7bau
#define PJS_MEMORY_GUARD_WORDS 64u
#define PJS_STACK_GUARD_PATTERN 0x53544b47u
#define PJS_HEAP_GUARD_PATTERN 0x48454147u
#define PJS_RUNTIME_MIN_LARGEST_FREE (8u * 1024u * 1024u)

extern unsigned char __heap_start;
extern unsigned char __ram_end;
extern unsigned char __stack_bottom;
extern const uint8_t pjs_embedded_package[];
extern const uint32_t pjs_embedded_package_length;

static uint16_t framebuffer[PJS_FRAME_PIXELS] __attribute__((aligned(16)));
static uint32_t last_presented_damage_area;
static volatile uint32_t *stack_guard;
static volatile uint32_t *heap_guard;

typedef struct {
    const char *file_name;
    uint32_t source;
} PjsBootSlot;

static const char pending_package[11] =
    {'P','E','N','D','I','N','G',' ','P','K','T'};
static const char active_package[11] =
    {'A','C','T','I','V','E',' ',' ','P','K','T'};
static const char last_good_package[11] =
    {'L','A','S','T','G','O','O','D','P','K','T'};
static const char legacy_package[11] =
    {'A','P','P',' ',' ',' ',' ',' ','P','K','T'};
#if !PJS_PHASE1_LINEAGE_GATE
static const char launcher_package[11] =
    {'L','A','U','N','C','H','E','R','P','K','T'};
#endif

static const PjsBootSlot boot_slots[] = {
    {pending_package, PJS_BOOT_SOURCE_PENDING},
    {active_package, PJS_BOOT_SOURCE_ACTIVE},
    {last_good_package, PJS_BOOT_SOURCE_LAST_GOOD},
    {legacy_package, PJS_BOOT_SOURCE_LEGACY_APP},
};

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
    stack_guard = (volatile uint32_t *)(uintptr_t)&__stack_bottom;
    heap_guard = (volatile uint32_t *)end;
    for (uint32_t index = 0u; index < PJS_MEMORY_GUARD_WORDS; ++index) {
        stack_guard[index] = PJS_STACK_GUARD_PATTERN;
        heap_guard[index] = PJS_HEAP_GUARD_PATTERN;
    }
    if (!pjs_heap_init((void *)start, end - start) || !pjs_heap_validate()) {
        panic_code(0x48454132u); /* HEA2 */
    }
}

#if PJS_PHASE1_RELIABILITY_GATE || PJS_PHASE1_NATIVE_KERNEL_GATE
static bool memory_integrity_ok(void)
{
    if (!pjs_heap_validate()) return false;
    for (uint32_t index = 0u; index < PJS_MEMORY_GUARD_WORDS; ++index) {
        if (stack_guard[index] != PJS_STACK_GUARD_PATTERN ||
            heap_guard[index] != PJS_HEAP_GUARD_PATTERN) return false;
    }
    return true;
}
#endif

#if PJS_PHASE1_RELIABILITY_GATE
static bool runtime_memory_admitted(void)
{
    if (!memory_integrity_ok()) return false;
    PjsHeapStats stats = {0};
    pjs_heap_stats(&stats);
    return stats.largest_free >= PJS_RUNTIME_MIN_LARGEST_FREE;
}
#endif

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

static uint32_t error_magnitude(int32_t result)
{
    return result < 0 ? (uint32_t)(-(int64_t)result) : (uint32_t)result;
}

static void reset_core_after_failed_guest(void)
{
    pjs_core_shutdown();
    if (pjs_core_init() != 0) panic_code(0x50314331u); /* P1C1 */
}

#if PJS_PHASE1_RELIABILITY_GATE
static bool boot_embedded_recovery(PjsGuestPackage *guest,
                                   PjsCoreInput *input)
{
    reset_core_after_failed_guest();
    *guest = (PjsGuestPackage){0};
    if (pjs_package_open_ipod_photo(
            pjs_embedded_package, pjs_embedded_package_length, guest) != 0) {
        return false;
    }
    input->runtime_status = PJS_RUNTIME_PACKAGE_ADMITTED;
    if (!qjs_runtime_boot(guest) || !qjs_runtime_frame(input)) {
        qjs_runtime_shutdown();
        return false;
    }
    input->runtime_status = PJS_RUNTIME_READY;
    return true;
}
#endif

static void remember_boot_failure(uint32_t stage, uint32_t code,
                                  uint32_t *failure_stage,
                                  uint32_t *failure_code)
{
    if (*failure_stage != PJS_BOOT_FAILURE_NONE) return;
    *failure_stage = stage;
    *failure_code = code;
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
    last_presented_damage_area = 0u;
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
        last_presented_damage_area = damage.area;
    }
    return timer_now_us() - started;
}

#if PJS_PHASE1_NATIVE_KERNEL_GATE
static uint32_t kernel_power_mode(uint8_t flags)
{
    if ((flags & PJS_POWER_USB) != 0u) return 2u;
    if ((flags & PJS_POWER_FIREWIRE) != 0u) return 3u;
    return 1u;
}

static bool kernel_lcd_cycle(void)
{
    backlight_enable(false);
    if (!lcd_sleep()) {
        backlight_enable(true);
        return false;
    }
    timer_delay_us(750000u);
    if (!lcd_wake() || !lcd_present(framebuffer, PJS_FRAME_PIXELS)) return false;
    backlight_enable(true);
    return true;
}

static uint32_t kernel_shutdown_preflight(bool runtime_active,
                                          bool lineage_ready,
                                          uint8_t filtered_power)
{
    if (!runtime_active || !lineage_ready || !memory_integrity_ok()) return 1u;
    if (!lcd_ready()) return 2u;
    if ((filtered_power & PJS_POWER_SOURCE_UNSTABLE) != 0u) return 3u;
    PjsStorageDiskHandoff handoff = {0};
    if (pjs_storage_ata_quiesce(&handoff) != PJS_STORAGE_OK) {
        return 10u + handoff.state;
    }
    return 0u;
}
#endif

#if PJS_PHASE1_LINEAGE_GATE
static const char *package_name_for_source(uint32_t source)
{
    for (uint32_t index = 0u;
         index < sizeof(boot_slots) / sizeof(boot_slots[0]); ++index) {
        if (boot_slots[index].source == source) return boot_slots[index].file_name;
    }
    return 0;
}

static bool package_hash_matches(const PjsGuestPackage *guest,
                                 uint32_t low, uint32_t high)
{
    return guest->package_hash_low == low && guest->package_hash_high == high;
}

static bool source_list_add(uint32_t sources[6], uint32_t *count,
                            uint32_t source)
{
    if (source == PJS_BOOT_SOURCE_NONE || source > PJS_BOOT_SOURCE_EMBEDDED) {
        return false;
    }
    for (uint32_t index = 0u; index < *count; ++index) {
        if (sources[index] == source) return true;
    }
    if (*count >= 6u) return false;
    sources[(*count)++] = source;
    return true;
}
#endif

#if !PJS_PHASE1_LINEAGE_GATE
static void catalog_labels(const PjsStorageCatalog *catalog,
                           uint8_t labels[PJS_STORAGE_MAX_APPS][9])
{
    for (uint32_t app = 0u; app < PJS_STORAGE_MAX_APPS; ++app) {
        for (uint32_t index = 0u; index < 9u; ++index) labels[app][index] = 0u;
        if (app >= catalog->count) continue;
        for (uint32_t index = 0u; index < 8u; ++index) {
            char character = catalog->apps[app].file_name[index];
            if (character == ' ') break;
            labels[app][index] = (uint8_t)character;
        }
    }
}

static int32_t run_package_launcher(const PjsStorageCatalog *catalog,
                                    PjsInputState *input,
                                    const PjsPowerTelemetry *power,
                                    bool cache_enabled)
{
    uint8_t labels[PJS_STORAGE_MAX_APPS][9];
    catalog_labels(catalog, labels);
    if (!qjs_runtime_set_launcher_catalog(
            &labels[0][0], catalog->count, sizeof(labels[0]))) return -1;

    PjsStorageFile launcher_file = {0};
    int32_t storage_result = pjs_storage_load_guest_named(
        &launcher_file, launcher_package);
    if (storage_result != PJS_STORAGE_OK) {
        qjs_runtime_set_launcher_catalog(0, 0u, 0u);
        return -1;
    }
    PjsGuestPackage launcher_guest = {0};
    if (pjs_package_open_ipod_photo(
            launcher_file.bytes, launcher_file.length, &launcher_guest) != 0 ||
        launcher_guest.package_hash_low != PJS_LAUNCHER_PACKAGE_HASH_LOW ||
        launcher_guest.package_hash_high != PJS_LAUNCHER_PACKAGE_HASH_HIGH ||
        !qjs_runtime_boot(&launcher_guest)) {
        pjs_storage_release(&launcher_file);
        qjs_runtime_set_launcher_catalog(0, 0u, 0u);
        reset_core_after_failed_guest();
        return -1;
    }

    PjsScheduler scheduler = {0};
    int32_t pending_wheel_delta = 0;
    uint32_t last_frame_us = 0u;
    uint32_t next_frame = timer_now_us();
    uint32_t exit_chord_start = 0u;
    for (;;) {
        input_poll(input);
        pending_wheel_delta = clamp_wheel_delta(
            pending_wheel_delta + (int32_t)input->wheel_delta);
        uint32_t now = timer_now_us();
        uint32_t chord = PJS_BUTTON_MENU | PJS_BUTTON_PLAY;
        if ((input->buttons & chord) == chord) {
            if (exit_chord_start == 0u) exit_chord_start = now;
            if ((uint32_t)(now - exit_chord_start) >= 2000000u) pp_reboot();
        } else {
            exit_chord_start = 0u;
        }
        if ((int32_t)(now - next_frame) < 0) continue;

        PjsCoreInput frame_input = core_input(
            input, power, &scheduler, pending_wheel_delta, cache_enabled,
            last_frame_us, PJS_RUNTIME_READY_DISK);
        pending_wheel_delta = 0;
        if (!qjs_runtime_frame(&frame_input) || pjs_core_step(&frame_input) < 0) {
            qjs_runtime_shutdown();
            pjs_storage_release(&launcher_file);
            qjs_runtime_set_launcher_catalog(0, 0u, 0u);
            reset_core_after_failed_guest();
            return -1;
        }
        int32_t selection = qjs_runtime_launcher_selection();
        if (pjs_core_needs_render() != 0u) last_frame_us = render_and_present();
        next_frame = timer_now_us() + 16667u;
        if (selection < 0) continue;

        qjs_runtime_shutdown();
        pjs_storage_release(&launcher_file);
        qjs_runtime_set_launcher_catalog(0, 0u, 0u);
        reset_core_after_failed_guest();
        return selection;
    }
}
#endif

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
    bool runtime_from_catalog = false;
    uint32_t catalog_selection = 0u;
    uint32_t catalog_count = 0u;
    uint32_t boot_source = PJS_BOOT_SOURCE_NONE;
    uint32_t boot_failure_stage = PJS_BOOT_FAILURE_NONE;
    uint32_t boot_failure_code = 0u;
    PjsStorageFile disk_package = {0};
    PjsGuestPackage guest = {0};
    PjsInputState input = {0};
    input_poll(&input);
    PjsScheduler initial_scheduler = {0};
    PjsCoreInput initial_input = core_input(
        &input, &power, &initial_scheduler, 0, cache_enabled, 0u,
        PJS_RUNTIME_PACKAGE_ADMITTED);
    pjs_storage_reset_diagnostics();
#if PJS_PHASE1_LINEAGE_GATE
    PjsLineageState lineage = {0};
    int32_t lineage_result = pjs_storage_lineage_load(&lineage);
    bool lineage_ready = lineage_result == PJS_STORAGE_OK;
    bool lineage_accept_pending = false;
    uint32_t lineage_event = 4u;
    PjsLineageRecord lineage_accept = {0};
#if PJS_PHASE1_RELIABILITY_GATE
    bool admission_rejected = false;
    bool embedded_recovery_attempted = false;
#endif
#else
    PjsStorageCatalog catalog = {0};
    int32_t discovery_result = pjs_storage_discover_apps(&catalog);
    if (discovery_result == PJS_STORAGE_OK && catalog.count != 0u) {
        int32_t selection = run_package_launcher(
            &catalog, &input, &power, cache_enabled);
        if (selection >= 0 && (uint32_t)selection < catalog.count) {
            catalog_selection = (uint32_t)selection;
            catalog_count = catalog.count;
            int32_t storage_result = pjs_storage_load_app(
                &disk_package, catalog.apps[catalog_selection].file_name);
            if (storage_result == PJS_STORAGE_OK) {
                int32_t package_result = pjs_package_open_ipod_photo(
                    disk_package.bytes, disk_package.length, &guest);
                if (package_result == 0) {
                    runtime_status = PJS_RUNTIME_PACKAGE_ADMITTED;
                    runtime_active = qjs_runtime_boot(&guest);
                    if (runtime_active && qjs_runtime_frame(&initial_input)) {
                        runtime_from_disk = true;
                        runtime_from_catalog = true;
                    } else {
                        remember_boot_failure(
                            runtime_active ? PJS_BOOT_FAILURE_FRAME :
                                             PJS_BOOT_FAILURE_QUICKJS,
                            qjs_runtime_error_code(),
                            &boot_failure_stage, &boot_failure_code);
                        if (runtime_active) qjs_runtime_shutdown();
                        runtime_active = false;
                        pjs_storage_release(&disk_package);
                        reset_core_after_failed_guest();
                    }
                } else {
                    remember_boot_failure(
                        PJS_BOOT_FAILURE_PACKAGE, error_magnitude(package_result),
                        &boot_failure_stage, &boot_failure_code);
                    pjs_storage_release(&disk_package);
                }
            } else {
                remember_boot_failure(
                    PJS_BOOT_FAILURE_STORAGE, pjs_storage_last_error(),
                    &boot_failure_stage, &boot_failure_code);
            }
        }
    }
#endif

#if PJS_PHASE1_LINEAGE_GATE
    if (lineage_ready) {
        PjsLineageRecord boot_record = lineage.record;
        bool rollback = boot_record.phase != PJS_LINEAGE_PHASE_ACTIVE;
        PjsGuestPackage embedded_identity = {0};
        if (pjs_package_open_ipod_photo(
                pjs_embedded_package, pjs_embedded_package_length,
                &embedded_identity) != 0) {
            lineage_ready = false;
            lineage_result = PJS_STORAGE_ERR_STATE;
        }
        uint32_t sources[6] = {0};
        uint32_t source_count = 0u;
        if (rollback) {
            source_list_add(sources, &source_count, boot_record.last_good_source);
            source_list_add(sources, &source_count, PJS_BOOT_SOURCE_EMBEDDED);
        } else {
            if (boot_record.active_source != PJS_BOOT_SOURCE_PENDING) {
                source_list_add(sources, &source_count, PJS_BOOT_SOURCE_PENDING);
            }
            source_list_add(sources, &source_count, boot_record.active_source);
            source_list_add(sources, &source_count, boot_record.last_good_source);
            source_list_add(sources, &source_count, PJS_BOOT_SOURCE_LEGACY_APP);
            source_list_add(sources, &source_count, PJS_BOOT_SOURCE_EMBEDDED);
        }

        for (uint32_t index = 0u; lineage_ready && !runtime_active &&
             index < source_count; ++index) {
            uint32_t source = sources[index];
            guest = (PjsGuestPackage){0};
            int32_t package_result = 0;
            if (source == PJS_BOOT_SOURCE_EMBEDDED) {
                package_result = pjs_package_open_ipod_photo(
                    pjs_embedded_package, pjs_embedded_package_length, &guest);
            } else {
                const char *name = package_name_for_source(source);
                if (name == 0) continue;
                int32_t storage_result = pjs_storage_load_guest_named(
                    &disk_package, name);
                if (storage_result == PJS_STORAGE_ERR_NOT_FOUND) continue;
                if (storage_result != PJS_STORAGE_OK) {
                    remember_boot_failure(PJS_BOOT_FAILURE_STORAGE,
                                          pjs_storage_last_error(),
                                          &boot_failure_stage,
                                          &boot_failure_code);
                    continue;
                }
                package_result = pjs_package_open_ipod_photo(
                    disk_package.bytes, disk_package.length, &guest);
            }
            if (package_result != 0) {
#if PJS_PHASE1_RELIABILITY_GATE
                if (source == PJS_BOOT_SOURCE_PENDING && package_result == -4) {
                    admission_rejected = true;
                }
#endif
                remember_boot_failure(PJS_BOOT_FAILURE_PACKAGE,
                                      error_magnitude(package_result),
                                      &boot_failure_stage, &boot_failure_code);
                pjs_storage_release(&disk_package);
                continue;
            }

#if PJS_PHASE1_RELIABILITY_GATE
            if (!runtime_memory_admitted()) {
                remember_boot_failure(PJS_BOOT_FAILURE_MEMORY, 1u,
                                      &boot_failure_stage, &boot_failure_code);
                if (source == PJS_BOOT_SOURCE_PENDING) admission_rejected = true;
                pjs_storage_release(&disk_package);
                continue;
            }
#endif

            bool is_new_pending = source == PJS_BOOT_SOURCE_PENDING &&
                source != boot_record.active_source;
            if (is_new_pending &&
                (package_hash_matches(&guest,
                                      boot_record.active_hash_low,
                                      boot_record.active_hash_high) ||
                 package_hash_matches(&guest,
                                      boot_record.rejected_hash_low,
                                      boot_record.rejected_hash_high))) {
                pjs_storage_release(&disk_package);
                continue;
            }
            if (source == boot_record.active_source &&
                !package_hash_matches(&guest,
                                      boot_record.active_hash_low,
                                      boot_record.active_hash_high)) {
                pjs_storage_release(&disk_package);
                continue;
            }
            if (source == boot_record.last_good_source &&
                !package_hash_matches(&guest,
                                      boot_record.last_good_hash_low,
                                      boot_record.last_good_hash_high)) {
                pjs_storage_release(&disk_package);
                continue;
            }

            PjsLineageRecord trial = lineage.record;
            trial.phase = PJS_LINEAGE_PHASE_TRIAL;
            trial.trial_source = source;
            trial.trial_hash_low = guest.package_hash_low;
            trial.trial_hash_high = guest.package_hash_high;
            trial.failure_stage = PJS_BOOT_FAILURE_NONE;
            trial.failure_code = 0u;
            if (pjs_storage_lineage_write(&lineage, &trial) != PJS_STORAGE_OK) {
                lineage_ready = false;
                pjs_storage_release(&disk_package);
                break;
            }

            runtime_status = PJS_RUNTIME_PACKAGE_ADMITTED;
            runtime_active = qjs_runtime_boot(&guest);
            if (runtime_active && qjs_runtime_frame(&initial_input)) {
                runtime_from_disk = source != PJS_BOOT_SOURCE_EMBEDDED;
                boot_source = source;
                lineage_event = rollback ? 1u : (is_new_pending ? 0u : 2u);
#if PJS_PHASE1_RELIABILITY_GATE
                if (admission_rejected && boot_record.generation == 1u) {
                    lineage_event = 5u;
                }
#endif
                lineage_accept = lineage.record;
                lineage_accept.phase = PJS_LINEAGE_PHASE_RUNNING;
                if (source != boot_record.active_source) {
                    if (rollback) {
                        lineage_accept.last_good_source = PJS_BOOT_SOURCE_EMBEDDED;
                        lineage_accept.last_good_hash_low =
                            embedded_identity.package_hash_low;
                        lineage_accept.last_good_hash_high =
                            embedded_identity.package_hash_high;
                    } else {
                        lineage_accept.last_good_source = boot_record.active_source;
                        lineage_accept.last_good_hash_low = boot_record.active_hash_low;
                        lineage_accept.last_good_hash_high = boot_record.active_hash_high;
                    }
                    lineage_accept.active_source = source;
                    lineage_accept.active_hash_low = guest.package_hash_low;
                    lineage_accept.active_hash_high = guest.package_hash_high;
                }
                lineage_accept_pending = true;
                break;
            }

            uint32_t failure_stage = runtime_active ? PJS_BOOT_FAILURE_FRAME :
                                                      PJS_BOOT_FAILURE_QUICKJS;
            uint32_t failure_code = qjs_runtime_error_code();
            PjsLineageRecord crashed = lineage.record;
            crashed.phase = PJS_LINEAGE_PHASE_CRASHED;
            crashed.rejected_source = source;
            crashed.rejected_hash_low = guest.package_hash_low;
            crashed.rejected_hash_high = guest.package_hash_high;
            crashed.failure_stage = failure_stage;
            crashed.failure_code = failure_code;
            (void)pjs_storage_lineage_write(&lineage, &crashed);
            remember_boot_failure(failure_stage, failure_code,
                                  &boot_failure_stage, &boot_failure_code);
            if (runtime_active) qjs_runtime_shutdown();
            runtime_active = false;
            pjs_storage_release(&disk_package);
            reset_core_after_failed_guest();
            rollback = true;
        }
    }
#else
    for (uint32_t index = 0u; !runtime_active &&
         index < sizeof(boot_slots) / sizeof(boot_slots[0]); ++index) {
        int32_t storage_result = pjs_storage_load_guest_named(
            &disk_package, boot_slots[index].file_name);
        if (storage_result == PJS_STORAGE_ERR_NOT_FOUND) continue;
        if (storage_result != PJS_STORAGE_OK) {
            remember_boot_failure(PJS_BOOT_FAILURE_STORAGE,
                                  pjs_storage_last_error(),
                                  &boot_failure_stage, &boot_failure_code);
            break;
        }

        guest = (PjsGuestPackage){0};
        int32_t package_result = pjs_package_open_ipod_photo(
            disk_package.bytes, disk_package.length, &guest);
        if (package_result != 0) {
            remember_boot_failure(PJS_BOOT_FAILURE_PACKAGE,
                                  error_magnitude(package_result),
                                  &boot_failure_stage, &boot_failure_code);
            pjs_storage_release(&disk_package);
            continue;
        }

        runtime_status = PJS_RUNTIME_PACKAGE_ADMITTED;
        runtime_active = qjs_runtime_boot(&guest);
        if (runtime_active && qjs_runtime_frame(&initial_input)) {
            runtime_from_disk = true;
            boot_source = boot_slots[index].source;
            break;
        }

        uint32_t failure_stage = runtime_active ? PJS_BOOT_FAILURE_FRAME :
                                                  PJS_BOOT_FAILURE_QUICKJS;
        remember_boot_failure(failure_stage,
                              qjs_runtime_error_code(),
                              &boot_failure_stage, &boot_failure_code);
        if (runtime_active) qjs_runtime_shutdown();
        runtime_active = false;
        pjs_storage_release(&disk_package);
        reset_core_after_failed_guest();
    }
#endif
    if (!runtime_active) {
        pjs_storage_release(&disk_package);
        guest = (PjsGuestPackage){0};
        int32_t embedded_result = pjs_package_open_ipod_photo(
            pjs_embedded_package, pjs_embedded_package_length, &guest);
        boot_source = PJS_BOOT_SOURCE_EMBEDDED;
        if (embedded_result == 0) {
            runtime_status = PJS_RUNTIME_PACKAGE_ADMITTED;
            runtime_active = qjs_runtime_boot(&guest);
            if (runtime_active && !qjs_runtime_frame(&initial_input)) {
                boot_failure_stage = PJS_BOOT_FAILURE_FRAME;
                boot_failure_code = qjs_runtime_error_code();
                qjs_runtime_shutdown();
                runtime_active = false;
            } else if (!runtime_active) {
                boot_failure_stage = PJS_BOOT_FAILURE_EMBEDDED_QUICKJS;
                boot_failure_code = qjs_runtime_error_code();
            }
        } else {
            boot_failure_stage = PJS_BOOT_FAILURE_EMBEDDED_PACKAGE;
            boot_failure_code = error_magnitude(embedded_result);
        }
    }
    runtime_status = runtime_active ?
        (runtime_from_disk ? PJS_RUNTIME_READY_DISK : PJS_RUNTIME_READY) :
        PJS_RUNTIME_ERROR;

    initial_input.runtime_status = runtime_status;
    if (runtime_from_catalog) {
        pjs_core_set_app_diagnostic(
            catalog_selection, catalog_count, pjs_storage_sector_read_count());
    } else {
        pjs_core_set_boot_diagnostic(boot_source, boot_failure_stage,
                                     boot_failure_code,
                                     pjs_storage_sector_read_count());
    }
#if PJS_PHASE1_PERSISTENCE_GATE
    PjsPersistenceState persistence = {0};
    int32_t persistence_result = pjs_storage_state_load(&persistence);
    if (persistence_result == PJS_STORAGE_OK) {
        pjs_core_set_persistence_diagnostic(
            0u, persistence.active_slot, persistence.generation, 0u);
    } else {
        pjs_core_set_persistence_diagnostic(
            3u, 0u, 0u, error_magnitude(persistence_result));
    }
#endif
#if PJS_PHASE1_LINEAGE_GATE
    if (!lineage_ready) {
        pjs_core_set_lineage_diagnostic(
            6u, 0u, 0u, error_magnitude(lineage_result));
    }
#endif
    if (pjs_core_step(&initial_input) < 0) panic_code(0x50314332u); /* P1C2 */
    uint32_t last_frame_us = render_and_present();
#if PJS_PHASE1_LINEAGE_GATE
    if (lineage_ready && lineage_accept_pending) {
        if (last_presented_damage_area == 0u) {
            pjs_core_set_lineage_diagnostic(
                6u, boot_source, lineage.record.generation,
                (uint32_t)(-PJS_STORAGE_ERR_VERIFY));
        } else if (pjs_storage_lineage_write(
                       &lineage, &lineage_accept) == PJS_STORAGE_OK) {
            pjs_core_set_lineage_diagnostic(
                lineage_event, boot_source, lineage.record.generation, 0u);
        } else {
            pjs_core_set_lineage_diagnostic(
                6u, boot_source, lineage.record.generation, lineage.error);
        }
        last_frame_us = render_and_present();
    }
#endif
#if PJS_PHASE1_NATIVE_KERNEL_GATE
    pjs_core_set_kernel_diagnostic(0u, 0u);
    last_frame_us = render_and_present();
#endif

    /* Start the 60 Hz clock only after the expensive initial frame. */
    scheduler_global_reset();
    timer_irq_init();
    irq_enable_global();

#if PJS_PHASE1_POWER_TELEMETRY
    /* Keep the already-qualified boot frame independent from I2C. Telemetry
     * starts only after the UI, cache, timer and input paths are alive. */
    power_telemetry_init();
#endif

#if PJS_PHASE1_NATIVE_KERNEL_GATE
    PjsPowerSourceFilter source_filter;
    power_source_filter_init(&source_filter);
    uint8_t filtered_power = PJS_POWER_SOURCE_UNSTABLE;
    uint32_t last_power_mode = UINT32_MAX;
    bool lcd_tested = false;
    bool shutdown_ready = false;
    uint32_t next_source_sample = timer_now_us();
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
#if PJS_PHASE1_RELIABILITY_GATE
    uint32_t next_memory_check = now + 1000000u;
#endif
#if PJS_PHASE1_PERSISTENCE_GATE || PJS_PHASE1_NATIVE_KERNEL_GATE
    uint32_t previous_buttons = input.buttons;
#endif

    for (;;) {
        /* This path remains hot while no frame is required. It samples input
         * continuously and preserves wheel motion until the next fixed step. */
        input_poll(&input);
#if PJS_PHASE1_PERSISTENCE_GATE || PJS_PHASE1_NATIVE_KERNEL_GATE
        uint32_t pressed = input.buttons & ~previous_buttons;
        previous_buttons = input.buttons;
#endif
#if PJS_PHASE1_PERSISTENCE_GATE
        if (persistence.available != 0u &&
            (pressed & PJS_BUTTON_SELECT) != 0u) {
            uint32_t slot = 0u;
            uint32_t generation = 0u;
            int32_t result = pjs_storage_state_write(
                &persistence, true, &slot, &generation);
            if (result == PJS_STORAGE_OK) {
                pjs_core_set_persistence_diagnostic(
                    1u, slot, generation, 0u);
            } else {
                pjs_core_set_persistence_diagnostic(
                    3u, slot, generation, error_magnitude(result));
            }
        } else if (persistence.available != 0u &&
                   (pressed & PJS_BUTTON_PLAY) != 0u &&
                   (input.buttons & PJS_BUTTON_MENU) == 0u) {
            uint32_t slot = 0u;
            uint32_t generation = 0u;
            int32_t result = pjs_storage_state_write(
                &persistence, false, &slot, &generation);
            if (result == PJS_STORAGE_OK) {
                pjs_core_set_persistence_diagnostic(
                    2u, slot, generation, 0u);
            } else {
                pjs_core_set_persistence_diagnostic(
                    3u, slot, generation, error_magnitude(result));
            }
        }
#endif
#if PJS_PHASE1_NATIVE_KERNEL_GATE
        if ((pressed & PJS_BUTTON_SELECT) != 0u &&
            (input.buttons & PJS_BUTTON_MENU) == 0u) {
            if (kernel_lcd_cycle()) {
                lcd_tested = true;
                shutdown_ready = false;
                pjs_core_set_kernel_diagnostic(4u, 0u);
            } else {
                pjs_core_set_kernel_diagnostic(8u, 20u);
            }
        } else if ((pressed & PJS_BUTTON_PLAY) != 0u &&
            (input.buttons & PJS_BUTTON_MENU) == 0u) {
            uint32_t error = lcd_tested ? kernel_shutdown_preflight(
                runtime_active,
                lineage_ready &&
                    lineage.record.phase == PJS_LINEAGE_PHASE_RUNNING,
                filtered_power) : 4u;
            if (error == 0u) {
                shutdown_ready = true;
                pjs_core_set_kernel_diagnostic(5u, 0u);
            } else {
                shutdown_ready = false;
                pjs_core_set_kernel_diagnostic(8u, error);
            }
        }
#endif
        pending_wheel_delta = clamp_wheel_delta(
            pending_wheel_delta + (int32_t)input.wheel_delta);
        now = timer_now_us();

#if PJS_PHASE1_NATIVE_KERNEL_GATE
        if ((int32_t)(now - next_source_sample) >= 0) {
            PjsPowerSourceSample sample = {0};
            power_source_sample_read_only(&sample);
            filtered_power = power_source_filter_update(
                &source_filter, sample.flags, sample.valid_mask);
            if ((filtered_power & PJS_POWER_SOURCE_UNSTABLE) == 0u) {
                uint32_t mode = kernel_power_mode(filtered_power);
                if (mode != last_power_mode) {
                    last_power_mode = mode;
                    pjs_core_set_kernel_diagnostic(mode, 0u);
                }
            }
            next_source_sample = now + 100000u;
        }
#endif

#if PJS_PHASE1_RELIABILITY_GATE
        if ((int32_t)(now - next_memory_check) >= 0) {
            if (!memory_integrity_ok()) panic_code(0x4d454d47u); /* MEMG */
            next_memory_check = now + 1000000u;
        }
#endif

        uint32_t chord = PJS_BUTTON_MENU | PJS_BUTTON_PLAY;
        if ((input.buttons & chord) == chord) {
            if (exit_chord_start == 0u) exit_chord_start = now;
            if ((uint32_t)(now - exit_chord_start) >= 2000000u) {
#if PJS_PHASE1_NATIVE_KERNEL_GATE
                bool usb_ready = (filtered_power &
                    (PJS_POWER_USB | PJS_POWER_SOURCE_UNSTABLE)) == PJS_POWER_USB;
                if (!shutdown_ready || !usb_ready) {
                    pjs_core_set_kernel_diagnostic(
                        8u, shutdown_ready ? 31u : 30u);
                    exit_chord_start = now;
                    continue;
                }
                pjs_core_set_kernel_diagnostic(6u, 0u);
                (void)render_and_present();
#endif
#if PJS_PHASE1_LINEAGE_GATE
                if (lineage_ready &&
                    lineage.record.phase == PJS_LINEAGE_PHASE_RUNNING) {
                    PjsLineageRecord clean = lineage.record;
                    clean.phase = PJS_LINEAGE_PHASE_ACTIVE;
                    clean.trial_source = PJS_BOOT_SOURCE_NONE;
                    clean.trial_hash_low = 0u;
                    clean.trial_hash_high = 0u;
                    clean.failure_stage = PJS_BOOT_FAILURE_NONE;
                    clean.failure_code = 0u;
                    if (pjs_storage_lineage_write(&lineage, &clean) !=
                        PJS_STORAGE_OK) {
#if PJS_PHASE1_NATIVE_KERNEL_GATE
                        pjs_core_set_kernel_diagnostic(8u, 32u);
                        exit_chord_start = now;
                        continue;
#endif
                    }
                }
#endif
#if PJS_PHASE1_NATIVE_KERNEL_GATE
                PjsStorageDiskHandoff handoff = {0};
                if (pjs_storage_prepare_disk_handoff(&handoff) !=
                        PJS_STORAGE_OK ||
                    !pjs_storage_disk_handoff_armed()) {
                    pjs_storage_disk_handoff_clear();
                    pjs_core_set_kernel_diagnostic(
                        8u, 40u + handoff.state);
                    exit_chord_start = now;
                    continue;
                }
                pjs_core_set_kernel_diagnostic(7u, 0u);
                (void)render_and_present();
                /* Match Rockbox's two-second PP5020 handoff settling window,
                 * without issuing its ATA standby command in this gate. */
                timer_delay_us(2000000u);
                qjs_runtime_shutdown();
                pjs_storage_release(&disk_package);
#endif
                timer_irq_stop();
                irq_disable_global();
#if PJS_PHASE1_NATIVE_KERNEL_GATE
                backlight_enable(false);
                (void)lcd_sleep();
                pp_reboot_disk_mode();
#else
                pp_reboot();
#endif
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
                    boot_failure_stage = PJS_BOOT_FAILURE_FRAME;
                    boot_failure_code = qjs_runtime_error_code();
                    qjs_runtime_shutdown();
                    runtime_active = false;
                    runtime_status = runtime_failure_status();
                    frame_input.runtime_status = runtime_status;
#if PJS_PHASE1_LINEAGE_GATE
                    if (lineage_ready) {
                        PjsLineageRecord crashed = lineage.record;
                        crashed.phase = PJS_LINEAGE_PHASE_CRASHED;
                        crashed.rejected_source = crashed.active_source;
                        crashed.rejected_hash_low = crashed.active_hash_low;
                        crashed.rejected_hash_high = crashed.active_hash_high;
                        crashed.failure_stage = boot_failure_stage;
                        crashed.failure_code = boot_failure_code;
                        if (pjs_storage_lineage_write(
                                &lineage, &crashed) == PJS_STORAGE_OK) {
                            pjs_core_set_lineage_diagnostic(
                                3u, crashed.active_source,
                                lineage.record.generation, 0u);
                        } else {
                            pjs_core_set_lineage_diagnostic(
                                6u, crashed.active_source,
                                lineage.record.generation, lineage.error);
                        }
                    }
#else
                    pjs_core_set_boot_diagnostic(
                        boot_source, boot_failure_stage, boot_failure_code,
                        pjs_storage_sector_read_count());
#endif
#if PJS_PHASE1_RELIABILITY_GATE
                    if (!embedded_recovery_attempted) {
                        embedded_recovery_attempted = true;
                        pjs_storage_release(&disk_package);
                        if (boot_embedded_recovery(&guest, &frame_input)) {
                            runtime_active = true;
                            runtime_from_disk = false;
                            runtime_status = PJS_RUNTIME_READY;
                            boot_source = PJS_BOOT_SOURCE_EMBEDDED;
                            frame_input.runtime_status = runtime_status;
                            pjs_core_set_lineage_diagnostic(
                                4u, PJS_BOOT_SOURCE_EMBEDDED,
                                lineage.record.generation, 0u);
                        } else {
                            pjs_core_set_lineage_diagnostic(
                                6u, PJS_BOOT_SOURCE_EMBEDDED,
                                lineage.record.generation,
                                qjs_runtime_error_code());
                        }
                    }
#endif
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
