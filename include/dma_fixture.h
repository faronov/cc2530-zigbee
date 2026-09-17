/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef DMA_FIXTURE_H
#define DMA_FIXTURE_H

#include "bringup.h"
#include "clock.h"
#include "dma.h"

#define DMF_SIZE 116u
#define DMF_TIMEOUT 1024u
#define DMF_LIMIT 4096u
#define DMF_NOT_ATTEMPTED 255u
enum dmf_phase { DMF_INIT = 1, DMF_RUNNING, DMF_READY, DMF_FAULT };
enum dmf_stage { DMF_INITIAL_RC, DMF_COPY_RC, DMF_CLOCK_X, DMF_COPY_X, DMF_CLOCK_RC };
enum dmf_reason { DMF_NONE, DMF_ENTRY, DMF_PHASE, DMF_CLOCK_ERROR, DMF_DMA_ERROR, DMF_INVARIANT, DMF_BYTES };

#define DMF_REGISTERS(X) \
    X(DMF_IP0, 0xa9) X(DMF_IP1, 0xb9) X(DMF_TCON, 0x88) \
    X(DMF_S0CON, 0x98) X(DMF_S1CON, 0x9b) X(DMF_RFIRQF0, 0xe9) \
    X(DMF_RFIRQF1, 0x91) X(DMF_IRCON2, 0xe8)
#define DMF_ADDRESS(name, address) name##_ADDRESS = address,
enum dmf_register_address { DMF_REGISTERS(DMF_ADDRESS) };
#undef DMF_ADDRESS
#if defined(__SDCC)
#define DMF_REGISTER(name, address) __sfr __at(address) name;
#else
#define DMF_REGISTER(name, address) extern volatile uint8_t name;
#endif
DMF_REGISTERS(DMF_REGISTER)
#undef DMF_REGISTER

typedef struct { uint8_t before, data[16], after; } dma_fixture_buffer_t;
typedef union { clock_diagnostics_t clock; dma_diagnostics_t dma; } dma_fixture_work_t;
typedef struct {
    uint8_t signature[4], version, size, phase, reason, stage, completed;
    uint8_t clock_result, dma_result, length, checked, mismatch_buffer, mismatch_index, actual, expected;
    uint8_t source[2], destination[2], timeout[3], limit[2], clock[19], dma[19];
    uint8_t command, status, sleep, enables[3], controller[8], sample_valid, fault_latch, descriptor[8];
    uint8_t initial_sleep, initial_ircon, initial_cfg1[2], initial_flags[9], flags[9], guards[2], reserved[3];
} dma_fixture_t;
typedef char dmf_size[(sizeof(dma_fixture_t) == DMF_SIZE) ? 1 : -1];
#define DMF_OFFSET(field, n) typedef char dmf_offset_##field[(offsetof(dma_fixture_t, field) == n) ? 1 : -1]
DMF_OFFSET(clock, 27); DMF_OFFSET(dma, 46); DMF_OFFSET(command, 65);
DMF_OFFSET(controller, 71); DMF_OFFSET(descriptor, 81); DMF_OFFSET(guards, 111);
#undef DMF_OFFSET

extern volatile MCU_XDATA dma_fixture_t dma_fixture_state;
extern volatile MCU_XDATA dma_fixture_buffer_t dma_fixture_a, dma_fixture_b;
extern MCU_XDATA dma_fixture_work_t dma_fixture_work;
void dma_fixture_initialize(void);
#if defined(__SDCC)
void dma_fixture_before(void);
void dma_fixture_ready(void);
void dma_fixture_fault(void);
#else
void dma_fixture_step(void);
#endif
#endif
