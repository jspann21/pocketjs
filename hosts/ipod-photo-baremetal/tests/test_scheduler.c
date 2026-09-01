#include "scheduler.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    PjsScheduler scheduler;
    scheduler_reset(&scheduler);
    for (unsigned i = 0; i < 1000; ++i) scheduler_advance_us(&scheduler, 1000u);
    assert(scheduler.total_ticks == 60u);
    assert(scheduler.pending == 60u);
    assert(scheduler.dropped_ticks == 0u);
    assert(scheduler_take_batch(&scheduler, 2u) == 2u);
    assert(scheduler.pending == 58u);
    assert(scheduler_take_batch(&scheduler, 99u) == 58u);
    assert(scheduler.pending == 0u);
    assert(scheduler_take_batch(&scheduler, 4u) == 0u);
    assert(!scheduler_take(&scheduler));

    /* A render can replenish the global queue, but a claimed batch remains
     * finite and never turns into an open-ended drain loop. */
    scheduler_reset(&scheduler);
    scheduler_advance_us(&scheduler, 534000u);
    assert(scheduler.pending == 32u);
    uint32_t claimed = scheduler_take_batch(&scheduler, PJS_SCHEDULER_MAX_STEPS_PER_PASS);
    assert(claimed == 32u);
    scheduler_advance_us(&scheduler, 20000u);
    assert(scheduler.pending == 1u);
    assert(claimed == 32u);

    scheduler_reset(&scheduler);
    scheduler_advance_us(&scheduler, 5000000u);
    assert(scheduler.pending == PJS_SCHEDULER_MAX_PENDING);
    assert(scheduler.dropped_ticks == 44u);

    scheduler_reset(&scheduler);
    static const unsigned jitter[] = {100u, 999u, 5000u, 1u, 15733u, 233u, 77934u};
    unsigned total = 0u;
    while (total < 1000000u) {
        unsigned step = jitter[total % (sizeof(jitter) / sizeof(jitter[0]))];
        if (step > 1000000u - total) step = 1000000u - total;
        scheduler_advance_us(&scheduler, step);
        while (scheduler_take(&scheduler)) {}
        total += step;
    }
    assert(scheduler.total_ticks == 60u);
    assert(scheduler.phase == 0u);
    assert(scheduler.dropped_ticks == 0u);
    puts("scheduler tests: OK");
    return 0;
}
