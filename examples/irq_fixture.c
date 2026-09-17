/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "irq_fixture.h"

void irq_fixture_before_stop(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void irq_fixture_armed_stop(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void irq_fixture_pending_stop(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void irq_fixture_inner_stop(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void irq_fixture_ready_stop(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void irq_fixture_fault_stop(void) __naked
{
    __asm
        nop
        sjmp _irq_fixture_fault_stop
    __endasm;
}

void main(void)
{
    irq_fixture_initialize();
    if (irq_fixture_state.phase == IRQ_FAULT)
        irq_fixture_fault_stop();
    irq_fixture_before_stop();
    for (;;) {
        irq_fixture_begin();
        if (irq_fixture_state.phase == IRQ_FAULT)
            irq_fixture_fault_stop();
        irq_fixture_armed_stop();
        irq_fixture_start();
        while (irq_fixture_state.phase == IRQ_RUNNING && irq_fixture_state.stage == IRQ_WAIT_PENDING)
            irq_fixture_poll();
        if (irq_fixture_state.phase == IRQ_FAULT)
            irq_fixture_fault_stop();
        irq_fixture_pending_stop();
        irq_fixture_inner();
        if (irq_fixture_state.phase == IRQ_FAULT)
            irq_fixture_fault_stop();
        irq_fixture_inner_stop();
        irq_fixture_release();
        while (irq_fixture_state.phase == IRQ_RUNNING)
            irq_fixture_poll();
        if (irq_fixture_state.phase == IRQ_FAULT)
            irq_fixture_fault_stop();
        irq_fixture_ready_stop();
    }
}
