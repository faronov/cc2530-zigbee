/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "security_keys.h"
#include "security_counter.h"
#include "zigbee_key_hash.h"
#include <string.h>

#if defined(__SDCC)
/* Genuine link/resource probe, not a successful target service substitute.
 * No board/radio linkage. Host corpus below is NOT a target simulation. */
static MCU_XDATA security_keys_status_t target_status;
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t security_keys_test_result;
void main(void)
{
    security_keys_test_result = (uint8_t)security_keys_open();
    (void)security_keys_status(&target_status);
    for (;;) {}
}
#else
#include "security_joint_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static const uint8_t install[18] = {
    0x83,0xfe,0xd3,0x40,0x7a,0x93,0x97,0x23,0xa5,0xc6,0x39,0xb2,0x69,0x16,0xd5,0x05,0xc3,0xb5
};
static const uint8_t nwk_key[16] = {0x10,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const uint8_t new_tc[16] = {0x30,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const uint8_t alt_key[16] = {0x50,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static uint8_t bootstrap[16], raw[116], frame[116], inner[116], written, event;
static uint8_t snapshot[4096], waiting[4096], received[4096], provisioned[4096];
static uint8_t wire_length, sealed_aps_secured;
static ed_packet_t packet, output, saved_output;
static security_keys_status_t status;
static zigbee_security_key_t effective;
static zigbee_security_info_t info;
static zigbee_security_meta_t meta;
static zigbee_mmo_info_t hash_info;
static nwk_frame_info_t nwk;
static ccm_star_limits_t limits = {1000,128};
static security_keys_config_t config = {
    {0x11,2,3,4,5,6,7,8}, {0x22,2,3,4,5,6,7,8},
    {0x33,2,3,4,5,6,7,8}, 0x1234, 0xffff, 15, 254
};
static unsigned checks;
static jmp_buf power_cut;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "security keys check failed at line %d\n", __LINE__); abort(); } checks++; } while (0)

/* Fixed public ciphertexts cross-checked with cryptography AESCCM and an
 * independent host MMO/HMAC construction; not merely encrypt/decrypt symmetry. */
