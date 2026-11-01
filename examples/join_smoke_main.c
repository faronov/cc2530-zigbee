/* SPDX-License-Identifier: BSD-3-Clause */
#include "join_smoke.h"
#if defined(CC2530_AUTONOMOUS)
#if !defined(CC2530_CLANG) || !defined(CC2530_DEFAULT_TC_KEY)
#error "the autonomous image is an LLVM default-TC build"
#endif
#include "flash_exec.h"
#include "timebase.h"
#include <string.h>

/* SWRU191F 16.3 WDCTL (MODE[3:2], INT[1:0]); 4.10 SLEEPSTA.RST[4:3]. */
#define SOC_WDCTL_ADDRESS 0xc9u
#define SOC_SLEEPSTA_ADDRESS 0x9du
#define WDCTL_RESET 0x0bu           /* watchdog mode, 64 periods (1.9 ms) */
#define SLEEPSTA_RST_MASK 0x18u
#define SLEEPSTA_RST_WATCHDOG 0x10u
#define RFST_ISRFOFF 0xefu
/* Factory IEEE address in the information page, LSB first (SWRU191F 2.3.3). */
#define INFO_IEEE 0x780cu
/* BDB primary channel set (11, 15, 20, 25). */
#define AUTONOMOUS_CHANNELS UINT32_C(0x02108800)
#define PAUSE_LEAVE (2UL*32768UL)
#define PAUSE_FIRST (5UL*32768UL)
#define PAUSE_RETRY (60UL*32768UL)

static MCU_XDATA uint8_t reset_cause, lfsr;
static const uint8_t arm[8] = {0x4a,0xb5,0x4e,0xb1,0xa9,0x56,1,0xfe};
static const uint8_t run[8] = {0x4a,0xb5,0x4e,0xb1,0x56,0xa9,1,0xfe};

static void post(const uint8_t *packet)
{
    uint8_t n;
    for(n=0;n<8;n++) join_smoke_mailbox[n]=packet[n];
    join_smoke_poll();
}

/* The admission the HIL operator otherwise loads, built in firmware: default
 * TC key, own IEEE from the information page, BDB primary channels. CSMA
 * draws come from the 8-bit Galois LFSR seeded by IEEE XOR ST0: distinct per
 * device, deterministic, NOT entropy (backoff only). */
static void admit(void)
{
    join_smoke_admission_t MCU_XDATA *a=&join_smoke_phase.admission;
    uint8_t n, seed=MMIO_READ(SOC_ST0);
    memset(a,0,sizeof(*a));
    for(n=0;n<8;n++) {
        a->identity.own_ieee[n]=MMIO_XREAD(INFO_IEEE+n);
        seed^=a->identity.own_ieee[n];
    }
    a->identity.pan=a->identity.address=0xffff; a->identity.channel=11;
    a->join.scan.channels=AUTONOMOUS_CHANNELS; a->join.scan.lifetime=400000;
    a->join.scan.work=MAC_SCAN_MAX_WORK; a->join.scan.duration=4;
    a->join.scan.saved.pan=0xffff; a->join.scan.saved.channel=11;
    a->join.association.extraction.epoch=1; a->join.association.extraction.frame_wait=1986;
    a->join.association.extraction.lifetime=100000; a->join.association.extraction.work=512;
    a->join.association.extraction.pan=0xffff; a->join.association.extraction.channel=11;
    a->join.association.extraction.local_mode=MAC_ADDRESS_EXTENDED;
    a->join.association.extraction.coordinator_mode=MAC_ADDRESS_EXTENDED;
    memcpy(a->join.association.extraction.local,a->identity.own_ieee,8);
    a->join.association.saved.pan=0xffff; a->join.association.saved.channel=11;
    a->join.association.response_wait=2; a->join.association.capability=0x80;
    a->join.descriptor.max_incoming=a->join.descriptor.max_outgoing=64;
    a->join.descriptor.logical_type=2; a->join.descriptor.frequency_band=8;
    a->join.descriptor.mac_capability=0x80; a->join.descriptor.max_buffer=127;
    a->join.descriptor.stack_revision=22;
    a->join.transport.limits.block_timeout=JOIN_SMOKE_SERVICE_TICKS;
    a->join.transport.limits.block_polls=JOIN_SMOKE_SERVICE_POLLS;
    a->join.transport.ack_wait=NWK_APS_ACK_WAIT; a->join.transport.broadcast_time=1000000;
    a->join.transport.nv_polls=FLASH_EXEC_POLL_MAX; a->join.transport.profile=0x0104;
    a->join.transport.endpoint=1; a->join.link_cost=1;
    a->nwk_floor=a->aps_floor=1; a->qualified_draws=JOIN_SMOKE_DRAWS;
    lfsr=seed?seed:0x5a;
    for(n=0;n<JOIN_SMOKE_DRAWS;n++) {
        lfsr=(uint8_t)((lfsr>>1)^(lfsr&1u?0xb8u:0u));
        join_smoke_draws[n]=lfsr;
    }
    post(arm); post(run);
}

/* Fault: radio off, pause, watchdog reset. A reset restarts from NV: VERIFIED
 * membership rejoins; LEFT or an incomplete join steers again. No RAM
 * survives reset, so the pause follows the reset cause: a fault after a
 * watchdog reset is a retry and waits longer. The pause keeps status readable. */
__attribute__((noinline)) static void restart(void)
{
    uint32_t pause=PAUSE_FIRST, start;
    MMIO_CLEAR(SOC_IEN0,0x80);
    MMIO_WRITE(SOC_RFST,RFST_ISRFOFF);
    if(join_smoke_status.reason==JS_BDB && (join_smoke_status.bdb_reason==BDB_JOIN_LOCAL_LEAVE ||
       join_smoke_status.bdb_reason==BDB_JOIN_REMOTE_LEAVE)) pause=PAUSE_LEAVE;
    else if(reset_cause==SLEEPSTA_RST_WATCHDOG) pause=PAUSE_RETRY;
    start=timebase_read_awake_ticks24();
    while(((timebase_read_awake_ticks24()-start)&TIMEBASE_TICKS_MASK)<pause) {}
    MMIO_WRITE(SOC_WDCTL,WDCTL_RESET);
    for(;;) {}
}
#endif
#if defined(CC2530_CLANG)
__attribute__((naked, noinline)) void join_smoke_wait(void)
{
    __asm__ volatile("nop\n\tret");
}
__attribute__((naked, noinline)) void join_smoke_fault(void)
{
    __asm__ volatile("nop\n\tsjmp join_smoke_fault");
}
#else
void join_smoke_wait(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void join_smoke_fault(void) __naked
{
    __asm
        nop
        sjmp _join_smoke_fault
    __endasm;
}
#endif
void main(void)
{
#if defined(CC2530_CLANG)
    /* LLVM cc2530_startup does not call the SDCC external-startup hook. */
    CC2530_EXTERNAL_STARTUP();
#endif
#if defined(CC2530_AUTONOMOUS)
    reset_cause=MMIO_READ(SOC_SLEEPSTA)&SLEEPSTA_RST_MASK;
#endif
    join_smoke_initialize();
#if defined(CC2530_AUTONOMOUS)
    admit();
#endif
    for(;;) {
        if(join_smoke_status.phase!=JS_RUNNING && join_smoke_status.phase!=JS_READY) join_smoke_wait();
        join_smoke_poll();
        if(join_smoke_status.phase==JS_FAULT) {
#if defined(CC2530_AUTONOMOUS)
            restart();
#endif
            join_smoke_fault();
        }
    }
}
