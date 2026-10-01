/* SPDX-License-Identifier: BSD-3-Clause */
#include "join_smoke.h"
void join_smoke_wait(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void join_smoke_fault(void) __naked
{
    __asm
        nop
        sjmp _join_smoke_fault
    __endasm;
}
void main(void)
{
    join_smoke_initialize();
    for(;;) {
        if(join_smoke_status.phase!=JS_RUNNING && join_smoke_status.phase!=JS_READY) join_smoke_wait();
        join_smoke_poll();
        if(join_smoke_status.phase==JS_FAULT) join_smoke_fault();
    }
}
