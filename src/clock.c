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

static uint8_t effective_status(uint8_t command)
{
    /* SWRU191F pp.68-69: RC clamps TICKSPD=000 to effective 001. */
    if ((command & CLOCK_OSC) != 0 && (command & CLOCK_TICKSPD) == 0)
        return command | 0x08u;
    return command;
}

static void observe(clock_diagnostics_t *diagnostics)
{
    diagnostics->observed_command = MMIO_READ(SOC_CLKCONCMD);
    diagnostics->observed_status = MMIO_READ(SOC_CLKCONSTA);
}

static clock_result_t request_and_wait(uint8_t command, uint32_t timeout_ticks,
                                       uint16_t poll_limit, clock_diagnostics_t *diagnostics,
                                       clock_wait_diagnostics_t *wait)
{
    uint32_t start, previous, now, deadline;
    bool expired;

    start = timebase_read_awake_ticks24();
    previous = start;
    wait->timebase_status = timebase_deadline_after(start, timeout_ticks, &deadline);
    /* Also cancel a pending request when the rollback timebase is unusable. */
    MMIO_WRITE(SOC_CLKCONCMD, command);
    if (wait->timebase_status != TIMEBASE_OK) {
        observe(diagnostics);
        return CLOCK_TIMEBASE_ERROR;
    }
    while (wait->polls < poll_limit) {
        observe(diagnostics);
        now = timebase_read_awake_ticks24();
        wait->polls++;
        wait->elapsed_ticks = (now - start) & TIMEBASE_TICKS_MASK;
        if (diagnostics->observed_command != command)
            return CLOCK_COMMAND_CHANGED;
        wait->timebase_status = timebase_expired(now, deadline, &expired);
        if (wait->timebase_status != TIMEBASE_OK)
            return CLOCK_TIMEBASE_ERROR;
        if (wait->elapsed_ticks >= TIMEBASE_HALF_RANGE ||
            ((now - previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
            return CLOCK_COUNTER_RANGE;
        if (expired && wait->elapsed_ticks > timeout_ticks)
            return CLOCK_TIMEOUT;
        if (diagnostics->observed_status == effective_status(command))
            return CLOCK_OK;
        if (expired)
            return CLOCK_TIMEOUT;
        previous = now;
    }
    return CLOCK_POLL_LIMIT;
}

clock_result_t clock_select_init(clock_source_t source, uint32_t timeout_ticks,
                                 uint16_t poll_limit, clock_diagnostics_t *diagnostics)
{
    uint8_t ien0, ien1, ien2, sleep_command, command;
    clock_result_t result;

    if ((source != CLOCK_RC16 && source != CLOCK_XOSC32) ||
        timeout_ticks >= TIMEBASE_HALF_RANGE || poll_limit == 0 || diagnostics == NULL)
        return CLOCK_INVALID_ARGUMENT;

    diagnostics->request.elapsed_ticks = diagnostics->rollback.elapsed_ticks = 0;
    diagnostics->request.polls = diagnostics->rollback.polls = 0;
    diagnostics->request.timebase_status = diagnostics->rollback.timebase_status = TIMEBASE_OK;
    diagnostics->rollback_result = CLOCK_NOT_ATTEMPTED;
    ien0 = MMIO_READ(SOC_IEN0);
    ien1 = MMIO_READ(SOC_IEN1);
    ien2 = MMIO_READ(SOC_IEN2);
    sleep_command = MMIO_READ(SOC_SLEEPCMD);
    observe(diagnostics);
    command = diagnostics->observed_command;
    diagnostics->saved_command = command;
    diagnostics->requested_command = (command & (uint8_t)~(CLOCK_OSC | CLOCK_CLKSPD)) |
                                    (source == CLOCK_RC16 ? CLOCK_OSC | 1u : 0u);
    if (ien0 != 0 || ien1 != 0 || ien2 != 0 || (sleep_command & 0x07u) != 0x04u ||
        (command & CLOCK_CLKSPD) != ((command & CLOCK_OSC) != 0 ? 1u : 0u) ||
        diagnostics->observed_status != effective_status(command))
        return CLOCK_UNSUPPORTED_STATE;
    if (command == diagnostics->requested_command)
        return CLOCK_OK;

    result = request_and_wait(diagnostics->requested_command, timeout_ticks, poll_limit,
                              diagnostics, &diagnostics->request);
    if (result != CLOCK_OK)
        diagnostics->rollback_result = request_and_wait(command, timeout_ticks, poll_limit,
                                                        diagnostics, &diagnostics->rollback);
    return result;
}
