/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "bringup.h"

volatile MCU_XDATA MCU_AT(M0_STATUS_ADDRESS) m0_status_t m0_status;

void bringup_initialize(void)
{
    uint8_t offset;
    volatile MCU_XDATA uint8_t *storage = (volatile MCU_XDATA uint8_t *)&m0_status;
    for (offset = 0; offset < M0_STATUS_SIZE; offset++)
        storage[offset] = 0;

    m0_status.phase = M0_INITIALIZING;
    m0_status.abi_version = M0_ABI_VERSION;
    m0_status.byte_size = M0_STATUS_SIZE;
    m0_status.board = board_description.identifier;
    m0_status.policy = board_description.policy;
    m0_status.ports[0] = SOC_P0;
    m0_status.ports[1] = SOC_P1;
    m0_status.ports[2] = SOC_P2;
    m0_status.directions[0] = SOC_P0DIR;
    m0_status.directions[1] = SOC_P1DIR;
    m0_status.directions[2] = SOC_P2DIR;
    m0_status.selections[0] = SOC_P0SEL;
    m0_status.selections[1] = SOC_P1SEL;
    m0_status.selections[2] = SOC_P2SEL;
    m0_status.pulls[0] = SOC_P0INP;
    m0_status.pulls[1] = SOC_P1INP;
    m0_status.pulls[2] = SOC_P2INP;
    m0_status.analog = SOC_APCFG;
    m0_status.routing = SOC_PERCFG;
    m0_status.clock_request = SOC_CLKCONCMD;
    m0_status.clock_status = SOC_CLKCONSTA;
    m0_status.interrupt_enables[0] = SOC_IEN0;
    m0_status.interrupt_enables[1] = SOC_IEN1;
    m0_status.interrupt_enables[2] = SOC_IEN2;
    m0_status.signature[0] = 'M';
    m0_status.signature[1] = '0';
    m0_status.signature[2] = 'C';
    m0_status.signature[3] = 'C';
    m0_status.phase = M0_BOOTSTRAP_READY;
}

void bringup_tick(void)
{
    m0_status.heartbeat = (uint8_t)(m0_status.heartbeat + 1u);
}
