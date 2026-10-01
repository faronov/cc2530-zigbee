/* SPDX-License-Identifier: BSD-3-Clause
 * Fresh-process caller tests over the existing real MMIO/crypto/NV peer model.
 */
#define MAC_LINK_E2E_MAIN reference_e2e_main
#include "test_mac_link_e2e.c"
#include "join_smoke.h"
#include "flash_exec.h"
#include "security_counter.h"
#include <stddef.h>

extern uint8_t flash_exec_work[9];
extern uint8_t aes_used;

static const uint8_t public_install_code[18] = {
    0x83,0xfe,0xd3,0x40,0x7a,0x93,0x97,0x23,0xa5,0xc6,0x39,0xb2,0x69,0x16,0xd5,0x05,0xc3,0xb5
};
static const uint8_t arm_packet[8] = {0x4a,0xb5,0x4e,0xb1,0xa9,0x56,1,0xfe};
static const uint8_t run_packet[8] = {0x4a,0xb5,0x4e,0xb1,0x56,0xa9,1,0xfe};

enum { CALLER_MISSING_KEY=8, CALLER_RADIO_FAULT, CALLER_FLASH_BUSY };
static unsigned caller_case, caller_stopped, caller_radio_fault;
#if defined(CC2530_DEFAULT_TC_KEY)
enum { CALLER_ALL_CHANNELS=11, CALLER_AMBIGUOUS, CALLER_CLOSED, CALLER_OVERFLOW,
       CALLER_WRONG_TC };
static unsigned caller_extra_beacons;

static void discovery_peer(void)
{
    if ((caller_case==CALLER_AMBIGUOUS || caller_case==CALLER_OVERFLOW) &&
        received_frames && caller_extra_beacons<(caller_case==CALLER_AMBIGUOUS?1u:4u) &&
        join_smoke_device.phase==BDB_JOIN_SCANNING &&
        join_smoke_device.work.scan.phase==MAC_SCAN_RX &&
        XR(0x618b) && mode==2 && !ack_active && !packets &&
        !mac_adapter_diagnostic()->held) {
        mac_frame_info_t beacon;
        assert(mac_frame_decode(link_beacon,link_beacon_length,&beacon)==MAC_CODEC_OK);
        link_beacon[beacon.payload_offset+7]++;
        air_frame(link_beacon,link_beacon_length,1);
        caller_extra_beacons++;
    } else peer_progress_for(&join_smoke_device,&join_smoke_tx);
}
#endif
static jmp_buf caller_flash_stop;

#if defined(JOIN_SMOKE_TRACE)
#include "join_smoke_trace.h"
#endif

static void caller_poll(void)
{
#if defined(JOIN_SMOKE_TRACE)
    join_trace_before();
#endif
    assert(!caller_stopped);
    if(caller_case==CALLER_FLASH_BUSY) {
        switch(setjmp(caller_flash_stop)) {
        case 0:
            security_joint_cut(&caller_flash_stop,0,1,0);
            join_smoke_poll();
            break;
        case 2:
            caller_stopped=1;
            break;
        default:
            fputs("Unexpected power cut instead of retained flash stop\n",stderr);
            abort();
        }
        security_joint_cut(NULL,0,1,0);
    } else join_smoke_poll();
#if defined(JOIN_SMOKE_TRACE)
    join_trace_after(!caller_stopped);
#endif
}

static uint16_t caller_address(const volatile void *p)
{
#if defined(JOIN_SMOKE_TRACE)
#define AES_ADDRESS(name) if(p==name) return TARGET_##name;
    AES_ADDRESS(aes_dma0) AES_ADDRESS(aes_dma1) AES_ADDRESS(aes_key)
    AES_ADDRESS(aes_iv) AES_ADDRESS(aes_input) AES_ADDRESS(aes_output)
#undef AES_ADDRESS
#endif
    if(p==&join_smoke_initial) return normal_config;
    if(p==&join_smoke_clock) return adapter_clock_address;
    if(p==&join_smoke_tx) return adapter_owner;
    if(p==&join_smoke_phase.driver.config) return 0x1900;
    if(p==&join_smoke_phase.driver.clock) return 0x1920;
    if(p==&join_smoke_phase.driver.end) return 0x1930;
    if(p==&join_smoke_phase.driver.radio) return 0x1940;
    return joint_address(p);
}

