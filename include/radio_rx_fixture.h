/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef RADIO_RX_FIXTURE_H
#define RADIO_RX_FIXTURE_H
#include "bringup.h"
#include "clock.h"
#include "radio_rx.h"

#define RXF_SIZE 96u
#define RXF_ATTEMPTS 16u
#define RXF_TIMEOUT 65536UL
#define RXF_LIMIT 65535u
enum rxf_phase { RXF_INIT = 1, RXF_RUNNING, RXF_READY, RXF_FAULT, RXF_END };
enum rxf_reason { RXF_NONE, RXF_ENTRY, RXF_PHASE, RXF_CLOCK_ERROR, RXF_RX_ERROR, RXF_INVARIANT };
#define RXF_REGISTERS(X) \
    X(RXF_IP0, 0xa9) X(RXF_IP1, 0xb9) X(RXF_TCON, 0x88) X(RXF_S0CON, 0x98) \
    X(RXF_S1CON, 0x9b) X(RXF_IRCON2, 0xe8)
#define RXF_ADDRESS(name, address) name##_ADDRESS = address,
enum rxf_register_address { RXF_REGISTERS(RXF_ADDRESS) };
#undef RXF_ADDRESS
#if defined(__SDCC)
#define RXF_REGISTER(name, address) __sfr __at(address) name;
#else
#define RXF_REGISTER(name, address) extern volatile uint8_t name;
#endif
RXF_REGISTERS(RXF_REGISTER)
#undef RXF_REGISTER

/* Byte-only wire ABI. Native clock/RX structures are explicitly serialized. */
typedef struct {
    uint8_t signature[4], version, size, phase, reason, stage, attempt, completed;
    uint8_t result, fault_latch, clock_result, channel, maximum, timeout[4], limit[2];
    uint8_t clock[19], diagnostic[31];
    uint8_t command, status, sleep, enables[3], initial_sleep;
    uint8_t initial_flags[7], flags[7], guards[3];
} radio_rx_fixture_t;
typedef char rxf_size[(sizeof(radio_rx_fixture_t) == RXF_SIZE) ? 1 : -1];
extern volatile MCU_XDATA radio_rx_fixture_t radio_rx_fixture_state;
extern MCU_XDATA radio_rx_frame_t radio_rx_fixture_frame;
extern MCU_XDATA radio_rx_diagnostics_t radio_rx_fixture_diagnostics;
extern MCU_XDATA clock_diagnostics_t radio_rx_fixture_clock;
void radio_rx_fixture_initialize(void);
#if defined(__SDCC)
void radio_rx_fixture_before(void);
void radio_rx_fixture_ready(void);
void radio_rx_fixture_fault(void);
void radio_rx_fixture_end(void);
#else
void radio_rx_fixture_step(void);
#endif
#endif
