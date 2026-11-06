/* SPDX-License-Identifier: BSD-3-Clause */
#include "join_smoke.h"
#include "flash_exec.h"
#include "timebase.h"
#include "zcl_sensor.h"
#include "bdb_join_internal.h"
#include <string.h>
#if defined(CC2530_RX_DIAG)
#include "cc2530_rx_diag.h"
#endif
#if defined(CC2530_PM2_IDLE)
#if !defined(CC2530_RX_OFF_IDLE) || !defined(CC2530_RADIO_RX_ISR) || \
    !defined(CC2530_MAC_RADIO) || !defined(CC2530_CLANG)
#error "CC2530_PM2_IDLE requires the LLVM RX-off-when-idle radio owner"
#endif
#include "mac_time.h"
#include "aes.h"
#define SOC_SLEEPSTA_ADDRESS 0x9du
#define SOC_STLOAD_ADDRESS 0xadu
#endif

MCU_XDATA bdb_join_t join_smoke_device;
MCU_XDATA mac_tx_interval_t join_smoke_tx;
MCU_XDATA join_smoke_phase_t join_smoke_phase;
MCU_XDATA radio_autoack_config_t join_smoke_initial;
MCU_XDATA mac_epoch_stamp_t join_smoke_clock;
MCU_XDATA uint8_t join_smoke_draws[JOIN_SMOKE_DRAWS];
volatile MCU_XDATA uint8_t join_smoke_mailbox[8];
#if defined(CC2530_PARENT_RECOVERY)
MCU_XDATA uint8_t join_smoke_parent_history[20];
#endif
#if !defined(CC2530_CLANG) /* LLVM: fixed by --defsym join_smoke_status=0x1e00 */
volatile MCU_XDATA MCU_AT(M0_STATUS_ADDRESS) join_smoke_status_t join_smoke_status;
#endif

static MCU_XDATA uint8_t initialized, gate, i, resumed;
/* Admission scratch is dead once started; the start loop and RUNNING/READY
 * reuse its storage. */
static MCU_XDATA union {
    struct { uint8_t packet[8], any, opcode; } admit;
    struct { uint8_t backoff, draw, result; } serve;
#if defined(CC2530_PARENT_RECOVERY)
    security_keys_parent_t parent;
#endif
} scratch;
#define BACKOFF scratch.serve.backoff
#define DRAW scratch.serve.draw
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

/* READY: the application ZCL server (zcl_sensor.h) answers published frames,
 * which are then cleared, and supplies at most one due report while no
 * application TX is outstanding. FULL and the APS counter-wrap quarantine
 * leave the report uncommitted, so a later poll rebuilds it. */