#if defined(JOIN_SMOKE_TRACE)
#define SERVE_SECONDS 110u  /* Basic plus three synthetic reports: short replay. */
#else
#define SERVE_SECONDS 10800u  /* Past one APS counter wrap of reports. */
#endif

/* One acknowledged report: Basic ModelIdentifier first, then alternating
 * SYNTHETIC temperature/humidity MeasuredValue reports, one per slot. */
static void serve_report(unsigned *reports, uint8_t *slot)
{
    uint16_t value=(uint16_t)(link_peer_report[6]|link_peer_report[7]<<8);
    assert(link_peer_report[0]==0x18 && link_peer_report[2]==0x0a && !link_peer_report[4]);
    if(!*reports) {
        assert(!link_peer_report_cluster && link_peer_report[3]==5 && link_peer_report[5]==0x42);
        assert(link_peer_report_length==6+sizeof(zdo_runtime_model) &&
            !memcmp(link_peer_report+7,zdo_runtime_model,sizeof(zdo_runtime_model)-1));
    } else {
        assert(link_peer_report_length==8 && !link_peer_report[3]);
        if(*reports>1) assert(link_peer_report[1]==(uint8_t)(*slot+1u));
        if(link_peer_report[1]&1) {
            assert(link_peer_report_cluster==ZDO_SRV_HUMIDITY_CLUSTER && link_peer_report[5]==0x21);
            assert(value>=4500 && value<=5500 && !(value%25));
        } else {
            assert(link_peer_report_cluster==ZDO_SRV_TEMPERATURE_CLUSTER && link_peer_report[5]==0x29);
            assert(value>=2100 && value<=2490 && !(value%10));
        }
    }
    *slot=link_peer_report[1];
    (*reports)++;
}

/* READY serves without a time bound, a fault or further qualified draws. */
static void serve_ready(unsigned *calls)
{
    unsigned served=0, reports=0, seen=link_peer_app, confirmed=0;
    uint32_t steps;
    uint8_t slot=0, draws=join_smoke_status.draws;
    uint64_t cycles=0, credited=0;
    while(join_smoke_status.phase==JS_READY && join_smoke_status.stage==5 &&
          cycles<(uint64_t)SERVE_SECONDS*32000000u) {
        uint32_t step;
        served++;
#if defined(CC2530_DEFAULT_TC_KEY)
        discovery_peer();
#else
        peer_progress_for(&join_smoke_device,&join_smoke_tx);
#endif
        if(link_peer_app!=seen) { assert(link_peer_app==seen+1u); seen++; serve_report(&reports,&slot); confirmed=served; }
        /* Coarse time only while no MAC transmission owns a stop window. */
        step=(join_smoke_status.announce==255 || join_smoke_tx.engine.phase!=MAC_TX_IDLE ||
            join_smoke_device.work.runtime.transport.active || join_smoke_device.work.runtime.transport.queued ?
            4u : 20000u)*512u;
        advance_clocks(step);
        /* The modeled 32.768-kHz sleep timer otherwise moves only per read. */
        cycles+=step;
        ticks=(uint32_t)(ticks+cycles*32768u/32000000u-credited)&TIMEBASE_TICKS_MASK;
        credited=cycles*32768u/32000000u;
        handoff_tick();
        if(served==200) {
            /* A published non-Basic frame is released, not left to block RX. */
            assert(!join_smoke_device.work.runtime.zdo.application_ready);
            link_peer_application(&join_smoke_device.work.runtime.zdo.application);
            join_smoke_device.work.runtime.zdo.application_ready=1;
        }
        caller_poll();
        if(served==200) assert(!join_smoke_device.work.runtime.zdo.application_ready &&
            !join_smoke_device.work.runtime.zdo.application.length && join_smoke_status.discarded==1);
        (*calls)++;
    }
    if(join_smoke_status.phase!=JS_READY || join_smoke_status.stage!=5)
        fprintf(stderr,"serve phase=%u reason=%u stage=%u BDB=%u/%u driver=%u announce=%u served=%u\n",
            join_smoke_status.phase,join_smoke_status.reason,join_smoke_status.stage,
            join_smoke_device.phase,join_smoke_device.result,join_smoke_status.driver_result,
            join_smoke_status.announce,served);
    assert(join_smoke_status.phase==JS_READY && join_smoke_status.stage==5 && join_smoke_status.saw_ready);
    assert(join_smoke_device.phase==BDB_JOIN_READY && !join_smoke_status.announce);
    assert(join_smoke_status.discarded==1 && !join_smoke_status.dropped && served>200);
    /* One report per 2^21-symbol slot, give or take the slot boundaries. */
    assert(reports+2u>=SERVE_SECONDS*62500u/2097152u && reports<=SERVE_SECONDS*62500u/2097152u+2u);
    assert(join_smoke_status.draws==draws);
    /* The status snapshot follows the last report confirmation. */
    steps=(uint32_t)join_smoke_status.steps[0]|(uint32_t)join_smoke_status.steps[1]<<8|
        (uint32_t)join_smoke_status.steps[2]<<16|(uint32_t)join_smoke_status.steps[3]<<24;
    assert(steps>=confirmed && steps<=served && confirmed);
}

