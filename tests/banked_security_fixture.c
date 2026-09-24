/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "security_keys.h"

__sfr __at(0xa8) fixture_ien0;
__sfr __at(0xb8) fixture_ien1;
__sfr __at(0x9a) fixture_ien2;

MCU_XDATA security_keys_config_t fixture_config;
MCU_XDATA ccm_star_limits_t fixture_limits;
MCU_XDATA security_keys_status_t fixture_metadata;
MCU_XDATA ed_packet_t fixture_packet;
MCU_XDATA uint8_t fixture_install[18], fixture_frame[116];
MCU_XDATA uint16_t fixture_polls, fixture_address, fixture_capacity;
MCU_XDATA uint8_t fixture_action, fixture_length, fixture_written, fixture_event;
MCU_XDATA uint8_t fixture_nwk_sequence, fixture_aps_counter, fixture_secure;
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t fixture_status[64];

void main(void)
{
    fixture_ien0 = fixture_ien1 = fixture_ien2 = 0;
    for (;;) {
        __asm
            .globl _banked_security_before
            _banked_security_before = .
        __endasm;
        if (fixture_action == 0)
            fixture_status[0] = security_keys_open();
        else if (fixture_action == 1)
            fixture_status[0] = security_keys_provision(&fixture_config, fixture_install,
                10, 20, &fixture_limits, fixture_polls);
        else if (fixture_action == 2)
            fixture_status[0] = security_keys_associate(fixture_address, fixture_polls);
        else if (fixture_action == 3)
            fixture_status[0] = security_keys_receive(fixture_frame, fixture_length,
                &fixture_packet, &fixture_event, &fixture_limits, fixture_polls);
        else if (fixture_action == 4)
            fixture_status[0] = security_keys_request(fixture_nwk_sequence, fixture_aps_counter,
                fixture_frame, fixture_capacity, &fixture_written, &fixture_limits, fixture_polls);
        else if (fixture_action == 5)
            fixture_status[0] = security_keys_verify(fixture_nwk_sequence, fixture_aps_counter,
                fixture_frame, fixture_capacity, &fixture_written, &fixture_limits, fixture_polls);
        else if (fixture_action == 6)
            fixture_status[0] = security_keys_send(&fixture_packet, fixture_secure,
                fixture_frame, fixture_capacity, &fixture_written, &fixture_limits, fixture_polls);
        else if (fixture_action == 7)
            fixture_status[0] = security_keys_leave(fixture_nwk_sequence,
                fixture_frame, fixture_capacity, &fixture_written, &fixture_limits, fixture_polls);
        else
            fixture_status[0] = SECURITY_KEYS_ARGUMENT;
        fixture_status[1] = security_keys_status(&fixture_metadata);
        __asm
            .globl _banked_security_after
            _banked_security_after = .
            nop
        __endasm;
    }
}
