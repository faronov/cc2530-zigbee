/* SPDX-License-Identifier: BSD-3-Clause */
#define main existing_security_corpus_main
#include "test_security_keys.c"
#undef main
#include "nwk_aps.h"

static uint8_t completion_initial[4096], completion_record[144], completion_length;
static const uint8_t completion_key[16] = {
    0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,
    0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f
};

static uint32_t completion_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void completion_read(void)
{
    CHECK(security_counter_read(completion_record,sizeof(completion_record),
                                &completion_length)==SECURITY_COUNTER_OK);
    CHECK(completion_length==144 && completion_record[111]==3);
}
static void completion_restore(void)
{
    reboot(completion_initial,SECURITY_KEYS_VERIFIED);
}
static void completion_unsupported(uint32_t count, const uint8_t *network, uint8_t sequence)
{
    base(0); packet.nwk.type=ED_NWK_COMMAND;
    packet.length=1; packet.payload[0]=0xfe;
    router_seal_using(count,0,network,sequence);
}
static void completion_nwk(void)
{
    completion_restore();
    completion_unsupported(400,alt_key,0); reject(SECURITY_KEYS_UNSUPPORTED);
    printf("NWK rejected frame: repeated counter must return REPLAY, not UNSUPPORTED\n");
    reject(SECURITY_KEYS_REPLAY);
    CHECK(!security_joint_flash_commands());
    completion_read(); CHECK(completion_u32(completion_record+126)==1);
}
static void completion_aps(void)
{
    completion_restore();
    base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
    router_seal_counted(400,1,alt_key,0,400); reject(SECURITY_KEYS_UNSUPPORTED);
    completion_read();
    printf("APS rejected frame: durable floor %lu, required 401\n",
           (unsigned long)completion_u32(completion_record+88));
    CHECK(completion_u32(completion_record+88)==401);
    router_seal_counted(401,1,alt_key,0,400); reject(SECURITY_KEYS_REPLAY);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    router_seal_counted(402,1,alt_key,0,400); reject(SECURITY_KEYS_REPLAY);
}
static void completion_adopt(void)
{
    completion_restore();
    transport(1,completion_key,1);
    router_seal_counted(1,2,alt_key,0,22); accept(SECURITY_KEYS_EVENT_NETWORK_KEY);
    completion_unsupported(0,completion_key,1); reject(SECURITY_KEYS_UNSUPPORTED);
    state(SECURITY_KEYS_VERIFIED);
    printf("Newer-slot rejected frame: active sequence %u, required 1\n",
           (unsigned)status.active_sequence);
    CHECK(status.active_sequence==1 && !status.newer_pending);
    completion_read();
    CHECK(completion_u32(completion_record+122)==1 &&
          completion_u32(completion_record+126)==2);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    completion_unsupported(0,completion_key,1); reject(SECURITY_KEYS_REPLAY);
}
static void completion_wrap(uint8_t length, uint32_t count, const uint8_t *network,
                            uint8_t sequence, const uint8_t *sender)
{
    material(network,1,count,sequence,0);
    memcpy(effective.source,sender,8);
    CHECK(ed_wire_crypt(0,0,&effective,frame,length,raw,sizeof(raw),&info)==ZIGBEE_SECURITY_OK);
    wire_length=info.length;
}
static void completion_plain(uint32_t count)
{
    base(APS_FRAME_DATA); packet.aps.profile_id=0x0104;
    packet.aps.source_endpoint=packet.aps.destination_endpoint=1;
    packet.length=1; packet.payload[0]=0x69;
    router_seal_using(count,0,alt_key,0);
}
static void completion_negative(void)
{
    uint8_t length, captured[116], captured_length, sender[8];
    uint32_t before_af, until[2];
    unsigned commands;
    completion_restore(); completion_read(); before_af=completion_u32(completion_record+88);
    until[0]=security_counter_status()->until[0]; until[1]=security_counter_status()->until[1];

    completion_unsupported(400,alt_key,0);
    raw[wire_length-1]^=1; reject(SECURITY_KEYS_AUTH);
    raw[wire_length-1]^=1; reject(SECURITY_KEYS_UNSUPPORTED);
    reject(SECURITY_KEYS_REPLAY);
    completion_unsupported(401,alt_key,42); reject(SECURITY_KEYS_SELECTOR);
    completion_unsupported(401,alt_key,0);
    length=wire_length-18;
    CHECK(ed_wire_nwk(frame,length,&nwk)==ZIGBEE_SECURITY_OK);
    memcpy(sender,router_ieee,8); sender[0]^=0x80;
    completion_wrap(length,401,alt_key,0,sender);
    reject(SECURITY_KEYS_IDENTITY);
    completion_unsupported(401,alt_key,0); reject(SECURITY_KEYS_UNSUPPORTED);
    reject(SECURITY_KEYS_REPLAY);

    base(APS_FRAME_DATA); packet.length=1;
    CHECK(ed_wire_encode(&packet,frame,sizeof(frame),&length)==ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_nwk(frame,length,&nwk)==ZIGBEE_SECURITY_OK);
    frame[nwk.payload_offset]|=4u; /* Reserved APS delivery mode, under valid NWK MIC. */
    completion_wrap(length,402,alt_key,0,router_ieee);
    reject(SECURITY_KEYS_FORMAT); reject(SECURITY_KEYS_REPLAY);
    CHECK(!security_joint_flash_commands());
    completion_read(); CHECK(completion_u32(completion_record+88)==before_af);
    /* RAM-only completion retains the documented cold-reset suffix exposure. */
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    reject(SECURITY_KEYS_FORMAT); reject(SECURITY_KEYS_REPLAY);
    CHECK(!security_joint_flash_commands());

    base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
    router_seal_counted(403,1,alt_key,0,500);
    length=wire_length-18;
    frame[length-1]^=1;
    completion_wrap(length,403,alt_key,0,router_ieee);
    reject(SECURITY_KEYS_AUTH); reject(SECURITY_KEYS_REPLAY);
    completion_read(); CHECK(completion_u32(completion_record+88)==before_af);
    router_seal_counted(404,1,alt_key,0,500);
    captured_length=wire_length; memcpy(captured,raw,wire_length);
    reject(SECURITY_KEYS_UNSUPPORTED); reject(SECURITY_KEYS_REPLAY);
    completion_read();
    CHECK(completion_u32(completion_record+88)==501 && completion_u32(completion_record+126)==405);
    CHECK(security_joint_flash_commands()==46);
    router_seal_counted(405,1,alt_key,0,500); reject(SECURITY_KEYS_REPLAY);
    /* A fresh outer wrapper cannot defeat the independent durable APS floor. */
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    memcpy(raw,captured,captured_length); wire_length=captured_length; reject(SECURITY_KEYS_REPLAY);
    router_seal_counted(405,1,alt_key,0,500); reject(SECURITY_KEYS_REPLAY);
    router_seal_counted(406,1,alt_key,0,501); reject(SECURITY_KEYS_UNSUPPORTED);
    CHECK(security_counter_status()->until[0]==until[0] &&
          security_counter_status()->until[1]==until[1]);

    completion_restore();
    transport(1,completion_key,1); packet.length--;
    router_seal_counted(600,2,alt_key,0,600);
    reject(SECURITY_KEYS_FORMAT); reject(SECURITY_KEYS_REPLAY);
    completion_read();
    CHECK(completion_u32(completion_record+88)==601 && completion_u32(completion_record+126)==601);
    state(SECURITY_KEYS_VERIFIED); CHECK(status.slot_valid==3 && !status.newer_pending);
    reboot(NULL,SECURITY_KEYS_VERIFIED); reject(SECURITY_KEYS_REPLAY);

    reboot(snapshot,SECURITY_KEYS_VERIFIED);
    base(0); packet.nwk.type=ED_NWK_COMMAND; packet.length=1; packet.payload[0]=0xfe;
    seal_packet(NULL,0,0,nwk_key,255,400);
    reject(SECURITY_KEYS_UNSUPPORTED); reject(SECURITY_KEYS_REPLAY);
    CHECK(!security_joint_flash_commands());
    base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
    seal_packet(new_tc,0,400,nwk_key,255,401); reject(SECURITY_KEYS_UNSUPPORTED);
    {
        uint8_t tc_record[112], tc_length;
        CHECK(security_counter_read(tc_record,sizeof(tc_record),&tc_length)==SECURITY_COUNTER_OK);
        CHECK(completion_u32(tc_record+80)==402 && completion_u32(tc_record+88)==401);
    }
    reboot(NULL,SECURITY_KEYS_VERIFIED); reject(SECURITY_KEYS_REPLAY);

    completion_restore();
    completion_plain(0xfffffffeUL); accept(SECURITY_KEYS_EVENT_DATA); reject(SECURITY_KEYS_REPLAY);
    CHECK(ed_wire_inspect(0,raw,wire_length,&meta)==ZIGBEE_SECURITY_OK);
    raw[meta.header_length+1]=255; reject(SECURITY_KEYS_FORMAT);
    CHECK(!security_joint_flash_commands());

    completion_restore();
    completion_unsupported(400,nwk_key,255); reject(SECURITY_KEYS_UNSUPPORTED);
    completion_unsupported(400,alt_key,0); reject(SECURITY_KEYS_UNSUPPORTED);
    completion_unsupported(400,nwk_key,255); reject(SECURITY_KEYS_REPLAY);
    completion_unsupported(400,alt_key,0); reject(SECURITY_KEYS_REPLAY);
    completion_read();
    CHECK(completion_u32(completion_record+122)==103 && completion_u32(completion_record+126)==1);
    commands=security_joint_flash_commands();
    /* A valid counter under an unadmitted TC sender does not consume router history. */
    completion_unsupported(401,alt_key,0);
    completion_wrap(wire_length-18,401,alt_key,0,config.tc_ieee);
    reject(SECURITY_KEYS_IDENTITY);
    completion_unsupported(401,alt_key,0); reject(SECURITY_KEYS_UNSUPPORTED);
    CHECK(security_joint_flash_commands()==commands);
    printf("Completion negatives: MIC/identity/sequence failures, malformed APS, "
           "fresh wrappers, both slots, exhaustion and permitted cold replay PASS\n");
}
static void completion_pending(void)
{
    uint8_t old[112], length;
    uint32_t floor;
    reboot(waiting,SECURITY_KEYS_WAIT_CONFIRM);
    CHECK(security_counter_read(old,sizeof(old),&length)==SECURITY_COUNTER_OK && length==112);
    floor=completion_u32(old+88);
    base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
    seal_packet(new_tc,0,400,nwk_key,255,400);
    reject(SECURITY_KEYS_UNSUPPORTED); reject(SECURITY_KEYS_REPLAY);
    seal_packet(new_tc,0,400,nwk_key,255,401); reject(SECURITY_KEYS_UNSUPPORTED);
    state(SECURITY_KEYS_WAIT_CONFIRM);
    CHECK(security_counter_read(old,sizeof(old),&length)==SECURITY_COUNTER_OK);
    CHECK(completion_u32(old+88)==floor && !(old[108]&32u) && !security_joint_flash_commands());
    base(APS_FRAME_DATA); packet.length=1;
    seal_packet(new_tc,0,400,nwk_key,255,402);
    reject(SECURITY_KEYS_AUTH); reject(SECURITY_KEYS_REPLAY);
    confirm(new_tc,0,403); accept(SECURITY_KEYS_EVENT_VERIFIED);
    state(SECURITY_KEYS_VERIFIED);
    CHECK(security_counter_read(old,sizeof(old),&length)==SECURITY_COUNTER_OK);
    CHECK(completion_u32(old+88)==1 && (old[108]&32u));
    base(APS_FRAME_DATA); packet.length=1;
    seal_packet(bootstrap,0,500,nwk_key,255,404);
    reject(SECURITY_KEYS_AUTH); reject(SECURITY_KEYS_REPLAY);

    reboot(waiting,SECURITY_KEYS_WAIT_CONFIRM);
    base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
    seal_packet(bootstrap,0,400,nwk_key,255,400);
    reject(SECURITY_KEYS_UNSUPPORTED);
    CHECK(security_counter_read(old,sizeof(old),&length)==SECURITY_COUNTER_OK);
    CHECK(completion_u32(old+88)==401 && security_joint_flash_commands()==38);
    state(SECURITY_KEYS_WAIT_CONFIRM); CHECK(!(old[108]&32u));
    reboot(NULL,SECURITY_KEYS_WAIT_CONFIRM);
    seal_packet(bootstrap,0,400,nwk_key,255,401); reject(SECURITY_KEYS_REPLAY);
    confirm(new_tc,0,402); accept(SECURITY_KEYS_EVENT_VERIFIED);
    printf("Pending/retired: no unsupported promotion or application admission; "
           "current A completion and valid outstanding Confirm under B PASS\n");
}
static void completion_adopt_negative(void)
{
    uint8_t length;
    completion_restore();
    transport(1,completion_key,1);
    router_seal_counted(1,2,alt_key,0,22); accept(SECURITY_KEYS_EVENT_NETWORK_KEY);
    completion_unsupported(0,completion_key,1);
    raw[wire_length-1]^=1; reject(SECURITY_KEYS_AUTH);
    state(SECURITY_KEYS_VERIFIED); CHECK(status.active_sequence==0 && status.newer_pending);
    base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
    router_seal_counted(0,1,completion_key,1,500);
    /* Encrypting the outer layer added 18 bytes of NWK security overhead. */
    length=wire_length-18; frame[length-1]^=1;
    completion_wrap(length,0,completion_key,1,router_ieee);
    reject(SECURITY_KEYS_AUTH);
    state(SECURITY_KEYS_VERIFIED); CHECK(status.active_sequence==1 && !status.newer_pending);
    completion_read();
    CHECK(completion_u32(completion_record+122)==1 && completion_u32(completion_record+88)==23);
    reboot(NULL,SECURITY_KEYS_VERIFIED); reject(SECURITY_KEYS_REPLAY);
    printf("Newer slot: bad NWK MIC cannot adopt; valid NWK with bad APS MIC "
           "completes authorized adoption without delivery PASS\n");
}
static void completion_bootstrap(void)
{
    uint8_t record[112], length;
    reboot(provisioned,SECURITY_KEYS_PROVISIONED);
    CHECK(security_keys_associate(0x1234,3)==SECURITY_KEYS_OK);
    transport(1,nwk_key,255); packet.payload[19]^=1;
    seal_packet(bootstrap,2,400,NULL,0,0);
    reject(SECURITY_KEYS_IDENTITY);
    state(SECURITY_KEYS_ASSOCIATED);
    CHECK(security_counter_read(record,sizeof(record),&length)==SECURITY_COUNTER_OK);
    CHECK(completion_u32(record+88)==401 && !record[108]);
    reject(SECURITY_KEYS_REPLAY);
    reboot(NULL,SECURITY_KEYS_ASSOCIATED); reject(SECURITY_KEYS_REPLAY);
    transport(1,nwk_key,255); seal_packet(bootstrap,2,401,NULL,0,0);
    accept(SECURITY_KEYS_EVENT_NETWORK_KEY);
    state(SECURITY_KEYS_RECEIVED);
    CHECK(security_counter_read(record,sizeof(record),&length)==SECURITY_COUNTER_OK);
    CHECK(completion_u32(record+88)==402);
    printf("Initial bootstrap: authenticated rejected transport completes APS "
           "without installing a network key or invalidating staged K2 state PASS\n");
}
static void completion_faults(void)
{
    static uint8_t image[4096], old[144], committed_record[144], mode;
    static unsigned command, kind, bits, limit, cuts, failures, committed, prior;
    security_keys_result_t opened;
    for(mode=0;mode<2;mode++) {
        completion_restore();
        if(mode) {
            transport(1,completion_key,1);
            router_seal_counted(1,2,alt_key,0,22); accept(SECURITY_KEYS_EVENT_NETWORK_KEY);
        }
        memcpy(image,security_joint_nv(),sizeof(image));
        completion_read(); memcpy(old,completion_record,sizeof(old));
        reboot(image,SECURITY_KEYS_VERIFIED);
        completion_unsupported(400,alt_key,0); reject(SECURITY_KEYS_UNSUPPORTED);
        if(mode) completion_unsupported(0,completion_key,1);
        else {
            base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
            router_seal_counted(401,1,alt_key,0,500);
        }
        reject(SECURITY_KEYS_UNSUPPORTED);
        completion_read(); memcpy(committed_record,completion_record,sizeof(committed_record));
        cuts=failures=committed=0;
        for(command=1;command<=46;command++) for(kind=1;kind<=3;kind++) {
            limit=kind==3?(command==1?5u:33u):1u;
            for(bits=0;bits<limit;bits++) {
                reboot(image,SECURITY_KEYS_VERIFIED);
                completion_unsupported(400,alt_key,0); reject(SECURITY_KEYS_UNSUPPORTED);
                if(mode) completion_unsupported(0,completion_key,1);
                else {
                    base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
                    router_seal_counted(401,1,alt_key,0,500);
                }
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
                    state(SECURITY_KEYS_VERIFIED); completion_read();
                    CHECK(!memcmp(completion_record,old,sizeof(old)) ||
                          !memcmp(completion_record,committed_record,sizeof(committed_record)));
                    if(!memcmp(completion_record,committed_record,sizeof(committed_record))) {
                        committed++; reject(SECURITY_KEYS_REPLAY);
                    } else reject(SECURITY_KEYS_UNSUPPORTED);
                } else { failures++; CHECK(opened==SECURITY_KEYS_STORAGE); }
            }
        }
        CHECK(cuts==1582 && failures && committed);
        for(prior=0;prior<2;prior++) {
            reboot(image,SECURITY_KEYS_VERIFIED);
            completion_unsupported(400,alt_key,0); reject(SECURITY_KEYS_UNSUPPORTED);
            if(mode) completion_unsupported(0,completion_key,1);
            else {
                base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
                router_seal_counted(401,1,alt_key,0,500);
            }
            security_joint_fail_read(prior?12650:1);
            reject(SECURITY_KEYS_STORAGE);
            if(prior) CHECK(nv_record_status()->phase==7);
            else CHECK(!security_joint_flash_commands());
            reboot(NULL,SECURITY_KEYS_VERIFIED);
            completion_read();
            CHECK(!memcmp(completion_record,prior?committed_record:old,sizeof(old)));
            reject(prior?SECURITY_KEYS_REPLAY:SECURITY_KEYS_UNSUPPORTED);
        }
        reboot(image,SECURITY_KEYS_VERIFIED);
        if(mode) completion_unsupported(0,completion_key,1);
        else {
            base(ED_APS_COMMAND); packet.length=1; packet.payload[0]=0xfe;
            router_seal_counted(401,1,alt_key,0,500);
        }
        memset(&output,0xa5,sizeof(output)); saved_output=output; event=0xa5;
        security_joint_cut(&power_cut,0,1,0); security_joint_stall_flash(1);
        {
            int stopped=setjmp(power_cut);
            if(!stopped) {
                (void)security_keys_receive(raw,wire_length,&output,&event,&limits,3);
                CHECK(0);
            }
            CHECK(stopped==2 && security_counter_status()->state==SECURITY_COUNTER_BUSY);
            CHECK(!memcmp(&output,&saved_output,sizeof(output)) && event==0xa5);
        }
        printf("Rejected %s completion: %u cuts, %u fail-closed, %u committed; "
               "precommit failure, ambiguous physical commit and busy fail-stop PASS\n",
               mode?"adoption":"APS",cuts,failures,committed);
    }
}
int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    setup(); handshake(); remembered_parent();
    memcpy(completion_initial,security_joint_nv(),sizeof(completion_initial));
    if(argc==1 || !strcmp(argv[1],"nwk")) completion_nwk();
    if(argc==1 || !strcmp(argv[1],"aps")) completion_aps();
    if(argc==1 || !strcmp(argv[1],"adopt")) completion_adopt();
    if(argc==1) {
        completion_negative(); completion_pending(); completion_adopt_negative(); completion_bootstrap();
        completion_faults();
    }
    CHECK(argc==1 || (argc==2 && (!strcmp(argv[1],"nwk") ||
          !strcmp(argv[1],"aps") || !strcmp(argv[1],"adopt"))));
    printf("Security receive completion: %u checks PASS\n",checks);
    return 0;
}
