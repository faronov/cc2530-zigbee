/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Native synthetic ownership/MMIO regression, never target firmware.
 */
#if !defined(CC2530_HOST_TEST) || !defined(CC2530_MAC_LINK_WORKSPACE)
#error This test requires the explicit native UPPER workspace profile
#endif
#include "mac_link_workspace_internal.h"
#include "security_counter.h"
#include "security_joint_model.h"
#include "flash_exec.h"
#include "flash_write.h"
#include "nv_record.h"
#include "timebase.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAC_LINK_RAM_MAIN workspace_ram_main
#include "test_mac_link_ram.c"
#undef MAC_LINK_RAM_MAIN

static unsigned ws_checks;
#define WS_CHECK(c) do { ws_checks++; if (!(c)) { \
    fprintf(stderr, "UPPER workspace line%u: %s\n", (unsigned)__LINE__, #c); exit(1); \
} } while (0)
#define WA link_work_arena
static host_mmio_xaddress_hook_t ws_address;
static uint16_t ws_arena_address;
static uint16_t ws_map(const volatile void *p)
{
    uintptr_t a = (uintptr_t)p, b = (uintptr_t)&WA;
    if (a >= b && a-b < sizeof(WA)) return ws_arena_address + (uint16_t)(a-b);
    return ws_address(p);
}
static void ws_bytes(const void *object, size_t size, uint8_t value)
{
    const uint8_t *p = object;
    size_t i;
    for (i = 0; i < size; i++) WS_CHECK(p[i] == value);
}

