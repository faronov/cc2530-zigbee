/* SPDX-License-Identifier: BSD-3-Clause */
#include "mac_smoke.h"
#include "timebase.h"
#include <string.h>

volatile MCU_XDATA mac_smoke_status_t mac_smoke_status;
volatile MCU_XDATA uint8_t mac_smoke_mailbox[8];
MCU_XDATA radio_autoack_config_t mac_smoke_config;
MCU_XDATA mac_tx_interval_t mac_smoke_tx;
MCU_XDATA mac_tx_interval_action_t mac_smoke_action;
MCU_XDATA mac_tx_interval_event_t mac_smoke_random;
MCU_XDATA mac_epoch_stamp_t mac_smoke_clock;
/* FCS-free DATA, compressed PAN1234 / destinationffff / source5678.
 * submit replaces placeholder DSN00 with the initialized owner DSN5a. */
static const MCU_CODE uint8_t body[MAC_SMOKE_LENGTH] = {
    0x41,0x88,0,0x34,0x12,0xff,0xff,0x78,0x56,'M','A','C','1'
};
static const MCU_CODE uint8_t draws[5] = {7,11,19,23,29};
static MCU_XDATA uint8_t initialized, gate, packet[8], i, any, opcode, token;
static MCU_XDATA uint16_t admission, steps;
static MCU_XDATA uint32_t started, previous, now, elapsed;
static const mac_adapter_diagnostics_t MCU_XDATA * MCU_XDATA diag;
static const mac_adapter_observation_t MCU_XDATA * MCU_XDATA event;
#define S mac_smoke_status
#define TX mac_smoke_tx
#define ACT mac_smoke_action
#define RND mac_smoke_random
#define A(call) do { S.adapter_result=(uint8_t)(call); if(S.adapter_result!=MAC_ADAPTER_OK) { fault(MS_ADAPTER); return; } } while(0)
#define M(call) do { S.mac_result=(uint8_t)(call); if(S.mac_result!=MAC_TX_OK) { fault(MS_MAC); return; } } while(0)
static void snapshot(void)
{
    const radio_autoack_diagnostics_t MCU_XDATA *radio;
    diag=mac_adapter_diagnostic();
    S.radio_result=diag->radio_result; S.adapter_phase=diag->phase;
    S.mac_phase=TX.engine.phase; S.transmissions=TX.engine.transmissions; S.mac_outcome=TX.engine.outcome;
    S.held=diag->held; S.ready=diag->ready; S.pending=TX.engine.pending;
    S.goal=diag->goal; S.normal_rx=diag->normal_rx;
    for(i=0;i<4;i++) S.live[i]=(uint8_t)(diag->live.symbols>>(8u*i));
    S.steps[0]=(uint8_t)steps; S.steps[1]=(uint8_t)(steps>>8);
    for(i=0;i<3;i++) S.elapsed[i]=(uint8_t)(elapsed>>(8u*i));
    radio=radio_autoack_diagnostic();
    S.radio_phase=radio->phase; S.radio_detail_result=radio->result;
    S.radio_errors=radio->errors; S.radio_flags0=radio->flags0; S.radio_flags1=radio->flags1;
}
static void fault(uint8_t reason)
{
    if(gate==MS_RUNNING) snapshot();
    S.reason=reason; gate=MS_FAULT; S.phase=gate;
}
static uint8_t progress(void)
{
    now=timebase_read_awake_ticks24();
    elapsed=(now-started)&TIMEBASE_TICKS_MASK;
    if(elapsed>=MAC_SMOKE_RAW_TICKS || ((now-previous)&TIMEBASE_TICKS_MASK)>=TIMEBASE_HALF_RANGE) {
        fault(MS_TIME); return 0;
    }
    previous=now;
    if(steps==MAC_SMOKE_STEPS) { fault(MS_WORK); return 0; }
    steps++; return 1;
}
void mac_smoke_initialize(void)
{
    if(initialized) return;
    initialized=1; bringup_initialize();
    for(i=0;i<sizeof(S);i++) ((volatile uint8_t MCU_XDATA *)&S)[i]=0;
    for(i=0;i<8;i++) { mac_smoke_mailbox[i]=0; mac_smoke_config.ieee[i]=0x10u+i; }
    mac_smoke_config.pan=0x1234; mac_smoke_config.short_address=0x5678;
    mac_smoke_config.channel=MAC_SMOKE_CHANNEL; mac_smoke_config.power=RADIO_AUTOACK_POWER_05;
    S.signature[0]='M'; S.signature[1]='A'; S.signature[2]='C'; S.signature[3]='1';
    S.version=1; S.size=sizeof(S); gate=MS_DISARMED; S.phase=gate;
    S.channel=MAC_SMOKE_CHANNEL; S.power=RADIO_AUTOACK_POWER_05; S.length=MAC_SMOKE_LENGTH; S.dsn=0x5a;
    S.adapter_result=S.mac_result=S.radio_result=255;
    S.guards[0]=0x69; S.guards[1]=0x96;
    admission=MAC_SMOKE_ADMISSION_POLLS; S.remaining[0]=0; S.remaining[1]=1;
}
void mac_smoke_poll(void)
{
    if(gate==MS_END || gate==MS_FAULT) return;
    if(!initialized || S.phase!=gate || S.consumed || S.stage || S.completed ||
       S.channel!=26 || S.power!=5 || S.length!=13 || S.dsn!=0x5a ||
       S.guards[0]!=0x69 || S.guards[1]!=0x96) { fault(MS_INVARIANT); return; }
    if(gate==MS_DISARMED || gate==MS_ARMED) {
        any=0;
        for(i=0;i<8;i++) { packet[i]=mac_smoke_mailbox[i]; any|=packet[i]; mac_smoke_mailbox[i]=0; }
        if(!admission) { fault(MS_ADMISSION_EXPIRED); return; }
        if(!any) { if(!--admission) fault(MS_ADMISSION_EXPIRED); }
        else {
            opcode=gate==MS_DISARMED?0xa9:0x56; token=gate==MS_DISARMED?0x36:0xc9;
            if(packet[0]!=opcode || packet[1]!=(uint8_t)~opcode || packet[2]!=26 || packet[3]!=0xe5 ||
               packet[4]!=token || packet[5]!=(uint8_t)~token || packet[6]!=0x4d || packet[7]!=0xb2) {
                fault(MS_PACKET); return;
            }
            gate++; S.phase=gate; admission=gate==MS_ARMED?MAC_SMOKE_ADMISSION_POLLS:0;
        }
        S.remaining[0]=(uint8_t)admission; S.remaining[1]=(uint8_t)(admission>>8); return;
    }
    if(gate!=MS_ADMITTED) { fault(MS_INVARIANT); return; }
    for(i=0;i<8;i++) if(mac_smoke_mailbox[i]) { fault(MS_PACKET); return; }
    if(mac_smoke_config.pan!=0x1234 || mac_smoke_config.short_address!=0x5678 ||
       mac_smoke_config.channel!=26 || mac_smoke_config.power!=5) { fault(MS_INVARIANT); return; }
    for(i=0;i<8;i++) if(mac_smoke_config.ieee[i]!=0x10u+i) { fault(MS_INVARIANT); return; }
    S.consumed=1; gate=MS_RUNNING; S.phase=gate;
    started=timebase_read_awake_ticks24(); previous=started;
    S.stage=1;
    A(mac_adapter_init(&mac_smoke_config,MAC_SMOKE_SERVICE_TICKS,MAC_SMOKE_SERVICE_POLLS));
    S.stage=2; A(mac_adapter_close(NULL));
    for(;;) {
        if(!progress()) { snapshot(); return; }
        S.adapter_result=mac_adapter_step(MAC_SMOKE_SERVICE_TICKS,MAC_SMOKE_SERVICE_POLLS);
        if(S.adapter_result==MAC_ADAPTER_WAIT) continue;
        if(S.adapter_result!=MAC_ADAPTER_EVENT) { snapshot(); fault(MS_ADAPTER); return; }
        event=mac_adapter_observation(); any=event->kind;
        if(any!=MAC_ADAPTER_CLOSED_EVENT && any!=MAC_ADAPTER_RX_EVENT) { snapshot(); fault(MS_ADAPTER); return; }
        if(any==MAC_ADAPTER_RX_EVENT) {
            if(S.received==255) { snapshot(); fault(MS_WORK); return; }
            S.received++;
        }
        A(mac_adapter_consume(event->token));
        if(any==MAC_ADAPTER_CLOSED_EVENT) break;
    }
    S.stage=3;
    A(mac_adapter_now(MAC_SMOKE_SERVICE_TICKS,MAC_SMOKE_SERVICE_POLLS,&mac_smoke_clock));
    M(mac_tx_interval_init(&TX,0x5a,mac_smoke_clock.symbols));
    M(mac_tx_interval_submit(&TX,body,sizeof(body),mac_smoke_clock.symbols,MAC_SMOKE_LIFETIME,MAC_SMOKE_STEPS));
    S.stage=4;
    for(;;) {
        if(!progress()) { snapshot(); return; }
        if(TX.engine.phase==MAC_TX_DRAW) {
            A(mac_adapter_prepare(&TX,MAC_ADAPTER_STOP_RX,MAC_SMOKE_SERVICE_TICKS,MAC_SMOKE_SERVICE_POLLS));
            A(mac_adapter_now(MAC_SMOKE_SERVICE_TICKS,MAC_SMOKE_SERVICE_POLLS,&mac_smoke_clock));
            M(mac_tx_observed_step(&TX,mac_smoke_clock.symbols,NULL,&ACT));
        } else {
            S.adapter_result=mac_adapter_step(MAC_SMOKE_SERVICE_TICKS,MAC_SMOKE_SERVICE_POLLS);
            if(S.adapter_result!=MAC_ADAPTER_WAIT && S.adapter_result!=MAC_ADAPTER_EVENT &&
               S.adapter_result!=MAC_ADAPTER_EXPIRED) { snapshot(); fault(MS_ADAPTER); return; }
            diag=mac_adapter_diagnostic(); event=mac_adapter_observation();
            any=S.adapter_result==MAC_ADAPTER_EVENT;
            if(any) {
                if(event->kind==MAC_ADAPTER_RX_EVENT) {
                    if(S.received==255) { snapshot(); fault(MS_WORK); return; }
                    S.received++;
                }
                if(event->tx.source.kind==MAC_TX_EVENT_BUSY_INTERVAL) S.busy++;
                if(event->tx.source.kind==MAC_TX_EVENT_SENT_INTERVAL) S.sent++;
                if(event->tx.source.kind==MAC_TX_EVENT_RETIRED) S.retired++;
            }
            M(mac_tx_observed_step(&TX,diag->live.symbols,any&&event->tx.source.kind?&event->tx:NULL,&ACT));
            if(any) { A(mac_adapter_consume(event->token)); }
        }
        if(ACT.control.kind==MAC_TX_ACTION_RANDOM) {
            if(S.draws>=5) { snapshot(); fault(MS_DRAW); return; }
            S.actions++;
            memset(&RND,0,sizeof(RND));
            RND.source.kind=MAC_TX_EVENT_RANDOM; RND.source.value=draws[S.draws++];
            RND.source.generation=TX.engine.generation; RND.source.retry=TX.engine.retries;
            RND.source.nb=TX.engine.nb; diag=mac_adapter_diagnostic();
            RND.source.stamp=diag->live.symbols;
            M(mac_tx_observed_step(&TX,diag->live.symbols,&RND,&ACT));
        }
        if(ACT.control.kind!=MAC_TX_ACTION_NONE) {
            if(ACT.control.kind!=MAC_TX_ACTION_ATTEMPT && ACT.control.kind!=MAC_TX_ACTION_QUIESCE) {
                snapshot(); fault(MS_MAC); return;
            }
            S.actions++;
            if(ACT.control.kind==MAC_TX_ACTION_ATTEMPT) {
                if(S.attempts>=5 || ACT.control.ack_requested) { snapshot(); fault(MS_MAC); return; }
                S.attempts++;
            }
            A(mac_adapter_accept(&TX,&ACT));
        }
        if(TX.engine.phase==MAC_TX_DONE || TX.engine.phase==MAC_TX_FAULT) break;
    }
    snapshot(); S.stage=5;
    if(TX.engine.phase!=MAC_TX_DONE || TX.engine.pending || TX.engine.retries ||
       diag->phase!=MAC_ADAPTER_OFF || diag->held || diag->ready || diag->goal ||
       S.radio_errors || (S.radio_phase!=RADIO_AUTOACK_OFF && S.radio_phase!=RADIO_AUTOACK_OFF_NOACK)) {
        fault(MS_TERMINAL); return;
    }
    if(TX.engine.outcome==MAC_TX_UNACKNOWLEDGED && TX.engine.transmissions==1 && S.sent==1)
        S.outcome=MS_SENT;
    else if(TX.engine.outcome==MAC_TX_CHANNEL_ACCESS && !TX.engine.transmissions && !S.sent && S.busy==5)
        S.outcome=MS_CCA_BUSY;
    else { fault(MS_TERMINAL); return; }
    M(mac_tx_interval_release(&TX));
    S.released=1; snapshot(); S.completed=1; S.stage=6; gate=MS_END; S.phase=gate;
}
