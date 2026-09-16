/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_TIMEBASE_FIXTURE_H
#define CC2530_TIMEBASE_FIXTURE_H

#include "bringup.h"
#include "timebase.h"

#define TIMEBASE_FIXTURE_SIZE 32u
#define TIMEBASE_FIXTURE_DELAY 128u
#define TIMEBASE_FIXTURE_POLL_LIMIT 1024u

enum timebase_fixture_phase {
    TIMEBASE_FIXTURE_INITIALIZED = 1,
    TIMEBASE_FIXTURE_RUNNING = 2,
    TIMEBASE_FIXTURE_READY = 3,
    TIMEBASE_FIXTURE_FAULT = 4
};

enum timebase_fixture_reason {
    TIMEBASE_FIXTURE_NONE = 0,
    TIMEBASE_FIXTURE_DEADLINE_ERROR = 1,
    TIMEBASE_FIXTURE_EXPIRY_ERROR = 2,
    TIMEBASE_FIXTURE_CLOCK_RANGE = 3,
    TIMEBASE_FIXTURE_POLL_LIMIT_REACHED = 4,
    TIMEBASE_FIXTURE_BAD_PHASE = 5
};

/* Byte-oriented debugger ABI. Multibyte arrays are little-endian raw ticks/counts. */
typedef struct {
    uint8_t signature[4];
    uint8_t abi_version;
    uint8_t byte_size;
    uint8_t phase;
    uint8_t reason;
    uint8_t completed_cycles;
    uint8_t helper_status;
    uint8_t start[3];
    uint8_t end[3];
    uint8_t deadline[3];
    uint8_t elapsed[3];
    uint8_t polls[2];
    uint8_t delay[2];
    uint8_t poll_limit[2];
    uint8_t reserved[2];
    uint8_t guards[2];
} timebase_fixture_t;

#define ABI_AT(field, position) \
    typedef char timebase_fixture_abi_##field[(offsetof(timebase_fixture_t, field) == (position)) ? 1 : -1]
ABI_AT(signature, 0);
ABI_AT(abi_version, 4);
ABI_AT(byte_size, 5);
ABI_AT(phase, 6);
ABI_AT(reason, 7);
ABI_AT(completed_cycles, 8);
ABI_AT(helper_status, 9);
ABI_AT(start, 10);
ABI_AT(end, 13);
ABI_AT(deadline, 16);
ABI_AT(elapsed, 19);
ABI_AT(polls, 22);
ABI_AT(delay, 24);
ABI_AT(poll_limit, 26);
ABI_AT(reserved, 28);
ABI_AT(guards, 30);
#undef ABI_AT
typedef char timebase_fixture_abi_size[(sizeof(timebase_fixture_t) == TIMEBASE_FIXTURE_SIZE) ? 1 : -1];

extern volatile MCU_XDATA timebase_fixture_t timebase_fixture_state;

/* One foreground owner. Only explicit initialization clears a latched fault.
 * begin samples once; each poll samples once, with an independent finite limit.
 * Observe complete records only at the target example's named checkpoints.
 */
void timebase_fixture_initialize(void);
void timebase_fixture_begin(void);
void timebase_fixture_poll(void);

#endif