static void prepare_caller(unsigned selected)
{
    unsigned i;
    /* Construct the external synthetic peer, then start a genuinely fresh
     * modeled DUT. No provisioned DUT state is injected into the caller. */
    security_joint_reset(1);
    link_peer_setup(&join_config);
    if(selected==4) {
        /* Provisioning only stages; commit it so the DUT boots non-EMPTY. */
        uint8_t record[112], length;
        assert(security_counter_read(record,sizeof(record),&length)==SECURITY_COUNTER_OK);
        assert(security_counter_save(record,length,FLASH_EXEC_POLL_MAX)==SECURITY_COUNTER_OK);
    }
    security_joint_reset(selected!=4);
    crypto_read=host_mmio_read_hook; crypto_write=host_mmio_write_hook;
    crypto_xread=host_mmio_xread_hook; crypto_xwrite=host_mmio_xwrite_hook;
    crypto_address=host_mmio_xaddress_hook; crypto_cycles=host_mmio_cycles_hook;
    adapter_reset();
    SOC_ENCCS=8; SOC_S0CON=0xa4; SOC_IRCON=0xbe; SOC_MEMCTR=2;
    host_mmio_read_hook=joint_load; host_mmio_write_hook=joint_store;
    host_mmio_xread_hook=joint_xload; host_mmio_xwrite_hook=joint_xstore;
    host_mmio_xaddress_hook=caller_address; host_mmio_cycles_hook=joint_cycles;
    memcpy(config.value.ieee,link_identity.own_ieee,8);
    config.value.pan=config.value.short_address=0xffff; config.value.channel=15;
    config.value.power=RADIO_AUTOACK_POWER_D5;
    scenario=selected==6?1:selected==CALLER_MISSING_KEY?3:0;
    if(selected==CALLER_FLASH_BUSY) security_joint_stall_flash(1);
    join_smoke_initialize();
    join_smoke_phase.admission.join=join_config;
    join_smoke_phase.admission.join.scan.saved.channel=15;
    join_smoke_phase.admission.join.association.saved.channel=15;
    join_smoke_phase.admission.join.transport.limits.block_timeout=JOIN_SMOKE_SERVICE_TICKS;
    join_smoke_phase.admission.join.transport.limits.block_polls=JOIN_SMOKE_SERVICE_POLLS;
    join_smoke_phase.admission.identity=link_identity;
#if defined(CC2530_DEFAULT_TC_KEY)
    config.value.channel=11;
    join_smoke_phase.admission.identity.channel=11;
    join_smoke_phase.admission.join.scan.saved.channel=11;
    join_smoke_phase.admission.join.association.saved.channel=11;
    memset(join_smoke_phase.admission.identity.tc_ieee,0,8);
    memset(join_smoke_phase.admission.identity.extended_pan,0,8);
    join_smoke_phase.admission.identity.pan=0xffff;
    memset(join_smoke_phase.admission.join.association.extraction.coordinator,0,8);
    join_smoke_phase.admission.join.association.extraction.pan=0xffff;
    join_smoke_phase.admission.join.association.extraction.channel=11;
    if(selected==CALLER_ALL_CHANNELS) {
        join_smoke_phase.admission.join.scan.channels=NWK_CANDIDATES_CHANNEL_MASK;
        join_smoke_phase.admission.join.scan.lifetime=1000000;
        join_smoke_phase.admission.join.scan.work=MAC_SCAN_MAX_WORK;
    }
    if(selected==CALLER_CLOSED) {
        mac_frame_info_t beacon;
        assert(mac_frame_decode(link_beacon,link_beacon_length,&beacon)==MAC_CODEC_OK);
        link_beacon[beacon.payload_offset+1]&=0x7f;
    }
    if(selected==CALLER_WRONG_TC) {
        mac_frame_info_t response;
        assert(mac_frame_decode(link_response,link_response_length,&response)==MAC_CODEC_OK);
        link_response[response.payload_offset-8]^=0x40;
    }
#endif
    join_smoke_phase.admission.nwk_floor=join_smoke_phase.admission.aps_floor=1;
    memcpy(join_smoke_phase.admission.install_code,public_install_code,sizeof(public_install_code));
    join_smoke_phase.admission.qualified_draws=selected==3?0:selected==5?1:JOIN_SMOKE_DRAWS;
    for(i=0;i<JOIN_SMOKE_DRAWS;i++) join_smoke_draws[i]=(uint8_t)(17u+73u*i);
}