static void ws_counter_boundary(void)
{
    uint8_t payload[112], count, before[sizeof(WA)];
    security_counter_status_t diagnostic;
    security_counter_result_t result;
    uint32_t elsewhere = 0x12345678UL;
    unsigned i, commands;
    memset(payload, 0x3c, sizeof(payload));
    security_joint_reset(1);
    WS_CHECK(security_counter_open() == SECURITY_COUNTER_EMPTY);
    WS_CHECK(security_counter_create(256, 256, payload, 112, 64) == SECURITY_COUNTER_OK);
    WS_CHECK(security_joint_flash_commands() == 38);
    ws_address = host_mmio_xaddress_hook;
    host_mmio_xaddress_hook = ws_map;
    for (i = 0; i < 2; i++) {
        /* The same real counter/journal/flash reproduction above and below
         * the model's 0C00 fence. Only precise handoffs may bypass that fence. */
        ws_arena_address = i ? 0x0800 : 0x1000;
        count = 0x9a;
        WS_CHECK(security_counter_read(WA.keys.record,112,&count) == SECURITY_COUNTER_OWNERSHIP);
        WS_CHECK(count == 0x9a && link_work_clean());
        WS_CHECK(link_work_enter(LW_KEYS));
        memset(&WA.keys, 0x69, sizeof(WA.keys));
        memcpy(before, &WA, sizeof(WA));
        memcpy(&diagnostic, security_counter_status(), sizeof(diagnostic));
        WS_CHECK(security_counter_read(WA.keys.record,112,&WA.keys.size) == SECURITY_COUNTER_OWNERSHIP);
        WS_CHECK(!memcmp(before,&WA,sizeof(WA)));
        WS_CHECK(link_work_grant(LW_COUNTER_READ));
        WS_CHECK(!link_work_grant(LW_COUNTER_SAVE));
        WS_CHECK(!link_work_leave(LW_KEYS));
        WS_CHECK(!link_work_enter(LW_JOIN_STEP));
        result = security_counter_read(WA.keys.record,111,&WA.keys.size);
        WS_CHECK(result == SECURITY_COUNTER_OWNERSHIP);
        WS_CHECK(security_counter_read(WA.keys.record+1,112,&WA.keys.size) == SECURITY_COUNTER_OWNERSHIP);
        WS_CHECK(security_counter_read(WA.keys.record,112,&count) == SECURITY_COUNTER_OWNERSHIP);
        WS_CHECK(security_counter_read(WA.keys.record,112,WA.keys.record) == SECURITY_COUNTER_OWNERSHIP);
        WS_CHECK(security_counter_save(WA.keys.record,112,64) == SECURITY_COUNTER_OWNERSHIP);
        WS_CHECK(!memcmp(before,&WA,sizeof(WA)) && count == 0x9a);
        WS_CHECK(!memcmp(&diagnostic,security_counter_status(),sizeof(diagnostic)));
        WS_CHECK(security_joint_flash_commands() == 38);
        WS_CHECK(link_work_call_result(result) == result);
        WS_CHECK(LW_CALL(LW_COUNTER_READ,
            security_counter_read(WA.keys.record,112,&WA.keys.size)) == SECURITY_COUNTER_OK);
        WS_CHECK(WA.keys.size == 112 && !memcmp(WA.keys.record,payload,112));
        WS_CHECK(link_work_leave(LW_KEYS) && link_work_clean());

        WS_CHECK(link_work_enter(LW_JOIN_STEP));
        memset(&WA.protocol.parent.join,0xa5,sizeof(WA.protocol.parent.join));
        memcpy(before,&WA,sizeof(WA));
        memcpy(&diagnostic,security_counter_status(),sizeof(diagnostic));
        WS_CHECK(!link_work_enter(LW_KEYS));
        WS_CHECK(!link_work_leave(LW_KEYS));
        WS_CHECK(!link_work_grant(LW_COUNTER_READ));
        WS_CHECK(security_counter_read((uint8_t *)&WA,112,&count) == SECURITY_COUNTER_OWNERSHIP);
        WS_CHECK(security_counter_take(0,(uint32_t *)&WA,64) == SECURITY_COUNTER_OWNERSHIP);
        WS_CHECK(!memcmp(before,&WA,sizeof(WA)) && count == 0x9a);
        WS_CHECK(!memcmp(&diagnostic,security_counter_status(),sizeof(diagnostic)));
        WS_CHECK(security_joint_flash_commands() == 38);
        WS_CHECK(link_work_leave(LW_JOIN_STEP) && link_work_clean());
    }
    WS_CHECK(link_work_enter(LW_KEYS));
    commands = security_joint_flash_commands();
    WS_CHECK(LW_CALL(LW_COUNTER_TAKE,security_counter_take(0,&elsewhere,64)) == SECURITY_COUNTER_OWNERSHIP);
    WS_CHECK(elsewhere == 0x12345678UL && security_joint_flash_commands() == commands);
    WS_CHECK(LW_CALL(LW_COUNTER_TAKE,security_counter_take(0,&WA.keys.counter,64)) == SECURITY_COUNTER_OK);
    WS_CHECK(WA.keys.counter == 256 && security_counter_status()->next[0] == 257);
    WS_CHECK(LW_CALL(LW_COUNTER_READ,security_counter_read(WA.keys.record,112,&WA.keys.size)) == SECURITY_COUNTER_OK);
    WA.keys.record[0] ^= 1;
    WS_CHECK(LW_CALL(LW_COUNTER_SAVE,security_counter_save(WA.keys.record,112,64)) == SECURITY_COUNTER_OK);
    WS_CHECK(link_work_leave(LW_KEYS) && link_work_clean());
    security_joint_reset(1);
    WS_CHECK(security_counter_open() == SECURITY_COUNTER_EMPTY);
    WS_CHECK(link_work_enter(LW_KEYS));
    memcpy(WA.keys.record,payload,112);
    WS_CHECK(LW_CALL(LW_COUNTER_CREATE,security_counter_create(256,256,WA.keys.record,112,64)) == SECURITY_COUNTER_OK);
    WS_CHECK(security_joint_flash_commands() == 38);
    WS_CHECK(link_work_leave(LW_KEYS) && link_work_clean());
}

