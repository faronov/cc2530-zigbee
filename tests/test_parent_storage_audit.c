/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Host-only audit of unchanged production storage, with real flash services.
 */
#define main existing_security_corpus_main
#include "test_security_keys.c"
#undef main

static unsigned audit_reads, audit_read_bytes, audit_erases, audit_programs;
static uint8_t image[4096], incoming[116], incoming_length;

flash_result_t __real_flash_nv_read(uint8_t page, uint16_t offset, uint8_t *out, uint8_t length);
flash_write_result_t __real_flash_nv_erase(uint8_t page, uint16_t limit);
flash_write_result_t __real_flash_nv_program(uint8_t page, uint16_t offset, const uint8_t *word,
                                           uint16_t limit);

flash_result_t __wrap_flash_nv_read(uint8_t page, uint16_t offset, uint8_t *out, uint8_t length)
{
    audit_reads++; audit_read_bytes += length;
    return __real_flash_nv_read(page, offset, out, length);
}
flash_write_result_t __wrap_flash_nv_erase(uint8_t page, uint16_t limit)
{
    audit_erases++;
    return __real_flash_nv_erase(page, limit);
}
flash_write_result_t __wrap_flash_nv_program(uint8_t page, uint16_t offset, const uint8_t *word,
                                           uint16_t limit)
{
    audit_programs++;
    return __real_flash_nv_program(page, offset, word, limit);
}
static void clear_audit(void)
{
    audit_reads=audit_read_bytes=audit_erases=audit_programs=0;
}
static void result(const char *name)
{
    printf("{\"operation\":\"%s\",\"page_erases\":%u,\"program_commands\":%u,"
           "\"program_bytes\":%u,\"read_calls\":%u,\"read_bytes\":%u,"
           "\"controller_nominal_us\":%u}\n",name,audit_erases,audit_programs,
           4u*audit_programs,audit_reads,audit_read_bytes,
           20000u*audit_erases+20u*audit_programs);
}
static void prepare(void)
{
    setup(); handshake(); remembered_parent();
    CHECK(status.router_active);
    memcpy(image,security_joint_nv(),sizeof(image));
    base(0); packet.aps.profile_id=0x0104;
    packet.aps.source_endpoint=packet.aps.destination_endpoint=1;
    packet.length=1; packet.payload[0]=0x69;
    router_seal_using(1,0,alt_key,0);
    memcpy(incoming,raw,wire_length); incoming_length=wire_length;
}
static void restore(void)
{
    reboot(image,SECURITY_KEYS_VERIFIED);
    memcpy(raw,incoming,incoming_length); wire_length=incoming_length;
}
static void amplification(void)
{
    uint8_t before[144], after[144], length;
    unsigned i, changed, commands, erases;
    uint32_t count;
    restore();
    CHECK(security_counter_read(before,sizeof(before),&length)==SECURITY_COUNTER_OK);
    commands=security_joint_flash_commands();
    erases=security_joint_flash_erases(0)+security_joint_flash_erases(1);
    clear_audit(); accept(SECURITY_KEYS_EVENT_DATA); result("router_nwk_only_receive");
    CHECK(audit_erases==1 && audit_programs==45 && audit_read_bytes==6505 &&
          audit_reads==283);
    CHECK(security_joint_flash_commands()==commands+46);
    CHECK(security_joint_flash_erases(0)+security_joint_flash_erases(1)==erases+1);
    CHECK(security_counter_read(after,sizeof(after),&length)==SECURITY_COUNTER_OK);
    changed=0;
    for(i=0;i<sizeof(before);i++) if(before[i]!=after[i]) {
        CHECK(i>=126 && i<130); changed++;
    }
    CHECK(changed==1);
    clear_audit(); CHECK(security_counter_take(0,&count,3)==SECURITY_COUNTER_OK);
    result("next_outgoing_nwk_allocation");
    CHECK(audit_erases==1 && audit_programs==45 && audit_read_bytes==6505);
    clear_audit(); CHECK(security_counter_take(1,&count,3)==SECURITY_COUNTER_OK);
    result("next_outgoing_aps_allocation");
    CHECK(audit_erases==1 && audit_programs==45 && audit_read_bytes==6505);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    memcpy(raw,incoming,incoming_length); wire_length=incoming_length;
    clear_audit(); reject(SECURITY_KEYS_REPLAY); result("router_replay_after_reset");
    CHECK(!audit_erases && !audit_programs);
}
static void interruption(void)
{
    static unsigned command, kind, bits, limit, cuts, failures, consumed;
    security_keys_result_t opened;
    uint8_t record[144], length;
    cuts=failures=consumed=0;
    for(command=1;command<=46;command++) for(kind=1;kind<=3;kind++) {
        limit=kind==3 ? (command==1 ? 5u : 33u) : 1u;
        for(bits=0;bits<limit;bits++) {
            restore();
            memset(&output,0xa5,sizeof(output)); saved_output=output; event=0xa5;
            security_joint_cut(&power_cut,command,(uint8_t)kind,
                command==1 ? (bits==4 ? 16384u : bits*4096u) : bits);
            if(!setjmp(power_cut)) {
                (void)security_keys_receive(raw,wire_length,&output,&event,&limits,3);
                CHECK(0);
            }
            cuts++;
            CHECK(!memcmp(&output,&saved_output,sizeof(output)) && event==0xa5);
            security_joint_reset(0); opened=security_keys_open();
            if(opened==SECURITY_KEYS_OK) {
                CHECK(security_counter_read(record,sizeof(record),&length)==SECURITY_COUNTER_OK);
                CHECK(length==144 && record[111]==3 && record[126]<=2);
                if(record[126]==2) {
                    consumed++;
                    memcpy(raw,incoming,incoming_length); wire_length=incoming_length;
                    reject(SECURITY_KEYS_REPLAY);
                } else CHECK(record[126]==1);
            } else { failures++; CHECK(opened==SECURITY_KEYS_STORAGE); }
        }
    }
    CHECK(cuts==1582 && failures>0 && consumed>0);
    printf("baseline router RX: %u command/reset cuts, %u fail-closed outcomes, "
           "%u fully committed replay-rejecting outcomes PASS\n",cuts,failures,consumed);

    restore();
    /* The first read of the journal's post-commit scan: error does not mean
     * that the physically committed replay update can be retried after reset. */
    security_joint_fail_read(4458);
    reject(SECURITY_KEYS_STORAGE);
    CHECK(nv_record_status()->phase==7);
    reboot(NULL,SECURITY_KEYS_VERIFIED);
    memcpy(raw,incoming,incoming_length); wire_length=incoming_length;
    reject(SECURITY_KEYS_REPLAY);
}
int main(void)
{
    prepare(); amplification(); interruption();
    printf("parent storage baseline audit: %u checks PASS; host flash/AES composition, "
           "not MCU or hardware endurance qualification\n",checks);
    return 0;
}
