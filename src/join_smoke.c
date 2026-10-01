/* SPDX-License-Identifier: BSD-3-Clause */
#include "join_smoke.h"
#include "flash_exec.h"
#include "timebase.h"
#include "zcl_sensor.h"
#include <string.h>

MCU_XDATA bdb_join_t join_smoke_device;
MCU_XDATA mac_tx_interval_t join_smoke_tx;
MCU_XDATA join_smoke_phase_t join_smoke_phase;
MCU_XDATA radio_autoack_config_t join_smoke_initial;
MCU_XDATA mac_epoch_stamp_t join_smoke_clock;
MCU_XDATA uint8_t join_smoke_draws[JOIN_SMOKE_DRAWS];
volatile MCU_XDATA uint8_t join_smoke_mailbox[8];
volatile MCU_XDATA MCU_AT(M0_STATUS_ADDRESS) join_smoke_status_t join_smoke_status;

static MCU_XDATA uint8_t initialized, gate, i;
/* Admission scratch is dead once started; the start loop and RUNNING/READY
 * reuse its storage. */
static MCU_XDATA union {
    struct { uint8_t packet[8], any, opcode; } admit;
    struct { uint8_t backoff, draw, slot, current, result; } serve;
} scratch;
#define BACKOFF scratch.serve.backoff
#define DRAW scratch.serve.draw
#define SLOT scratch.serve.slot
#define CURRENT scratch.serve.current
#define RESULT scratch.serve.result
#define EVENT scratch.serve.result /* start loop only, before RESULT is live */
static MCU_XDATA uint16_t admission;
static MCU_XDATA uint32_t started, previous, now, elapsed, steps;
static const mac_adapter_diagnostics_t MCU_XDATA * MCU_XDATA diagnostic;
static const mac_adapter_observation_t MCU_XDATA * MCU_XDATA observation;

#define S join_smoke_status
#define IN join_smoke_phase.admission
#define DRIVER join_smoke_phase.driver
#define DEVICE join_smoke_device
#define TX join_smoke_tx
#define INITIAL join_smoke_initial
#define CLOCK join_smoke_clock
#define ADAPTER(call) do { S.adapter_result=(uint8_t)(call); \
    if(S.adapter_result!=MAC_ADAPTER_OK) { fault(JS_ADAPTER); return; } } while(0)

static void snapshot(void)
{
    const radio_autoack_diagnostics_t MCU_XDATA *radio;
    diagnostic=mac_adapter_diagnostic();
    S.bdb_phase=DEVICE.phase; S.bdb_reason=DEVICE.result; S.member=DEVICE.member;
    S.adapter_phase=diagnostic->phase; S.held=diagnostic->held;
    S.ready=diagnostic->ready; S.goal=diagnostic->goal; S.normal_rx=diagnostic->normal_rx;
    S.radio_result=diagnostic->radio_result;
    radio=radio_autoack_diagnostic();
    S.radio_phase=radio->phase; S.radio_errors=radio->errors;
    for(i=0;i<4;i++) {
        S.steps[i]=(uint8_t)(steps>>(8u*i));
        S.live[i]=(uint8_t)(diagnostic->live.symbols>>(8u*i));
    }
    for(i=0;i<3;i++) S.elapsed[i]=(uint8_t)(elapsed>>(8u*i));
    S.dropped=DRIVER.dropped;
}

static void fault(uint8_t reason)
{
    S.reason=reason;
    if(gate==JS_RUNNING || gate==JS_READY) snapshot();
    gate=JS_FAULT; S.phase=gate;
}

/* One Basic ModelIdentifier report first: any frame from an uninterviewed
 * device invites the coordinator to interview it; the runtime answers the
 * interview. Then one SYNTHETIC MeasuredValue report per 2^21-symbol slot,
 * temperature in even and humidity in odd slots (each cluster ~every 67 s). */