static void ws_nested(void)
{
    static const uint8_t data[9] = {0x61,0x88,1,0x34,0x12,0,0,0x78,0x56};
    static const uint8_t beacon[4] = {0xff,0xcf,0,0};
    mac_tx_interval_t tx;
    mac_beacon_info_t decoded;
    security_keys_status_t status, saved_status;
    uint8_t before[sizeof(WA)], raw[116], length = 0;
    aes_diagnostics_t aes_status;
    uint32_t deadline = 0x87654321UL;
    unsigned commands;
    memset(&status,0x6d,sizeof(status)); memcpy(&saved_status,&status,sizeof(status));
    WS_CHECK(link_work_clean());
    WS_CHECK(mac_tx_interval_init(&tx,9,100) == MAC_TX_OK);
    WS_CHECK(link_work_enter(LW_JOIN_STEP));
    memset(&WA.protocol.parent.join,0xa5,sizeof(WA.protocol.parent.join));
    memcpy(before,&WA,sizeof(WA));
    commands = security_joint_flash_commands();
    WS_CHECK(security_keys_status(&status) == SECURITY_KEYS_STATE);
    WS_CHECK(!memcmp(&status,&saved_status,sizeof(status)));
    WS_CHECK(!link_work_enter(LW_JOIN_STEP));
    WS_CHECK(!link_work_enter(LW_NWK_HINT));
    WS_CHECK(!link_work_leave(LW_POLL_STEP));
    WS_CHECK(nwk_aps_armed((nwk_aps_t *)&WA,1,0,0) == NWK_APS_ARGUMENT);
    WS_CHECK(nwk_aps_disarmed((nwk_aps_t *)&WA,1,0,0) == NWK_APS_ARGUMENT);
    WS_CHECK(nv_record_load((uint8_t *)&WA,128) == NV_RECORD_BUFFER_OWNERSHIP);
    WS_CHECK(flash_nv_read(0,0,(uint8_t *)&WA,4) == FLASH_BUFFER_OWNERSHIP);
    WS_CHECK(flash_nv_program(0,0,(const uint8_t *)&WA,64) == FLASH_WRITE_BUFFER_OWNERSHIP);
    WS_CHECK(flash_exec_command(FLASH_EXEC_PROGRAM,0,0,(const uint8_t *)&WA,64) == FLASH_EXEC_INVALID_ARGUMENT);
    WS_CHECK(timebase_deadline_after(1,2,(uint32_t *)&WA) == TIMEBASE_INVALID_ARGUMENT);
    WS_CHECK(aes128_encrypt_block((const uint8_t *)&WA,raw,raw+16,1000,128,&aes_status) == AES_BUFFER_OWNERSHIP);
    WS_CHECK(ed_wire_nwk(data,sizeof(data),(nwk_frame_info_t *)&WA) == ZIGBEE_SECURITY_ARGUMENT);
    WS_CHECK(nwk_frame_decode(data,sizeof(data),(nwk_frame_info_t *)&WA) == NWK_CODEC_INVALID_ARGUMENT);
    WS_CHECK(aps_frame_decode(data,sizeof(data),(aps_frame_info_t *)&WA) == APS_CODEC_INVALID_ARGUMENT);
    WS_CHECK(mac_command_encode((const mac_command_t *)&WA,raw,116,&length) == MAC_CODEC_INVALID_ARGUMENT);
    WS_CHECK(!memcmp(before,&WA,sizeof(WA)) && !length);
    WS_CHECK(security_joint_flash_commands() == commands);
    WS_CHECK(timebase_deadline_after(1,2,&deadline) == TIMEBASE_OK && deadline == 3);

    WS_CHECK(link_work_enter(LW_POLL_STEP));
    memset(&WA.protocol.poll,0x5a,sizeof(WA.protocol.poll));
    WS_CHECK(mac_tx_interval_submit(&tx,data,sizeof(data),100,1000,64) == MAC_TX_OK);
    WS_CHECK(tx.engine.phase == MAC_TX_DRAW && tx.engine.frame[2] == 9);
    ws_bytes(&WA.protocol.parent.join,sizeof(WA.protocol.parent.join),0xa5);
    ws_bytes(&WA.protocol.poll,sizeof(WA.protocol.poll),0x5a);
    ws_bytes(&WA.protocol.operation,sizeof(WA.protocol.operation),0);
    WS_CHECK(link_work_leave(LW_POLL_STEP));
    WS_CHECK(link_work_enter(LW_TX_OBSERVED));
    memset(&WA.protocol.operation.tx,0x33,sizeof(WA.protocol.operation.tx));
    WS_CHECK(link_work_enter(LW_TX_INTERVAL));
    WS_CHECK(link_work_enter(LW_TX_STEP));
    WS_CHECK(link_work_enter(LW_MAC_DECODE));
    memset(&WA.protocol.codec.candidate,0x6d,sizeof(WA.protocol.codec.candidate));
    WS_CHECK(mac_beacon_decode(beacon,4,&decoded) == MAC_CODEC_OK);
    ws_bytes(&WA.protocol.codec.candidate,sizeof(WA.protocol.codec.candidate),0x6d);
    ws_bytes(&WA.protocol.operation.tx,sizeof(WA.protocol.operation.tx),0x33);
    WS_CHECK(link_work_leave(LW_MAC_DECODE));
    WS_CHECK(link_work_leave(LW_TX_STEP));
    WS_CHECK(link_work_leave(LW_TX_INTERVAL));
    ws_bytes(&WA.protocol.operation.tx,sizeof(WA.protocol.operation.tx),0x33);
    WS_CHECK(link_work_leave(LW_TX_OBSERVED));
    ws_bytes(&WA.protocol.operation,sizeof(WA.protocol.operation),0);
    ws_bytes(&WA.protocol.parent.join,sizeof(WA.protocol.parent.join),0xa5);
    WS_CHECK(link_work_leave(LW_JOIN_STEP) && link_work_clean());
}