static void serve(void)
{
    if(DEVICE.phase!=BDB_JOIN_READY) return;
    zcl_sensor_tick();
    RESULT=zcl_sensor_serve();
    if(DEVICE.work.runtime.zdo.application_ready) {
        if(zdo_runtime_discard_application(&DEVICE.work.runtime.zdo)!=ZDO_RUNTIME_OK) {
            fault(JS_BDB); return;
        }
        if(RESULT==ZCL_SENSOR_DISCARDED && S.discarded!=255) S.discarded++;
    }
    if(DEVICE.application_done) {
        if(bdb_join_confirm(&DEVICE,&RESULT)!=BDB_JOIN_OK) { fault(JS_BDB); return; }
        S.announce=RESULT; snapshot(); return;
    }
    if(RESULT!=ZCL_SENSOR_REPORT) return;
    RESULT=bdb_join_send(&DEVICE,&DEVICE.work.runtime.zdo.application,0,DRIVER.now);
    memset(&DEVICE.work.runtime.zdo.application,0,sizeof(ed_packet_t));
    if(RESULT==BDB_JOIN_FULL || (RESULT==BDB_JOIN_TRANSMIT_FAILED &&
       DEVICE.work.runtime.transport.wrap_wait)) return;
    if(RESULT!=BDB_JOIN_OK) {
        S.announce=(uint8_t)(0x80u|RESULT);
        fault(JS_BDB); return;
    }
    ZCL_SENSOR_STATE.sent=1;
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

#if defined(CC2530_PM2_IDLE)
MCU_XDATA join_smoke_sleep_t join_smoke_sleep_status;
static MCU_XDATA uint32_t span, mark, wake;
static MCU_XDATA uint16_t spin;
static MCU_XDATA uint8_t interrupts, flags;
#define SL join_smoke_sleep_status
#define T DEVICE.work.runtime.transport
#define Z DEVICE.work.runtime.zdo
#define SPIN(condition) for(spin=1; condition; ) if(!++spin) goto failed

/* Never taken in a correct sequence: the PCON write follows an IEN0 write
 * (no interrupt between them) and the instruction after the wake clears EA
 * before the vector can run (SWRU191F 4.2). A leaf that only clears STIF. */
__attribute__((interrupt(5))) MCU_COMMON_CODE void join_smoke_sleep_isr(void)
{
    __asm__ volatile("clr 0xc7");
}

/* STIF clear; IEN0 = EA|STIE, then PCON.IDLE whose successor must not start
 * on a 4-byte boundary (SWRU191F 4.2); clr EA runs before any ISR. Common
 * code: the alignment padding stays out of the size-packed banks. */
__attribute__((noinline)) MCU_COMMON_CODE static void enter_pm2(void)
{
    __asm__ volatile("clr 0xc7\n\t.p2align 2\n\tmov 0xa8,#0xa0\n\tmov 0x87,#1\n\tclr 0xaf" ::: "memory");
}

/* M7b: PM2 between READY polls (docs/M7_SLEEP.md). Only a fully quiescent
 * READY state sleeps: radio OFF and idle, no MAC/NWK/APS/ZDO/application
 * work, no armed DMA or interrupt. The Sleep Timer compare is the only wake
 * source; it is set a margin before the next Data Poll or keepalive deadline
 * (capped at JOIN_SMOKE_SLEEP_CAP symbols). Timer2 is stopped and restarted in
 * Sleep-Timer sync mode, so MAC symbol time stays continuous. Any failure
 * after the pause is a JS_SLEEP fault (autonomous builds restart). */
void join_smoke_sleep(void)
{
    if(gate!=JS_READY) return;
    SL.reason=1;
    if(DEVICE.phase!=BDB_JOIN_READY || (DEVICE.config.association.capability&8u) ||
       DEVICE.application_pending || DEVICE.application_done ||
       T.queued || T.active || T.waiting || T.reply || T.receive_ready || T.completed ||
       T.cancel || T.stopping || T.poll || T.active_poll || T.armed ||
       Z.query || Z.query_tx || Z.response_pending || Z.response_tx ||
       Z.application_ready || Z.leave) goto skip;
    SL.reason=2;
    diagnostic=mac_adapter_diagnostic();
    if(DRIVER.fault || DRIVER.radio.control.kind || DRIVER.action.kind || DRIVER.random_wait ||
       DRIVER.random_ready || DRIVER.stage || DRIVER.rx_held || TX.engine.phase!=MAC_TX_IDLE ||
       diagnostic->phase!=MAC_ADAPTER_OFF || diagnostic->ready || diagnostic->goal ||
       diagnostic->held) goto skip;
    SL.reason=3;
    if(MMIO_XREAD(0x618b) || (MMIO_XREAD(0x6192)&0x7fu) || (MMIO_XREAD(0x6193)&0x27u) ||
       MMIO_READ(SOC_IEN0) || MMIO_READ(SOC_IEN1) || (MMIO_READ(SOC_IEN2)&0xfeu) ||
       (MMIO_READ(SOC_SLEEPCMD)&7u)!=4u || MMIO_READ(SOC_DMAARM)) goto skip;
    SL.reason=4;
    S.adapter_result=mac_adapter_now(JOIN_SMOKE_SERVICE_TICKS,JOIN_SMOKE_SERVICE_POLLS,&CLOCK);
    if(S.adapter_result!=MAC_ADAPTER_OK) { fault(JS_ADAPTER); return; }
    span=DEVICE.poll_at-CLOCK.symbols;
    if(span>=0x80000000UL) goto skip;
    /* bdb_join poll_keepalive(): otherwise the ED Timeout keepalive is due. */
    if(!(Z.parent_known && (Z.parent_information&3u)==1u)) {
        mark=DEVICE.keepalive-CLOCK.symbols;
        if(mark>=0x80000000UL) goto skip;
        if(mark<span) span=mark;
    }
#if defined(CC2530_APP_QUIET)
    /* Application sensor cycle (zcl_flower.h): 0 keeps the device awake. */
    SL.reason=5;
    mark=zcl_sensor_quiet(CLOCK.symbols);
    if(!mark) goto skip;
    SL.reason=4;
    if(mark<span) span=mark;
#endif
    if(span>JOIN_SMOKE_SLEEP_CAP) span=JOIN_SMOKE_SLEEP_CAP;
    span=((span>>3)*2147u)>>9;   /* 16-us symbols to 32.768-kHz ticks, rounded down */
    if(span<JOIN_SMOKE_SLEEP_MIN+JOIN_SMOKE_SLEEP_MARGIN) goto skip;
    span-=JOIN_SMOKE_SLEEP_MARGIN;
    SL.reason=6;
    SL.time_result=mac_time_pause(JOIN_SMOKE_SLEEP_SYNC_TICKS,JOIN_SMOKE_SLEEP_SYNC_POLLS);
    if(SL.time_result!=MAC_TIME_OK) goto failed;
    /* SWRU191F 11.2: ST0 latches ST1/ST2; compare loads ST2, ST1, then ST0. */
    mark=timebase_read_awake_ticks24();
    wake=(mark+span)&TIMEBASE_TICKS_MASK;
    SPIN(!(MMIO_READ(SOC_STLOAD)&1u));
    MMIO_WRITE(SOC_ST2,(uint8_t)(wake>>16));
    MMIO_WRITE(SOC_ST1,(uint8_t)(wake>>8));
    MMIO_WRITE(SOC_ST0,(uint8_t)wake);
    if(((wake-timebase_read_awake_ticks24())&TIMEBASE_TICKS_MASK)<JOIN_SMOKE_SLEEP_MIN) {
        SL.late++;
    } else {
        interrupts=MMIO_READ(SOC_IEN2);
        flags=MMIO_READ(SOC_RFIRQF0);
        MMIO_WRITE(SOC_IEN2,0);
        MMIO_WRITE(SOC_SLEEPCMD,0x06);   /* PM2, unused 16-MHz RCOSC off */
        enter_pm2();
        MMIO_WRITE(SOC_IEN0,0);
        MMIO_WRITE(SOC_SLEEPCMD,0x04);
        MMIO_WRITE(SOC_IEN2,interrupts);
        if(!(MMIO_READ(SOC_IRCON)&0x80u)) SL.stray++;
        SL.reason=7;
        /* XOSC back as system clock, then a fresh 32-kHz edge (SWRU191F 22.4). */
        SPIN(MMIO_READ(SOC_CLKCONSTA)!=MMIO_READ(SOC_CLKCONCMD));
        SPIN(MMIO_READ(SOC_SLEEPSTA)&1u);
        SPIN(!(MMIO_READ(SOC_SLEEPSTA)&1u));
        SL.reason=10;
        /* H2 (LH24): RFIRQF1.RFIDLE is lost across PM2, although SWRU191F 4.6
         * lists RF registers as retained, and the radio owner requires it while
         * OFF. ISRXON then ISRFOFF ends calibration before RX starts and
         * latches RFIDLE (and RFIRQF0.RXMASKZERO) like a normal stop. */
        if(!(MMIO_READ(SOC_RFIRQF1)&4u)) SL.lost++;
        MMIO_WRITE(SOC_RFST,0xe3);
        MMIO_WRITE(SOC_RFST,0xef);
        SPIN(!(MMIO_READ(SOC_RFIRQF1)&4u) || (MMIO_XREAD(0x6192)&0xbfu));
        if(MMIO_READ(SOC_RFERRF) || (MMIO_READ(SOC_RFIRQF1)&0xf8u) ||
           (MMIO_XREAD(0x6193)&0x27u) || MMIO_XREAD(0x619b) ||
           (MMIO_READ(SOC_RFIRQF0)&(uint8_t)~flags&0x7fu)) goto failed;
        SL.sleeps++;
    }
    SL.reason=8;
    SL.time_result=mac_time_resume(JOIN_SMOKE_SLEEP_SYNC_TICKS,JOIN_SMOKE_SLEEP_SYNC_POLLS);
    if(SL.time_result!=MAC_TIME_OK) goto failed;
    SL.reason=9;
    SL.aes_result=aes_power_restored();
    if(SL.aes_result!=AES_OK) goto failed;
    SL.slept+=(timebase_read_awake_ticks24()-mark)&TIMEBASE_TICKS_MASK;
    SL.reason=0;
    return;
skip:
    SL.skips++;
    return;
failed:
    SL.failure=SL.reason;
    fault(JS_SLEEP);
}
#undef T
#undef Z
#undef SPIN
#endif

void join_smoke_initialize(void)
{
    if(initialized) return;
    initialized=1;
    for(i=0;i<sizeof(S);i++) ((volatile uint8_t MCU_XDATA *)&S)[i]=0;
    for(i=0;i<8;i++) join_smoke_mailbox[i]=0;
#if defined(CC2530_RX_DIAG)
    RX_DIAG_RESET(i);
#endif
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
#if defined(CC2530_PARENT_RECOVERY)
            if(memcmp(join_smoke_parent_history, "\0\0\0\0\0\0\0\0", 8)) {
                if(resumed) { fault(JS_INPUT); return; }
                memset(&scratch.parent,0,sizeof(scratch.parent));
                memcpy(scratch.parent.ieee,join_smoke_parent_history,8);
                scratch.parent.address=(uint16_t)join_smoke_parent_history[8] |
                    (uint16_t)join_smoke_parent_history[9]<<8;
                for(i=0;i<8;i++)
                    scratch.parent.nwk_floor[i/4]|=(uint32_t)join_smoke_parent_history[10+i]<<(8u*(i%4));
                scratch.parent.sequence[0]=join_smoke_parent_history[18];
                scratch.parent.sequence[1]=join_smoke_parent_history[19];
                S.security_result=security_keys_upgrade_parent(FLASH_EXEC_POLL_MAX);
                if(S.security_result==SECURITY_KEYS_OK)
                    S.security_result=security_keys_remember_parent(&scratch.parent,FLASH_EXEC_POLL_MAX);
                if(S.security_result!=SECURITY_KEYS_OK) { fault(JS_SECURITY); return; }
                memset(join_smoke_parent_history,0,sizeof(join_smoke_parent_history));
            } else {
                for(i=8;i<20;i++) if(join_smoke_parent_history[i]) { fault(JS_INPUT); return; }
            }
#endif
            if(S.draws==S.draw_limit) { fault(JS_RANDOM); return; }
            BACKOFF=join_smoke_draws[S.draws]; join_smoke_draws[S.draws++]=0;
            if(!BACKOFF) { fault(JS_RANDOM); return; }
            S.saw_ready=1; S.stage=5; gate=JS_READY; S.phase=gate;
            started=previous=now; steps=0;
            S.security_result=security_keys_status(&bdb_join_keys);
            if(S.security_result!=SECURITY_KEYS_OK || bdb_join_keys.phase!=SECURITY_KEYS_VERIFIED ||
               !zcl_sensor_init(&bdb_join_keys.config)) { fault(JS_SECURITY); return; }
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
    /* Complete persisted membership resumes through secured rejoin. Default-TC:
     * LEFT or an incomplete join is recommissioned (journal and counter
     * ceilings kept, never erased); otherwise existing storage is refused. */
    resumed=S.security_result==SECURITY_KEYS_OK && bdb_join_resumable();
    if(!resumed) {
#if defined(CC2530_DEFAULT_TC_KEY)
        if(S.security_result==SECURITY_KEYS_OK) {
            S.security_result=security_keys_recommission();
            if(S.security_result!=SECURITY_KEYS_OK) { fault(JS_SECURITY); return; }
        } else
#endif
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
    }
    for(i=0;i<8;i++) INITIAL.ieee[i]=IN.identity.own_ieee[i];
    INITIAL.pan=INITIAL.short_address=0xffff;
    INITIAL.channel=IN.identity.channel; INITIAL.power=RADIO_AUTOACK_POWER_F5;
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
    S.bdb_result=resumed?bdb_join_resume(&DEVICE,&TX,&IN.join,CLOCK.symbols):
        bdb_join_start(&DEVICE,&TX,&IN.join,CLOCK.symbols);
    if(S.bdb_result!=BDB_JOIN_OK) { fault(JS_BDB); return; }
    memset(&join_smoke_phase,0,sizeof(join_smoke_phase));
    S.driver_result=mac_link_driver_init(&DRIVER,&DEVICE,&INITIAL,
        JOIN_SMOKE_SERVICE_TICKS,JOIN_SMOKE_SERVICE_POLLS);
    if(S.driver_result!=MAC_LINK_DRIVER_OK) { fault(JS_DRIVER); return; }
    S.bound=1; S.stage=4;
}
