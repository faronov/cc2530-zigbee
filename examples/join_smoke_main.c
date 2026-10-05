/* SPDX-License-Identifier: BSD-3-Clause */
#include "join_smoke.h"
#if defined(CC2530_CLANG)
__attribute__((naked, noinline)) void join_smoke_wait(void)
{
    __asm__ volatile("nop\n\tret");
}
__attribute__((naked, noinline)) void join_smoke_fault(void)
{
    __asm__ volatile("nop\n1:\n\tsjmp 1b");
}
#else
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
#endif
void main(void)
{
#if defined(CC2530_CLANG)
    /* LLVM cc2530_startup does not call the SDCC external-startup hook. */
    CC2530_EXTERNAL_STARTUP();
#endif
    join_smoke_initialize();
    for(;;) {
        if(join_smoke_status.phase!=JS_RUNNING && join_smoke_status.phase!=JS_READY) join_smoke_wait();
        join_smoke_poll();
        if(join_smoke_status.phase==JS_FAULT) join_smoke_fault();
    }
}
