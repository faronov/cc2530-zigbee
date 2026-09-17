/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef RADIO_FIFO_FIXTURE_H
#define RADIO_FIFO_FIXTURE_H

#include "bringup.h"
#include "clock.h"
#include "radio_fifo.h"

#define RFF_SIZE 108u
#define RFF_TIMEOUT 1024u
#define RFF_LIMIT 4096u
#define RFF_NOT_ATTEMPTED 255u

enum rff_phase { RFF_INIT = 1, RFF_RUNNING, RFF_READY, RFF_FAULT };
enum rff_stage { RFF_CLOCK, RFF_EMPTY, RFF_SMALL, RFF_CLEAR_SMALL, RFF_MAX, RFF_CLEAR_MAX };
enum rff_reason { RFF_NONE, RFF_ENTRY, RFF_PHASE, RFF_CLOCK_ERROR, RFF_FIFO_ERROR, RFF_INVARIANT, RFF_BYTES };

#define RFF_REGISTERS(X) \
    X(RFF_IP0, 0xa9) X(RFF_IP1, 0xb9) X(RFF_RFIRQF0, 0xe9) \
    X(RFF_RFIRQF1, 0x91) X(RFF_S1CON, 0x9b) X(RFF_TCON, 0x88)
#define RFF_ADDRESS(name, address) name##_ADDRESS = address,
enum rff_register_address { RFF_REGISTERS(RFF_ADDRESS) };
#undef RFF_ADDRESS
#if defined(__SDCC)
#define RFF_REGISTER(name, address) __sfr __at(address) name;
#else
#define RFF_REGISTER(name, address) extern volatile uint8_t name;
#endif
RFF_REGISTERS(RFF_REGISTER)
#undef RFF_REGISTER

typedef struct {
    uint8_t signature[4], version, size, phase, reason, stage, completed;
    uint8_t clock_result, fifo_result, timeout[3], limit[2];
    uint8_t clock[19], fifo[21];
    uint8_t checked, mismatch_index, actual, expected;
    uint8_t command, status, sleep, enables[3], radio[14];
    uint8_t initial_flags[9], flags[9], radio_valid, initial_sleep, reserved[5], guards[2];
} radio_fifo_fixture_t;

#define RFF_OFFSET(field, n) typedef char rff_offset_##field[(offsetof(radio_fifo_fixture_t, field) == n) ? 1 : -1]
RFF_OFFSET(clock, 17);
RFF_OFFSET(fifo, 36);
RFF_OFFSET(checked, 57);
RFF_OFFSET(command, 61);
RFF_OFFSET(radio, 67);
RFF_OFFSET(initial_flags, 81);
RFF_OFFSET(flags, 90);
RFF_OFFSET(guards, 106);
#undef RFF_OFFSET
typedef char rff_size[(sizeof(radio_fifo_fixture_t) == RFF_SIZE) ? 1 : -1];

extern volatile MCU_XDATA radio_fifo_fixture_t radio_fifo_fixture_state;
void radio_fifo_fixture_initialize(void);
#if defined(__SDCC)
void radio_fifo_fixture_before(void);
void radio_fifo_fixture_ready(void);
void radio_fifo_fixture_fault(void);
#else
void radio_fifo_fixture_step(void);
#endif

#endif
