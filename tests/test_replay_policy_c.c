/* SPDX-License-Identifier: BSD-3-Clause */
#define main existing_security_corpus_main
#include "test_security_keys.c"
#undef main

static unsigned reads, read_bytes, erases, programs;
static uint8_t initial[4096], checkpoint[144], checkpoint_length;

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

flash_result_t __real_flash_nv_read(uint8_t, uint16_t, uint8_t *, uint8_t);
flash_write_result_t __real_flash_nv_erase(uint8_t, uint16_t);
flash_write_result_t __real_flash_nv_program(uint8_t, uint16_t, const uint8_t *, uint16_t);
flash_result_t __wrap_flash_nv_read(uint8_t page, uint16_t offset, uint8_t *out, uint8_t length)
{
    reads++; read_bytes += length;
    return __real_flash_nv_read(page, offset, out, length);
}
flash_write_result_t __wrap_flash_nv_erase(uint8_t page, uint16_t limit)
{
    erases++;
    return __real_flash_nv_erase(page, limit);
}
flash_write_result_t __wrap_flash_nv_program(uint8_t page, uint16_t offset,
                                           const uint8_t *word, uint16_t limit)
{
    programs++;
    return __real_flash_nv_program(page, offset, word, limit);
}
static void clear_cost(void) { reads=read_bytes=erases=programs=0; }
static void record(void)
{
    CHECK(security_counter_read(checkpoint,sizeof(checkpoint),&checkpoint_length)==SECURITY_COUNTER_OK);
    CHECK(checkpoint_length==144 && checkpoint[111]==3);
}
static void ordinary(uint32_t count, const uint8_t *network, uint8_t sequence)
{
    base(0); packet.aps.profile_id=0x0104;
    packet.aps.source_endpoint=packet.aps.destination_endpoint=1;
    packet.length=1; packet.payload[0]=0x69;
    router_seal_using(count,0,network,sequence);
}
static void restore(void) { reboot(initial,SECURITY_KEYS_VERIFIED); clear_cost(); }
static void update(uint32_t count)
{
    update_packet(config.update_id,config.pan);
    router_seal_using(count,0,alt_key,0);
}
static void ordinary_and_checkpoint(void)
{
    unsigned count;
    uint32_t counter;
    restore();
    for(count=1;count<=10000;count++) {
        ordinary(count,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
        reject(SECURITY_KEYS_REPLAY);
    }
    CHECK(!reads && !erases && !programs && !security_joint_flash_commands());
    record(); CHECK(u32(checkpoint+126)==1);
    CHECK(security_counter_take(0,&counter,3)==SECURITY_COUNTER_OK);
    CHECK(erases==1 && programs==45);
    ordinary(10000,alt_key,0); reject(SECURITY_KEYS_REPLAY);
    record(); CHECK(u32(checkpoint+126)==1); /* Counter refill is not an RX checkpoint. */
    clear_cost(); update(10001); accept(SECURITY_KEYS_EVENT_UPDATE);
    CHECK(erases==1 && programs==45 && read_bytes==14697 && reads==539);
    record(); CHECK(u32(checkpoint+126)==10002);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    ordinary(10000,alt_key,0); reject(SECURITY_KEYS_REPLAY);
    update(10001); reject(SECURITY_KEYS_REPLAY);
    ordinary(10002,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    clear_cost(); ordinary(10002,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
    reject(SECURITY_KEYS_REPLAY);
    CHECK(!reads && !erases && !programs);
    printf("Policy C: 10000 protected router frames + 10000 uptime replay drops: "
           "0 erases/programs/reads; permitted above-checkpoint cold replay demonstrated\n");
}
static void slots_and_aps(void)
{
    static const uint8_t third[16]={0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,
                                   0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f};
    restore();
    ordinary(103,nwk_key,255); accept(SECURITY_KEYS_EVENT_DATA);
    ordinary(1,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
    ordinary(103,nwk_key,255); reject(SECURITY_KEYS_REPLAY);
    ordinary(1,alt_key,0); reject(SECURITY_KEYS_REPLAY);
    CHECK(!erases && !programs);
    transport(1,third,1);
    router_seal_counted(2,2,alt_key,0,22); accept(SECURITY_KEYS_EVENT_NETWORK_KEY);
    record(); CHECK(u32(checkpoint+126)==3 && !u32(checkpoint+122));
    clear_cost();
    ordinary(0,third,1); accept(SECURITY_KEYS_EVENT_DATA); /* Durable auto-adoption. */
    CHECK(erases==1 && programs==45);
    record(); CHECK(u32(checkpoint+122)==1 && u32(checkpoint+126)==3);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    ordinary(0,third,1); reject(SECURITY_KEYS_REPLAY);
    ordinary(2,alt_key,0); reject(SECURITY_KEYS_REPLAY);
    clear_cost(); ordinary(3,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
    ordinary(1,third,1); accept(SECURITY_KEYS_EVENT_DATA);
    CHECK(!erases && !programs);
    /* APS durability checkpoints both live slot histories, not stale NV. */
    router_seal_counted(2,1,third,1,23); accept(SECURITY_KEYS_EVENT_DATA);
    record(); CHECK(u32(checkpoint+122)==3 && u32(checkpoint+126)==4 &&
                    u32(checkpoint+88)==24);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    reject(SECURITY_KEYS_REPLAY);
    ordinary(3,alt_key,0); reject(SECURITY_KEYS_REPLAY);
    ordinary(0xfffffffeUL,third,1); accept(SECURITY_KEYS_EVENT_DATA);
    reject(SECURITY_KEYS_REPLAY);
    CHECK(ed_wire_inspect(ZIGBEE_SECURITY_NWK,raw,wire_length,&meta)==ZIGBEE_SECURITY_OK);
    raw[meta.header_length+1]=255; /* FFFFFFFF fails before MIC processing. */
    reject(SECURITY_KEYS_FORMAT);
    CHECK(security_keys_select_parent(1,config.update_id,3)==SECURITY_KEYS_OK);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    ordinary(0xfffffffeUL,third,1); reject(SECURITY_KEYS_REPLAY);
}
static void switch_parent(uint8_t router, uint32_t count)
{
    CHECK(security_keys_select_parent(router,config.update_id,3)==SECURITY_KEYS_OK);
    state(SECURITY_KEYS_VERIFIED);
    base(0); packet.nwk.type=ED_NWK_COMMAND;
    packet.nwk.source=status.config.address; packet.nwk.destination=router?0x4567:0;
    packet.nwk.flags=NWK_FLAG_SOURCE_IEEE|NWK_FLAG_DESTINATION_IEEE;
    memcpy(packet.nwk.source_ieee,config.own_ieee,8);
    memcpy(packet.nwk.destination_ieee,router?router_ieee:config.tc_ieee,8);
    packet.length=2; packet.payload[0]=6; packet.payload[1]=8;
    CHECK(security_keys_send(&packet,0,raw,sizeof(raw),&written,&limits,3)==SECURITY_KEYS_OK);
    base(0); packet.nwk.type=ED_NWK_COMMAND; packet.nwk.source=router?0x4567:0;
    packet.nwk.flags=NWK_FLAG_SOURCE_IEEE|NWK_FLAG_DESTINATION_IEEE;
    memcpy(packet.nwk.source_ieee,router?router_ieee:config.tc_ieee,8);
    memcpy(packet.nwk.destination_ieee,config.own_ieee,8);
    packet.length=4; packet.payload[0]=7; packet.payload[1]=0x34; packet.payload[2]=0x12;
    if(router) router_seal_using(count,0,alt_key,0);
    else seal_packet(NULL,0,0,alt_key,0,count);
    accept(SECURITY_KEYS_EVENT_REJOINED);
}
static void owners_and_leave(void)
{
    restore();
    ordinary(20,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
    switch_parent(0,10); record();
    CHECK(u32(checkpoint+84)==11 && u32(checkpoint+126)==21);
    base(0); packet.aps.profile_id=0x0104;
    packet.aps.source_endpoint=packet.aps.destination_endpoint=1; packet.length=1;
    clear_cost(); seal_packet(NULL,0,0,alt_key,0,11); accept(SECURITY_KEYS_EVENT_DATA);
    CHECK(!erases && !programs);
    switch_parent(1,21); record();
    CHECK(u32(checkpoint+84)==12 && u32(checkpoint+126)==22);
    ordinary(21,alt_key,0); reject(SECURITY_KEYS_REPLAY);
    ordinary(22,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
    CHECK(security_keys_leave(0x80,raw,sizeof(raw),&written,&limits,3)==SECURITY_KEYS_OK);
    reboot(NULL,SECURITY_KEYS_LEFT); record();
    CHECK(u32(checkpoint+84)==12 && u32(checkpoint+126)==23 &&
          !memcmp(checkpoint+8,config.tc_ieee,8));
}
static void checkpoint_cuts(void)
{
    static unsigned command, kind, bits, limit, cuts, failures, committed;
    security_keys_result_t opened;
    cuts=failures=committed=0;
    for(command=1;command<=46;command++) for(kind=1;kind<=3;kind++) {
        limit=kind==3?(command==1?5u:33u):1u;
        for(bits=0;bits<limit;bits++) {
            restore();
            ordinary(1,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
            ordinary(2,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
            update(3);
            memset(&output,0xa5,sizeof(output)); saved_output=output; event=0xa5;
            security_joint_cut(&power_cut,command,(uint8_t)kind,
                command==1?(bits==4?16384u:bits*4096u):bits);
            if(!setjmp(power_cut)) {
                (void)security_keys_receive(raw,wire_length,&output,&event,&limits,3);
                CHECK(0);
            }
            cuts++;
            CHECK(!memcmp(&output,&saved_output,sizeof(output)) && event==0xa5);
            security_joint_reset(0); opened=security_keys_open();
            if(opened==SECURITY_KEYS_OK) {
                state(SECURITY_KEYS_VERIFIED); record();
                CHECK(u32(checkpoint+126)==1 || u32(checkpoint+126)==4);
                if(u32(checkpoint+126)==4) {
                    committed++; update(3); reject(SECURITY_KEYS_REPLAY);
                    ordinary(2,alt_key,0); reject(SECURITY_KEYS_REPLAY);
                } else {
                    ordinary(2,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
                }
            } else { failures++; CHECK(opened==SECURITY_KEYS_STORAGE); }
        }
    }
    CHECK(cuts==1582 && failures && committed);
    restore(); ordinary(1,alt_key,0); accept(SECURITY_KEYS_EVENT_DATA);
    update(2); security_joint_fail_read(12650);
    reject(SECURITY_KEYS_STORAGE);
    CHECK(nv_record_status()->phase==7);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    update(2); reject(SECURITY_KEYS_REPLAY);
    ordinary(1,alt_key,0); reject(SECURITY_KEYS_REPLAY);
    printf("Policy C checkpoint: %u cuts, %u fail-closed, %u committed; "
           "post-physical-commit API failure and reset PASS\n",cuts,failures,committed);
}
int main(void)
{
    CHECK(existing_security_corpus_main()==0);
    setup(); handshake(); remembered_parent();
    memcpy(initial,security_joint_nv(),sizeof(initial));
    ordinary_and_checkpoint(); slots_and_aps(); owners_and_leave(); checkpoint_cuts();
    printf("Policy C: %u checks PASS; actual production flash/AES/key services, "
           "host composition only\n",checks);
    return 0;
}
