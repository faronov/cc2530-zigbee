/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_CLOCK_FIXTURE_H
#define CC2530_CLOCK_FIXTURE_H

#include "bringup.h"
#include "clock.h"

#define CLOCK_FIXTURE_SIZE 56u
#define CLOCK_FIXTURE_TIMEOUT 1024u
#define CLOCK_FIXTURE_POLL_LIMIT 4096u

enum clock_fixture_phase {
    CLOCK_FIXTURE_INITIALIZED = 1,
    CLOCK_FIXTURE_RUNNING,
    CLOCK_FIXTURE_READY,
    CLOCK_FIXTURE_FAULT
};

enum clock_fixture_reason {
    CLOCK_FIXTURE_NONE = 0,
    CLOCK_FIXTURE_DRIVER_ERROR,
    CLOCK_FIXTURE_INVARIANT,
    CLOCK_FIXTURE_BAD_PHASE
};

/* All multibyte arrays are little-endian. diagnostics explicitly encodes the
 * driver fields in 19 bytes; it is never a copy of a compiler-padded C struct.
 */
typedef struct {
    uint8_t signature[4];
    uint8_t abi_version;
    uint8_t byte_size;
    uint8_t phase;
    uint8_t reason;
    uint8_t stage;
    uint8_t requested_source;
    uint8_t completed_steps;
    uint8_t clock_result;
    uint8_t timeout[3];
    uint8_t poll_limit[2];
    uint8_t diagnostics[19];
    uint8_t initial_sleep_command;
    uint8_t initial_interrupt_enables[3];
    uint8_t current_clock_command;
    uint8_t current_clock_status;
    uint8_t current_sleep_command;
    uint8_t current_interrupt_enables[3];
    uint8_t reserved[8];
    uint8_t guards[2];
} clock_fixture_t;

#define ABI_AT(field, position) \
    typedef char clock_fixture_abi_##field[(offsetof(clock_fixture_t, field) == (position)) ? 1 : -1]
ABI_AT(signature, 0);
ABI_AT(abi_version, 4);
ABI_AT(byte_size, 5);
ABI_AT(phase, 6);
ABI_AT(reason, 7);
ABI_AT(stage, 8);
ABI_AT(requested_source, 9);
ABI_AT(completed_steps, 10);
ABI_AT(clock_result, 11);
ABI_AT(timeout, 12);
ABI_AT(poll_limit, 15);
ABI_AT(diagnostics, 17);
ABI_AT(initial_sleep_command, 36);
ABI_AT(initial_interrupt_enables, 37);
ABI_AT(current_clock_command, 40);
ABI_AT(current_clock_status, 41);
ABI_AT(current_sleep_command, 42);
ABI_AT(current_interrupt_enables, 43);
ABI_AT(reserved, 46);
ABI_AT(guards, 54);
#undef ABI_AT
typedef char clock_fixture_abi_size[(sizeof(clock_fixture_t) == CLOCK_FIXTURE_SIZE) ? 1 : -1];

extern volatile MCU_XDATA clock_fixture_t clock_fixture_state;

/* Awake initialization exercise, no application/peripheral activity.
 * Stages 0/1/2: RC16 idempotence, XOSC32, RC16. Advance only after success.
 * Only explicit initialization/reset clears a fault; failed calls never retry.
 */
void clock_fixture_initialize(void);
void clock_fixture_cycle(void);

#endif
