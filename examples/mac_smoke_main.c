/* SPDX-License-Identifier: BSD-3-Clause */
#include "mac_smoke.h"
void mac_smoke_wait(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void mac_smoke_end(void) __naked
{
    __asm
        nop
        sjmp _mac_smoke_end
    __endasm;
}
void mac_smoke_fault(void) __naked
{
    __asm
        nop
        sjmp _mac_smoke_fault
    __endasm;
}
void main(void)
{
    mac_smoke_initialize();
    for(;;) {
        mac_smoke_wait();
        mac_smoke_poll();
        if(mac_smoke_status.phase==MS_END) mac_smoke_end();
        if(mac_smoke_status.phase==MS_FAULT) mac_smoke_fault();
    }
}