static void ws_key_slots_and_faults(void)
{
    static const uint8_t code[18] = {
        0x83,0xfe,0xd3,0x40,0x7a,0x93,0x97,0x23,0xa5,0xc6,0x39,0xb2,0x69,0x16,0xd5,0x05,0xc3,0xb5
    };
    static const security_keys_config_t config = {
        {0x11,2,3,4,5,6,7,8},{0x22,2,3,4,5,6,7,8},{0x33,2,3,4,5,6,7,8},
        0x1234,0xffff,15,254
    };
    ccm_star_limits_t limits = {1000,128};
    security_keys_status_t status;
    security_counter_status_t diagnostic;
    uint8_t before[sizeof(WA)];
    unsigned blocks, frame;
    WS_CHECK(!link_work_enter(LW_NONE));
    WS_CHECK(!link_work_enter(LW_FRAMES));
    WS_CHECK(!link_work_enter(255));
    WS_CHECK(link_work_call_result(0) == 255 && link_work_clean());
    WS_CHECK(link_work_enter(LW_KEYS));
    memset(&WA.keys,0x69,sizeof(WA.keys));
    memset(&WA.keys.packet,0,sizeof(WA.keys.packet));
    WA.keys.packet.nwk.version = 2; WA.keys.packet.nwk.radius = 1;
    WA.keys.packet.nwk.source = 0x5678; WA.keys.packet.nwk.destination = 0x1234;
    WA.keys.packet.aps.source_endpoint = WA.keys.packet.aps.destination_endpoint = 1;
    WA.keys.packet.aps.profile_id = 0x0104; WA.keys.packet.aps.cluster_id = 6;
    WA.keys.packet.length = 1; WA.keys.packet.payload[0] = 0x5a;
    memcpy(before,&WA,sizeof(WA));
    for (frame = LW_KEYS; frame < LW_FRAMES; frame++) {
        WS_CHECK(!link_work_enter((uint8_t)frame));
        if (frame != LW_KEYS) WS_CHECK(link_work_return((uint8_t)frame,0) == 255);
    }
    WS_CHECK(!memcmp(before,&WA,sizeof(WA)));
    blocks = security_joint_aes_blocks();
    WS_CHECK(link_work_grant(LW_WIRE_ENCODE));
    WS_CHECK(ed_wire_encode(&WA.keys.packet,WA.keys.a+1,115,&WA.keys.size) == ZIGBEE_SECURITY_ARGUMENT);
    WS_CHECK(ed_wire_encode(&WA.keys.packet,WA.keys.a,115,&WA.keys.size) == ZIGBEE_SECURITY_ARGUMENT);
    WS_CHECK(!memcmp(before,&WA,sizeof(WA)));
    WS_CHECK(link_work_call_result(ZIGBEE_SECURITY_ARGUMENT) == ZIGBEE_SECURITY_ARGUMENT);
    WS_CHECK(link_work_grant(LW_HASH_KEY));
    WS_CHECK(zigbee_key_hash(WA.keys.hash_key,0,WA.keys.key.key+1,1000,128,&WA.keys.hash_info) == ZIGBEE_MMO_ARGUMENT);
    WS_CHECK(!memcmp(before,&WA,sizeof(WA)) && security_joint_aes_blocks() == blocks);
    WS_CHECK(link_work_call_result(ZIGBEE_MMO_ARGUMENT) == ZIGBEE_MMO_ARGUMENT);
    WS_CHECK(link_work_leave(LW_KEYS) && link_work_clean());

    security_joint_reset(1);
    WS_CHECK(security_keys_open() == SECURITY_KEYS_EMPTY);
    WS_CHECK(security_keys_provision(&config,code,256,256,&limits,64) == SECURITY_KEYS_OK);
    WS_CHECK(link_work_clean());
    WS_CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_PROVISIONED);
    security_joint_fail_read(1);
    WS_CHECK(security_keys_associate(0x3344,64) == SECURITY_KEYS_STORAGE);
    WS_CHECK(link_work_clean());
    memcpy(&diagnostic,security_counter_status(),sizeof(diagnostic));
    WS_CHECK(diagnostic.state == SECURITY_COUNTER_FAILED);
    WS_CHECK(security_keys_associate(0x3344,64) == SECURITY_KEYS_STORAGE);
    WS_CHECK(security_keys_status(&status) == SECURITY_KEYS_OK);
    WS_CHECK(status.phase == SECURITY_KEYS_FAILED && status.result == SECURITY_KEYS_STORAGE);
    WS_CHECK(!memcmp(&diagnostic,security_counter_status(),sizeof(diagnostic)) && link_work_clean());
    security_joint_reset(0); /* actual modeled complete CPU reset, not a loan release */
    WS_CHECK(security_keys_open() == SECURITY_KEYS_OK && link_work_clean());
    WS_CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_PROVISIONED);
    security_joint_reset(1);
    WS_CHECK(security_keys_open() == SECURITY_KEYS_EMPTY);
    security_joint_stall_aes(1);
    WS_CHECK(security_keys_provision(&config,code,256,256,&limits,64) == SECURITY_KEYS_CRYPTO);
    WS_CHECK(link_work_clean());
    security_joint_reset(1);
}

