/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "bringup.h"
#include "host_mmio.h"

#include <assert.h>
#include <stdio.h>

static void exercise_startup(uint8_t p0, uint8_t p1, uint8_t unrelated0, uint8_t unrelated1)
{
    unsigned i, operations;
    const volatile uint8_t *bytes = (const volatile uint8_t *)&m0_status;
    uint8_t saved[32];
    host_mmio_reset();
    SOC_P0 = p0;
    SOC_P1 = p1;
    SOC_P2 = 0xa5;
    SOC_P0DIR = unrelated0;
    SOC_P1DIR = unrelated1;
    SOC_P2DIR = 0x08;
    SOC_P0SEL = unrelated0;
    SOC_P1SEL = unrelated1;
    SOC_P2SEL = 0x40;
    SOC_P1INP = 0xc0;
    SOC_P2INP = 0x20;
    SOC_APCFG = unrelated0;
    SOC_PERCFG = 0x03;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9;
    SOC_IEN0 = SOC_IEN1 = SOC_IEN2 = 0xff;

    assert(_sdcc_external_startup() == 0);
    assert(write_count >= 3);
    assert(writes[0].address == SOC_IEN0_ADDRESS && writes[0].after == 0);
    assert(writes[1].address == SOC_IEN1_ADDRESS && writes[1].after == 0);
    assert(writes[2].address == SOC_IEN2_ADDRESS && writes[2].after == 0);
    assert(!SOC_IEN0 && !SOC_IEN1 && !SOC_IEN2);
#if CC2530_BOARD == BOARD_GENERIC
    assert(write_count == 3);
    assert(SOC_P0 == p0 && SOC_P1 == p1);
    assert(SOC_P0DIR == unrelated0 && SOC_P1DIR == unrelated1);
    assert(SOC_P0SEL == unrelated0 && SOC_P1SEL == unrelated1 && SOC_P0INP == 0);
#else
    assert(write_count == 15);
    assert(writes[3].address == SOC_P0_ADDRESS && writes[3].after == (uint8_t)(p0 & 0x7f));
    assert((SOC_P0 & 0xbc) == 0 && (SOC_P1 & 0x02) == 0);
    assert((SOC_P0DIR & 0xbc) == 0xbc && (SOC_P1DIR & 0x02) == 0x02);
    assert((SOC_P0DIR & 0x43) == unrelated0 && (SOC_P1DIR & 0xfd) == unrelated1);
    assert(SOC_P0SEL == unrelated0 && SOC_P1SEL == unrelated1 && SOC_P0INP == 0xbc);
    assert((SOC_P0 & 0x43) == (p0 & 0x43) && (SOC_P1 & 0xfd) == (p1 & 0xfd));
#endif
    assert(SOC_P2 == 0xa5 && SOC_P2DIR == 0x08 && SOC_P2SEL == 0x40);
    assert(SOC_P1INP == 0xc0 && SOC_P2INP == 0x20);
    assert(SOC_APCFG == unrelated0 && SOC_PERCFG == 0x03);
    assert(SOC_CLKCONCMD == 0xc9 && SOC_CLKCONSTA == 0xc9);

    operations = write_count;
    bringup_initialize();
    assert(write_count == operations);
    assert(bytes[0] == 'M' && bytes[1] == '0' && bytes[2] == 'C' && bytes[3] == 'C');
    assert(bytes[4] == 1 && bytes[5] == 32 && bytes[6] == 2);
    assert(bytes[7] == CC2530_BOARD && bytes[8] == 0);
    assert(bytes[9] == (CC2530_BOARD == BOARD_GENERIC ? 0 : 1));
    assert(bytes[10] == SOC_P0 && bytes[11] == SOC_P1 && bytes[12] == SOC_P2);
    assert(bytes[13] == SOC_P0DIR && bytes[14] == SOC_P1DIR && bytes[15] == SOC_P2DIR);
    assert(bytes[16] == SOC_P0SEL && bytes[17] == SOC_P1SEL && bytes[18] == SOC_P2SEL);
    assert(bytes[19] == SOC_P0INP && bytes[20] == SOC_P1INP && bytes[21] == SOC_P2INP);
    assert(bytes[22] == SOC_APCFG && bytes[23] == SOC_PERCFG);
    assert(bytes[24] == 0xc9 && bytes[25] == 0xc9);
    for (i = 26; i < 32; i++)
        assert(bytes[i] == 0);
    for (i = 0; i < 32; i++)
        saved[i] = bytes[i];
    for (i = 0; i < 300; i++)
        bringup_tick();
    assert(m0_status.heartbeat == 44);
    m0_status.heartbeat = 255;
    bringup_tick();
    assert(m0_status.heartbeat == 0 && write_count == operations);
    for (i = 0; i < 32; i++) {
        if (i != 8)
            assert(bytes[i] == saved[i]);
    }
}

int main(void)
{
    exercise_startup(0xff, 0xff, 0, 0);
    exercise_startup(0xe7, 0xbd, 0x43, 0xc1);
    printf("host bootstrap board=%u: ABI, IRQ-first/latch-before-direction, untouched pins and heartbeat wrap PASS\n",
           (unsigned)CC2530_BOARD);
    return 0;
}