static void golden(const uint8_t *bytes, uint8_t length, const char *hex)
{
    unsigned i, value;
    CHECK(strlen(hex) == 2u*length);
    for (i = 0; i < length; i++) {
        CHECK(sscanf(hex+2*i,"%2x",&value) == 1);
        CHECK(bytes[i] == value);
    }
}
static void state(uint8_t phase)
{
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK);
    CHECK(status.phase == phase);
}
static void reboot(const uint8_t *image, uint8_t phase)
{
    security_joint_reset(0);
    if (image) memcpy(security_joint_nv(), image, 4096);
    CHECK(security_keys_open() == SECURITY_KEYS_OK);
    state(phase);
}
static void base(uint8_t type)
{
    memset(&packet, 0, sizeof(packet));
    packet.nwk.version = 2; packet.nwk.destination = 0x1234;
    packet.nwk.radius = 1; packet.aps.type = type; packet.aps.counter = 0x42;
}
static void material(const uint8_t *key, uint8_t id, uint32_t count, uint8_t seq, uint8_t sending)
{
    memset(&effective, 0, sizeof(effective)); effective.limits = limits;
    effective.level = 5; effective.extended_nonce = 1; effective.key_identifier = id;
    effective.counter = count; effective.key_sequence = seq;
    memcpy(effective.source, sending ? config.own_ieee : config.tc_ieee, 8);
    if (id == 2 || id == 3)
        CHECK(zigbee_key_hash(key, id == 2 ? 0 : 2, effective.key, 1000,128,&hash_info) == ZIGBEE_MMO_OK);
    else memcpy(effective.key, key, 16);
}
static void seal_packet(const uint8_t *link, uint8_t id, uint32_t ac,
                        const uint8_t *net, uint8_t seq, uint32_t nc)
{
    uint8_t n;
    sealed_aps_secured = link != NULL;
    CHECK(ed_wire_encode(&packet, frame, 116, &n) == ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_nwk(frame, n, &nwk) == ZIGBEE_SECURITY_OK);
    if (link) {
        material(link,id,ac,0,0);
        CHECK(ed_wire_crypt(0,1,&effective,frame+nwk.payload_offset,nwk.payload_length,
                           inner,116,&info) == ZIGBEE_SECURITY_OK);
        memcpy(frame+nwk.payload_offset,inner,info.length);
        n = nwk.payload_offset+info.length;
    }
    if (net) {
        material(net,1,nc,seq,0);
        CHECK(ed_wire_crypt(0,0,&effective,frame,n,raw,116,&info) == ZIGBEE_SECURITY_OK);
        wire_length = info.length;
    } else { memcpy(raw,frame,n); wire_length = n; }
}
static void transport(uint8_t type, const uint8_t *key, uint8_t seq)
{
    uint8_t offset = type == 1 ? 19 : 18;
    base(ED_APS_COMMAND); packet.payload[0] = 5; packet.payload[1] = type;
    memcpy(packet.payload+2,key,16);
    if (type == 1) packet.payload[18] = seq;
    memcpy(packet.payload+offset,config.own_ieee,8);
    memcpy(packet.payload+offset+8,config.tc_ieee,8);
    packet.length = type == 1 ? 35 : 34;
}
static void accept(uint8_t expected)
{
    memset(&output,0xa5,sizeof(output)); event = 0xa5;
    CHECK(security_keys_receive(raw,wire_length,&output,&event,&limits,3) == SECURITY_KEYS_OK);
    CHECK((event & SECURITY_KEYS_EVENT_MASK) == expected);
    CHECK((event & SECURITY_KEYS_EVENT_APS_SECURED) ==
          (sealed_aps_secured ? SECURITY_KEYS_EVENT_APS_SECURED : 0));
    CHECK(!(output.aps.flags & APS_FLAG_SECURITY));
}
static void reject(security_keys_result_t expected)
{
    security_keys_result_t r;
    memset(&output,0xa5,sizeof(output)); saved_output = output; event = 0xa5;
    r = security_keys_receive(raw,wire_length,&output,&event,&limits,3);
    if (r != expected) fprintf(stderr,"result %u expected %u\n",(unsigned)r,(unsigned)expected);
    CHECK(r == expected);
    CHECK(!memcmp(&output,&saved_output,sizeof(output)) && event == 0xa5);
}
static void confirm(const uint8_t *link, uint32_t ac, uint32_t nc)
{
    base(ED_APS_COMMAND); packet.length = 11;
    packet.payload[0] = 16; packet.payload[1] = 0; packet.payload[2] = 4;
    memcpy(packet.payload+3,config.own_ieee,8);
    seal_packet(link,0,ac,nwk_key,255,nc);
}
static void data(uint32_t nc, const uint8_t *net, uint8_t seq)
{
    base(APS_FRAME_DATA); packet.aps.profile_id = 0x0104;
    packet.aps.source_endpoint = packet.aps.destination_endpoint = 1;
    packet.length = 3; memcpy(packet.payload,"abc",3);
    seal_packet(NULL,0,0,net,seq,nc);
}
static void outgoing_open(const uint8_t *link, uint8_t command)
{
    uint8_t n;
    material(nwk_key,1,0,255,1);
    CHECK(ed_wire_crypt(1,0,&effective,raw,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
    n = info.length;
    CHECK(ed_wire_nwk(frame,n,&nwk) == ZIGBEE_SECURITY_OK);
    if (link) {
        material(link,0,0,0,1);
        CHECK(ed_wire_crypt(1,1,&effective,frame+nwk.payload_offset,nwk.payload_length,
                           inner,116,&info) == ZIGBEE_SECURITY_OK);
        memcpy(frame+nwk.payload_offset,inner,info.length);
        n = nwk.payload_offset+info.length;
    }
    CHECK(ed_wire_decode(frame,n,&output) == ZIGBEE_SECURITY_OK);
    CHECK(output.aps.type == ED_APS_COMMAND && output.payload[0] == command);
}
static void setup(void)
{
    security_joint_reset(1);
    CHECK(security_keys_open() == SECURITY_KEYS_EMPTY);
    CHECK(!security_joint_flash_commands());
    state(SECURITY_KEYS_UNPROVISIONED);
    CHECK(security_keys_associate(0x1234,3) == SECURITY_KEYS_STATE);
    CHECK(install_code_derive(install,18,bootstrap,1000,128,&hash_info) == ZIGBEE_MMO_OK);
    CHECK(security_keys_provision(&config,install,10,20,&limits,3) == SECURITY_KEYS_OK);
    CHECK(security_joint_aes_blocks() == 4 && security_joint_flash_commands() == 38);
    state(SECURITY_KEYS_PROVISIONED);
    CHECK(!status.parent_information && !status.timeout_pending);
    memcpy(provisioned,security_joint_nv(),4096);
    CHECK(security_keys_associate(0,3) == SECURITY_KEYS_ARGUMENT);
    CHECK(security_keys_associate(0xfff8,3) == SECURITY_KEYS_ARGUMENT);
    CHECK(security_keys_associate(0x1234,3) == SECURITY_KEYS_OK);
    state(SECURITY_KEYS_ASSOCIATED);
    CHECK(!status.parent_information && !status.timeout_pending);
    transport(1,nwk_key,255); seal_packet(bootstrap,2,0,NULL,0,0);
    CHECK(wire_length == 62 && raw[8] == 0x21 && raw[10] == 0x30);
    golden(raw,wire_length,
        "0800341200000100214230000000002202030405060708db1e6d0cc6bf5193b86402c1"
        "b7ca687916ae81c9a398c57b91d1190824b2d01897375fabba4977");
    raw[wire_length-1] ^= 1; reject(SECURITY_KEYS_AUTH); raw[wire_length-1] ^= 1;
    accept(SECURITY_KEYS_EVENT_NETWORK_KEY);
    CHECK(output.length == 0);
    for (unsigned i = 0; i < sizeof(output.payload); i++) CHECK(output.payload[i] == 0);
    state(SECURITY_KEYS_RECEIVED);
    memcpy(received,security_joint_nv(),4096);
    reject(SECURITY_KEYS_CONTEXT); /* unsecured NWK never allowed again */
}
static void handshake(void)
{
    uint8_t digest[16];
    reboot(received,SECURITY_KEYS_RECEIVED);
    data(0,nwk_key,255); reject(SECURITY_KEYS_CONTEXT);
    CHECK(security_keys_request(7,8,raw,46,&written,&limits,3) == SECURITY_KEYS_SPACE);
    CHECK(security_keys_request(7,8,raw,47,&written,&limits,3) == SECURITY_KEYS_OK && written == 47);
    golden(raw,written,
        "0802000034120107280a0000001102030405060708ffe64277b35fdecbe8f7023b280508"
        "447ff74c22620aa155c25f");
    state(SECURITY_KEYS_REQUESTED);
    outgoing_open(bootstrap,8);
    CHECK(output.length == 2 && output.payload[1] == 4);
    reboot(NULL,SECURITY_KEYS_REQUESTED);
    transport(4,bootstrap,0); seal_packet(bootstrap,3,1,nwk_key,255,0);
    reject(SECURITY_KEYS_SELECTOR);
    transport(4,new_tc,0); packet.payload[26] ^= 1; seal_packet(bootstrap,3,1,nwk_key,255,0);
    reject(SECURITY_KEYS_IDENTITY);
    transport(4,new_tc,0); seal_packet(bootstrap,2,1,nwk_key,255,0);
    reject(SECURITY_KEYS_SELECTOR);
    transport(4,new_tc,0); seal_packet(bootstrap,3,1,nwk_key,255,0);
    accept(SECURITY_KEYS_EVENT_TC_KEY);
    state(SECURITY_KEYS_PROVISIONAL);
    reboot(NULL,SECURITY_KEYS_PROVISIONAL);
    confirm(new_tc,0,1); reject(SECURITY_KEYS_REPLAY); /* no outstanding Verify; old floor applies */
    CHECK(security_keys_verify(9,10,raw,53,&written,&limits,3) == SECURITY_KEYS_SPACE);
    CHECK(security_keys_verify(9,10,raw,54,&written,&limits,3) == SECURITY_KEYS_OK && written == 54);
    golden(raw,written,
        "0802000034120109280a0100001102030405060708ff0ccfd0e5dc1e3a8695618c43d1bd"
        "1ed90d41748429d76c4dcd414623b36e84cc");
    outgoing_open(NULL,15);
    CHECK(output.length == 26 && output.payload[1] == 4 &&
          !memcmp(output.payload+2,config.own_ieee,8));
    CHECK(zigbee_key_hash(new_tc,3,digest,1000,128,&hash_info) == ZIGBEE_MMO_OK);
    CHECK(!memcmp(output.payload+10,digest,16));
    state(SECURITY_KEYS_WAIT_CONFIRM);
    memcpy(waiting,security_joint_nv(),4096);
    reboot(waiting,SECURITY_KEYS_WAIT_CONFIRM);
    confirm(bootstrap,2,1); reject(SECURITY_KEYS_CONTEXT); /* old A cannot confirm B */
    confirm(NULL,0,1); reject(SECURITY_KEYS_CONTEXT);
    confirm(new_tc,0,1); packet.payload[1] = 1;
    seal_packet(new_tc,0,0,nwk_key,255,1); reject(SECURITY_KEYS_FORMAT);
    confirm(new_tc,0,1); packet.payload[2] = 1;
    seal_packet(new_tc,0,0,nwk_key,255,1); reject(SECURITY_KEYS_FORMAT);
    confirm(new_tc,0,1); packet.payload[3] ^= 1;
    seal_packet(new_tc,0,0,nwk_key,255,1); reject(SECURITY_KEYS_IDENTITY);
    /* Old key is still accepted for ordinary protected join management. */
    base(APS_FRAME_DATA); packet.aps.cluster_id = 0x8002;
    packet.length = 2; seal_packet(bootstrap,0,2,nwk_key,255,1);
    accept(SECURITY_KEYS_EVENT_MANAGEMENT);
    confirm(new_tc,0,2); accept(SECURITY_KEYS_EVENT_VERIFIED);
    state(SECURITY_KEYS_VERIFIED);
    CHECK(security_counter_status()->until[0] >= 10 && security_counter_status()->until[1] >= 20);
    memcpy(snapshot,security_joint_nv(),4096);
    reject(SECURITY_KEYS_REPLAY);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    confirm(new_tc,0,3); reject(SECURITY_KEYS_REPLAY);
    base(APS_FRAME_DATA); packet.length = 1;
    seal_packet(bootstrap,0,3,nwk_key,255,3); reject(SECURITY_KEYS_AUTH);
}
static void replay_and_rotation(void)
{
    uint32_t ceiling;
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    data(3,nwk_key,255); accept(SECURITY_KEYS_EVENT_DATA);
    reject(SECURITY_KEYS_REPLAY);
    data(2,nwk_key,255); reject(SECURITY_KEYS_REPLAY);
    data(4,nwk_key,255); raw[wire_length-1] ^= 1; reject(SECURITY_KEYS_AUTH);
    raw[wire_length-1] ^= 1; accept(SECURITY_KEYS_EVENT_DATA);
    transport(1,nwk_key,255); seal_packet(new_tc,2,1,nwk_key,255,5);
    accept(SECURITY_KEYS_EVENT_NETWORK_KEY);
    data(4,nwk_key,255); reject(SECURITY_KEYS_REPLAY); /* duplicate didn't reset */
    transport(1,nwk_key,0); seal_packet(new_tc,2,2,nwk_key,255,6);
    reject(SECURITY_KEYS_SELECTOR); /* same bytes, different sequence */
    transport(1,alt_key,255); seal_packet(new_tc,2,2,nwk_key,255,6);
    reject(SECURITY_KEYS_SELECTOR); /* conflicting sequence */
    transport(1,alt_key,1); seal_packet(new_tc,2,2,nwk_key,255,6);
    reject(SECURITY_KEYS_SELECTOR); /* not next chronology */
    transport(1,alt_key,0); seal_packet(new_tc,2,2,nwk_key,255,6);
    accept(SECURITY_KEYS_EVENT_NETWORK_KEY);
    state(SECURITY_KEYS_VERIFIED);
    CHECK(status.slot_valid == 3 && status.newer_pending && status.active_sequence == 255);
    data(0,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
    state(SECURITY_KEYS_VERIFIED);
    CHECK(!status.newer_pending && status.active_sequence == 0);
    data(7,nwk_key,255); accept(SECURITY_KEYS_EVENT_DATA);
    state(SECURITY_KEYS_VERIFIED); CHECK(status.active_sequence == 0);
    base(ED_APS_COMMAND); packet.length = 2; packet.payload[0] = 9; packet.payload[1] = 255;
    packet.nwk.destination = 0xffff; seal_packet(NULL,0,0,alt_key,0,1);
    reject(SECURITY_KEYS_SELECTOR); /* cannot switch backwards */
    ceiling = security_counter_status()->until[0];
    base(APS_FRAME_DATA); packet.nwk.source = 0x1234; packet.nwk.destination = 0;
    packet.aps.source_endpoint = packet.aps.destination_endpoint = 1; packet.aps.profile_id = 0x104;
    packet.length = 1;
    CHECK(security_keys_send(&packet,1,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    CHECK(ed_wire_inspect(0,raw,written,&meta) == ZIGBEE_SECURITY_OK);
    CHECK(meta.key_sequence == 0 && meta.counter >= ceiling);
    material(alt_key,1,0,0,1);
    CHECK(ed_wire_crypt(1,0,&effective,raw,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_nwk(frame,info.length,&nwk) == ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_inspect(1,frame+nwk.payload_offset,nwk.payload_length,&meta) == ZIGBEE_SECURITY_OK &&
          meta.key_identifier == 0);
}
static void framing_and_identity(void)
{
    unsigned n;
    uint8_t good[116], goodlen, prior[4096];
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    data(3,nwk_key,255); goodlen = wire_length; memcpy(good,raw,goodlen);
    memcpy(prior,security_joint_nv(),4096);
    for (n = 0; n < goodlen; n++) {
        memset(&output,0xa5,sizeof(output)); saved_output = output; event = 0xa5;
        CHECK(security_keys_receive(good,(uint16_t)n,&output,&event,&limits,3) != SECURITY_KEYS_OK);
        CHECK(!memcmp(&output,&saved_output,sizeof(output)) && event == 0xa5);
    }
    memcpy(raw,good,goodlen); wire_length = goodlen+1; raw[goodlen] = 0;
    reject(SECURITY_KEYS_AUTH);
    data(3,nwk_key,255); raw[4] = 1; reject(SECURITY_KEYS_IDENTITY);
    data(3,nwk_key,255); raw[2] ^= 1; reject(SECURITY_KEYS_IDENTITY);
    data(3,nwk_key,255); raw[13] ^= 1; reject(SECURITY_KEYS_IDENTITY);
    data(3,nwk_key,255); raw[21] ^= 1; reject(SECURITY_KEYS_SELECTOR);
    data(3,nwk_key,255); memset(raw+9,255,4); reject(SECURITY_KEYS_FORMAT);
    CHECK(!memcmp(prior,security_joint_nv(),4096));
    /* Outgoing exact allocation and unchanged tail/failure outputs. */
    base(APS_FRAME_DATA); packet.nwk.source = 0x1234; packet.nwk.destination = 0;
    packet.length = 3; packet.aps.profile_id = 0x104;
    packet.aps.source_endpoint = packet.aps.destination_endpoint = 1;
    memset(raw,0xa5,sizeof(raw)); written = 0xa5;
    CHECK(security_keys_send(&packet,0,raw,36,&written,&limits,3) == SECURITY_KEYS_SPACE);
    CHECK(written == 0xa5 && raw[0] == 0xa5 && raw[115] == 0xa5);
    {
        uint8_t *exact = malloc(37);
        CHECK(exact != NULL);
        CHECK(security_keys_send(&packet,0,exact,37,&written,&limits,3) == SECURITY_KEYS_OK);
        CHECK(written == 37); free(exact);
    }
    packet.length = 65; memset(packet.payload,0x69,65);
    CHECK(security_keys_send(&packet,1,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    CHECK(written == 116);
    packet.length = 66; packet.payload[65] = 0x69;
    memset(raw,0xa5,sizeof(raw)); written = 0xa5;
    CHECK(security_keys_send(&packet,1,raw,116,&written,&limits,3) != SECURITY_KEYS_OK);
    CHECK(written == 0xa5 && raw[0] == 0xa5 && raw[115] == 0xa5);
    packet.length = 0; packet.aps.type = ED_APS_ACK; packet.nwk.destination = 0xffff;
    CHECK(security_keys_send(&packet,0,raw,116,&written,&limits,3) == SECURITY_KEYS_IDENTITY);
}
static void updates_and_leave(void)
{
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    base(0); packet.nwk.type = ED_NWK_COMMAND;
    packet.nwk.flags = NWK_FLAG_SOURCE_IEEE;
    memcpy(packet.nwk.source_ieee,config.tc_ieee,8);
    packet.nwk.destination = 0xffff; packet.length = 13;
    packet.payload[0] = 10; packet.payload[1] = 1;
    memcpy(packet.payload+2,config.extended_pan,8);
    packet.payload[10] = 255; packet.payload[11] = 0x78; packet.payload[12] = 0x56;
    seal_packet(NULL,0,0,nwk_key,255,3);
    accept(SECURITY_KEYS_EVENT_UPDATE);
    state(SECURITY_KEYS_VERIFIED); CHECK(status.config.pan == 0x5678 && status.config.update_id == 255);
    reboot(NULL,SECURITY_KEYS_VERIFIED); CHECK(status.config.pan == 0x5678);
    base(0); packet.nwk.type = ED_NWK_COMMAND;
    packet.nwk.flags = NWK_FLAG_SOURCE_IEEE;
    memcpy(packet.nwk.source_ieee,config.tc_ieee,8);
    packet.length = 2; packet.payload[0] = 4; packet.payload[1] = 0x60;
    seal_packet(NULL,0,0,nwk_key,255,4); accept(SECURITY_KEYS_EVENT_LEAVE);
    state(SECURITY_KEYS_LEFT);
    reboot(NULL,SECURITY_KEYS_LEFT);
    data(5,nwk_key,255); reject(SECURITY_KEYS_STATE);
}
static void update_packet(uint8_t update_id, uint16_t pan)
{
    base(0); packet.nwk.type = ED_NWK_COMMAND;
    packet.nwk.flags = NWK_FLAG_SOURCE_IEEE;
    memcpy(packet.nwk.source_ieee,config.tc_ieee,8);
    packet.nwk.destination = 0xffff; packet.length = 13;
    packet.payload[0] = 10; packet.payload[1] = 1;
    memcpy(packet.payload+2,config.extended_pan,8);
    packet.payload[10] = update_id;
    packet.payload[11] = (uint8_t)pan; packet.payload[12] = (uint8_t)(pan >> 8);
}
static void update_policy(void)
{
    static const uint8_t deltas[] = {0,1,2,127,128,129,255};
    uint8_t i, id, before[4096];
    uint32_t outgoing;
    for (i = 0; i < sizeof(deltas); i++) {
        reboot(snapshot,SECURITY_KEYS_VERIFIED);
        memcpy(before,security_joint_nv(),4096);
        id = (uint8_t)(config.update_id+deltas[i]);
        update_packet(id,deltas[i] ? 0x5678 : config.pan);
        seal_packet(NULL,0,0,nwk_key,255,3);
        if (deltas[i] >= 128) {
            reject(SECURITY_KEYS_CONTEXT);
            CHECK(!memcmp(before,security_joint_nv(),4096));
        } else {
            accept(SECURITY_KEYS_EVENT_UPDATE);
            reboot(NULL,SECURITY_KEYS_VERIFIED);
            CHECK(status.config.update_id == id &&
                  status.config.pan == (deltas[i] ? 0x5678 : config.pan));
            CHECK(status.config.address == 0x1234 && status.active_sequence == 255 &&
                  status.slot_valid == 1 && !status.newer_pending);
            reject(SECURITY_KEYS_REPLAY);
        }
    }
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    outgoing = security_counter_status()->until[0];
    update_packet(config.update_id,0x5678);
    seal_packet(NULL,0,0,nwk_key,255,3); reject(SECURITY_KEYS_CONTEXT);
    update_packet(config.update_id,config.pan);
    seal_packet(NULL,0,0,nwk_key,255,3); accept(SECURITY_KEYS_EVENT_UPDATE);
    seal_packet(NULL,0,0,nwk_key,255,4); accept(SECURITY_KEYS_EVENT_UPDATE);
    CHECK(security_counter_status()->until[0] == outgoing);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    CHECK(status.config.pan == config.pan && status.config.update_id == config.update_id);
    reject(SECURITY_KEYS_REPLAY); /* idempotence did not reset the NWK floor */
    update_packet(0,0x5678); /* delta2 across wrap */
    seal_packet(NULL,0,0,nwk_key,255,5); accept(SECURITY_KEYS_EVENT_UPDATE);
    update_packet(127,0x4321); /* largest strictly newer delta */
    seal_packet(NULL,0,0,nwk_key,255,6); accept(SECURITY_KEYS_EVENT_UPDATE);
    state(SECURITY_KEYS_VERIFIED);
    CHECK(status.config.pan == 0x4321 && status.config.update_id == 127);
    /* Command-specific addressing, manager identity and exact one-record form. */
    for (i = 0; i < 8; i++) {
        reboot(snapshot,SECURITY_KEYS_VERIFIED);
        memcpy(before,security_joint_nv(),4096);
        update_packet(0,0x5678);
        if (i == 0) packet.nwk.flags = 0;
        if (i == 1) {
            packet.nwk.flags |= NWK_FLAG_DESTINATION_IEEE;
            packet.nwk.destination = 0x1234;
            memcpy(packet.nwk.destination_ieee,config.own_ieee,8);
        }
        if (i == 2) packet.nwk.destination = 0x1234;
        if (i == 3) packet.nwk.source_ieee[0] ^= 1;
        if (i == 4) packet.payload[1] = 2;
        if (i == 5) packet.payload[1] = 0x21;
        if (i == 6) packet.payload[2] ^= 1;
        if (i == 7) packet.payload[11] = packet.payload[12] = 255;
        seal_packet(NULL,0,0,nwk_key,255,3);
        reject(i == 3 || i >= 6 ? SECURITY_KEYS_IDENTITY : SECURITY_KEYS_FORMAT);
        CHECK(!memcmp(before,security_joint_nv(),4096));
    }
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    update_packet(0,0x5678); seal_packet(NULL,0,0,NULL,0,0);
    reject(SECURITY_KEYS_CONTEXT);
    seal_packet(NULL,0,0,nwk_key,255,3);
    raw[wire_length-1] ^= 1; reject(SECURITY_KEYS_AUTH);
    raw[wire_length-1] ^= 1;
    security_joint_fail_read(1); reject(SECURITY_KEYS_STORAGE);
}
static void announce_and_retry(void)
{
    uint32_t first_nwk, first_aps;
    reboot(received,SECURITY_KEYS_RECEIVED);
    base(APS_FRAME_DATA); packet.nwk.source = 0x1234; packet.nwk.destination = 0xfffd;
    packet.aps.delivery_mode = APS_DELIVERY_BROADCAST;
    packet.aps.cluster_id = 0x0013; packet.aps.counter = 0x44;
    packet.length = 12; packet.payload[0] = 0x55;
    packet.payload[1] = 0x34; packet.payload[2] = 0x12;
    memcpy(packet.payload+3,config.own_ieee,8); packet.payload[11] = 0x88;
    CHECK(security_keys_send(&packet,0,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    state(SECURITY_KEYS_RECEIVED); /* announce is allowed, never final TC success */
    material(nwk_key,1,0,255,1);
    CHECK(ed_wire_crypt(1,0,&effective,raw,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_decode(frame,info.length,&output) == ZIGBEE_SECURITY_OK);
    CHECK(output.nwk.destination == 0xfffd && output.aps.delivery_mode == APS_DELIVERY_BROADCAST &&
          output.aps.cluster_id == 0x0013 && output.length == 12 && output.aps.counter == 0x44);
    CHECK(security_keys_request(1,0x66,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    CHECK(ed_wire_inspect(0,raw,written,&meta) == ZIGBEE_SECURITY_OK);
    first_nwk = meta.counter;
    outgoing_open(bootstrap,8); CHECK(output.aps.counter == 0x66);
    CHECK(output.aps.flags == 0 && output.nwk.sequence == 1);
    first_aps = info.meta.counter;
    CHECK(security_keys_request(2,0x66,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    CHECK(ed_wire_inspect(0,raw,written,&meta) == ZIGBEE_SECURITY_OK && meta.counter > first_nwk);
    outgoing_open(bootstrap,8);
    CHECK(output.aps.counter == 0x66 && info.meta.counter > first_aps);
    CHECK(output.aps.flags == 0 && output.nwk.sequence == 2);
    reboot(waiting,SECURITY_KEYS_WAIT_CONFIRM);
    first_aps = security_counter_status()->until[SECURITY_COUNTER_APS];
    CHECK(security_keys_verify(0x50,0x66,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    CHECK(ed_wire_inspect(0,raw,written,&meta) == ZIGBEE_SECURITY_OK);
    first_nwk = meta.counter;
    outgoing_open(NULL,15);
    CHECK(output.aps.counter == 0x66 && output.aps.flags == 0 && output.nwk.sequence == 0x50);
    CHECK(security_keys_verify(0x51,0x66,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    CHECK(ed_wire_inspect(0,raw,written,&meta) == ZIGBEE_SECURITY_OK && meta.counter > first_nwk);
    outgoing_open(NULL,15);
    CHECK(output.aps.counter == 0x66 && output.aps.flags == 0 && output.nwk.sequence == 0x51);
    CHECK(security_counter_status()->until[SECURITY_COUNTER_APS] == first_aps);
    state(SECURITY_KEYS_WAIT_CONFIRM);
}
static void protected_ack(void)
{
    uint8_t short_form, normalized_length, frame_length;
    uint32_t incoming_nwk = 3, incoming_aps = 1, sent_nwk, sent_aps;
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    for (short_form = 0; short_form < 2; short_form++) {
        base(ED_APS_ACK); packet.aps.counter = 0x73;
        if (short_form) packet.aps.flags = APS_FLAG_ACK_FORMAT;
        else {
            packet.aps.source_endpoint = 2; packet.aps.destination_endpoint = 1;
            packet.aps.cluster_id = 6; packet.aps.profile_id = 0x104;
        }
        seal_packet(new_tc,0,incoming_aps++,nwk_key,255,incoming_nwk++);
        accept(SECURITY_KEYS_EVENT_MANAGEMENT);
        CHECK(event & SECURITY_KEYS_EVENT_APS_SECURED);
        CHECK(output.length == 0 && output.aps.counter == 0x73 &&
              output.aps.flags == (short_form ? APS_FLAG_ACK_FORMAT : 0));
        /* Exercise outgoing ACK syntax; upper transport owns correlation. */
        packet.nwk.source = 0x1234; packet.nwk.destination = 0;
        normalized_length = short_form ? 2 : 8;
        frame_length = short_form ? 45 : 51;
        memset(raw,0xa5,sizeof(raw)); written = 0xa5;
        CHECK(security_keys_send(&packet,1,raw,frame_length-1u,&written,&limits,3) == SECURITY_KEYS_SPACE);
        CHECK(written == 0xa5 && raw[0] == 0xa5 && raw[115] == 0xa5);
        CHECK(security_keys_send(&packet,1,raw,frame_length,&written,&limits,3) == SECURITY_KEYS_OK);
        CHECK(written == frame_length);
        material(nwk_key,1,0,255,1);
        CHECK(ed_wire_crypt(1,0,&effective,raw,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
        sent_nwk = info.meta.counter;
        CHECK(ed_wire_nwk(frame,info.length,&nwk) == ZIGBEE_SECURITY_OK);
        material(new_tc,0,0,0,1);
        CHECK(ed_wire_crypt(1,1,&effective,frame+nwk.payload_offset,nwk.payload_length,
                           inner,116,&info) == ZIGBEE_SECURITY_OK);
        sent_aps = info.meta.counter;
        CHECK(info.length == normalized_length && info.meta.key_identifier == 0 &&
              inner[0] == (short_form ? 0x12 : 0x02) && inner[normalized_length-1u] == 0x73);
        CHECK(security_keys_send(&packet,1,raw,frame_length,&written,&limits,3) == SECURITY_KEYS_OK);
        material(nwk_key,1,0,255,1);
        CHECK(ed_wire_crypt(1,0,&effective,raw,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
        CHECK(info.meta.counter > sent_nwk);
        CHECK(ed_wire_nwk(frame,info.length,&nwk) == ZIGBEE_SECURITY_OK);
        material(new_tc,0,0,0,1);
        CHECK(ed_wire_crypt(1,1,&effective,frame+nwk.payload_offset,nwk.payload_length,
                           inner,116,&info) == ZIGBEE_SECURITY_OK);
        CHECK(info.meta.counter > sent_aps && inner[normalized_length-1u] == 0x73);
    }
    reboot(received,SECURITY_KEYS_RECEIVED);
    base(ED_APS_ACK); packet.nwk.source = 0x1234; packet.nwk.destination = 0;
    packet.aps.flags = APS_FLAG_ACK_FORMAT;
    CHECK(security_keys_send(&packet,1,raw,45,&written,&limits,3) == SECURITY_KEYS_OK);
    material(nwk_key,1,0,255,1);
    CHECK(ed_wire_crypt(1,0,&effective,raw,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_nwk(frame,info.length,&nwk) == ZIGBEE_SECURITY_OK);
    material(bootstrap,0,0,0,1);
    CHECK(ed_wire_crypt(1,1,&effective,frame+nwk.payload_offset,nwk.payload_length,
                       inner,116,&info) == ZIGBEE_SECURITY_OK);
    CHECK(info.length == 2 && inner[0] == 0x12);
    state(SECURITY_KEYS_RECEIVED);
}
static void timeout_packet(uint8_t response)
{
    base(0); packet.nwk.type = ED_NWK_COMMAND;
    packet.nwk.flags = NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE;
    packet.nwk.source = response ? 0 : 0x1234;
    packet.nwk.destination = response ? 0x1234 : 0;
    memcpy(packet.nwk.source_ieee,response ? config.tc_ieee : config.own_ieee,8);
    memcpy(packet.nwk.destination_ieee,response ? config.own_ieee : config.tc_ieee,8);
    packet.length = 3; packet.payload[0] = response ? 12 : 11;
    packet.payload[1] = response ? 0 : 12;
    packet.payload[2] = response ? 2 : 0;
}
static void timeout_management(void)
{
    static const uint8_t parent_info[] = {0,1,2,3,255};
    uint8_t stage, i, phase, status_code, before[112], after[112], length, retained_info;
    unsigned commands;
    uint32_t floor, first;
    /* A timeout procedure cannot create TC verification or application
     * readiness. It is already-admitted management for the upper owner. */
    for (stage = 0; stage < 2; stage++) {
        phase = stage ? SECURITY_KEYS_VERIFIED : SECURITY_KEYS_RECEIVED;
        reboot(stage ? snapshot : received,phase);
        CHECK(!status.parent_information && !status.timeout_pending);
        CHECK(security_counter_read(before,112,&length) == SECURITY_COUNTER_OK && length == 112);
        timeout_packet(0); packet.nwk.sequence = 0x77;
        memset(raw,0xa5,sizeof(raw)); written = 0xa5;
        CHECK(security_keys_send(&packet,0,raw,44,&written,&limits,3) == SECURITY_KEYS_SPACE);
        CHECK(written == 0xa5 && raw[0] == 0xa5 && raw[115] == 0xa5);
        state(phase); CHECK(!status.timeout_pending);
        CHECK(security_keys_send(&packet,0,raw,45,&written,&limits,3) == SECURITY_KEYS_OK && written == 45);
        material(nwk_key,1,0,255,1);
        CHECK(ed_wire_crypt(1,0,&effective,raw,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
        first = info.meta.counter;
        CHECK(ed_wire_decode(frame,info.length,&output) == ZIGBEE_SECURITY_OK);
        CHECK(output.nwk.type == ED_NWK_COMMAND && output.nwk.source == 0x1234 &&
              output.nwk.destination == 0 && output.nwk.radius == 1 && output.nwk.sequence == 0x77 &&
              output.nwk.flags == (NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE) &&
              !memcmp(output.nwk.source_ieee,config.own_ieee,8) &&
              !memcmp(output.nwk.destination_ieee,config.tc_ieee,8) &&
              output.length == 3 && !memcmp(output.payload,"\x0b\x0c\x00",3));
        packet.nwk.sequence++;
        CHECK(security_keys_send(&packet,0,raw,45,&written,&limits,3) == SECURITY_KEYS_OK);
        CHECK(ed_wire_inspect(0,raw,written,&meta) == ZIGBEE_SECURITY_OK && meta.counter > first);
        state(phase);
        CHECK(status.timeout_pending && !status.parent_information);
        floor = 3; retained_info = 0;
        for (status_code = 0; status_code < 2; status_code++)
            for (i = 0; i < sizeof(parent_info); i++) {
                timeout_packet(0);
                CHECK(security_keys_send(&packet,0,raw,45,&written,&limits,3) == SECURITY_KEYS_OK);
                timeout_packet(1); packet.payload[1] = status_code; packet.payload[2] = parent_info[i];
                seal_packet(NULL,0,0,nwk_key,255,floor++);
                accept(SECURITY_KEYS_EVENT_MANAGEMENT);
                CHECK(event == SECURITY_KEYS_EVENT_MANAGEMENT); /* no APS security/key event */
                CHECK(output.nwk.type == ED_NWK_COMMAND && output.length == 3 &&
                      output.payload[0] == 12 && output.payload[1] == status_code &&
                      output.payload[2] == parent_info[i]);
                CHECK(security_counter_read(after,112,&length) == SECURITY_COUNTER_OK && length == 112);
                CHECK(!memcmp(before,after,80) && !memcmp(before+88,after+88,21) &&
                      !memcmp(before+110,after+110,2));
                state(phase);
                if (!status_code) retained_info = parent_info[i] & 7u;
                CHECK(!status.timeout_pending && status.parent_information == retained_info);
            }
        reject(SECURITY_KEYS_REPLAY);
        reboot(NULL,phase); reject(SECURITY_KEYS_REPLAY);
    }
    /* All required addressing fields are checked on TX and RX, not merely
     * copied. TX config/reserved values are separate from RX parent-info. */
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    for (i = 0; i < 8; i++) {
        timeout_packet(0);
        if (i == 0) packet.nwk.flags &= (uint16_t)~NWK_FLAG_SOURCE_IEEE;
        if (i == 1) packet.nwk.flags &= (uint16_t)~NWK_FLAG_DESTINATION_IEEE;
        if (i == 2) packet.nwk.radius = 0;
        if (i == 3) packet.nwk.radius = 2;
        if (i == 4) packet.payload[1] = 15;
        if (i == 5) packet.payload[2] = 1;
        if (i == 6) packet.length = 2;
        if (i == 7) { packet.length = 4; packet.payload[3] = 0; }
        commands = security_joint_flash_commands();
        memset(raw,0xa5,sizeof(raw)); written = 0xa5;
        CHECK(security_keys_send(&packet,0,raw,116,&written,&limits,3) == SECURITY_KEYS_UNSUPPORTED);
        CHECK(written == 0xa5 && raw[0] == 0xa5 && security_joint_flash_commands() == commands);
    }
    timeout_packet(0);
    CHECK(security_keys_send(&packet,0,raw,45,&written,&limits,3) == SECURITY_KEYS_OK);
    for (i = 0; i < 10; i++) {
        timeout_packet(1);
        if (i == 0) packet.nwk.flags &= (uint16_t)~NWK_FLAG_SOURCE_IEEE;
        if (i == 1) packet.nwk.flags &= (uint16_t)~NWK_FLAG_DESTINATION_IEEE;
        if (i == 2) packet.nwk.radius = 0;
        if (i == 3) packet.nwk.radius = 2;
        if (i == 4) packet.payload[1] = 2;
        if (i == 5) packet.length = 2;
        if (i == 6) { packet.length = 4; packet.payload[3] = 0; }
        if (i == 7) packet.nwk.source = 1;
        if (i == 8) packet.nwk.source_ieee[0] ^= 1;
        if (i == 9) packet.nwk.destination_ieee[0] ^= 1;
        seal_packet(NULL,0,0,nwk_key,255,3);
        commands = security_joint_flash_commands();
        reject(i >= 7 ? SECURITY_KEYS_IDENTITY : SECURITY_KEYS_FORMAT);
        CHECK(security_joint_flash_commands() == commands);
    }
    timeout_packet(1); seal_packet(NULL,0,0,NULL,0,0); reject(SECURITY_KEYS_CONTEXT);
    seal_packet(NULL,0,0,nwk_key,255,3);
    raw[wire_length-1] ^= 1; reject(SECURITY_KEYS_AUTH);
    raw[wire_length-1] ^= 1; security_joint_fail_read(1); reject(SECURITY_KEYS_STORAGE);
}
static void post_key_passthrough(void)
{
    uint8_t profile, protected_aps;
    uint32_t incoming = 3, aps_count = 1;
    reboot(received,SECURITY_KEYS_RECEIVED);
    base(APS_FRAME_DATA); packet.nwk.source = 0x1234; packet.nwk.destination = 0;
    packet.aps.cluster_id = 0x0036; packet.length = 3;
    memcpy(packet.payload,"\x69\x3c\x01",3);
    CHECK(security_keys_send(&packet,0,raw,116,&written,&limits,3) == SECURITY_KEYS_CONTEXT);
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    for (profile = 0; profile < 2; profile++) for (protected_aps = 0; protected_aps < 2; protected_aps++) {
        base(APS_FRAME_DATA);
        packet.aps.profile_id = profile ? 0x0104 : 0;
        packet.aps.source_endpoint = packet.aps.destination_endpoint = profile ? 1 : 0;
        packet.aps.cluster_id = profile ? 6 : 0x0036; packet.length = 3;
        memcpy(packet.payload,"\x69\x3c\x01",3);
        seal_packet(protected_aps ? new_tc : NULL,0,aps_count++,nwk_key,255,incoming++);
        accept(SECURITY_KEYS_EVENT_DATA);
        CHECK(output.length == 3 && !memcmp(output.payload,packet.payload,3) &&
              output.aps.cluster_id == packet.aps.cluster_id && output.aps.profile_id == packet.aps.profile_id);
        packet.nwk.source = 0x1234; packet.nwk.destination = 0;
        CHECK(security_keys_send(&packet,protected_aps,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
        state(SECURITY_KEYS_VERIFIED);
    }
}
static void secured_rejoin(void)
{
    uint32_t old_ceiling;
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    old_ceiling = security_counter_status()->until[0];
    base(0); packet.nwk.type = ED_NWK_COMMAND;
    packet.nwk.source = 0x1234; packet.nwk.destination = 0;
    packet.nwk.flags = NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE;
    memcpy(packet.nwk.source_ieee,config.own_ieee,8);
    memcpy(packet.nwk.destination_ieee,config.tc_ieee,8);
    packet.length = 2; packet.payload[0] = 6; packet.payload[1] = 0x88;
    CHECK(security_keys_send(&packet,0,raw,43,&written,&limits,3) == SECURITY_KEYS_SPACE);
    state(SECURITY_KEYS_VERIFIED);
    CHECK(security_keys_send(&packet,0,raw,44,&written,&limits,3) == SECURITY_KEYS_OK);
    CHECK(written == 44);
    state(SECURITY_KEYS_REJOINING);
    CHECK(ed_wire_inspect(0,raw,written,&meta) == ZIGBEE_SECURITY_OK && meta.counter >= old_ceiling);
    reboot(NULL,SECURITY_KEYS_REJOINING);
    data(3,nwk_key,255); reject(SECURITY_KEYS_CONTEXT);
    base(0); packet.nwk.type = ED_NWK_COMMAND;
    packet.nwk.flags = NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE;
    memcpy(packet.nwk.source_ieee,config.tc_ieee,8);
    memcpy(packet.nwk.destination_ieee,config.own_ieee,8);
    packet.length = 4; packet.payload[0] = 7;
    packet.payload[1] = 0x45; packet.payload[2] = 0x23;
    packet.payload[3] = 1;
    seal_packet(NULL,0,0,nwk_key,255,3); reject(SECURITY_KEYS_CONTEXT);
    packet.payload[3] = 0;
    seal_packet(NULL,0,0,nwk_key,255,3); accept(SECURITY_KEYS_EVENT_REJOINED);
    state(SECURITY_KEYS_VERIFIED); CHECK(status.config.address == 0x2345);
    reboot(NULL,SECURITY_KEYS_VERIFIED); CHECK(status.config.address == 0x2345);
    data(4,nwk_key,255); reject(SECURITY_KEYS_IDENTITY); /* old short no longer admitted */
    base(0); packet.nwk.type = ED_NWK_COMMAND;
    packet.nwk.source = 0x2345; packet.nwk.destination = 0xfffd;
    packet.nwk.flags = NWK_FLAG_SOURCE_IEEE;
    memcpy(packet.nwk.source_ieee,config.own_ieee,8);
    packet.length = 2; packet.payload[0] = 4; packet.payload[1] = 0x20;
    CHECK(security_keys_send(&packet,0,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    state(SECURITY_KEYS_LEFT);
    reboot(NULL,SECURITY_KEYS_LEFT);
}
static void selectors_and_floors(void)
{
    uint8_t good[116], n;
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    base(APS_FRAME_DATA); packet.length = 1;
    seal_packet(new_tc,0,0,nwk_key,255,3); reject(SECURITY_KEYS_REPLAY);
    seal_packet(new_tc,0,1,nwk_key,255,3); accept(SECURITY_KEYS_EVENT_DATA);
    /* Endpoint0 cluster0 is not join-critical but is admitted after verification. */
    CHECK((event & SECURITY_KEYS_EVENT_MASK) == SECURITY_KEYS_EVENT_DATA ||
          (event & SECURITY_KEYS_EVENT_MASK) == SECURITY_KEYS_EVENT_MANAGEMENT);
    seal_packet(new_tc,0,1,nwk_key,255,4); reject(SECURITY_KEYS_REPLAY);
    transport(1,alt_key,0); seal_packet(new_tc,2,2,nwk_key,255,4);
    accept(SECURITY_KEYS_EVENT_NETWORK_KEY);
    base(ED_APS_COMMAND); packet.length = 2; packet.payload[0] = 9; packet.payload[1] = 0;
    seal_packet(NULL,0,0,nwk_key,255,5); reject(SECURITY_KEYS_CONTEXT); /* unicast switch prohibited */
    packet.nwk.destination = 0xffff;
    seal_packet(NULL,0,0,nwk_key,255,5); accept(SECURITY_KEYS_EVENT_SWITCH);
    state(SECURITY_KEYS_VERIFIED); CHECK(status.active_sequence == 0 && !status.newer_pending);
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    data(0xfffffffeUL,nwk_key,255); accept(SECURITY_KEYS_EVENT_DATA);
    memcpy(good,raw,wire_length); n = wire_length;
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    memcpy(raw,good,n); wire_length = n; reject(SECURITY_KEYS_REPLAY);
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    CHECK(security_keys_request(1,2,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    transport(4,bootstrap,0); seal_packet(new_tc,3,1,nwk_key,255,3);
    reject(SECURITY_KEYS_SELECTOR); /* retained old TC material cannot reenter */
}
static void schema_and_arguments(void)
{
    uint8_t record[112], length, code[18];
    unsigned i, before;
    static const uint8_t offsets[] = {0,8,92,100,102,104,108,109,110,111};
    for (i = 0; i < sizeof(offsets); i++) {
        reboot(provisioned,SECURITY_KEYS_PROVISIONED);
        CHECK(security_counter_read(record,112,&length) == SECURITY_COUNTER_OK && length == 112);
        if (offsets[i] == 0 || offsets[i] == 8 || offsets[i] == 92) memset(record+offsets[i],0,8);
        else if (offsets[i] == 100) memset(record+100,255,2);
        else if (offsets[i] == 102) memset(record+102,0,2);
        else record[offsets[i]] = 0xff;
        CHECK(security_counter_save(record,112,3) == SECURITY_COUNTER_OK);
        security_joint_reset(0); CHECK(security_keys_open() == SECURITY_KEYS_FORMAT);
        CHECK(!security_joint_flash_commands());
        CHECK(security_keys_provision(&config,install,10,20,&limits,3) == SECURITY_KEYS_STATE);
    }
    for (i = 0; i < 3; i++) {
        reboot(provisioned,SECURITY_KEYS_PROVISIONED);
        CHECK(security_counter_read(record,112,&length) == SECURITY_COUNTER_OK);
        if (i == 0) record[111] = 1; /* no silent v1 migration */
        else record[109] |= i == 1 ? 0x10 : 0x80; /* no parent state before association */
        CHECK(security_counter_save(record,112,3) == SECURITY_COUNTER_OK);
        security_joint_reset(0); CHECK(security_keys_open() == SECURITY_KEYS_FORMAT);
        CHECK(!security_joint_flash_commands());
    }
    security_joint_reset(1); CHECK(security_keys_open() == SECURITY_KEYS_EMPTY);
    memcpy(code,install,18); code[17] ^= 1;
    CHECK(security_keys_provision(&config,code,10,20,&limits,3) == SECURITY_KEYS_CRYPTO);
    CHECK(!security_joint_flash_commands());
    CHECK(security_keys_provision(&config,install,0xffffffffUL,20,&limits,3) == SECURITY_KEYS_ARGUMENT);
    reboot(snapshot,SECURITY_KEYS_VERIFIED); before = security_joint_flash_commands();
    memset(&output,0xa5,sizeof(output)); saved_output = output;
    CHECK(security_keys_receive(NULL,1,&output,&event,&limits,3) == SECURITY_KEYS_ARGUMENT);
    CHECK(!memcmp(&output,&saved_output,sizeof(output)));
    CHECK(security_keys_receive(raw,1,NULL,&event,&limits,3) == SECURITY_KEYS_ARGUMENT);
    CHECK(security_keys_request(1,2,raw,116,&written,NULL,3) == SECURITY_KEYS_ARGUMENT);
    CHECK(security_keys_send(&packet,2,raw,116,&written,&limits,3) == SECURITY_KEYS_ARGUMENT);
    CHECK(security_joint_flash_commands() == before);
}
static void retained_leave_record(const uint8_t *before)
{
    uint8_t after[112], length, i;
    CHECK(security_counter_read(after,112,&length) == SECURITY_COUNTER_OK && length == 112);
    for (i = 0; i < 112; i++)
        CHECK(after[i] == (i == 109 ? ((before[i] & 0x70u) | SECURITY_KEYS_LEFT) :
                          i == 108 ? (before[i] & (uint8_t)~64u) : before[i]));
}
static void local_leave(void)
{
    uint8_t before[112], length, phase, i, *exact;
    uint32_t nwk_until, aps_until, first_count;
    unsigned blocks, commands;
    security_joint_reset(1); CHECK(security_keys_open() == SECURITY_KEYS_EMPTY);
    memset(raw,0xa5,sizeof(raw)); written = 0xa5;
    CHECK(security_keys_leave(1,raw,116,&written,&limits,3) == SECURITY_KEYS_STATE);
    CHECK(written == 0xa5 && raw[0] == 0xa5 && !security_joint_flash_commands());
    /* Explicit quiet abandonment before/after association, never a frame. */
    for (i = 0; i < 2; i++) {
        reboot(provisioned,SECURITY_KEYS_PROVISIONED);
        if (i) CHECK(security_keys_associate(0x1234,3) == SECURITY_KEYS_OK);
        CHECK(security_counter_read(before,112,&length) == SECURITY_COUNTER_OK && length == 112);
        blocks = security_joint_aes_blocks(); commands = security_joint_flash_commands();
        nwk_until = security_counter_status()->until[0]; aps_until = security_counter_status()->until[1];
        memset(raw,0xa5,sizeof(raw)); written = 0xa5;
        CHECK(security_keys_leave(0x80,raw,0,&written,&limits,3) == SECURITY_KEYS_OK);
        CHECK(written == 0 && raw[0] == 0xa5 && raw[115] == 0xa5);
        CHECK(security_joint_aes_blocks() == blocks && security_joint_flash_commands() == commands+38);
        CHECK(security_counter_status()->until[0] == nwk_until &&
              security_counter_status()->until[1] == aps_until);
        retained_leave_record(before);
        reboot(NULL,SECURITY_KEYS_LEFT);
        CHECK(status.slot_valid == 0 && status.config.address == (i ? 0x1234 : 0xffff));
        CHECK(security_keys_associate(0x1234,3) == SECURITY_KEYS_STATE);
        CHECK(security_keys_provision(&config,install,10,20,&limits,3) == SECURITY_KEYS_STATE);
    }
    /* Received/provisional exchange/verified states all have a real key.
     * Cancellation retains even the abandoned pending TC key and its old
     * operational link-key replay floor, never turning it into a Confirm. */
    for (i = 0; i < 3; i++) {
        phase = i == 0 ? SECURITY_KEYS_RECEIVED :
                i == 1 ? SECURITY_KEYS_WAIT_CONFIRM : SECURITY_KEYS_VERIFIED;
        reboot(i == 0 ? received : i == 1 ? waiting : snapshot,phase);
        CHECK(security_counter_read(before,112,&length) == SECURITY_COUNTER_OK && length == 112);
        nwk_until = security_counter_status()->until[0]; aps_until = security_counter_status()->until[1];
        commands = security_joint_flash_commands(); memset(raw,0xa5,sizeof(raw)); written = 0xa5;
        CHECK(security_keys_leave(0x80,raw,35,&written,&limits,3) == SECURITY_KEYS_SPACE);
        CHECK(written == 0xa5 && raw[0] == 0xa5 && security_joint_flash_commands() == commands);
        state(phase);
        exact = malloc(36); CHECK(exact != NULL);
        CHECK(security_keys_leave(0x80,exact,36,&written,&limits,3) == SECURITY_KEYS_OK && written == 36);
        CHECK(ed_wire_inspect(0,exact,written,&meta) == ZIGBEE_SECURITY_OK);
        first_count = meta.counter;
        CHECK(meta.counter >= nwk_until && meta.key_identifier == 1 && meta.key_sequence == 255);
        material(nwk_key,1,0,255,1);
        CHECK(ed_wire_crypt(1,0,&effective,exact,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
        free(exact);
        CHECK(ed_wire_decode(frame,info.length,&output) == ZIGBEE_SECURITY_OK);
        CHECK(output.nwk.type == ED_NWK_COMMAND && output.nwk.destination == 0xfffd &&
              output.nwk.source == 0x1234 && output.nwk.flags == NWK_FLAG_SOURCE_IEEE &&
              output.nwk.radius == 1 && output.nwk.sequence == 0x80 &&
              !memcmp(output.nwk.source_ieee,config.own_ieee,8) &&
              output.length == 2 && output.payload[0] == 4 && output.payload[1] == 0);
        CHECK(security_counter_status()->until[0] > first_count &&
              security_counter_status()->until[1] == aps_until &&
              security_counter_status()->next[0] == security_counter_status()->until[0] &&
              security_counter_status()->next[1] == aps_until);
        retained_leave_record(before);
        reboot(NULL,SECURITY_KEYS_LEFT); retained_leave_record(before);
        CHECK(security_keys_verify(1,2,raw,116,&written,&limits,3) == SECURITY_KEYS_CONTEXT);
        confirm(new_tc,0,10); reject(SECURITY_KEYS_STATE);
        CHECK(security_keys_leave(0x81,raw,36,&written,&limits,3) == SECURITY_KEYS_OK);
        CHECK(ed_wire_inspect(0,raw,written,&meta) == ZIGBEE_SECURITY_OK && meta.counter > first_count);
        retained_leave_record(before);
    }
}
static void interrupted_leave(void)
{
    static const uint8_t boundaries[] = {1,38,39,76};
    static uint8_t i, kind, before[112], length;
    unsigned n;
    for (i = 0; i < sizeof(boundaries); i++) for (kind = 1; kind <= 3; kind++) {
        reboot(waiting,SECURITY_KEYS_WAIT_CONFIRM);
        CHECK(security_counter_read(before,112,&length) == SECURITY_COUNTER_OK);
        memset(raw,0xa5,sizeof(raw)); written = 0xa5;
        security_joint_cut(&power_cut,boundaries[i],kind,13);
        if (!setjmp(power_cut)) {
            (void)security_keys_leave(0x80,raw,116,&written,&limits,3);
            CHECK(0);
        }
        CHECK(written == 0xa5);
        for (n = 0; n < sizeof(raw); n++) CHECK(raw[n] == 0xa5);
        security_joint_reset(0);
        {
            security_keys_result_t r = security_keys_open();
            if (r == SECURITY_KEYS_OK) {
                CHECK(security_keys_status(&status) == SECURITY_KEYS_OK);
                CHECK(status.phase == SECURITY_KEYS_WAIT_CONFIRM || status.phase == SECURITY_KEYS_LEFT);
                if (status.phase == SECURITY_KEYS_LEFT) retained_leave_record(before);
            } else {
                CHECK(r == SECURITY_KEYS_STORAGE);
                CHECK(security_keys_provision(&config,install,10,20,&limits,3) == SECURITY_KEYS_STATE);
                CHECK(!security_joint_flash_commands());
            }
        }
    }
    reboot(provisioned,SECURITY_KEYS_PROVISIONED);
    memset(raw,0xa5,sizeof(raw)); written = 0xa5;
    security_joint_fail_read(1);
    CHECK(security_keys_leave(0,raw,0,&written,&limits,3) == SECURITY_KEYS_STORAGE);
    CHECK(written == 0xa5 && raw[0] == 0xa5);
}
static void sent_edi(uint8_t expected)
{
    material(nwk_key,1,0,255,1);
    CHECK(ed_wire_crypt(1,0,&effective,raw,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_nwk(frame,info.length,&nwk) == ZIGBEE_SECURITY_OK);
    CHECK(((nwk.header.flags & NWK_FLAG_END_DEVICE_INITIATOR) != 0) == expected);
}
static void request_timeout(uint8_t sequence)
{
    timeout_packet(0); packet.nwk.sequence = sequence;
    CHECK(security_keys_send(&packet,0,raw,45,&written,&limits,3) == SECURITY_KEYS_OK);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.timeout_pending);
}
static void parent_information_edi(void)
{
    static const uint8_t information[] = {0,1,2,4,7,0x80,255};
    uint8_t i, expected, record[112], length;
    unsigned commands;
    /* Only recognized low3 bits affect EDI. Neither caller header assertions
     * nor reserved response bits manufacture parent information. */
    for (i = 0; i < sizeof(information); i++) {
        reboot(snapshot,SECURITY_KEYS_VERIFIED);
        timeout_packet(1); packet.payload[2] = information[i];
        seal_packet(NULL,0,0,nwk_key,255,3); reject(SECURITY_KEYS_CONTEXT);
        timeout_packet(0); packet.nwk.flags |= NWK_FLAG_END_DEVICE_INITIATOR;
        CHECK(security_keys_send(&packet,0,raw,45,&written,&limits,3) == SECURITY_KEYS_OK);
        sent_edi(0);
        reboot(NULL,SECURITY_KEYS_VERIFIED);
        CHECK(status.timeout_pending && !status.parent_information);
        timeout_packet(1); packet.payload[2] = information[i];
        seal_packet(NULL,0,0,nwk_key,255,3); accept(SECURITY_KEYS_EVENT_MANAGEMENT);
        expected = information[i] & 7u;
        reboot(NULL,SECURITY_KEYS_VERIFIED);
        CHECK(!status.timeout_pending && status.parent_information == expected);
        CHECK(security_counter_read(record,112,&length) == SECURITY_COUNTER_OK && length == 112);
        CHECK(record[109] == (uint8_t)(SECURITY_KEYS_VERIFIED | (expected << 4)) && record[111] == 2);
        reject(SECURITY_KEYS_REPLAY);
        timeout_packet(1); packet.payload[2] = expected ^ 7u;
        seal_packet(NULL,0,0,nwk_key,255,4); reject(SECURITY_KEYS_CONTEXT);
        base(APS_FRAME_DATA); packet.nwk.source = 0x1234; packet.nwk.destination = 0;
        packet.aps.profile_id = 0x104; packet.aps.source_endpoint = packet.aps.destination_endpoint = 1;
        packet.length = 1; packet.payload[0] = 0x69;
        packet.nwk.flags = expected ? 0 : NWK_FLAG_END_DEVICE_INITIATOR;
        CHECK(security_keys_send(&packet,1,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
        sent_edi(expected != 0);
        request_timeout(0x40); sent_edi(expected != 0);
        timeout_packet(1); packet.payload[1] = 1; packet.payload[2] = expected ^ 7u;
        seal_packet(NULL,0,0,nwk_key,255,4); accept(SECURITY_KEYS_EVENT_MANAGEMENT);
        state(SECURITY_KEYS_VERIFIED);
        CHECK(!status.timeout_pending && status.parent_information == expected);
    }
    /* Parent state survives every key-exchange phase without verifying the TC
     * by itself, and determines EDI on both APS security commands and Leave. */
    reboot(received,SECURITY_KEYS_RECEIVED);
    request_timeout(1); sent_edi(0);
    timeout_packet(1); packet.payload[2] = 0x86;
    seal_packet(NULL,0,0,nwk_key,255,3); accept(SECURITY_KEYS_EVENT_MANAGEMENT);
    state(SECURITY_KEYS_RECEIVED); CHECK(status.parent_information == 6 && !status.timeout_pending);
    CHECK(security_keys_request(2,0x66,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    sent_edi(1);
    transport(4,new_tc,0); seal_packet(bootstrap,3,1,nwk_key,255,4);
    accept(SECURITY_KEYS_EVENT_TC_KEY);
    reboot(NULL,SECURITY_KEYS_PROVISIONAL); CHECK(status.parent_information == 6);
    CHECK(security_keys_verify(3,0x67,raw,116,&written,&limits,3) == SECURITY_KEYS_OK);
    sent_edi(1);
    confirm(new_tc,0,5); accept(SECURITY_KEYS_EVENT_VERIFIED);
    state(SECURITY_KEYS_VERIFIED); CHECK(status.parent_information == 6);
    request_timeout(4); sent_edi(1);
    CHECK(security_counter_read(record,112,&length) == SECURITY_COUNTER_OK);
    CHECK(security_keys_leave(5,raw,36,&written,&limits,3) == SECURITY_KEYS_OK);
    sent_edi(1); retained_leave_record(record);
    reboot(NULL,SECURITY_KEYS_LEFT);
    CHECK(status.parent_information == 6 && !status.timeout_pending);

    /* Lack of a returned request must not grant a context on ordinary errors. */
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    timeout_packet(0); commands = security_joint_flash_commands();
    security_joint_stall_aes(security_joint_aes_blocks()+1);
    memset(raw,0xa5,sizeof(raw)); written = 0xa5;
    CHECK(security_keys_send(&packet,0,raw,45,&written,&limits,3) == SECURITY_KEYS_CRYPTO);
    CHECK(written == 0xa5 && raw[0] == 0xa5);
    state(SECURITY_KEYS_VERIFIED);
    CHECK(!status.timeout_pending && !status.parent_information &&
          security_joint_flash_commands() == commands+38); /* counter reservation only */
}
static void permit_packet(uint8_t sending)
{
    base(APS_FRAME_DATA); packet.nwk.source = sending ? 0x1234 : 0;
    packet.nwk.destination = 0xfffc; packet.nwk.sequence = 0x22;
    packet.aps.delivery_mode = APS_DELIVERY_BROADCAST;
    packet.aps.cluster_id = 0x0036; packet.aps.counter = 0x51;
    packet.length = 3; memcpy(packet.payload,"\x69\x3c\x01",3);
}
static void outbound_router_broadcast(void)
{
    uint8_t short_form, before[4096];
    uint16_t destination;
    unsigned commands;
    reboot(received,SECURITY_KEYS_RECEIVED);
    permit_packet(1); memset(raw,0xa5,sizeof(raw)); written = 0xa5;
    CHECK(security_keys_send(&packet,0,raw,116,&written,&limits,3) == SECURITY_KEYS_CONTEXT);
    CHECK(written == 0xa5 && raw[0] == 0xa5); /* FFFC is not a phase bypass */
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    request_timeout(1);
    timeout_packet(1); seal_packet(NULL,0,0,nwk_key,255,3);
    accept(SECURITY_KEYS_EVENT_MANAGEMENT);
    state(SECURITY_KEYS_VERIFIED); CHECK(status.parent_information == 2);
    permit_packet(1); memset(raw,0xa5,sizeof(raw)); written = 0xa5;
    CHECK(security_keys_send(&packet,0,raw,36,&written,&limits,3) == SECURITY_KEYS_SPACE);
    CHECK(written == 0xa5 && raw[0] == 0xa5 && raw[115] == 0xa5);
    CHECK(security_keys_send(&packet,0,raw,37,&written,&limits,3) == SECURITY_KEYS_OK && written == 37);
    material(nwk_key,1,0,255,1);
    CHECK(ed_wire_crypt(1,0,&effective,raw,written,frame,116,&info) == ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_decode(frame,info.length,&output) == ZIGBEE_SECURITY_OK);
    CHECK(output.nwk.destination == 0xfffc && output.nwk.source == 0x1234 &&
          output.nwk.flags == NWK_FLAG_END_DEVICE_INITIATOR && output.nwk.sequence == 0x22 &&
          output.aps.type == APS_FRAME_DATA && output.aps.delivery_mode == APS_DELIVERY_BROADCAST &&
          output.aps.cluster_id == 0x0036 && output.aps.profile_id == 0 &&
          !output.aps.destination_endpoint && !output.aps.source_endpoint && !output.aps.flags &&
          output.aps.counter == 0x51 && output.length == 3 &&
          !memcmp(output.payload,"\x69\x3c\x01",3));
    commands = security_joint_flash_commands();
    permit_packet(1); packet.aps.delivery_mode = APS_DELIVERY_UNICAST;
    memset(raw,0xa5,sizeof(raw)); written = 0xa5;
    CHECK(security_keys_send(&packet,0,raw,116,&written,&limits,3) == SECURITY_KEYS_IDENTITY);
    CHECK(written == 0xa5 && raw[0] == 0xa5 && security_joint_flash_commands() == commands);
    for (short_form = 0; short_form < 2; short_form++) {
        permit_packet(1); packet.aps.type = ED_APS_ACK; packet.length = 0;
        packet.aps.delivery_mode = APS_DELIVERY_UNICAST;
        packet.aps.flags = short_form ? APS_FLAG_ACK_FORMAT : 0;
        CHECK(security_keys_send(&packet,0,raw,116,&written,&limits,3) == SECURITY_KEYS_IDENTITY);
        CHECK(written == 0xa5 && raw[0] == 0xa5 && security_joint_flash_commands() == commands);
    }
    permit_packet(1);
    CHECK(security_keys_send(&packet,1,raw,116,&written,&limits,3) == SECURITY_KEYS_UNSUPPORTED);
    CHECK(written == 0xa5 && raw[0] == 0xa5 && security_joint_flash_commands() == commands);
    for (destination = 0xfff8; destination <= 0xfffe; destination++) {
        if (destination == 0xfffc || destination == 0xfffd) continue;
        permit_packet(1); packet.nwk.destination = destination;
        CHECK(security_keys_send(&packet,0,raw,116,&written,&limits,3) == SECURITY_KEYS_IDENTITY);
        CHECK(written == 0xa5 && raw[0] == 0xa5 && security_joint_flash_commands() == commands);
    }
    /* The same valid secured FFFC APDU must NOT be admitted by an ED. */
    permit_packet(0); seal_packet(NULL,0,0,nwk_key,255,4);
    memcpy(before,security_joint_nv(),4096);
    reject(SECURITY_KEYS_IDENTITY);
    CHECK(!memcmp(before,security_joint_nv(),4096) && security_joint_flash_commands() == commands);
    packet.nwk.destination = 0xfffd;
    seal_packet(NULL,0,0,nwk_key,255,4); accept(SECURITY_KEYS_EVENT_DATA);
    packet.nwk.destination = 0xffff;
    seal_packet(NULL,0,0,nwk_key,255,5); accept(SECURITY_KEYS_EVENT_DATA);
}
static void parent_information_cuts(void)
{
    static const uint8_t boundaries[] = {1,38,39,76};
    static uint8_t stage, boundary, kind, pending_image[4096], encoded[116], size;
    unsigned n;
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    request_timeout(1);
    memcpy(pending_image,security_joint_nv(),4096);
    for (stage = 0; stage < 2; stage++)
        for (boundary = 0; boundary < (stage ? 4 : 2); boundary++)
            for (kind = 1; kind <= 3; kind++) {
                reboot(stage ? snapshot : pending_image,SECURITY_KEYS_VERIFIED);
                if (stage) timeout_packet(0);
                else {
                    timeout_packet(1);
                    seal_packet(NULL,0,0,nwk_key,255,3);
                    memcpy(encoded,raw,wire_length); size = wire_length;
                }
                memset(raw,0xa5,sizeof(raw)); written = 0xa5;
                memset(&output,0xa5,sizeof(output)); saved_output = output; event = 0xa5;
                security_joint_cut(&power_cut,boundaries[boundary],kind,13);
                if (!setjmp(power_cut)) {
                    if (stage) (void)security_keys_send(&packet,0,raw,45,&written,&limits,3);
                    else (void)security_keys_receive(encoded,size,&output,&event,&limits,3);
                    CHECK(0);
                }
                CHECK(written == 0xa5 && event == 0xa5 &&
                      !memcmp(&output,&saved_output,sizeof(output)));
                for (n = 0; n < sizeof(raw); n++) CHECK(raw[n] == 0xa5);
                security_joint_reset(0);
                {
                    security_keys_result_t r = security_keys_open();
                    if (r == SECURITY_KEYS_OK) {
                        state(SECURITY_KEYS_VERIFIED);
                        if (stage) CHECK(status.parent_information == 0);
                        else {
                            CHECK((status.timeout_pending && !status.parent_information) ||
                                  (!status.timeout_pending && status.parent_information == 2));
                            if (!status.timeout_pending) {
                                memcpy(raw,encoded,size); wire_length = size;
                                reject(SECURITY_KEYS_REPLAY);
                            }
                        }
                    } else {
                        CHECK(r == SECURITY_KEYS_STORAGE && !security_joint_flash_commands());
                        CHECK(security_keys_provision(&config,install,10,20,&limits,3) == SECURITY_KEYS_STATE);
                    }
                }
            }
}
static void interrupted(void)
{
    /* Every physical command of the 112-byte owner payload save, with
     * before/after cuts and a representative torn word/page. */
    static unsigned boundary, kind;
    static uint8_t encoded[116], size;
    for (boundary = 1; boundary <= 38; boundary++) for (kind = 1; kind <= 3; kind++) {
        reboot(waiting,SECURITY_KEYS_WAIT_CONFIRM);
        confirm(new_tc,0,2); memcpy(encoded,raw,wire_length); size = wire_length;
        memset(&output,0xa5,sizeof(output)); saved_output = output; event = 0xa5;
        security_joint_cut(&power_cut,boundary,(uint8_t)kind,boundary == 1 ? 4096 : 13);
        if (!setjmp(power_cut)) {
            (void)security_keys_receive(encoded,size,&output,&event,&limits,3);
            CHECK(0);
        }
        CHECK(!memcmp(&output,&saved_output,sizeof(output)) && event == 0xa5);
        security_joint_reset(0);
        {
            security_keys_result_t r = security_keys_open();
            if (r == SECURITY_KEYS_OK) {
                CHECK(security_keys_status(&status) == SECURITY_KEYS_OK);
                CHECK(status.phase == SECURITY_KEYS_WAIT_CONFIRM || status.phase == SECURITY_KEYS_VERIFIED);
                if (status.phase == SECURITY_KEYS_VERIFIED) {
                    memcpy(raw,encoded,size); wire_length = size; reject(SECURITY_KEYS_REPLAY);
                }
            } else {
                CHECK(r == SECURITY_KEYS_STORAGE);
                CHECK(security_keys_provision(&config,install,10,20,&limits,3) == SECURITY_KEYS_STATE);
                CHECK(!security_joint_flash_commands());
            }
        }
    }
}
static void failures(void)
{
    unsigned before, n;
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    data(3,nwk_key,255); security_joint_fail_read(1);
    reject(SECURITY_KEYS_STORAGE);
    before = security_joint_flash_commands();
    reject(SECURITY_KEYS_STORAGE); CHECK(before == security_joint_flash_commands());
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    data(3,nwk_key,255);
    security_joint_stall_aes(security_joint_aes_blocks()+1); reject(SECURITY_KEYS_CRYPTO);
    reject(SECURITY_KEYS_CRYPTO);
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    security_joint_nv()[12] ^= 1;
    security_joint_reset(0); CHECK(security_keys_open() == SECURITY_KEYS_STORAGE);
    state(SECURITY_KEYS_FAILED);
    /* Inherited 32 erase attempts/page/power epoch is NOT weakened. */
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    for (n = 3; n < 67; n++) { data(n,nwk_key,255); accept(SECURITY_KEYS_EVENT_DATA); }
    CHECK(security_joint_flash_erases(0) == 32 && security_joint_flash_erases(1) == 32);
    data(67,nwk_key,255); before = security_joint_flash_commands();
    reject(SECURITY_KEYS_STORAGE); CHECK(before == security_joint_flash_commands());
    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    data(3,nwk_key,255);
    memset(&output,0xa5,sizeof(output)); saved_output = output; event = 0xa5;
    security_joint_cut(&power_cut,0,1,0);
    security_joint_stall_flash(1);
    {
        int stopped = setjmp(power_cut);
        if (!stopped) {
            (void)security_keys_receive(raw,wire_length,&output,&event,&limits,3);
            CHECK(0);
        }
        CHECK(stopped == 2);
        CHECK(security_counter_status()->state == SECURITY_COUNTER_BUSY);
        CHECK(!memcmp(&output,&saved_output,sizeof(output)) && event == 0xa5);
    }
}
int main(void)
{
    setup(); handshake(); replay_and_rotation(); framing_and_identity();
    updates_and_leave(); update_policy(); announce_and_retry(); protected_ack();
    timeout_management(); post_key_passthrough();
    secured_rejoin(); selectors_and_floors();
    schema_and_arguments(); local_leave(); interrupted_leave();
    parent_information_edi(); outbound_router_broadcast();
    parent_information_cuts(); interrupted(); failures();
    printf("security keys: %u checks PASS; real AES/CCM/HMAC/counter/journal/flash host composition only\n",checks);
    return 0;
}
#endif
