/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "dma.h"
#include "timebase.h"

#include <stddef.h>

volatile MCU_XDATA uint8_t dma_descriptor[8];
MCU_XDATA uint8_t dma_fault;
extern MCU_XDATA uint8_t dma_reserved_end;
/* SDCC's generic output helper, used by timebase, has one XDATA scratch byte.
 * Its separately linked allocation is checked as well as the private prefix.
 */
extern MCU_XDATA uint8_t _gptrput_PARM_2;

typedef struct {
    uint32_t start, previous, deadline;
    uint16_t limit;
    uint8_t command, cfg0_low, cfg0_high, cfg1_low, cfg1_high;
} dma_wait_t;

static uint8_t ordinary(uint16_t address, uint8_t length)
{
    return address < 0x1e00u && length <= 0x1e00u - address;
}

static uint8_t overlaps(uint16_t a, uint8_t an, uint16_t b, uint8_t bn)
{
    return a < b ? b - a < an : a - b < bn;
}

#if defined(__SDCC)
/* SWRU191F 8.1 p.93; 2.1 p.25 / Table 2-3 p.39: NOP >= 1 system clock.
 * The linked checker pins all 13 bytes, the sole caller and request ordering.
 */
static void arm0(void) __naked
{
    __asm
        mov _SOC_DMAARM,#1
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        ret
    __endasm;
}
#else
static void arm0(void)
{
    MMIO_WRITE(SOC_DMAARM, 1);
    host_mmio_system_cycles(9);
}
#endif

static dma_result_t observe(dma_diagnostics_t MCU_XDATA *d, uint8_t MCU_XDATA *command)
{
    uint8_t ien0, ien1, ien2, sleep, status;
    d->sample_valid = 0;
    ien0 = MMIO_READ(SOC_IEN0);
    ien1 = MMIO_READ(SOC_IEN1);
    ien2 = MMIO_READ(SOC_IEN2);
    sleep = MMIO_READ(SOC_SLEEPCMD);
    *command = MMIO_READ(SOC_CLKCONCMD);
    status = MMIO_READ(SOC_CLKCONSTA);
    if (ien0 || ien1 || ien2 || (sleep & 7u) != 4u || status != *command ||
        (*command & 7u) != ((*command & 0x40u) ? 1u : 0u) ||
        ((*command & 0x40u) && !(*command & 0x38u)))
        return DMA_UNSUPPORTED_STATE;
    d->arm = MMIO_READ(SOC_DMAARM);
    d->request = MMIO_READ(SOC_DMAREQ);
    d->irq = MMIO_READ(SOC_DMAIRQ);
    d->ircon = MMIO_READ(SOC_IRCON);
    d->cfg0_low = MMIO_READ(SOC_DMA0CFGL);
    d->cfg0_high = MMIO_READ(SOC_DMA0CFGH);
    d->cfg1_low = MMIO_READ(SOC_DMA1CFGL);
    d->cfg1_high = MMIO_READ(SOC_DMA1CFGH);
    d->sample_valid = 1;
    return DMA_OK;
}

