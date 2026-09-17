/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "irq_fixture.h"

volatile MCU_XDATA irq_fixture_t irq_fixture_state;
static MCU_XDATA uint32_t start, previous, deadline;
static MCU_XDATA uint16_t polls;
static uint8_t owned;

static void put16(volatile MCU_XDATA uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put24(volatile MCU_XDATA uint8_t *out, uint32_t value)
{
    put16(out, (uint16_t)value);
    out[2] = (uint8_t)(value >> 16);
}

static void observe(void)
{
    irq_fixture_state.ien0 = MMIO_READ(SOC_IEN0);
    irq_fixture_state.ien1 = MMIO_READ(SOC_IEN1);
    irq_fixture_state.ien2 = MMIO_READ(SOC_IEN2);
    irq_fixture_state.control = MMIO_READ(IRQ_T1CTL);
    irq_fixture_state.source = MMIO_READ(IRQ_T1STAT);
    irq_fixture_state.cpu = MMIO_READ(IRQ_IRCON);
    irq_fixture_state.ip0 = MMIO_READ(IRQ_IP0);
    irq_fixture_state.ip1 = MMIO_READ(IRQ_IP1);
    irq_fixture_state.timif = MMIO_READ(IRQ_TIMIF);
    irq_fixture_state.command = MMIO_READ(SOC_CLKCONCMD);
    irq_fixture_state.status = MMIO_READ(SOC_CLKCONSTA);
    irq_fixture_state.sleep = MMIO_READ(SOC_SLEEPCMD);
    irq_fixture_state.counter[0] = MMIO_READ(IRQ_T1CNTL);
    irq_fixture_state.counter[1] = MMIO_READ(IRQ_T1CNTH);
}

static uint8_t invariant(void)
{
    return irq_fixture_state.ien2 == 0 && !(irq_fixture_state.ien0 & 0x7fu) &&
        !(irq_fixture_state.ien1 & 0xfdu) &&
        irq_fixture_state.ip0 == irq_fixture_state.initial_ip0 &&
        irq_fixture_state.ip1 == irq_fixture_state.initial_ip1 &&
        irq_fixture_state.timif == irq_fixture_state.initial_timif &&
        (irq_fixture_state.cpu & 0xfdu) == irq_fixture_state.initial_ircon &&
        irq_fixture_state.command == irq_fixture_state.initial_command &&
        irq_fixture_state.status == irq_fixture_state.initial_status &&
        irq_fixture_state.sleep == irq_fixture_state.initial_sleep &&
        MMIO_READ(IRQ_T1CCTL0) == 0x40u && MMIO_READ(IRQ_T1CCTL1) == 0x40u &&
        MMIO_READ(IRQ_T1CCTL2) == 0x40u && IRQ_T1CCTL3 == 0x40u && IRQ_T1CCTL4 == 0x40u;
}

static void fault(uint8_t reason)
{
    if (irq_fixture_state.phase == IRQ_FAULT)
        return;
    if (owned) {
        /* Terminal abandonment, not a successful token restoration/retry. */
        MMIO_CLEAR(SOC_IEN0, 0x80u);
        MMIO_CLEAR(SOC_IEN1, 2u);
        MMIO_WRITE(IRQ_T1CTL, 0);
    }
    observe();
    irq_fixture_state.reason = reason;
    irq_fixture_state.phase = IRQ_FAULT;
}

static uint8_t wait_begin(void)
{
    polls = 0;
    start = timebase_read_awake_ticks24();
    previous = start;
    irq_fixture_state.helper_status = (uint8_t)timebase_deadline_after(start, IRQ_FIXTURE_TIMEOUT, &deadline);
    if (irq_fixture_state.helper_status != TIMEBASE_OK) {
        fault(IRQ_REASON_TIMEBASE);
        return 0;
    }
    return 1;
}

void irq_fixture_timer1_isr(void) IRQ_ISR
{
    /* CPU T1IF is H0 on entry. Mask first; R/W0 write preserves other flags,
     * including flags that assert between the observation and acknowledgment.
     */
    MMIO_CLEAR(SOC_IEN1, 2u);
    irq_fixture_state.isr_source = MMIO_READ(IRQ_T1STAT);
    irq_fixture_state.isr_cpu = MMIO_READ(IRQ_IRCON);
    MMIO_WRITE(IRQ_T1STAT, 0x1fu);
    irq_fixture_state.isr_count++;
}

void irq_fixture_initialize(void)
{
    uint8_t i;
    volatile MCU_XDATA uint8_t *bytes = (volatile MCU_XDATA uint8_t *)&irq_fixture_state;
    bringup_initialize();
    owned = 0;
    for (i = 0; i < IRQ_FIXTURE_SIZE; i++)
        bytes[i] = 0;
    irq_fixture_state.signature[0] = 'M';
    irq_fixture_state.signature[1] = '2';
    irq_fixture_state.signature[2] = 'I';
    irq_fixture_state.signature[3] = 'Q';
    irq_fixture_state.version = 1;
    irq_fixture_state.size = IRQ_FIXTURE_SIZE;
    put24(irq_fixture_state.timeout, IRQ_FIXTURE_TIMEOUT);
    put16(irq_fixture_state.poll_limit, IRQ_FIXTURE_POLL_LIMIT);
    irq_fixture_state.guards[0] = 0x69;
    irq_fixture_state.guards[1] = 0x96;
    observe();
    irq_fixture_state.initial_command = irq_fixture_state.command;
    irq_fixture_state.initial_status = irq_fixture_state.status;
    irq_fixture_state.initial_sleep = irq_fixture_state.sleep;
    irq_fixture_state.initial_ip0 = irq_fixture_state.ip0;
    irq_fixture_state.initial_ip1 = irq_fixture_state.ip1;
    irq_fixture_state.initial_timif = irq_fixture_state.timif;
    irq_fixture_state.initial_ircon = irq_fixture_state.cpu & 0xfdu;
    if (!invariant() || irq_fixture_state.ien0 || irq_fixture_state.ien1 ||
        irq_fixture_state.control || irq_fixture_state.source || (irq_fixture_state.cpu & 0x42u) ||
        !(irq_fixture_state.timif & 0x40u) || (irq_fixture_state.sleep & 7u) != 4u ||
        irq_fixture_state.command != 0xc9u || irq_fixture_state.status != 0xc9u ||
        m0_status.selections[0] || m0_status.selections[1] || m0_status.selections[2] ||
        irq_fixture_state.counter[0] || irq_fixture_state.counter[1]) {
        fault(IRQ_REASON_ENTRY);
        return;
    }
    owned = 1;
    irq_fixture_state.phase = IRQ_INITIALIZED;
}

void irq_fixture_begin(void)
{
    if (irq_fixture_state.phase == IRQ_FAULT)
        return;
    if (irq_fixture_state.phase != IRQ_INITIALIZED && irq_fixture_state.phase != IRQ_READY) {
        fault(IRQ_REASON_PHASE);
        return;
    }
    observe();
    if (!invariant() || irq_fixture_state.ien0 || irq_fixture_state.ien1 ||
        irq_fixture_state.control || irq_fixture_state.source || (irq_fixture_state.cpu & 2u)) {
        fault(IRQ_REASON_INVARIANT);
        return;
    }
    irq_fixture_state.phase = IRQ_RUNNING;
    irq_fixture_state.stage = IRQ_WAIT_PENDING;
    irq_fixture_state.isr_before = irq_fixture_state.isr_count;
    irq_fixture_state.isr_source = irq_fixture_state.isr_cpu = 0;
    put16(irq_fixture_state.pending_polls, 0);
    put24(irq_fixture_state.pending_elapsed, 0);
    put16(irq_fixture_state.delivery_polls, 0);
    put24(irq_fixture_state.delivery_elapsed, 0);
    irq_fixture_state.outer_result = 0xff;
    irq_fixture_state.disabled_token = irq_save_disable();
    irq_fixture_state.disabled_result = (uint8_t)irq_restore(irq_fixture_state.disabled_token);
    irq_fixture_state.invalid_result = (uint8_t)irq_restore(0xff);
    if (irq_fixture_state.disabled_token != 0 || irq_fixture_state.disabled_result != IRQ_OK ||
        irq_fixture_state.invalid_result != IRQ_INVALID_TOKEN || MMIO_READ(SOC_IEN0) != 0) {
        fault(IRQ_REASON_TOKEN);
        return;
    }
    /* Explicit fixture initialization policy; never forge a saved token. */
    MMIO_SET(SOC_IEN0, 0x80u);
    irq_fixture_state.outer_token = irq_save_disable();
    irq_fixture_state.inner_token = irq_save_disable();
    if (irq_fixture_state.outer_token != 1 || irq_fixture_state.inner_token != 0) {
        fault(IRQ_REASON_TOKEN);
        return;
    }
    MMIO_SET(SOC_IEN1, 2u);
    MMIO_WRITE(IRQ_T1CNTL, 0);
    if (!wait_begin())
        return;
    observe();
}

void irq_fixture_start(void)
{
    if (irq_fixture_state.phase == IRQ_FAULT)
        return;
    if (irq_fixture_state.phase != IRQ_RUNNING || irq_fixture_state.stage != IRQ_WAIT_PENDING) {
        fault(IRQ_REASON_PHASE);
        return;
    }
    MMIO_WRITE(IRQ_T1CTL, 1);
}

void irq_fixture_inner(void)
{
    if (irq_fixture_state.phase == IRQ_FAULT)
        return;
    if (irq_fixture_state.stage != IRQ_PENDING || irq_fixture_state.phase != IRQ_RUNNING) {
        fault(IRQ_REASON_PHASE);
        return;
    }
    irq_fixture_state.inner_result = (uint8_t)irq_restore(irq_fixture_state.inner_token);
    observe();
    if (irq_fixture_state.inner_result != IRQ_OK || irq_fixture_state.ien0 ||
        irq_fixture_state.isr_count != irq_fixture_state.isr_before) {
        fault(IRQ_REASON_EARLY);
        return;
    }
    if (!invariant() || irq_fixture_state.control || irq_fixture_state.source != 0x20u ||
        !(irq_fixture_state.cpu & 2u) || irq_fixture_state.ien1 != 2) {
        fault(IRQ_REASON_SOURCE);
        return;
    }
    irq_fixture_state.stage = IRQ_INNER;
}

void irq_fixture_release(void)
{
    if (irq_fixture_state.phase == IRQ_FAULT)
        return;
    if (irq_fixture_state.stage != IRQ_INNER || irq_fixture_state.phase != IRQ_RUNNING) {
        fault(IRQ_REASON_PHASE);
        return;
    }
    irq_fixture_state.stage = IRQ_DELIVERY;
    irq_fixture_state.outer_result = (uint8_t)irq_restore(irq_fixture_state.outer_token);
    if (irq_fixture_state.outer_result != IRQ_OK) {
        fault(IRQ_REASON_TOKEN);
        return;
    }
    /* Pending delivery may interrupt restore itself. Start the foreground wait
     * after its return, not across debugger inspection of the ISR/RETI.
     */
    (void)wait_begin();
}

void irq_fixture_poll(void)
{
    uint32_t now, elapsed;
    bool expired;
    uint8_t delivery;
    if (irq_fixture_state.phase == IRQ_FAULT)
        return;
    delivery = irq_fixture_state.stage == IRQ_DELIVERY;
    if (irq_fixture_state.phase != IRQ_RUNNING ||
        (!delivery && irq_fixture_state.stage != IRQ_WAIT_PENDING)) {
        fault(IRQ_REASON_PHASE);
        return;
    }
    now = timebase_read_awake_ticks24();
    elapsed = (now - start) & TIMEBASE_TICKS_MASK;
    polls++;
    if (delivery) {
        put16(irq_fixture_state.delivery_polls, polls);
        put24(irq_fixture_state.delivery_elapsed, elapsed);
    } else {
        put16(irq_fixture_state.pending_polls, polls);
        put24(irq_fixture_state.pending_elapsed, elapsed);
    }
    irq_fixture_state.helper_status = (uint8_t)timebase_expired(now, deadline, &expired);
    if (irq_fixture_state.helper_status != TIMEBASE_OK) {
        fault(IRQ_REASON_TIMEBASE);
        return;
    }
    if (elapsed >= TIMEBASE_HALF_RANGE || ((now - previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE) {
        fault(IRQ_REASON_RANGE);
        return;
    }
    previous = now;
    if (expired) {
        fault(IRQ_REASON_TIMEOUT);
        return;
    }
    observe();
    if (!invariant()) {
        fault(IRQ_REASON_INVARIANT);
        return;
    }
    if (!delivery) {
        if (irq_fixture_state.isr_count != irq_fixture_state.isr_before || irq_fixture_state.ien0) {
            fault(IRQ_REASON_EARLY);
            return;
        }
        if (irq_fixture_state.source & 0x20u) {
            MMIO_WRITE(IRQ_T1CTL, 0);
            observe();
            if (irq_fixture_state.source != 0x20u || !(irq_fixture_state.cpu & 2u) ||
                irq_fixture_state.ien1 != 2 || irq_fixture_state.control) {
                fault(IRQ_REASON_SOURCE);
                return;
            }
            irq_fixture_state.stage = IRQ_PENDING;
            return;
        }
    } else if (irq_fixture_state.isr_count != irq_fixture_state.isr_before) {
        if (irq_fixture_state.isr_count != (uint8_t)(irq_fixture_state.isr_before + 1u) ||
            irq_fixture_state.isr_source != 0x20u || (irq_fixture_state.isr_cpu & 2u) ||
            irq_fixture_state.source || (irq_fixture_state.cpu & 2u) ||
            irq_fixture_state.ien0 != 0x80u || irq_fixture_state.ien1 || irq_fixture_state.control) {
            fault(IRQ_REASON_SOURCE);
            return;
        }
        MMIO_CLEAR(SOC_IEN0, 0x80u);
        observe();
        irq_fixture_state.completed++;
        bringup_tick();
        irq_fixture_state.stage = IRQ_DONE;
        irq_fixture_state.phase = IRQ_READY;
        return;
    }
    if (polls == IRQ_FIXTURE_POLL_LIMIT)
        fault(IRQ_REASON_POLL_LIMIT);
}