static void serve(void)
{
    ed_packet_t MCU_XDATA *report=&DEVICE.work.runtime.zdo.application;
    if(DEVICE.phase!=BDB_JOIN_READY) return;
    if(DEVICE.work.runtime.zdo.application_ready) {
        if(zdo_runtime_discard_application(&DEVICE.work.runtime.zdo)!=ZDO_RUNTIME_OK) {
            fault(JS_BDB); return;
        }
        if(S.discarded!=255) S.discarded++;
    }
    if(DEVICE.application_done) {
        if(bdb_join_confirm(&DEVICE,&RESULT)!=BDB_JOIN_OK) { fault(JS_BDB); return; }
        S.announce=RESULT; snapshot(); return;
    }
    if(DEVICE.application_pending) return;
    /* Little-endian bits 21..23 detect a new slot; serve runs every poll. */
    CURRENT=((const uint8_t MCU_XDATA *)&DEVICE.work.runtime.zdo.last)[2]&0xe0u;
    if(S.announce!=255 && CURRENT==SLOT) return;
    zcl_sensor_report(&DEVICE,S.announce==255);
    RESULT=bdb_join_send(&DEVICE,report,0,DRIVER.now);
    memset(report,0,sizeof(*report));
    /* FULL and the APS counter-wrap quarantine are retried in this slot. */
    if(RESULT==BDB_JOIN_FULL || (RESULT==BDB_JOIN_TRANSMIT_FAILED &&
       DEVICE.work.runtime.transport.wrap_wait)) return;
    SLOT=CURRENT;
    if(RESULT!=BDB_JOIN_OK) S.announce=(uint8_t)(0x80u|RESULT);
}

/* READY CSMA backoff only: maximal 8-bit Galois LFSR (x^8+x^6+x^5+x^4+1)
 * seeded from one nonzero qualified draw. Deterministic, NOT entropy;
 * nothing else consumes it. */
static void backoff(void)
{
    BACKOFF=(uint8_t)((BACKOFF>>1)^(BACKOFF&1u?0xb8u:0u));
    DRAW=BACKOFF;
}

static uint8_t progress(void)
{
    now=timebase_read_awake_ticks24();
    elapsed=(now-started)&TIMEBASE_TICKS_MASK;
    if(((now-previous)&TIMEBASE_TICKS_MASK)>=TIMEBASE_HALF_RANGE) { fault(JS_TIME); return 0; }
    if(gate!=JS_READY) {
        if(elapsed>=JOIN_SMOKE_RAW_TICKS) { fault(JS_TIME); return 0; }
        if(steps==JOIN_SMOKE_STEPS) { fault(JS_WORK); return 0; }
    }
    previous=now;
    steps++;
    return 1;
}

static uint8_t input_ready(void)
{
    return IN.qualified_draws && IN.qualified_draws<=JOIN_SMOKE_DRAWS &&
        IN.identity.channel>=11 && IN.identity.channel<=26 &&
        IN.identity.address==0xffff && IN.nwk_floor && IN.aps_floor &&
        IN.join.scan.saved.pan==0xffff && !IN.join.scan.saved.filter &&
        !IN.join.scan.saved.rx_on && IN.join.scan.saved.channel==IN.identity.channel &&
        IN.join.association.saved.pan==0xffff && !IN.join.association.saved.filter &&
        !IN.join.association.saved.rx_on &&
        IN.join.association.saved.channel==IN.identity.channel &&
        IN.join.transport.limits.block_timeout==JOIN_SMOKE_SERVICE_TICKS &&
        IN.join.transport.limits.block_polls==JOIN_SMOKE_SERVICE_POLLS &&
        IN.join.transport.nv_polls==FLASH_EXEC_POLL_MAX;
}

void join_smoke_initialize(void)
{
    if(initialized) return;
    initialized=1;
    for(i=0;i<sizeof(S);i++) ((volatile uint8_t MCU_XDATA *)&S)[i]=0;
    for(i=0;i<8;i++) join_smoke_mailbox[i]=0;
    S.signature[0]='J'; S.signature[1]='S'; S.signature[2]='N'; S.signature[3]='1';
    S.version=2; S.size=sizeof(S); gate=JS_DISARMED; S.phase=gate;
    S.security_result=S.adapter_result=S.mac_result=S.bdb_result=S.driver_result=255;
    S.announce=255;
    S.guards[0]=0x69; S.guards[1]=0x96;
    admission=JOIN_SMOKE_ADMISSION_POLLS; S.remaining[0]=0; S.remaining[1]=1;
}

