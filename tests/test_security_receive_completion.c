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
int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    setup(); handshake(); remembered_parent();
    memcpy(completion_initial,security_joint_nv(),sizeof(completion_initial));
    if(argc==1 || !strcmp(argv[1],"nwk")) completion_nwk();
    if(argc==1 || !strcmp(argv[1],"aps")) completion_aps();
    if(argc==1 || !strcmp(argv[1],"adopt")) completion_adopt();
    CHECK(argc==1 || (argc==2 && (!strcmp(argv[1],"nwk") ||
          !strcmp(argv[1],"aps") || !strcmp(argv[1],"adopt"))));
    printf("Security receive completion: %u checks PASS\n",checks);
    return 0;
}
