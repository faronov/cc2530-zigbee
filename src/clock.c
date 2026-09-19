/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "clock.h"
#include "cc2530_mmio.h"
#include "timebase.h"
#include <stddef.h>

#define CLOCK_OSC 0x40u
#define CLOCK_TICKSPD 0x38u
#define CLOCK_CLKSPD 0x07u

/* The API was already non-reentrant. Explicit bounded XDATA staging and
 * generic-store leaves prevent SDCC from keeping generic pointer temporaries
 * in permanent DATA across timebase calls. The PUBLIC pointer remains generic.
 * Publish the same fields at the same MMIO boundaries, including failures;
 * this is NOT a success-only copy or a change to cancellation/rollback.
 */
static clock_diagnostics_t * MCU_XDATA output;
static MCU_XDATA struct {
    uint8_t saved_command, requested_command, observed_command, observed_status;
    clock_result_t rollback_result;
} observed;
/* Request and rollback are sequential: only their caller-owned diagnostics
 * must coexist. Reuse this bounded counter rather than duplicate both here. */
static MCU_XDATA clock_wait_diagnostics_t count;
static MCU_XDATA bool rollback;
static MCU_XDATA struct {
    uint32_t start, previous, now, deadline;
    uint8_t unsupported, source_seen;
    bool expired;
    clock_result_t result;
} work;

static uint8_t effective_status(uint8_t command)
{
    /* SWRU191F pp.68-69: RC clamps TICKSPD=000 to effective 001. */
    if ((command & CLOCK_OSC) != 0 && (command & CLOCK_TICKSPD) == 0)
        return command | 0x08u;
    return command;
}
static void observe(void)
{
    observed.observed_command = MMIO_READ(SOC_CLKCONCMD);
    output->observed_command = observed.observed_command;
    observed.observed_status = MMIO_READ(SOC_CLKCONSTA);
    output->observed_status = observed.observed_status;
}
static void reset_diagnostics(void)
{
    output->request.elapsed_ticks = output->rollback.elapsed_ticks = 0;
    output->request.polls = output->rollback.polls = 0;
    output->request.timebase_status = output->rollback.timebase_status = TIMEBASE_OK;
    output->rollback_result = CLOCK_NOT_ATTEMPTED;
    count.elapsed_ticks = 0;
    count.polls = 0;
    count.timebase_status = TIMEBASE_OK;
    observed.rollback_result = CLOCK_NOT_ATTEMPTED;
}
static void publish_commands(void)
{
    output->saved_command = observed.saved_command;
    output->requested_command = observed.requested_command;
}
static void publish_status(void)
{
    if (!rollback) output->request.timebase_status = count.timebase_status;
    else output->rollback.timebase_status = count.timebase_status;
}
static void publish_count(void)
{
    if (!rollback) {
        output->request.polls = count.polls;
        output->request.elapsed_ticks = count.elapsed_ticks;
    } else {
        output->rollback.polls = count.polls;
        output->rollback.elapsed_ticks = count.elapsed_ticks;
    }
}
static void publish_rollback(void) { output->rollback_result = observed.rollback_result; }

static clock_result_t request_and_wait(uint8_t command, uint32_t timeout_ticks,
                                       uint16_t poll_limit)
{
    work.start = timebase_read_awake_ticks24(); work.previous = work.start;
    count.timebase_status = timebase_deadline_after(work.start, timeout_ticks, &work.deadline);
    publish_status();
    /* Also cancel a pending request when the rollback timebase is unusable. */
    MMIO_WRITE(SOC_CLKCONCMD, command);
    while (count.polls < poll_limit) {
        observe();
        /* STA.OSC is source evidence, not a cancellation flag. */
        if (((observed.observed_status ^ observed.requested_command) & CLOCK_OSC) == 0)
            work.source_seen = 1;
        if (count.timebase_status != TIMEBASE_OK) return CLOCK_TIMEBASE_ERROR;
        work.now = timebase_read_awake_ticks24();
        count.polls++;
        count.elapsed_ticks = (work.now - work.start) & TIMEBASE_TICKS_MASK;
        publish_count();
        if (observed.observed_command != command) return CLOCK_COMMAND_CHANGED;
        count.timebase_status = timebase_expired(work.now, work.deadline, &work.expired);
        publish_status();
        if (count.timebase_status != TIMEBASE_OK) return CLOCK_TIMEBASE_ERROR;
        if (count.elapsed_ticks >= TIMEBASE_HALF_RANGE ||
            ((work.now - work.previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
            return CLOCK_COUNTER_RANGE;
        if (work.expired && count.elapsed_ticks > timeout_ticks) return CLOCK_TIMEOUT;
        if (work.source_seen && observed.observed_status == effective_status(command)) return CLOCK_OK;
        if (work.expired) return CLOCK_TIMEOUT;
        work.previous = work.now;
    }
    return CLOCK_POLL_LIMIT;
}
clock_result_t clock_select_init(clock_source_t source, uint32_t timeout_ticks,
                                 uint16_t poll_limit, clock_diagnostics_t *diagnostics)
{
    if ((source != CLOCK_RC16 && source != CLOCK_XOSC32) ||
        timeout_ticks >= TIMEBASE_HALF_RANGE || poll_limit == 0 || diagnostics == NULL)
        return CLOCK_INVALID_ARGUMENT;
    output = diagnostics;
    reset_diagnostics();
    work.unsupported = MMIO_READ(SOC_IEN0);
    work.unsupported |= MMIO_READ(SOC_IEN1);
    work.unsupported |= MMIO_READ(SOC_IEN2);
    work.unsupported |= (MMIO_READ(SOC_SLEEPCMD) & 7u) ^ 4u;
    observe();
    observed.saved_command = observed.observed_command;
    observed.requested_command = (observed.saved_command & (uint8_t)~(CLOCK_OSC | CLOCK_CLKSPD)) |
                                (source == CLOCK_RC16 ? CLOCK_OSC | 1u : 0u);
    publish_commands();
    if (work.unsupported != 0 ||
        (observed.saved_command & CLOCK_CLKSPD) != ((observed.saved_command & CLOCK_OSC) ? 1u : 0u) ||
        observed.observed_status != effective_status(observed.saved_command))
        return CLOCK_UNSUPPORTED_STATE;
    if (observed.saved_command == observed.requested_command) return CLOCK_OK;
    work.source_seen = 0; rollback = false;
    work.result = request_and_wait(observed.requested_command, timeout_ticks, poll_limit);
    if (work.result != CLOCK_OK) {
        rollback = true;
        count.polls = 0; count.elapsed_ticks = 0;
        observed.rollback_result = request_and_wait(observed.saved_command, timeout_ticks, poll_limit);
        if (!work.source_seen && (observed.rollback_result == CLOCK_TIMEOUT ||
                                 observed.rollback_result == CLOCK_POLL_LIMIT))
            observed.rollback_result = CLOCK_ROLLBACK_UNCONFIRMED;
        publish_rollback();
    }
    return work.result;
}