void join_smoke_poll(void)
{
    if(gate==JS_FAULT) return;
    if(!initialized || S.phase!=gate || S.saw_ready!=(gate==JS_READY) ||
       S.guards[0]!=0x69 || S.guards[1]!=0x96) { fault(JS_INPUT); return; }
    if(gate==JS_RUNNING || gate==JS_READY) {
        if(!S.consumed || S.stage!=(gate==JS_READY?5:4) || !S.bound || !S.draw_limit ||
           S.draw_limit>JOIN_SMOKE_DRAWS || S.draws>S.draw_limit) { fault(JS_INPUT); return; }
        if(!progress()) return;
        S.driver_result=mac_link_driver_step(&DRIVER);
        if(S.driver_result==MAC_LINK_DRIVER_RANDOM) {
            if(gate==JS_READY) backoff();
            else {
                if(S.draws==S.draw_limit) { fault(JS_RANDOM); return; }
                DRAW=join_smoke_draws[S.draws]; join_smoke_draws[S.draws++]=0;
            }
            S.driver_result=mac_link_driver_random(&DRIVER,TX.engine.generation,
                TX.engine.retries,TX.engine.nb,DRAW);
            if(S.driver_result!=MAC_LINK_DRIVER_OK) { fault(JS_DRIVER); return; }
        } else if(S.driver_result==MAC_LINK_DRIVER_FINISHED) {
            fault(JS_BDB); return;
        } else if(S.driver_result!=MAC_LINK_DRIVER_OK && S.driver_result!=MAC_LINK_DRIVER_WAIT) {
            fault(JS_DRIVER); return;
        }
        if(gate==JS_READY) { serve(); return; }
        if(DEVICE.phase==BDB_JOIN_READY) {
            snapshot();
            if(DRIVER.fault || DEVICE.workspace!=BDB_JOIN_WORK_RUNTIME || !DEVICE.member ||
               !DEVICE.work.runtime.transport.ready || S.radio_errors) {
                fault(JS_TERMINAL); return;
            }
            if(S.draws==S.draw_limit) { fault(JS_RANDOM); return; }
            BACKOFF=join_smoke_draws[S.draws]; join_smoke_draws[S.draws++]=0;
            if(!BACKOFF) { fault(JS_RANDOM); return; }
            S.saw_ready=1; S.stage=5; gate=JS_READY; S.phase=gate;
            started=previous=now; steps=0;
        }
        return;
    }
    if(S.consumed || S.stage || S.bound) { fault(JS_INPUT); return; }
    if(gate==JS_DISARMED || gate==JS_ARMED) {
        scratch.admit.any=0;
        for(i=0;i<8;i++) {
            scratch.admit.packet[i]=join_smoke_mailbox[i];
            scratch.admit.any|=scratch.admit.packet[i]; join_smoke_mailbox[i]=0;
        }
        if(!admission) { fault(JS_ADMISSION_EXPIRED); return; }
        if(!scratch.admit.any) { if(!--admission) fault(JS_ADMISSION_EXPIRED); }
        else {
            scratch.admit.opcode=gate==JS_DISARMED?0xa9:0x56;
            if(scratch.admit.packet[0]!=0x4a || scratch.admit.packet[1]!=0xb5 ||
               scratch.admit.packet[2]!=0x4e || scratch.admit.packet[3]!=0xb1 ||
               scratch.admit.packet[4]!=scratch.admit.opcode ||
               scratch.admit.packet[5]!=(uint8_t)~scratch.admit.opcode ||
               scratch.admit.packet[6]!=1 || scratch.admit.packet[7]!=0xfe) { fault(JS_PACKET); return; }
            if(!input_ready()) { fault(JS_INPUT); return; }
            gate++; S.phase=gate; admission=gate==JS_ARMED?JOIN_SMOKE_ADMISSION_POLLS:0;
        }
        S.remaining[0]=(uint8_t)admission; S.remaining[1]=(uint8_t)(admission>>8);
        return;
    }
    if(gate!=JS_ADMITTED || !input_ready()) { fault(JS_INPUT); return; }
    for(i=0;i<8;i++) if(join_smoke_mailbox[i]) { fault(JS_PACKET); return; }
    S.consumed=1; S.draw_limit=IN.qualified_draws; gate=JS_RUNNING; S.phase=gate;
    started=timebase_read_awake_ticks24(); previous=started;
    S.stage=1;
    S.security_result=security_keys_open();
    if(S.security_result!=SECURITY_KEYS_EMPTY) { fault(JS_SECURITY); return; }
#if defined(CC2530_DEFAULT_TC_KEY)
    if(IN.nwk_floor!=1 || IN.aps_floor!=1 ||
       memcmp(IN.join.association.extraction.local,IN.identity.own_ieee,8)) {
        fault(JS_INPUT); return;
    }
#else
    S.security_result=security_keys_provision(&IN.identity,IN.install_code,
        IN.nwk_floor,IN.aps_floor,&IN.join.transport.limits,IN.join.transport.nv_polls);
    if(S.security_result!=SECURITY_KEYS_OK) { fault(JS_SECURITY); return; }
#endif
    for(i=0;i<8;i++) INITIAL.ieee[i]=IN.identity.own_ieee[i];
    INITIAL.pan=INITIAL.short_address=0xffff;
    INITIAL.channel=IN.identity.channel; INITIAL.power=RADIO_AUTOACK_POWER_D5;
    S.stage=2;
    ADAPTER(mac_adapter_init(&INITIAL,JOIN_SMOKE_SERVICE_TICKS,JOIN_SMOKE_SERVICE_POLLS));
    ADAPTER(mac_adapter_close(NULL));
    for(;;) {
        if(!progress()) return;
        S.adapter_result=mac_adapter_step(JOIN_SMOKE_SERVICE_TICKS,JOIN_SMOKE_SERVICE_POLLS);
        if(S.adapter_result==MAC_ADAPTER_WAIT) continue;
        if(S.adapter_result!=MAC_ADAPTER_EVENT) { fault(JS_ADAPTER); return; }
        observation=mac_adapter_observation(); EVENT=observation->kind;
        if(EVENT!=MAC_ADAPTER_CLOSED_EVENT && EVENT!=MAC_ADAPTER_RX_EVENT) {
            fault(JS_ADAPTER); return;
        }
        ADAPTER(mac_adapter_consume(observation->token));
        if(EVENT==MAC_ADAPTER_CLOSED_EVENT) break;
    }
    S.stage=3;
    ADAPTER(mac_adapter_now(JOIN_SMOKE_SERVICE_TICKS,JOIN_SMOKE_SERVICE_POLLS,&CLOCK));
    S.mac_result=mac_tx_interval_init(&TX,0x5a,CLOCK.symbols);
    if(S.mac_result!=MAC_TX_OK) { fault(JS_MAC); return; }
    S.bdb_result=bdb_join_init(&DEVICE,CLOCK.symbols);
    if(S.bdb_result!=BDB_JOIN_OK) { fault(JS_BDB); return; }
    S.bdb_result=bdb_join_start(&DEVICE,&TX,&IN.join,CLOCK.symbols);
    if(S.bdb_result!=BDB_JOIN_OK) { fault(JS_BDB); return; }
    memset(&join_smoke_phase,0,sizeof(join_smoke_phase));
    S.driver_result=mac_link_driver_init(&DRIVER,&DEVICE,&INITIAL,
        JOIN_SMOKE_SERVICE_TICKS,JOIN_SMOKE_SERVICE_POLLS);
    if(S.driver_result!=MAC_LINK_DRIVER_OK) { fault(JS_DRIVER); return; }
    S.bound=1; S.stage=4;
}
