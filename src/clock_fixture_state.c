/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "clock_fixture.h"

volatile MCU_XDATA clock_fixture_t clock_fixture_state;
static MCU_XDATA clock_diagnostics_t diagnostics;

static void put16(volatile MCU_XDATA uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put32(volatile MCU_XDATA uint8_t *out, uint32_t value)
{
    put16(out, (uint16_t)value);
    put16(out + 2, (uint16_t)(value >> 16));
}

static void serialize(void)
{
    volatile MCU_XDATA uint8_t *out = clock_fixture_state.diagnostics;
    put32(out, diagnostics.request.elapsed_ticks);
    put16(out + 4, diagnostics.request.polls);
    out[6] = diagnostics.request.timebase_status;
    put32(out + 7, diagnostics.rollback.elapsed_ticks);
    put16(out + 11, diagnostics.rollback.polls);
    out[13] = diagnostics.rollback.timebase_status;
    out[14] = diagnostics.saved_command;
    out[15] = diagnostics.requested_command;
    out[16] = diagnostics.observed_command;
    out[17] = diagnostics.observed_status;
    out[18] = diagnostics.rollback_result;
}

static void observe(void)
{
    clock_fixture_state.current_clock_command = MMIO_READ(SOC_CLKCONCMD);
    clock_fixture_state.current_clock_status = MMIO_READ(SOC_CLKCONSTA);
    clock_fixture_state.current_sleep_command = MMIO_READ(SOC_SLEEPCMD);
    clock_fixture_state.current_interrupt_enables[0] = MMIO_READ(SOC_IEN0);
    clock_fixture_state.current_interrupt_enables[1] = MMIO_READ(SOC_IEN1);
    clock_fixture_state.current_interrupt_enables[2] = MMIO_READ(SOC_IEN2);
}

static uint8_t invariants(void)
{
    uint8_t i;
    if (clock_fixture_state.current_sleep_command != clock_fixture_state.initial_sleep_command ||
        (clock_fixture_state.current_sleep_command & 7u) != 4u ||
        (clock_fixture_state.current_clock_command & 0xb8u) != (m0_status.clock_request & 0xb8u))
        return 0;
    for (i = 0; i < 3; i++)
        if (clock_fixture_state.current_interrupt_enables[i] != 0 ||
            clock_fixture_state.initial_interrupt_enables[i] != 0)
            return 0;
    return 1;
}

static void fault(uint8_t reason)
{
    clock_fixture_state.reason = reason;
    clock_fixture_state.phase = CLOCK_FIXTURE_FAULT;
}

void clock_fixture_initialize(void)
{
    uint8_t i;
    volatile MCU_XDATA uint8_t *bytes = (volatile MCU_XDATA uint8_t *)&clock_fixture_state;

    bringup_initialize();
    for (i = 0; i < CLOCK_FIXTURE_SIZE; i++)
        bytes[i] = 0;
    clock_fixture_state.signature[0] = 'M';
    clock_fixture_state.signature[1] = '2';
    clock_fixture_state.signature[2] = 'C';
    clock_fixture_state.signature[3] = 'K';
    clock_fixture_state.abi_version = 1;
    clock_fixture_state.byte_size = CLOCK_FIXTURE_SIZE;
    clock_fixture_state.clock_result = CLOCK_NOT_ATTEMPTED;
    clock_fixture_state.diagnostics[18] = CLOCK_NOT_ATTEMPTED;
    put16(clock_fixture_state.timeout, CLOCK_FIXTURE_TIMEOUT);
    put16(clock_fixture_state.poll_limit, CLOCK_FIXTURE_POLL_LIMIT);
    clock_fixture_state.guards[0] = 0x69;
    clock_fixture_state.guards[1] = 0x96;
    observe();
    clock_fixture_state.initial_sleep_command = clock_fixture_state.current_sleep_command;
    for (i = 0; i < 3; i++)
        clock_fixture_state.initial_interrupt_enables[i] = clock_fixture_state.current_interrupt_enables[i];
    if (!invariants() || (clock_fixture_state.current_clock_command & 0x47u) != 0x41u ||
        clock_fixture_state.current_clock_command != m0_status.clock_request ||
        clock_fixture_state.current_clock_status !=
        (uint8_t)(clock_fixture_state.current_clock_command |
                  ((clock_fixture_state.current_clock_command & 0x38u) == 0 ? 8u : 0u))) {
        fault(CLOCK_FIXTURE_INVARIANT);
        return;
    }
    clock_fixture_state.phase = CLOCK_FIXTURE_INITIALIZED;
}

void clock_fixture_cycle(void)
{
    clock_result_t result;
    uint8_t i;
    uint8_t *bytes = (uint8_t *)&diagnostics;

    if (clock_fixture_state.phase == CLOCK_FIXTURE_FAULT)
        return;
    if ((clock_fixture_state.phase != CLOCK_FIXTURE_INITIALIZED &&
         clock_fixture_state.phase != CLOCK_FIXTURE_READY) || clock_fixture_state.stage > 2) {
        fault(CLOCK_FIXTURE_BAD_PHASE);
        return;
    }
    if (clock_fixture_state.phase == CLOCK_FIXTURE_READY)
        clock_fixture_state.stage = clock_fixture_state.stage == 2 ? 0 : clock_fixture_state.stage + 1;
    clock_fixture_state.requested_source = clock_fixture_state.stage == 1 ? CLOCK_XOSC32 : CLOCK_RC16;
    clock_fixture_state.phase = CLOCK_FIXTURE_RUNNING;
    clock_fixture_state.clock_result = CLOCK_NOT_ATTEMPTED;
    for (i = 0; i < sizeof(diagnostics); i++)
        bytes[i] = 0;
    diagnostics.rollback_result = CLOCK_NOT_ATTEMPTED;
    serialize();
    result = clock_select_init((clock_source_t)clock_fixture_state.requested_source,
                               CLOCK_FIXTURE_TIMEOUT, CLOCK_FIXTURE_POLL_LIMIT, &diagnostics);
    clock_fixture_state.clock_result = (uint8_t)result;
    serialize();
    observe();
    if (result != CLOCK_OK) {
        fault(CLOCK_FIXTURE_DRIVER_ERROR);
        return;
    }
    if (!invariants() || diagnostics.observed_command != clock_fixture_state.current_clock_command ||
        diagnostics.observed_status != clock_fixture_state.current_clock_status ||
        (clock_fixture_state.current_clock_command & 0x47u) !=
        (clock_fixture_state.requested_source == CLOCK_RC16 ? 0x41u : 0u) ||
        diagnostics.rollback_result != CLOCK_NOT_ATTEMPTED ||
        (clock_fixture_state.stage == 0 ? diagnostics.request.polls != 0 : diagnostics.request.polls == 0)) {
        fault(CLOCK_FIXTURE_INVARIANT);
        return;
    }
    clock_fixture_state.completed_steps++;
    bringup_tick();
    clock_fixture_state.phase = CLOCK_FIXTURE_READY;
}