static void mailbox(const uint8_t *bytes)
{
    unsigned i;
    for(i=0;i<8;i++) join_smoke_mailbox[i]=bytes[i];
    caller_poll();
    for(i=0;i<8;i++) assert(!join_smoke_mailbox[i]);
}

int main(int argc, char **argv)
{
    unsigned selected, calls=0;
    assert(argc==2);
    selected=(unsigned)strtoul(argv[1],NULL,10);
#if defined(CC2530_DEFAULT_TC_KEY)
    assert(selected<=CALLER_WRONG_TC && selected!=7);
#else
    assert(selected<=CALLER_FLASH_BUSY);
    if(selected==7) return reference_e2e_main();
#endif
    caller_case=selected;
    prepare_caller(selected);
#if defined(JOIN_SMOKE_TRACE)
    join_trace_start(selected);
#endif
    assert(join_smoke_status.phase==JS_DISARMED);
    if(selected==1) {
        while(join_smoke_status.phase!=JS_FAULT && calls++<JOIN_SMOKE_ADMISSION_POLLS)
            caller_poll();
        assert(join_smoke_status.reason==JS_ADMISSION_EXPIRED);
    } else if(selected==2) {
        join_smoke_mailbox[0]=0x4a;
        caller_poll();
        assert(join_smoke_status.reason==JS_PACKET);
    } else {
        mailbox(arm_packet);
        if(selected==3) assert(join_smoke_status.reason==JS_INPUT);
        else {
            assert(join_smoke_status.phase==JS_ARMED);
            assert(join_smoke_status.security_result==255 && !tx_started);
            mailbox(run_packet);
            assert(join_smoke_status.phase==JS_ADMITTED);
            assert(join_smoke_status.security_result==255 && !tx_started);
            caller_poll();
            if(selected==4) {
                assert(join_smoke_status.phase==JS_FAULT && join_smoke_status.reason==JS_SECURITY);
                assert(join_smoke_status.security_result==SECURITY_KEYS_OK && !tx_started);
            }
            else {
                if(join_smoke_status.phase!=JS_RUNNING)
                    fprintf(stderr,"initial phase=%u reason=%u stage=%u security=%u adapter=%u mac=%u bdb=%u driver=%u\n",
                        join_smoke_status.phase,join_smoke_status.reason,join_smoke_status.stage,
                        join_smoke_status.security_result,join_smoke_status.adapter_result,join_smoke_status.mac_result,
                        join_smoke_status.bdb_result,join_smoke_status.driver_result);
                assert(join_smoke_status.phase==JS_RUNNING);
                assert(join_smoke_initial.power==RADIO_AUTOACK_POWER_D5 &&
                    XR(0x6190)==RADIO_AUTOACK_POWER_D5);
                epoch_origin=((uint64_t)mac_radio_epoch.periods-mac_radio_epoch.symbols)*512u;
                while(join_smoke_status.phase==JS_RUNNING && !caller_stopped && calls++<JOIN_SMOKE_STEPS) {
                    uint8_t saved_aes_used=aes_used, saved_enccs=SOC_ENCCS;
                    unsigned saved_aes_blocks=security_aes_blocks();
#if defined(CC2530_DEFAULT_TC_KEY)
                    discovery_peer();
#else
                    peer_progress_for(&join_smoke_device,&join_smoke_tx);
#endif
                    assert(aes_used==saved_aes_used && SOC_ENCCS==saved_enccs &&
                        security_aes_blocks()==saved_aes_blocks);
                    advance_clocks(((selected==CALLER_MISSING_KEY
#if defined(CC2530_DEFAULT_TC_KEY)
                        || selected==CALLER_WRONG_TC
#endif
                        ) &&
                        join_smoke_device.phase==BDB_JOIN_WAIT_KEY?50000u:4u)*512u);
                    handoff_tick();
                    if(selected==CALLER_RADIO_FAULT && !caller_radio_fault &&
                       mac_adapter_diagnostic()->phase==MAC_ADAPTER_CLOSING) {
                        SOC_RFERRF=4; caller_radio_fault=1;
                    }
                    handoff_started=mac_adapter_diagnostic()->phase==MAC_ADAPTER_RETIRING &&
                        mac_adapter_diagnostic()->policy==MAC_ADAPTER_KEEP_AUTOACK &&
                        join_smoke_tx.engine.outcome==MAC_TX_ACKED;
                    if(handoff_started) handoff_configured=0;
                    caller_poll();
                    handoff_started=0;
                }
                if(selected==CALLER_FLASH_BUSY) {
                    assert(caller_stopped && security_joint_flash_commands()==1);
                    assert(flash_exec_work[7]==FLASH_EXEC_RAM_STOP && SOC_MEMCTR==0x0a);
                    assert(join_smoke_status.phase==JS_RUNNING && join_smoke_status.bound &&
                        !join_smoke_status.saw_ready);
                } else if(selected==5) {
                    assert(join_smoke_status.phase==JS_FAULT && join_smoke_status.reason==JS_RANDOM);
                    assert(join_smoke_status.draws==1 && join_smoke_phase.driver.random_wait);
                } else if(selected==6
#if defined(CC2530_DEFAULT_TC_KEY)
                    || selected==CALLER_AMBIGUOUS || selected==CALLER_CLOSED || selected==CALLER_OVERFLOW
#endif
                    ) {
                    assert(join_smoke_status.phase==JS_FAULT && join_smoke_status.reason==JS_BDB);
                    assert(join_smoke_device.result==BDB_JOIN_NO_PARENT);
#if defined(CC2530_DEFAULT_TC_KEY)
                    assert(!response_sent && !security_joint_flash_commands());
                    if(selected==CALLER_OVERFLOW) assert(join_smoke_device.scan_result.overflow);
                } else if(selected==CALLER_WRONG_TC) {
                    assert(join_smoke_status.phase==JS_FAULT && !join_smoke_status.saw_ready);
                    assert(response_sent && transport_sent && !join_smoke_device.member);
                    assert(join_smoke_device.result==BDB_JOIN_KEY_TIMEOUT);
#endif
                } else if(selected==CALLER_MISSING_KEY) {
                    assert(join_smoke_status.phase==JS_FAULT && join_smoke_status.reason==JS_BDB);
                    assert(join_smoke_device.phase==BDB_JOIN_FAILED &&
                        join_smoke_device.result==BDB_JOIN_KEY_TIMEOUT);
                    assert(!transport_sent && !join_smoke_device.member &&
                        !join_smoke_status.saw_ready && response_sent==1);
                } else if(selected==CALLER_RADIO_FAULT) {
                    assert(caller_radio_fault && join_smoke_status.phase==JS_FAULT &&
                        join_smoke_status.reason==JS_DRIVER);
                    assert(join_smoke_phase.driver.fault==MAC_LINK_DRIVER_RADIO &&
                        mac_adapter_diagnostic()->phase==MAC_ADAPTER_FAULT);
                    assert(join_smoke_device.owner==&join_smoke_tx && !join_smoke_status.saw_ready);
                } else {
                    if(join_smoke_status.phase!=JS_READY)
                        fprintf(stderr,"terminal phase=%u reason=%u stage=%u BDB=%u/%u driver=%u adapter=%u radio=%u/%u polls=%u timer=%u calls=%u\n",
                            join_smoke_status.phase,join_smoke_status.reason,join_smoke_status.stage,
                            join_smoke_device.phase,join_smoke_device.result,join_smoke_status.driver_result,
                            join_smoke_status.adapter_result,join_smoke_status.radio_result,
                            radio_autoack_diagnostic()->phase,radio_autoack_diagnostic()->polls,
                            mac_time_diagnostic()->result,calls);
                    assert(join_smoke_status.phase==JS_READY && join_smoke_status.saw_ready);
                    assert(join_smoke_device.phase==BDB_JOIN_READY && join_smoke_device.member);
                    assert(join_smoke_status.stage==5 && join_smoke_status.announce==255);
                    serve_ready(&calls);
                    link_peer_verify();
#if defined(CC2530_DEFAULT_TC_KEY)
                    security_keys_status_t keys;
                    assert(security_keys_status(&keys)==SECURITY_KEYS_OK);
                    assert(!memcmp(&keys.config,&link_identity,offsetof(security_keys_config_t,address)));
                    assert(keys.config.address==0x5678 && keys.config.channel==link_identity.channel);
                    assert(join_smoke_device.record.association.source_relation==MAC_ASSOCIATION_SOURCE_UNBOUND);
                    if(selected==CALLER_ALL_CHANNELS)
                        assert(join_smoke_device.scan_result.sent==NWK_CANDIDATES_CHANNEL_MASK &&
                            !join_smoke_device.scan_result.unscanned);
#endif
                }
            }
        }
    }
    if(selected>=1 && selected<=3) {
        assert(join_smoke_status.security_result==255 && !tx_started);
        assert(mac_adapter_diagnostic()->phase==MAC_ADAPTER_COLD);
        assert(security_keys_open()==SECURITY_KEYS_EMPTY);
    }
    /* READY never becomes terminal; only stopped phases are idempotent. */
    if(!caller_stopped && join_smoke_status.phase!=JS_READY) {
        join_smoke_status_t before=join_smoke_status;
        unsigned old_accesses=accesses, old_flash=security_joint_flash_commands();
        join_smoke_phase_t before_phase=join_smoke_phase;
        mac_tx_interval_t before_tx=join_smoke_tx;
        join_smoke_poll();
        assert(!memcmp((const void *)&join_smoke_status,&before,sizeof(before)));
        assert(!memcmp(&join_smoke_phase,&before_phase,sizeof(before_phase)) &&
            !memcmp(&join_smoke_tx,&before_tx,sizeof(before_tx)));
        assert(accesses==old_accesses && security_joint_flash_commands()==old_flash);
    }
#if defined(JOIN_SMOKE_TRACE)
    join_trace_finish();
#else
    printf("Join caller case%u: phase%u reason%u, %u polls, %u draws, %u peer TX, RAM stop%u PASS\n",
        selected,join_smoke_status.phase,join_smoke_status.reason,calls,
        join_smoke_status.draws,link_peer_tx,caller_stopped);
#endif
    return 0;
}
