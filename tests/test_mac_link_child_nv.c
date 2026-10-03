/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Real relocated counter/journal/flash, retained faults, and radio retirement.
 * All storage/data/peripheral events are synthetic. Never target firmware.
 */
#include "mac_link_child_workspace_internal.h"
#include "mac_link_workspace_internal.h"
#include "security_counter.h"
#include "flash_exec.h"
#define MAC_LINK_RAM_MAIN child_ram_main
#include "test_mac_link_ram.c"
#undef MAC_LINK_RAM_MAIN
#include <limits.h>
extern uint8_t flash_exec_work[9];
#define CA child_work_arena
#define WA link_work_arena
static unsigned child_nv_checks;
#define NV_CHECK(e) do { child_nv_checks++; if (!(e)) { \
    fprintf(stderr,"CHILD NV line%u: %s (owner=%u poison=%u counter=%u/%u NV=%u/%u)\n", \
    (unsigned)__LINE__,#e,child_work_phase(),child_work_poisoned(), \
    security_counter_status()->state,security_counter_status()->result, \
    nv_record_status()->result,nv_record_status()->phase); abort(); } } while (0)

static void nv_unchanged(const void *a,const void *b,size_t n) { NV_CHECK(!memcmp(a,b,n)); }
static void nv_filled(const void *p,size_t size,uint8_t value)
{
    const uint8_t *s=p;
    size_t i;
    for(i=0;i<size;i++) NV_CHECK(s[i]==value);
}
static void nv_seed(uint8_t *payload)
{
    unsigned i;
    security_joint_reset(1);
    for(i=0;i<112;i++) payload[i]=(uint8_t)(i^0x35);
    NV_CHECK(security_counter_open()==SECURITY_COUNTER_EMPTY);
    NV_CHECK(security_counter_create(256,768,payload,112,64)==SECURITY_COUNTER_OK);
    NV_CHECK(!security_joint_flash_commands()); /* staged until the first commit */
    NV_CHECK(security_counter_save(payload,112,64)==SECURITY_COUNTER_OK);
    NV_CHECK(security_joint_flash_commands()==38 && nv_record_status()->generation==1);
    NV_CHECK(child_work_clean() && link_work_clean());
}
static void nv_roundtrip_alias(void)
{
    uint8_t payload[112],out[128],n,saved[sizeof(CA)];
    uint32_t taken;
    security_counter_status_t cd;
    nv_record_status_t nd;
    unsigned commands;
    nv_seed(payload);
    NV_CHECK(security_counter_take(0,&taken,64)==SECURITY_COUNTER_OK && taken==256);
    NV_CHECK(security_counter_status()->until[0]==512 && nv_record_status()->generation==2);
    payload[7]^=0x80;
    NV_CHECK(security_counter_save(payload,112,64)==SECURITY_COUNTER_OK);
    NV_CHECK(security_counter_status()->next[0]==512 && nv_record_status()->generation==3);
    NV_CHECK(security_joint_flash_commands()==114);
    memset(out,0xa5,sizeof(out));n=0x9a;
    NV_CHECK(security_counter_read(out,112,&n)==SECURITY_COUNTER_OK && n==112);
    nv_unchanged(payload,out,112);NV_CHECK(child_work_clean());
    cd=*security_counter_status();nd=*nv_record_status();commands=security_joint_flash_commands();
    NV_CHECK(security_counter_read(out,112,out)==SECURITY_COUNTER_OWNERSHIP);
    nv_unchanged(payload,out,112);nv_unchanged(&cd,security_counter_status(),sizeof(cd));
    NV_CHECK(link_work_enter(LW_JOIN_STEP));
    memset(&WA.protocol.parent.join,0x73,sizeof(WA.protocol.parent.join));
    memcpy(saved,&CA,sizeof(CA));
    NV_CHECK(nv_record_load((uint8_t *)&WA,128)==NV_RECORD_BUFFER_OWNERSHIP);
    NV_CHECK(nv_record_load(CA.nv.counter_check,128)==NV_RECORD_BUFFER_OWNERSHIP);
    NV_CHECK(flash_nv_read(0,0,CA.nv.record.chunk,32)==FLASH_BUFFER_OWNERSHIP);
    NV_CHECK(flash_nv_program(0,0,CA.nv.record.word,64)==FLASH_WRITE_BUFFER_OWNERSHIP);
    nv_unchanged(saved,&CA,sizeof(CA));nv_unchanged(&nd,nv_record_status(),sizeof(nd));
    nv_filled(&WA.protocol.parent.join,sizeof(WA.protocol.parent.join),0x73);
    NV_CHECK(link_work_leave(LW_JOIN_STEP));
    NV_CHECK(child_work_enter(CW_COUNTER_SAVE));
    memset(CA.nv.counter_check,0x57,sizeof(CA.nv.counter_check));memcpy(saved,&CA,sizeof(CA));
    NV_CHECK(child_work_grant(CW_RECORD_LOAD));
    NV_CHECK(nv_record_load(CA.nv.counter_check+1,127)==NV_RECORD_BUFFER_OWNERSHIP);
    NV_CHECK(nv_record_load(CA.nv.counter_check,127)==NV_RECORD_BUFFER_OWNERSHIP);
    nv_unchanged(saved,&CA,sizeof(CA));nv_unchanged(&nd,nv_record_status(),sizeof(nd));
    NV_CHECK(child_work_end(CW_RECORD_LOAD,0)==0 && CW_RETURN(CW_COUNTER_SAVE,0)==0);
    NV_CHECK(child_work_clean() && security_joint_flash_commands()==commands);
}
static void nv_repeated_fault(void)
{
    uint8_t saved[sizeof(CA)],out[128],data[128]={0},written=0x7a,nonce[13]={0};
    security_counter_status_t cd=*security_counter_status();
    nv_record_status_t nd=*nv_record_status();
    flash_write_diagnostic_t wd=*flash_write_diagnostic();
    aes_diagnostics_t ad,old_ad;
    ccm_star_limits_t lim={1000,128};
    ccm_star_info_t ci,old_ci;
    zigbee_mmo_info_t hi,old_hi;
    unsigned i,commands=security_joint_flash_commands(),blocks=security_joint_aes_blocks();
    uint8_t poison=child_work_poisoned();
    NV_CHECK(poison && !child_work_phase());
    memcpy(saved,&CA,sizeof(CA));
    memset(out,0xa5,sizeof(out));memset(&ad,0x91,sizeof(ad));old_ad=ad;
    memset(&ci,0x6d,sizeof(ci));old_ci=ci;memset(&hi,0x37,sizeof(hi));old_hi=hi;
    for(i=0;i<3;i++) {
        NV_CHECK(security_counter_open()==cd.result);
        NV_CHECK(security_counter_read(out,112,&written)==cd.result);
        NV_CHECK(nv_record_load(out,128)!=NV_RECORD_OK);
        NV_CHECK(nv_record_replace(data,128,64,0)!=NV_RECORD_OK);
        NV_CHECK(flash_nv_read(0,0,out,32)!=FLASH_OK);
        NV_CHECK(flash_nv_erase(0,64)!=FLASH_WRITE_OK);
        NV_CHECK(aes128_encrypt_block(data,data+16,out,1000,128,&ad)==AES_BUFFER_OWNERSHIP);
        NV_CHECK(zigbee_mmo_hash(data,16,out,1000,128,&hi)==ZIGBEE_MMO_ARGUMENT);
        NV_CHECK(ccm_star_crypt(0,data,nonce,data+16,8,data+32,8,4,out,128,
                              &written,&lim,&ci)==CCM_STAR_ARGUMENT);
        nv_unchanged(saved,&CA,sizeof(CA));nv_unchanged(&cd,security_counter_status(),sizeof(cd));
        nv_unchanged(&nd,nv_record_status(),sizeof(nd));nv_unchanged(&wd,flash_write_diagnostic(),sizeof(wd));
        nv_unchanged(&ad,&old_ad,sizeof(ad));nv_unchanged(&ci,&old_ci,sizeof(ci));nv_unchanged(&hi,&old_hi,sizeof(hi));
        NV_CHECK(written==0x7a && child_work_poisoned()==poison);
        nv_filled(out,sizeof(out),0xa5);
        NV_CHECK(security_joint_flash_commands()==commands && security_joint_aes_blocks()==blocks);
    }
}
static void nv_fault_epochs(void)
{
    uint8_t payload[112],raw[128]={0},out[128],n;
    uint32_t value;
    security_joint_reset(1);
    NV_CHECK(nv_record_replace(raw,128,64,0)==NV_RECORD_OK);
    NV_CHECK(security_joint_flash_commands()==38);
    NV_CHECK(security_counter_open()==SECURITY_COUNTER_FORMAT);
    nv_repeated_fault();
    NV_CHECK(security_keys_open()==SECURITY_KEYS_STORAGE);
    NV_CHECK(child_work_poisoned()); /* Public open is not a reset/recovery. */
    security_joint_reset(0);
    NV_CHECK(!child_work_poisoned() && security_counter_open()==SECURITY_COUNTER_FORMAT);
    NV_CHECK(child_work_poisoned()); /* Full reset cannot repair a bad schema. */

    nv_seed(payload);
    security_joint_fail_read(1);
    NV_CHECK(security_counter_save(payload,112,64)==SECURITY_COUNTER_NV);
    NV_CHECK(nv_record_status()->result==NV_RECORD_READ_FAILED && child_work_poisoned());
    NV_CHECK(CA.nv.reader[0]=='N'); /* Retain actual partially read fault work. */
    nv_repeated_fault();
    security_joint_reset(0);
    NV_CHECK(child_work_clean() && security_counter_open()==SECURITY_COUNTER_OK);
    NV_CHECK(security_counter_read(out,112,&n)==SECURITY_COUNTER_OK && n==112);
    nv_unchanged(out,payload,112);
    NV_CHECK(security_counter_take(0,&value,64)==SECURITY_COUNTER_OK && value==256);
    NV_CHECK(child_work_clean());
}
static void nv_rollback(void)
{
    uint8_t payload[112],older[4096];
    nv_seed(payload);memcpy(older,security_joint_nv(),sizeof(older));
    payload[0]^=1;
    NV_CHECK(security_counter_save(payload,112,64)==SECURITY_COUNTER_OK);
    NV_CHECK(security_counter_status()->generation==2);
    /* Explicit synthetic media rollback, never a private progress-field edit. */
    memcpy(security_joint_nv(),older,sizeof(older));
    NV_CHECK(security_counter_save(payload,112,64)==SECURITY_COUNTER_ROLLBACK);
    NV_CHECK(child_work_poisoned());
    nv_repeated_fault();
}
static uint32_t nv_crc(const uint8_t *p,unsigned n)
{
    uint32_t crc=0xffffffffUL;
    unsigned i,bit;
    for(i=0;i<n;i++) {
        crc^=p[i];
        for(bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1)?0xedb88320UL:0);
    }
    return crc^0xffffffffUL;
}
static void nv_put32(uint8_t *p,uint32_t v)
{
    unsigned i;
    for(i=0;i<4;i++) { p[i]=(uint8_t)v;v>>=8; }
}
static void nv_generation_postcommit(void)
{
    uint8_t data[128],out[128];
    unsigned i,commands;
    security_joint_reset(1);
    for(i=0;i<128;i++) data[i]=(uint8_t)i;
    NV_CHECK(nv_record_replace(data,128,64,0)==NV_RECORD_OK);
    /* Build a valid synthetic committed maximum generation, preserving CRC.
     * Policy rejection is NOT a sticky controller fault in the old contract. */
    nv_put32(security_joint_nv()+8,0xffffffffUL);
    nv_put32(security_joint_nv()+2040,nv_crc(security_joint_nv(),2040));
    security_joint_reset(0);
    NV_CHECK(nv_record_load(out,128)==NV_RECORD_OK && nv_record_status()->generation==0xffffffffUL);
    commands=security_joint_flash_commands();
    NV_CHECK(nv_record_replace(data,128,64,0)==NV_RECORD_GENERATION_EXHAUSTED);
    NV_CHECK(security_joint_flash_commands()==commands && child_work_clean());
    nv_unchanged(data,out,128);
    security_joint_reset(1);
    /* Two scans + erase preflight/verify +37 program preflight/verifies:
     * inject the FIRST post-commit verification read, after all38 commands. */
    security_joint_fail_read(2*FLASH_PAGE_SIZE+1+FLASH_PAGE_SIZE+
                            ((12+128)/4+2)*8+1);
    NV_CHECK(nv_record_replace(data,128,64,0)==NV_RECORD_READ_FAILED);
    NV_CHECK(nv_record_status()->phase==7 && security_joint_flash_commands()==38);
    NV_CHECK(child_work_poisoned());
    memset(out,0xa5,sizeof(out));
    NV_CHECK(nv_record_load(out,128)==NV_RECORD_READ_FAILED);
    nv_filled(out,sizeof(out),0xa5);
    security_joint_reset(0);
    NV_CHECK(nv_record_load(out,128)==NV_RECORD_OK && nv_record_status()->generation==1);
    nv_unchanged(out,data,128);NV_CHECK(child_work_clean());
}
static void nv_failstop(void)
{
    jmp_buf env;
    uint8_t data[128]={0};
    int stopped;
    security_joint_reset(1);
    security_joint_cut(&env,UINT_MAX,1,0); /* Only installs the existing trap. */
    security_joint_stall_flash(1);
    stopped=setjmp(env);
    if(!stopped) {
        (void)nv_record_replace(data,128,4,0);
        NV_CHECK(0); /* The actual RAM fail-stop must NOT return. */
    }
    NV_CHECK(stopped==2 && flash_exec_work[7]==FLASH_EXEC_RAM_STOP);
    NV_CHECK(nv_record_status()->result==NV_RECORD_PENDING && nv_record_status()->phase==3);
    NV_CHECK(child_work_phase()==CW_WRITE && !child_work_clean());
    /* No ordinary CPU continuation/retirement after RAM fail-stop is claimed. */
    security_joint_reset(0);
    NV_CHECK(child_work_clean() && security_counter_open()==SECURITY_COUNTER_EMPTY);
}
static void nv_radio_retirement(void)
{
    uint8_t payload[112],n,saved[sizeof(CA)];
    security_counter_status_t cd;
    unsigned calls,commands,before_accesses,retired=0;
    scenario=STOP_ARM;e2e_checks=e2e_steps=0;cold_start();
    while(!(device.phase==BDB_JOIN_ANNOUNCING &&
            driver.action.kind==BDB_JOIN_ACTION_TX &&
            driver.action.data.tx.control.kind==NWK_APS_ACTION_ARM)) {
        NV_CHECK(e2e_steps<2000 && !driver.fault);
        tick_driver();
    }
    NV_CHECK(security_counter_read(payload,112,&n)==SECURITY_COUNTER_OK && n==112);
    security_joint_fail_read(1);
    NV_CHECK(security_counter_save(payload,112,64)==SECURITY_COUNTER_NV);
    nv_repeated_fault();
    cd=*security_counter_status();memcpy(saved,&CA,sizeof(CA));
    commands=security_joint_flash_commands();before_accesses=accesses;
    calls=0;
    /* Existing STOP_ARM helper calls the real public nwk_aps_stop. Actual
     * driver/adapter/MAC/MMIO finish the already-correlated retirement. */
    while(device.phase<BDB_JOIN_FAILED && !driver.fault) {
        NV_CHECK(++calls<300);tick_driver();
        if(race_disarmed && !retired) {
            NV_CHECK(mac_adapter_diagnostic()->phase==MAC_ADAPTER_OFF &&
                     !mac_adapter_diagnostic()->ready && !XR(0x618b));
            retired=1;
        }
        NV_CHECK(child_work_poisoned());
        nv_unchanged(saved,&CA,sizeof(CA));nv_unchanged(&cd,security_counter_status(),sizeof(cd));
    }
    NV_CHECK(!driver.fault && retired && race_started && race_disarmed && race_completed);
    NV_CHECK(device.phase==BDB_JOIN_FAILED && adapter_tx.engine.phase==MAC_TX_IDLE);
    NV_CHECK(mac_link_driver_step(&driver)==MAC_LINK_DRIVER_FINISHED);
    /* The accepted driver can reopen its independent RX lease between MAC
     * disarm and terminal BDB publication. FINISHED is not RX shutdown.
     * The foreground owner now explicitly closes that real remaining lease;
     * this test neither changes the driver nor silently claims automatic OFF. */
    if(mac_adapter_diagnostic()->phase==MAC_ADAPTER_RX) {
        NV_CHECK(mac_adapter_close(NULL)==MAC_ADAPTER_OK);
        while(mac_adapter_diagnostic()->phase!=MAC_ADAPTER_OFF ||
              mac_adapter_diagnostic()->ready) {
            mac_adapter_result_t r;
            NV_CHECK(++calls<300);advance_clocks(4u*512u);handoff_tick();
            r=mac_adapter_step(bound,cap);
            if(r==MAC_ADAPTER_EVENT) {
                NV_CHECK(mac_adapter_observation()->kind==MAC_ADAPTER_CLOSED_EVENT);
                NV_CHECK(mac_adapter_consume(mac_adapter_observation()->token)==MAC_ADAPTER_OK);
            } else NV_CHECK(r==MAC_ADAPTER_WAIT || r==MAC_ADAPTER_OK);
            nv_unchanged(saved,&CA,sizeof(CA));nv_unchanged(&cd,security_counter_status(),sizeof(cd));
        }
    }
    NV_CHECK(mac_adapter_diagnostic()->phase==MAC_ADAPTER_OFF &&
             !mac_adapter_diagnostic()->held && !mac_adapter_diagnostic()->ready && !XR(0x618b));
    NV_CHECK(accesses>before_accesses && security_joint_flash_commands()==commands);
    NV_CHECK(child_work_poisoned() && link_work_clean());
    security_joint_reset(0); /* Complete modeled epoch, not driver re-init. */
    NV_CHECK(child_work_clean() && security_counter_open()==SECURITY_COUNTER_OK);
    printf("CHILD returning NV fault: real radio/MAC retirement in%u progress steps PASS.\n",calls);
}
int main(void)
{
    nv_roundtrip_alias();nv_fault_epochs();nv_rollback();nv_generation_postcommit();
    nv_failstop();nv_radio_retirement();
    printf("CHILD relocated NV/fault/retirement: %u checks PASS.\n",child_nv_checks);
    return 0;
}