static void ws_return_bytes(void)
{
    uint8_t before[sizeof(WA)];
    unsigned frame, result;
    WS_CHECK(link_work_clean());
    /* The macro replaces two uint8_t parameters, not two reduced-range enums.
     * A returned255 can be a genuine caller result with successful cleanup;
     * a rejected release must retain the actual owner even for result255. */
    for (frame = 0; frame < 256; frame++) for (result = 0; result < 256; result++) {
        WS_CHECK(link_work_enter(LW_MAC_DECODE));
        memset(&WA.protocol.codec.candidate,0xa5,sizeof(WA.protocol.codec.candidate));
        memcpy(before,&WA,sizeof(WA));
        WS_CHECK(link_work_return(frame,result) == (frame == LW_MAC_DECODE ? result : 255u));
        if (frame != LW_MAC_DECODE) {
            WS_CHECK(!memcmp(before,&WA,sizeof(WA)));
            WS_CHECK(!link_work_enter(LW_KEYS));
            WS_CHECK(link_work_leave(LW_MAC_DECODE));
        }
        WS_CHECK(link_work_clean());
    }
    frame = LW_MAC_DECODE; result = 0x81;
    WS_CHECK(link_work_enter((uint8_t)frame));
    WS_CHECK(LW_RETURN(frame++,result++) == 0x81);
    WS_CHECK(frame == LW_MAC_DECODE+1 && result == 0x82 && link_work_clean());
    frame = 0x100u+LW_MAC_DECODE; result = 0x17au;
    WS_CHECK(link_work_enter(LW_MAC_DECODE));
    WS_CHECK(link_work_return(frame,result) == 0x7a && link_work_clean());

    for (result = 0; result < 256; result++) {
        WS_CHECK(link_work_enter(LW_KEYS));
        memset(&WA.keys,0x69,sizeof(WA.keys));
        memcpy(before,&WA,sizeof(WA));
        WS_CHECK(link_work_grant(LW_HASH_KEY));
        WS_CHECK(link_work_return(LW_KEYS,result) == 255);
        WS_CHECK(!memcmp(before,&WA,sizeof(WA)));
        WS_CHECK(!link_work_leave(LW_KEYS));
        WS_CHECK(link_work_call_result((uint8_t)result) == result);
        WS_CHECK(link_work_return(LW_KEYS,result) == result && link_work_clean());
    }
}

