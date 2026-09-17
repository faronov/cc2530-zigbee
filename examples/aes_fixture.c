/* SPDX-License-Identifier: BSD-3-Clause */
#include "aes_fixture.h"

void aes_fixture_before(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void aes_fixture_ready(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void aes_fixture_fault(void) __naked
{
    __asm
        nop
        sjmp _aes_fixture_fault
    __endasm;
}