static dma_result_t poll(dma_wait_t MCU_XDATA *w, dma_diagnostics_t MCU_XDATA *d)
{
    dma_result_t result;
    uint8_t command;
    uint32_t now;
    bool expired;
    if (d->polls == w->limit)
        return DMA_POLL_LIMIT;
    result = observe(d, &command);
    now = timebase_read_awake_ticks24();
    d->polls++;
    d->elapsed_ticks = (now - w->start) & TIMEBASE_TICKS_MASK;
    if (result != DMA_OK)
        return result;
    if (command != w->command || (d->arm & 0xfeu) || (d->request & 0xfeu) ||
        (d->irq & 0xfeu) || (d->ircon & 1u) ||
        d->cfg0_low != w->cfg0_low || d->cfg0_high != w->cfg0_high ||
        d->cfg1_low != w->cfg1_low || d->cfg1_high != w->cfg1_high)
        return DMA_STATE_CHANGED;
    if (!(d->actions & DMA_REQUESTED)) {
        if (d->arm != ((d->actions & DMA_ARMED) ? 1u : 0u) || d->request || d->irq)
            return DMA_STATE_CHANGED;
    } else if (d->actions & DMA_ACKNOWLEDGED) {
        if (d->arm || d->request || d->irq)
            return DMA_STATE_CHANGED;
    } else if (d->arm == 0 && d->request == 0 && d->irq == 1)
        d->complete = 1;
    d->timebase_status = timebase_expired(now, w->deadline, &expired);
    if (d->timebase_status != TIMEBASE_OK)
        return DMA_TIMEBASE_ERROR;
    if (d->elapsed_ticks >= TIMEBASE_HALF_RANGE ||
        ((now - w->previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
        return DMA_COUNTER_RANGE;
    if (expired)
        return DMA_TIMEOUT;
    if (d->polls == w->limit && !(d->actions & DMA_ACKNOWLEDGED))
        return DMA_POLL_LIMIT;
    w->previous = now;
    return DMA_OK;
}

dma_result_t dma_copy_init(uint16_t source, uint16_t destination, uint8_t length,
                          uint32_t timeout, uint16_t limit, dma_diagnostics_t MCU_XDATA *d)
{
    dma_wait_t w;
    dma_result_t result;
    uint16_t output, end, descriptor, helper;
    uint8_t index;
    if (dma_fault)
        return (dma_result_t)dma_fault;
    if (!length || length > DMA_COPY_MAX || !timeout || timeout >= TIMEBASE_HALF_RANGE ||
        !limit || d == NULL)
        return DMA_INVALID_ARGUMENT;
    output = MMIO_XADDRESS(d);
    end = MMIO_XADDRESS(&dma_reserved_end);
    helper = MMIO_XADDRESS(&_gptrput_PARM_2);
    if (!ordinary(source, length) || !ordinary(destination, length) ||
        !ordinary(output, sizeof(*d)) || overlaps(source, length, destination, length))
        return DMA_INVALID_RANGE;
    if (source <= end || destination <= end || output <= end ||
        overlaps(source, length, output, sizeof(*d)) ||
        overlaps(destination, length, output, sizeof(*d)) ||
        overlaps(source, length, helper, 1) || overlaps(destination, length, helper, 1) ||
        overlaps(output, sizeof(*d), helper, 1))
        return DMA_BUFFER_OWNERSHIP;
    for (index = 0; index < sizeof(*d); index++)
        ((uint8_t MCU_XDATA *)d)[index] = 0;
    result = observe(d, &w.command);
    if (result != DMA_OK)
        goto fault;
    if ((d->arm | d->request | d->irq) & 0xe0u) {
        result = DMA_UNSUPPORTED_STATE;
        goto fault;
    }
    if (d->arm) {
        result = DMA_BUSY;
        goto fault;
    }
    if (d->request || d->irq || (d->ircon & 1u)) {
        result = DMA_PENDING;
        goto fault;
    }
    w.cfg0_low = d->cfg0_low;
    w.cfg0_high = d->cfg0_high;
    w.cfg1_low = d->cfg1_low;
    w.cfg1_high = d->cfg1_high;
    w.limit = limit;
    w.start = timebase_read_awake_ticks24();
    w.previous = w.start;
    d->timebase_status = timebase_deadline_after(w.start, timeout, &w.deadline);
    if (d->timebase_status != TIMEBASE_OK) {
        result = DMA_TIMEBASE_ERROR;
        goto fault;
    }
    result = poll(&w, d);
    if (result != DMA_OK)
        goto fault;
    dma_descriptor[0] = (uint8_t)(source >> 8);
    dma_descriptor[1] = (uint8_t)source;
    dma_descriptor[2] = (uint8_t)(destination >> 8);
    dma_descriptor[3] = (uint8_t)destination;
    dma_descriptor[4] = 0;
    dma_descriptor[5] = length;
    dma_descriptor[6] = 0x20;
    dma_descriptor[7] = 0x51;
    descriptor = MMIO_XADDRESS(dma_descriptor);
    w.cfg0_low = (uint8_t)descriptor;
    w.cfg0_high = (uint8_t)(descriptor >> 8);
    MMIO_WRITE(SOC_DMA0CFGH, w.cfg0_high);
    MMIO_WRITE(SOC_DMA0CFGL, w.cfg0_low);
    d->actions = DMA_CONFIGURED;
    result = poll(&w, d);
    if (result != DMA_OK)
        goto fault;
    arm0();
    d->actions |= DMA_ARMED;
    result = poll(&w, d);
    if (result != DMA_OK)
        goto fault;
    MMIO_WRITE(SOC_DMAREQ, 1);
    d->actions |= DMA_REQUESTED;
    do {
        result = poll(&w, d);
        if (result != DMA_OK)
            goto fault;
    } while (!d->complete);
    /* DMAIRQ is R/W0. Preserve even foreign flags asserted since observation;
     * reserved bits are R0. Do not touch IRCON.DMAIF (IRQMASK=0).
     */
    MMIO_WRITE(SOC_DMAIRQ, 0x1e);
    d->actions |= DMA_ACKNOWLEDGED;
    result = poll(&w, d);
    if (result != DMA_OK)
        goto fault;
    d->verified = 1;
    return DMA_OK;
fault:
    dma_fault = (uint8_t)result;
    return result;
}

/* Allocation-order ABI: the linked checker covers every DMA/timebase XDATA
 * object, including compiler parameters/temporaries, with this private prefix.
 */
MCU_XDATA uint8_t dma_reserved_end;