static void ws_loan_bytes(void)
{
    uint8_t before[sizeof(WA)], outside[32];
    unsigned phase, operation, writing;
    memset(outside,0x77,sizeof(outside));
    for (phase = 0; phase < 3; phase++) {
        WS_CHECK(link_work_clean());
        if (phase) {
            WS_CHECK(link_work_enter(LW_KEYS));
            memset(&WA.keys,0x6d,sizeof(WA.keys));
        }
        if (phase == 2) WS_CHECK(link_work_grant(LW_HASH_KEY));
        memcpy(before,&WA,sizeof(WA));
        /* Independent selected-slot oracle: no grant is insufficient; the
         * precise HASH grant admits only whole input/output fields with the
         * exact operation and direction bytes. Do not normalize writing to
         * bool or truncate operation to seven bits. */
        for (operation = 0; operation < 256; operation++)
            for (writing = 0; writing < 256; writing++) {
                WS_CHECK(link_work_io((uint8_t)operation,WA.keys.hash_key,16,(uint8_t)writing) ==
                    (phase == 2 && operation == LW_CHILD_HASH && writing == 0));
                WS_CHECK(link_work_io((uint8_t)operation,WA.keys.key.key,16,(uint8_t)writing) ==
                    (phase == 2 && operation == LW_CHILD_HASH && writing == 1));
                WS_CHECK(!link_work_io((uint8_t)operation,WA.keys.hash_key+1,15,(uint8_t)writing));
                WS_CHECK(link_work_io((uint8_t)operation,NULL,0,(uint8_t)writing));
                WS_CHECK(!link_work_io((uint8_t)operation,NULL,1,(uint8_t)writing));
                WS_CHECK(link_work_io((uint8_t)operation,outside,sizeof(outside),(uint8_t)writing));
            }
        WS_CHECK(!memcmp(before,&WA,sizeof(WA)));
        ws_bytes(outside,sizeof(outside),0x77);
        if (phase == 2) {
            WS_CHECK(!link_work_leave(LW_KEYS));
            WS_CHECK(link_work_call_result(0x9a) == 0x9a);
        }
        if (phase) WS_CHECK(link_work_leave(LW_KEYS));
        WS_CHECK(link_work_clean());
    }
}

static void ws_borrowed_returns(void)
{
    unsigned result;
    for (result = 0; result < 256; result++) {
        WS_CHECK(link_work_enter(LW_JOIN_STEP));
        memset(&WA.protocol.parent.join,0xa5,sizeof(WA.protocol.parent.join));
        WS_CHECK(link_work_enter(LW_TX_OBSERVED));
        memset(&WA.protocol.operation.tx,0x33,sizeof(WA.protocol.operation.tx));
        WS_CHECK(link_work_enter(LW_TX_INTERVAL));
        WS_CHECK(link_work_enter(LW_TX_STEP));
        WS_CHECK(link_work_return(LW_TX_STEP,result) == result);
        WS_CHECK(link_work_return(LW_TX_INTERVAL,result) == result);
        ws_bytes(&WA.protocol.operation.tx,sizeof(WA.protocol.operation.tx),0x33);
        ws_bytes(&WA.protocol.parent.join,sizeof(WA.protocol.parent.join),0xa5);
        WS_CHECK(link_work_return(LW_TX_OBSERVED,result) == result);
        ws_bytes(&WA.protocol.operation,sizeof(WA.protocol.operation),0);
        ws_bytes(&WA.protocol.parent.join,sizeof(WA.protocol.parent.join),0xa5);
        WS_CHECK(link_work_return(LW_JOIN_STEP,result) == result && link_work_clean());
    }
}

int main(void)
{
    ws_return_bytes();
    ws_loan_bytes();
    ws_borrowed_returns();
    ws_counter_boundary();
    ws_nested();
    ws_key_slots_and_faults();
    WS_CHECK(workspace_ram_main() == 0);
    WS_CHECK(link_work_clean());
    printf("UPPER workspace: %u ownership/alias/fault/canary checks PASS.\n",ws_checks);
    return 0;
}
