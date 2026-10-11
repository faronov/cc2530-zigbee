/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Format proof experiment, not production storage or target qualification.
 */
#define main existing_security_corpus_main
#include "test_security_keys.c"
#undef main
#include "replay_log_reference.c"

static uint8_t ref_legacy[4096], ref_image[4096], ref_snapshot[160];
static uint8_t ref_desired[160], ref_output[160], ref_saved_delta[48];
static uint8_t ref_cut_source[4096], ref_cut_before[160];
static uint8_t ref_k3_transition[4096], ref_k3_data[160];

static void ref_advance(unsigned field)
{
    uint8_t offset=replay_ref_offsets[field];
    uint32_t floor=replay_ref_u32(ref_desired+offset);
    CHECK(floor!=0xffffffffUL);
    replay_ref_put(ref_desired+offset,floor+1);
}
static void ref_prepare(void)
{
    setup(); handshake();
    CHECK(security_keys_upgrade_parent(3)==SECURITY_KEYS_OK);
    CHECK(nv_record_load(ref_k3_data,sizeof(ref_k3_data))==NV_RECORD_OK);
    memcpy(ref_k3_transition,security_joint_nv(),sizeof(ref_k3_transition));
    remembered_parent();
    CHECK(nv_record_load(ref_snapshot,sizeof(ref_snapshot))==NV_RECORD_OK);
    CHECK(nv_record_status()->length==160);
    memcpy(ref_legacy,security_joint_nv(),sizeof(ref_legacy));
}
static void ref_migration_boundary(void)
{
    security_joint_reset(0); replay_ref_reset();
    memcpy(security_joint_nv(),snapshot,sizeof(snapshot));
    CHECK(replay_ref_open(NULL)==NV_RECORD_UNSUPPORTED);
    CHECK(!security_joint_flash_commands()); /* Never implicitly migrate K2. */
    security_joint_reset(0); replay_ref_reset();
    memcpy(security_joint_nv(),ref_k3_transition,sizeof(ref_k3_transition));
    CHECK(replay_ref_open(ref_output)==NV_RECORD_OK); /* Old K2, committed new K3. */
    CHECK(!memcmp(ref_output,ref_k3_data,sizeof(ref_output)));
    memcpy(ref_desired,ref_k3_data,sizeof(ref_desired));
    ref_advance(4);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_INVALID_ARGUMENT); /* No known router. */
    memcpy(ref_desired,ref_k3_data,sizeof(ref_desired));
    ref_advance(0);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    CHECK(security_joint_flash_commands()==46);
    security_joint_reset(0); replay_ref_reset();
    CHECK(replay_ref_open(ref_output)==NV_RECORD_OK);
    CHECK(!memcmp(ref_output,ref_desired,sizeof(ref_output)));
}
static void ref_start(void)
{
    security_joint_reset(0);
    memcpy(security_joint_nv(),ref_legacy,sizeof(ref_legacy));
    replay_ref_reset();
    CHECK(replay_ref_open(ref_output)==NV_RECORD_OK);
    CHECK(!memcmp(ref_output,ref_snapshot,sizeof(ref_output)));
    memcpy(ref_desired,ref_snapshot,sizeof(ref_desired));
}
static void ref_fresh(void)
{
    ref_start();
    CHECK(replay_ref_checkpoint(ref_snapshot)==NV_RECORD_OK);
    CHECK(security_joint_flash_commands()==46);
    CHECK(security_joint_flash_erases(0)+security_joint_flash_erases(1)==1);
}
static void ref_mutations(void)
{
    unsigned field, before, i;
    uint32_t floor;
    ref_fresh();
    for(field=0;field<5;field++) {
        ref_advance(field);
        before=security_joint_flash_commands();
        CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
        CHECK(security_joint_flash_commands()==before+12);
        CHECK(security_joint_flash_erases(0)+security_joint_flash_erases(1)==1);
        CHECK(replay_ref_open(ref_output)==NV_RECORD_OK);
        CHECK(!memcmp(ref_output,ref_desired,sizeof(ref_output)));
    }
    /* A routed APS-secured frame advances router NWK and TC APS independently. */
    ref_advance(2); ref_advance(4);
    before=security_joint_flash_commands();
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    CHECK(security_joint_flash_commands()==before+12);
    memcpy(ref_image,security_joint_nv(),sizeof(ref_image));
    security_joint_reset(0); replay_ref_reset();
    CHECK(replay_ref_open(ref_output)==NV_RECORD_OK);
    CHECK(!memcmp(ref_output,ref_desired,sizeof(ref_output)));
    CHECK(replay_ref_admit(ref_snapshot)==NV_RECORD_INVALID_ARGUMENT);
    CHECK(!security_joint_flash_commands());
    ref_advance(4);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    CHECK(security_joint_flash_commands()==46); /* Never append across reset. */
    CHECK(replay_ref_pages[replay_ref_selected].used==0);
    CHECK(security_joint_flash_erases(0)+security_joint_flash_erases(1)==1);
    for(i=0;i<160;i++) {
        uint8_t is_floor=0;
        for(field=0;field<5;field++)
            if(i>=replay_ref_offsets[field] && i<replay_ref_offsets[field]+4u) is_floor=1;
        if(!is_floor) {
            uint8_t saved=ref_desired[i];
            ref_desired[i]^=1;
            before=security_joint_flash_commands();
            CHECK(replay_ref_admit(ref_desired)==NV_RECORD_INVALID_ARGUMENT);
            CHECK(security_joint_flash_commands()==before);
            ref_desired[i]=saved;
        }
    }
    floor=replay_ref_u32(ref_desired+142);
    replay_ref_put(ref_desired+142,0xfffffffeUL);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    replay_ref_put(ref_desired+142,0xffffffffUL);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    replay_ref_put(ref_desired+142,floor);
    before=security_joint_flash_commands();
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_INVALID_ARGUMENT);
    CHECK(security_joint_flash_commands()==before);
}
static void ref_cuts(uint8_t checkpoint)
{
    static unsigned command, kind, bits, limit, commands, base_commands;
    static unsigned cuts, unavailable, old, committed;
    nv_record_result_t opened;
    unsigned i;
    memcpy(ref_cut_before,ref_snapshot,sizeof(ref_cut_before));
    if(checkpoint==2) {
        ref_fresh();
        for(i=0;i<38;i++) {
            ref_advance(2); ref_advance(4);
            CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
        }
        memcpy(ref_cut_source,security_joint_nv(),sizeof(ref_cut_source));
        memcpy(ref_cut_before,ref_desired,sizeof(ref_cut_before));
    }
    commands=checkpoint ? 46u : 12u;
    cuts=unavailable=old=committed=0;
    for(command=1;command<=commands;command++) for(kind=1;kind<=3;kind++) {
        limit=kind==3 ? (checkpoint && command==1 ? 5u : 33u) : 1u;
        for(bits=0;bits<limit;bits++) {
            if(checkpoint==2) {
                security_joint_reset(0); replay_ref_reset();
                memcpy(security_joint_nv(),ref_cut_source,sizeof(ref_cut_source));
                CHECK(replay_ref_open(ref_output)==NV_RECORD_OK);
                CHECK(!memcmp(ref_output,ref_cut_before,sizeof(ref_output)));
                memcpy(ref_desired,ref_cut_before,sizeof(ref_desired));
            } else if(checkpoint) ref_start(); else ref_fresh();
            ref_advance(2); ref_advance(4);
            base_commands=security_joint_flash_commands();
            security_joint_cut(&power_cut,base_commands+command,(uint8_t)kind,
                checkpoint && command==1 ? (bits==4 ? 16384u : bits*4096u) : bits);
            if(!setjmp(power_cut)) {
                (void)replay_ref_admit(ref_desired);
                CHECK(0);
            }
            cuts++;
            security_joint_reset(0); replay_ref_reset();
            memset(ref_output,0xa5,sizeof(ref_output));
            opened=replay_ref_open(ref_output);
            if(opened==NV_RECORD_OK) {
                if(!memcmp(ref_output,ref_desired,sizeof(ref_output))) {
                    committed++;
                    CHECK(replay_ref_admit(ref_cut_before)==NV_RECORD_INVALID_ARGUMENT);
                } else {
                    old++;
                    CHECK(!memcmp(ref_output,ref_cut_before,sizeof(ref_output)));
                    /* Includes an FF/zero-effect torn program: a new erase is
                     * still mandatory, regardless of how the media looks. */
                    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
                    CHECK(security_joint_flash_commands()==46);
                    CHECK(security_joint_flash_erases(0)+security_joint_flash_erases(1)==1);
                }
            } else {
                unavailable++;
                CHECK(opened==NV_RECORD_RECOVERY_REQUIRED || opened==NV_RECORD_CORRUPT ||
                      opened==NV_RECORD_UNSUPPORTED);
                for(i=0;i<sizeof(ref_output);i++) CHECK(ref_output[i]==0xa5);
                CHECK(replay_ref_admit(ref_desired)==opened);
            }
        }
    }
    CHECK(cuts==(checkpoint ? 1582u : 420u) && unavailable && committed && old);
    printf("replay format %s: %u cuts, %u fail-closed, %u old/unpublished, "
           "%u committed/floor-consumed PASS\n",
           checkpoint==2 ? "full-log compaction" : checkpoint ? "checkpoint" : "append",
           cuts,unavailable,old,committed);
}
static void ref_corruption_and_context(void)
{
    static const unsigned semantic_offsets[]={0,4,8,9,10,12,32,36};
    unsigned byte, before;
    uint8_t selected;
    ref_fresh(); ref_advance(4);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    selected=replay_ref_selected;
    memcpy(ref_image,security_joint_nv(),sizeof(ref_image));
    memcpy(ref_saved_delta,ref_image+2048u*selected+192,48);
    for(byte=0;byte<48;byte++) {
        security_joint_reset(0); replay_ref_reset();
        memcpy(security_joint_nv(),ref_image,sizeof(ref_image));
        security_joint_nv()[2048u*selected+192+byte]^=1;
        CHECK(replay_ref_open(NULL)!=NV_RECORD_OK);
        CHECK(!security_joint_flash_commands());
    }
    /* These records have a correct CRC but invalid ordering/domain metadata. */
    for(byte=0;byte<sizeof(semantic_offsets)/sizeof(semantic_offsets[0]);byte++) {
        uint8_t *d;
        security_joint_reset(0); replay_ref_reset();
        memcpy(security_joint_nv(),ref_image,sizeof(ref_image));
        d=security_joint_nv()+2048u*selected+192;
        d[semantic_offsets[byte]]^=1;
        replay_ref_put(d+40,replay_ref_crc(d,40));
        CHECK(replay_ref_open(NULL)!=NV_RECORD_OK);
        CHECK(!security_joint_flash_commands());
    }
    security_joint_reset(0); replay_ref_reset();
    memcpy(security_joint_nv(),ref_image,sizeof(ref_image));
    CHECK(replay_ref_open(NULL)==NV_RECORD_OK);
    /* Identity and key-slot changes cannot be encoded as a replay mutation. */
    ref_desired[128]^=1;
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_INVALID_ARGUMENT);
    CHECK(replay_ref_checkpoint(ref_desired)==NV_RECORD_OK);
    CHECK(replay_ref_pages[replay_ref_selected].used==0);
    memcpy(security_joint_nv()+2048u*replay_ref_selected+192,ref_saved_delta,48);
    security_joint_reset(0); replay_ref_reset();
    CHECK(replay_ref_open(NULL)!=NV_RECORD_OK); /* Old owner's intact CRC is insufficient. */

    ref_fresh(); ref_advance(4);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    memcpy(ref_saved_delta,security_joint_nv()+2048u*replay_ref_selected+192,48);
    ref_desired[32]^=1; ref_desired[122]++;
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_INVALID_ARGUMENT);
    CHECK(replay_ref_checkpoint(ref_desired)==NV_RECORD_OK);
    selected=replay_ref_selected;
    memcpy(security_joint_nv()+2048u*selected+192,ref_saved_delta,48);
    security_joint_reset(0); replay_ref_reset();
    CHECK(replay_ref_open(NULL)!=NV_RECORD_OK); /* Same identity, different key context. */

    ref_fresh(); ref_advance(4);
    before=security_joint_flash_commands();
    /* Both page reads + twelve pre/readback word pairs, then first post-commit read. */
    security_joint_fail_read(4096u+12u*8u+1u);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_READ_FAILED);
    CHECK(security_joint_flash_commands()==before+12);
    CHECK(replay_ref_open(NULL)==NV_RECORD_READ_FAILED);
    security_joint_reset(0); replay_ref_reset();
    CHECK(replay_ref_open(ref_output)==NV_RECORD_OK);
    CHECK(!memcmp(ref_output,ref_desired,sizeof(ref_output)));
    CHECK(replay_ref_admit(ref_snapshot)==NV_RECORD_INVALID_ARGUMENT);

    ref_start(); ref_advance(2); ref_advance(4);
    /* Both checkpoint selection pages, erase preflight/verification,
     * 45 word pre/readbacks, then first post-commit read. */
    security_joint_fail_read(4096u+1u+2048u+45u*8u+1u);
    CHECK(replay_ref_checkpoint(ref_desired)==NV_RECORD_READ_FAILED);
    CHECK(security_joint_flash_commands()==46);
    CHECK(replay_ref_open(NULL)==NV_RECORD_READ_FAILED);
    security_joint_reset(0); replay_ref_reset();
    CHECK(replay_ref_open(ref_output)==NV_RECORD_OK);
    CHECK(!memcmp(ref_output,ref_desired,sizeof(ref_output)));
    CHECK(replay_ref_admit(ref_snapshot)==NV_RECORD_INVALID_ARGUMENT);
}
static void ref_lineage_and_ceilings(void)
{
    unsigned change, before;
    uint8_t selected, *page;
    for(change=0;change<3;change++) {
        ref_fresh();
        selected=replay_ref_selected;
        page=security_joint_nv()+2048u*selected;
        if(change==2) page[4]=3;
        else replay_ref_put(page+8,replay_ref_u32(page+8)+(change ? 1u : (uint32_t)-1));
        replay_ref_put(page+176,replay_ref_crc(page,176));
        security_joint_reset(0); replay_ref_reset();
        CHECK(replay_ref_open(NULL)==(change==2 ? NV_RECORD_UNSUPPORTED : NV_RECORD_CONFLICT));
        CHECK(!security_joint_flash_commands());
    }
    ref_fresh();
    replay_ref_put(ref_desired+8,0xffffffffUL);
    replay_ref_put(ref_desired+12,0xffffffffUL);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_INVALID_ARGUMENT); /* Not a replay delta. */
    CHECK(replay_ref_checkpoint(ref_desired)==NV_RECORD_OK);
    before=security_joint_flash_commands();
    CHECK(replay_ref_checkpoint(ref_snapshot)==NV_RECORD_INVALID_ARGUMENT);
    CHECK(security_joint_flash_commands()==before); /* No outgoing-domain rollback. */

    ref_fresh(); ref_advance(0);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    ref_advance(4);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    page=security_joint_nv()+2048u*replay_ref_selected;
    memset(page+192,255,48); /* A gap before a committed record is not an empty tail. */
    security_joint_reset(0); replay_ref_reset();
    CHECK(replay_ref_open(NULL)!=NV_RECORD_OK);
    CHECK(!security_joint_flash_commands());
}
static void ref_exhaustion(void)
{
    unsigned i, before, page;
    uint32_t generation;
    ref_fresh();
    for(i=0;i<38;i++) {
        ref_advance(4);
        CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    }
    CHECK(replay_ref_pages[replay_ref_selected].used==38);
    generation=replay_ref_pages[replay_ref_selected].last;
    before=security_joint_flash_commands(); ref_advance(4);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_OK);
    CHECK(security_joint_flash_commands()==before+46);
    CHECK(replay_ref_pages[replay_ref_selected].base==generation+1);
    CHECK(replay_ref_pages[replay_ref_selected].used==0);
    security_joint_reset(0); replay_ref_reset();
    CHECK(replay_ref_open(ref_output)==NV_RECORD_OK);
    CHECK(!memcmp(ref_output,ref_desired,sizeof(ref_output)));

    ref_start();
    for(i=0;i<64;i++) CHECK(replay_ref_checkpoint(ref_snapshot)==NV_RECORD_OK);
    before=security_joint_flash_commands();
    CHECK(replay_ref_checkpoint(ref_snapshot)==NV_RECORD_ERASE_LIMIT_REACHED);
    CHECK(security_joint_flash_commands()==before);
    CHECK(replay_ref_attempts[0]==32 && replay_ref_attempts[1]==32);

    ref_start();
    for(page=0;page<2;page++) {
        uint8_t *raw=security_joint_nv()+2048u*page;
        uint32_t current=replay_ref_u32(raw+8);
        uint32_t newest=replay_ref_pages[replay_ref_selected].last;
        replay_ref_put(raw+8,0xfffffffeUL-(newest-current));
        replay_ref_put(raw+2040,replay_ref_crc(raw,2040));
    }
    security_joint_reset(0); replay_ref_reset();
    CHECK(replay_ref_open(NULL)==NV_RECORD_OK);
    CHECK(replay_ref_checkpoint(ref_snapshot)==NV_RECORD_OK);
    CHECK(replay_ref_pages[replay_ref_selected].last==0xffffffffUL);
    before=security_joint_flash_commands(); ref_advance(4);
    CHECK(replay_ref_admit(ref_desired)==NV_RECORD_GENERATION_EXHAUSTED);
    CHECK(replay_ref_checkpoint(ref_desired)==NV_RECORD_GENERATION_EXHAUSTED);
    CHECK(security_joint_flash_commands()==before);
}
int main(void)
{
    ref_prepare(); ref_migration_boundary(); ref_mutations();
    ref_cuts(0); ref_cuts(1); ref_cuts(2);
    ref_corruption_and_context(); ref_lineage_and_ceilings(); ref_exhaustion();
    printf("replay format reference: %u checks PASS; real host flash services, "
           "NOT production integration, MCU execution or physical endurance\n",checks);
    return 0;
}
