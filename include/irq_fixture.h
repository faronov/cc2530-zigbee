/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_IRQ_FIXTURE_H
#define CC2530_IRQ_FIXTURE_H

#include "bringup.h"
#include "irq.h"
#include "timebase.h"

#define IRQ_FIXTURE_SIZE 64u
#define IRQ_FIXTURE_TIMEOUT 1024u
#define IRQ_FIXTURE_POLL_LIMIT 4096u

/* Fixture-owned observations, not a public Timer1 service. SWRU191F
 * sections 2.5, 9.1/9.10/9.12, pp.41-48,104,113-119 and TIMIF p.127.
 */
#define IRQ_FIXTURE_REGISTERS(X) \
    X(IRQ_T1CNTL, 0xe2) X(IRQ_T1CNTH, 0xe3) X(IRQ_T1CTL, 0xe4) \
    X(IRQ_T1STAT, 0xaf) X(IRQ_T1CCTL0, 0xe5) X(IRQ_T1CCTL1, 0xe6) \
    X(IRQ_T1CCTL2, 0xe7) X(IRQ_TIMIF, 0xd8) X(IRQ_IRCON, 0xc0) \
    X(IRQ_IP0, 0xa9) X(IRQ_IP1, 0xb9)
#define IRQ_ADDRESS(name, address) name##_ADDRESS = address,
enum irq_fixture_address { IRQ_FIXTURE_REGISTERS(IRQ_ADDRESS) };
#undef IRQ_ADDRESS
#if defined(__SDCC)
#define IRQ_REGISTER(name, address) __sfr __at(address) name;
#define IRQ_T1CCTL3 (*(volatile MCU_XDATA uint8_t *)0x62a3)
#define IRQ_T1CCTL4 (*(volatile MCU_XDATA uint8_t *)0x62a4)
#define IRQ_ISR __interrupt(9)
#else
#define IRQ_REGISTER(name, address) extern volatile uint8_t name;
extern volatile uint8_t IRQ_T1CCTL3, IRQ_T1CCTL4;
#define IRQ_ISR
#endif
IRQ_FIXTURE_REGISTERS(IRQ_REGISTER)
#undef IRQ_REGISTER

enum irq_fixture_phase { IRQ_INITIALIZED = 1, IRQ_RUNNING, IRQ_READY, IRQ_FAULT };
enum irq_fixture_stage { IRQ_IDLE, IRQ_WAIT_PENDING, IRQ_PENDING, IRQ_INNER, IRQ_DELIVERY, IRQ_DONE };
enum irq_fixture_reason {
    IRQ_REASON_NONE, IRQ_REASON_ENTRY, IRQ_REASON_PHASE, IRQ_REASON_TOKEN,
    IRQ_REASON_TIMEBASE, IRQ_REASON_RANGE, IRQ_REASON_TIMEOUT, IRQ_REASON_POLL_LIMIT,
    IRQ_REASON_EARLY, IRQ_REASON_SOURCE, IRQ_REASON_INVARIANT
};

/* All multibyte arrays are little-endian; no native integer/struct padding. */
typedef struct {
    uint8_t signature[4], version, size, phase, reason, stage, completed;
    uint8_t isr_count, isr_before, disabled_token, disabled_result;
    uint8_t outer_token, inner_token, inner_result, outer_result, invalid_result, helper_status;
    uint8_t timeout[3], poll_limit[2], pending_polls[2], pending_elapsed[3];
    uint8_t delivery_polls[2], delivery_elapsed[3], isr_source, isr_cpu;
    uint8_t initial_command, initial_status, initial_sleep, initial_ip0, initial_ip1;
    uint8_t initial_timif, initial_ircon;
    uint8_t ien0, ien1, ien2, control, source, cpu, ip0, ip1, timif, command, status, sleep;
    uint8_t counter[2], reserved[4], guards[2];
} irq_fixture_t;

#define IRQ_OFFSET(field, n) typedef char irq_fixture_offset_##field[(offsetof(irq_fixture_t, field) == (n)) ? 1 : -1]
IRQ_OFFSET(phase, 6);
IRQ_OFFSET(isr_count, 10);
IRQ_OFFSET(outer_token, 14);
IRQ_OFFSET(timeout, 20);
IRQ_OFFSET(pending_polls, 25);
IRQ_OFFSET(delivery_polls, 30);
IRQ_OFFSET(isr_source, 35);
IRQ_OFFSET(initial_command, 37);
IRQ_OFFSET(ien0, 44);
IRQ_OFFSET(counter, 56);
IRQ_OFFSET(guards, 62);
#undef IRQ_OFFSET
typedef char irq_fixture_size[(sizeof(irq_fixture_t) == IRQ_FIXTURE_SIZE) ? 1 : -1];
extern volatile MCU_XDATA irq_fixture_t irq_fixture_state;

void irq_fixture_initialize(void);
void irq_fixture_begin(void);
void irq_fixture_start(void);
void irq_fixture_poll(void);
void irq_fixture_inner(void);
void irq_fixture_release(void);
void irq_fixture_timer1_isr(void) IRQ_ISR;

#endif
