/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic caller only. Never flash this image.
 */
#include "bdb_join.h"
#include <stddef.h>

#if !defined(CC2530_BANKED_JOIN) || !defined(CC2530_JOIN_WORKSPACE)
#error This caller requires the complete join placement profile
#endif

__sfr __at(0xa8) fixture_ien0;
__sfr __at(0xb8) fixture_ien1;
__sfr __at(0x9a) fixture_ien2;

typedef union {
    bdb_join_config_t config;
    ed_packet_t packet;
    struct {
        security_keys_config_t config;
        ccm_star_limits_t limits;
        uint32_t nwk, aps;
        uint16_t polls;
        uint8_t install[18];
    } provision;
    struct {
        bdb_join_event_t event;
        uint8_t bytes[125];
    } receive;
} fixture_input_t;

typedef union {
    bdb_join_action_t action;
    mac_tx_action_t radio;
    ed_packet_t packet;
    uint8_t bytes[125];
} fixture_output_t;

typedef struct {
    volatile uint8_t result, metadata_result;
    security_keys_status_t metadata;
    uint8_t reserved[25];
} fixture_status_t;

MCU_XDATA bdb_join_t fixture_device;
MCU_XDATA mac_tx_t fixture_mac;
MCU_XDATA fixture_input_t fixture_input;
MCU_XDATA fixture_output_t fixture_output;
MCU_XDATA uint32_t fixture_now;
MCU_XDATA uint16_t fixture_length;
MCU_XDATA uint8_t fixture_command, fixture_arg, fixture_written;
MCU_XDATA MCU_AT(0x1e00) fixture_status_t fixture_status;

typedef char full_context_size[sizeof(bdb_join_t) == 1676 ? 1 : -1];
typedef char full_phase_size[sizeof(bdb_join_work_t) == 1290 ? 1 : -1];
typedef char shadow_binding[
    offsetof(bdb_join_t, work.association.context) == 0 &&
    offsetof(bdb_join_t, work.association.staged) == 0x285 &&
    sizeof(fixture_device.work.association.staged) == 645 ? 1 : -1];
typedef char status_extent[sizeof(fixture_status_t) == 64 ? 1 : -1];
typedef char input_extent[sizeof(fixture_input_t) == 180 ? 1 : -1];

void main(void)
{
    __asm
        .globl _mac_join_staged
        _mac_join_staged = _fixture_device + 0x285
    __endasm;
    fixture_ien0 = fixture_ien1 = fixture_ien2 = 0;
    for (;;) {
        __asm
            .globl _banked_join_before
            _banked_join_before = .
        __endasm;
        if (fixture_command == 0)
            fixture_status.result = security_keys_open();
        else if (fixture_command == 1)
            fixture_status.result = security_keys_provision(&fixture_input.provision.config,
                fixture_input.provision.install, fixture_input.provision.nwk, fixture_input.provision.aps,
                &fixture_input.provision.limits, fixture_input.provision.polls);
        else if (fixture_command == 2)
            fixture_status.result = mac_tx_init(&fixture_mac, fixture_arg, fixture_now);
        else if (fixture_command == 3)
            fixture_status.result = bdb_join_init(&fixture_device, fixture_now);
        else if (fixture_command == 4)
            fixture_status.result = bdb_join_start(&fixture_device, &fixture_mac, &fixture_input.config, fixture_now);
        else if (fixture_command == 5)
            fixture_status.result = bdb_join_step(&fixture_device, fixture_now,
                fixture_arg ? &fixture_input.receive.event : NULL, &fixture_output.action);
        else if (fixture_command == 6)
            fixture_status.result = mac_tx_step(&fixture_mac, fixture_now,
                fixture_arg ? &fixture_input.receive.event.data.tx : NULL, &fixture_output.radio);
        else if (fixture_command == 7)
            fixture_status.result = bdb_join_receive(&fixture_device, fixture_input.receive.bytes,
                fixture_length, fixture_arg, fixture_now);
        else if (fixture_command == 8)
            fixture_status.result = bdb_join_send(&fixture_device, &fixture_input.packet, fixture_arg, fixture_now);
        else if (fixture_command == 9)
            fixture_status.result = bdb_join_confirm(&fixture_device, &fixture_output.bytes[0]);
        else if (fixture_command == 10)
            fixture_status.result = fixture_device.workspace == BDB_JOIN_WORK_RUNTIME ?
                zdo_runtime_take_application(&fixture_device.work.runtime.zdo, &fixture_output.packet) :
                ZDO_RUNTIME_STATE;
        else if (fixture_command == 11)
            fixture_status.result = mac_tx_copy(&fixture_mac, fixture_output.bytes,
                sizeof(fixture_output.bytes), &fixture_written);
        else
            fixture_status.result = 255;
        fixture_status.metadata_result = security_keys_status(&fixture_status.metadata);
        __asm
            .globl _banked_join_after
            _banked_join_after = .
            nop
        __endasm;
    }
}
