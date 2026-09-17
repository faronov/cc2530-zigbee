/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef PRNG_FIXTURE_H
#define PRNG_FIXTURE_H
#include "bringup.h"
#include "clock.h"
#include "prng.h"

#define PNF_SIZE 88u
#define PNF_VERSION 2u
#define PNF_WORDS 32u
#define PNF_LIMIT 16u
enum pnf_phase { PNF_INIT = 1, PNF_RUNNING, PNF_READY, PNF_FAULT };
enum pnf_stage { PNF_INITIAL, PNF_BENIGN, PNF_SEED, PNF_BATCH, PNF_CLOCK, PNF_END, PNF_PROBE };
enum pnf_reason { PNF_NONE, PNF_ENTRY, PNF_PHASE, PNF_CLOCK_ERROR, PNF_PRNG_ERROR, PNF_INVARIANT, PNF_BYTES, PNF_EXPECTED_STOP };
#define PNF_REGISTERS(X) \
    X(PNF_IP0, 0xa9) X(PNF_IP1, 0xb9) X(PNF_TCON, 0x88) X(PNF_S0CON, 0x98) \
    X(PNF_S1CON, 0x9b) X(PNF_RFIRQF0, 0xe9) X(PNF_RFIRQF1, 0x91) X(PNF_IRCON2, 0xe8)
#define PNF_ADDRESS(name, address) name##_ADDRESS = address,
enum pnf_register_address { PNF_REGISTERS(PNF_ADDRESS) };
#undef PNF_ADDRESS
#if defined(__SDCC)
#define PNF_REGISTER(name, address) __sfr __at(address) name;
#else
#define PNF_REGISTER(name, address) extern volatile uint8_t name;
#endif
PNF_REGISTERS(PNF_REGISTER)
#undef PNF_REGISTER

typedef struct { uint8_t before[2]; uint16_t words[PNF_WORDS]; uint8_t after[2]; } prng_fixture_buffer_t;
typedef struct {
    uint8_t signature[4], version, size, phase, reason, stage, run, completed, count;
    uint8_t result, seed_result, benign, checked, seed[2], index[2], batch[2], total[3], seed_calls;
    uint8_t mismatch, actual[2], expected[2], adc, initial_adc, command, status, sleep, enables[3];
    uint8_t initial_sleep, initial_flags[10], flags[10], hardware[2], probe[3], fault_latch;
    uint8_t clock_result, clock[19], guards[2];
} prng_fixture_t;
typedef char pnf_size[(sizeof(prng_fixture_t) == PNF_SIZE) ? 1 : -1];
typedef char pnf_buffer_size[(sizeof(prng_fixture_buffer_t) == 68) ? 1 : -1];
extern volatile MCU_XDATA prng_fixture_t prng_fixture_state;
extern MCU_XDATA prng_fixture_buffer_t prng_fixture_buffer;
extern MCU_XDATA uint16_t prng_fixture_probe_output;
extern MCU_XDATA clock_diagnostics_t prng_fixture_clock;
void prng_fixture_initialize(void);
#if defined(__SDCC)
void prng_fixture_before(void);
void prng_fixture_ready(void);
void prng_fixture_fault(void);
#else
void prng_fixture_step(void);
#endif
#endif
