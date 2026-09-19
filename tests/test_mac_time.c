/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic controller only. This component executable must NEVER be flashed.
 */
#include "mac_time.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>

MCU_XDATA struct {
    uint8_t before[4];
    mac_time_stamp_t value;
    uint8_t after[4];
} mac_time_test_output;
volatile MCU_XDATA uint8_t mac_time_test_action, mac_time_test_return;
volatile MCU_XDATA uint16_t mac_time_test_target, mac_time_test_limit;
volatile MCU_XDATA uint32_t mac_time_test_timeout;
const mac_time_diagnostics_t MCU_XDATA * MCU_XDATA mac_time_test_diagnostic;
extern MCU_XDATA uint8_t mac_time_fault, mac_time_ready, mac_time_reserved_end;

void mac_time_test_cycle(void)
{
#if defined(__SDCC)
    __asm
        .globl _mac_time_test_before
    _mac_time_test_before:
        nop
    __endasm;
#endif
    if (!mac_time_test_action)
        mac_time_test_return = mac_time_init(mac_time_test_timeout, mac_time_test_limit);
    else
        mac_time_test_return = mac_time_read_live(mac_time_test_timeout, mac_time_test_limit,
#if defined(__SDCC)
            (mac_time_stamp_t MCU_XDATA *)mac_time_test_target);
#else
            mac_time_test_target ? &mac_time_test_output.value : NULL);
#endif
    mac_time_test_diagnostic = mac_time_diagnostic();
#if defined(__SDCC)
    __asm
        .globl _mac_time_test_done
    _mac_time_test_done:
        nop
    __endasm;
#endif
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t mac_time_test_result[8];
void main(void)
{
    uint8_t i;
    for (i = 0; i < sizeof(mac_time_test_output); i++)
        ((uint8_t MCU_XDATA *)&mac_time_test_output)[i] = 0xa5;
    mac_time_test_result[0] = 'M'; mac_time_test_result[1] = 'T';
    mac_time_test_result[2] = 'I'; mac_time_test_result[3] = '1';
    mac_time_test_result[4] = 1; mac_time_test_result[5] = 8;
    mac_time_test_result[6] = mac_time_test_result[7] = 0;
    for (;;) mac_time_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>

uint8_t _gptrput_PARM_2;
static uint8_t xregs[1024];
#define XR(a) xregs[(a)-0x6000u]
static uint16_t fine, fine_latch, timer_period, fine_write;
static uint32_t overflow, overflow_latch, overflow_period, overflow_write;
static mac_time_stamp_t oracle;
static uint32_t ticks, tick_step, st_latch, mac_step, step_after_low;
static unsigned st_byte, start_delay, pending, stuck, force_ff;
static unsigned ignore_address, ignore_occurrence, write_occurrence;
static unsigned change_after_low, malformed_fine, malformed_overflow;
static unsigned fail_poll, poll_count, fail_address, fail_value;
static unsigned have_read, events, tracing, first_step, calls, live_reads, snapshot_order;
static uint16_t last_address;
static uint8_t last_value;

static void consume(void)
{
    if (have_read) {
        if (last_address < 256) {
            assert(read_count == 1 && !xread_count);
            assert(reads[0].address == last_address && reads[0].value == last_value);
            read_count = 0;
        } else {
            assert(xread_count == 1 && !read_count);
            assert(xreads[0].address == last_address && xreads[0].value == last_value);
            xread_count = 0;
        }
        have_read = 0;
    } else assert(!read_count && !xread_count);
}
static void event(char kind, uint16_t address, uint8_t value)
{
    if (tracing) printf("%s[\"%c\",%u,%u]", events ? "," : "", kind, address, value);
    events++;
}
static void advance(uint32_t clocks)
{
    uint64_t sum, periods;
    if (!(SOC_T2CTRL & 4)) return;
    assert(timer_period && overflow_period);
    sum = (uint64_t)fine + clocks;
    periods = sum / timer_period;
    fine = (uint16_t)(sum % timer_period);
    if (periods) SOC_T2IRQF |= 1;
    sum = (uint64_t)overflow + periods;
    if (sum >= overflow_period) SOC_T2IRQF |= 8;
    overflow = (uint32_t)(sum % overflow_period);
}
static uint8_t selected(uint8_t a)
{
    uint8_t selection = a <= 0xa3 ? SOC_T2MSEL & 7 : (SOC_T2MSEL >> 4) & 7;
    if (selection == 2) {
        if (a <= 0xa3) return (uint8_t)(timer_period >> (8*(a-0xa2)));
        return (uint8_t)(overflow_period >> (8*(a-0xa4)));
    }
    if (selection != 0) return 0; /* explicit stolen-selector fault, not a capture implementation */
    if (a == 0xa2) {
        uint8_t low;
        assert(!snapshot_order);
        if ((SOC_T2CTRL & 4) && force_ff) {
            advance((255u + timer_period - fine) % timer_period);
            force_ff--;
        }
        oracle.fine = fine; oracle.periods = overflow;
        low = (uint8_t)fine;
        if (SOC_T2CTRL & 4) {
            live_reads++;
            snapshot_order = low == 255 ? 0 : 1;
            /* SWRZ031: low byte is old, upper latch can be NEXT clock.
             * The oracle remains the counter at the initial read instant.
             */
            if (low == 255) advance(1);
        }
        fine_latch = malformed_fine ? 0x0200 : fine;
        if (SOC_T2CTRL & 8) overflow_latch = malformed_overflow ? 0xffffff : overflow;
        if (step_after_low) mac_step = step_after_low;
        if (change_after_low == 1) SOC_CLKCONCMD = SOC_CLKCONSTA = 0x98;
        if (change_after_low == 2) SOC_T2CTRL &= (uint8_t)~4u;
        if (change_after_low == 3) SOC_T2MSEL = 0x11;
        return malformed_fine ? 0 : low;
    }
    if ((SOC_T2CTRL & 4) && snapshot_order) {
        assert(a == 0xa2 + snapshot_order);
        snapshot_order = (snapshot_order + 1) % 5;
    }
    if (a == 0xa3) return (uint8_t)(fine_latch >> 8);
    if (a == 0xa4 && !(SOC_T2CTRL & 8)) overflow_latch = overflow;
    return (uint8_t)(overflow_latch >> (8*(a-0xa4)));
}
static uint8_t load(uint8_t a, uint8_t value)
{
    consume(); advance(mac_step);
    if (a == 0xa8 && fail_poll && ++poll_count == fail_poll) {
        if (fail_address == 0xc6) SOC_CLKCONCMD = SOC_CLKCONSTA = (uint8_t)fail_value;
        else if (fail_address == 0x94) SOC_T2CTRL = (uint8_t)fail_value;
        else if (fail_address == 0xa7) SOC_T2IRQM = (uint8_t)fail_value;
        else if (fail_address == 0x9c) SOC_T2EVTCFG = (uint8_t)fail_value;
        else XR(fail_address) = (uint8_t)fail_value;
    }
    if (a == 0x94) {
        if (pending && !stuck && !--pending) SOC_T2CTRL |= 4;
        value = SOC_T2CTRL;
    } else if (a == 0xa1) value = SOC_T2IRQF;
    else if (a == 0xa7) value = SOC_T2IRQM;
    else if (a == 0xc3) value = SOC_T2MSEL;
    else if (a >= 0xa2 && a <= 0xa6) value = selected(a);
    else if (a >= 0x95 && a <= 0x97) {
        assert(a == 0x95 + st_byte);
        if (!st_byte) { st_latch = ticks; ticks = (ticks + tick_step) & TIMEBASE_TICKS_MASK; }
        value = (uint8_t)(st_latch >> (8*st_byte));
        st_byte = (st_byte + 1) % 3;
    }
    event('r', a, value); last_address = a; last_value = value; have_read = 1;
    return value;
}
static uint8_t xload(uint16_t a)
{
    uint8_t value;
    consume(); advance(mac_step); assert(a >= 0x6000 && a < 0x6400);
    value = XR(a); event('r', a, value);
    last_address = a; last_value = value; have_read = 1; return value;
}
static void store(uint8_t a, uint8_t before, uint8_t value)
{
    consume(); advance(mac_step);
    assert(write_count == 1 && writes[0].address == a &&
           writes[0].before == before && writes[0].after == value);
    write_count = 0; event('w', a, value);
    if (a == ignore_address && ++write_occurrence == ignore_occurrence) {
        switch (a) {
        case 0x94: SOC_T2CTRL = before; break;
        case 0xc3: SOC_T2MSEL = before; break;
        case 0x9c: SOC_T2EVTCFG = before; break;
        default: break;
        }
        return;
    }
    if (a == 0x94) {
        assert(!(before & 4) && (value == 8 || value == 9));
        SOC_T2CTRL = value;
        if (value == 9) { assert(SOC_T2MSEL == 0 && SOC_T2EVTCFG == 0x77); pending = start_delay+1; }
    } else if (a == 0xc3) {
        assert(SOC_T2CTRL == 8 && (value == 0x22 || !value));
    } else if (a == 0x9c) {
        assert(SOC_T2CTRL == 2 && value == 0x77);
    } else {
        assert(SOC_T2CTRL == 8 && SOC_T2MSEL == 0x22); /* never live/delta writes */
        if (a == 0xa2) fine_write = value;
        else if (a == 0xa3) timer_period = fine_write | ((uint16_t)value << 8);
        else if (a == 0xa4) overflow_write = value;
        else if (a == 0xa5) overflow_write |= (uint32_t)value << 8;
        else { assert(a == 0xa6); overflow_period = overflow_write | ((uint32_t)value << 16); }
    }
}
static uint16_t xaddress(const volatile void *object)
{
    if (object == &mac_time_test_output.value) return mac_time_test_target;
    if (object == &mac_time_reserved_end) return 0x300;
    if (object == &_gptrput_PARM_2) return 0x700;
    assert(0); return 0;
}
static void reset(void)
{
    host_mmio_reset(); mac_time_fault = mac_time_ready = 0;
    memset((void *)mac_time_diagnostic(), 0, sizeof(mac_time_diagnostics_t)); /* synthetic SoC reset only */
    memset(&mac_time_test_output, 0xa5, sizeof(mac_time_test_output));
    memset(xregs, 0, sizeof(xregs)); XR(0x624a) = 0xa5;
    fine = fine_latch = fine_write = timer_period = 0;
    overflow = overflow_latch = overflow_write = overflow_period = 0;
    ticks = st_latch = step_after_low = 0; tick_step = 1; mac_step = 7;
    st_byte = pending = stuck = start_delay = force_ff = 0;
    ignore_address = ignore_occurrence = write_occurrence = 0;
    change_after_low = malformed_fine = malformed_overflow = 0;
    fail_poll = poll_count = fail_address = fail_value = 0;
    have_read = events = live_reads = snapshot_order = 0; first_step = 1;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88; SOC_SLEEPCMD = 4; SOC_T2CTRL = 2;
    mac_time_test_timeout = 10000; mac_time_test_limit = 1000; mac_time_test_target = 0x500;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xread_hook = xload; host_mmio_xaddress_hook = xaddress;
}
static void begin(const char *name)
{
    unsigned i;
    if (!tracing) return;
    printf("{\"name\":\"%s\",\"initial\":{", name);
#define SFR(name_, address_) printf("\"%u\":%u,", address_, name_);
    CC2530_REGISTER_LIST(SFR)
#undef SFR
    for (i = 0; i < sizeof(xregs); i++)
        printf("\"%u\":%u%s", 0x6000u+i, xregs[i], i+1 == sizeof(xregs) ? "" : ",");
    printf("},\"steps\":[");
}
static void finish(void) { if (tracing) puts("]}"); }
static void step(unsigned action, unsigned expected)
{
    mac_time_stamp_t saved;
    mac_time_diagnostics_t prior;
    const mac_time_diagnostics_t *d;
    unsigned i, fault = mac_time_fault;
    memcpy(&saved, &mac_time_test_output.value, sizeof(saved));
    memcpy(&prior, mac_time_diagnostic(), sizeof(prior));
    calls++; events = 0; mac_time_test_action = (uint8_t)action;
    if (tracing) {
        printf("%s{\"action\":%u,\"timeout\":%lu,\"limit\":%u,\"target\":%u,\"events\":[",
            first_step ? "" : ",", action, (unsigned long)mac_time_test_timeout, mac_time_test_limit,
            mac_time_test_target == 0x500 ? 65535 : mac_time_test_target == 0x700 ? 65534 :
            mac_time_test_target == 0x300 ? 65533 : mac_time_test_target);
        first_step = 0;
    }
    mac_time_test_cycle(); consume(); d = mac_time_diagnostic();
    if (mac_time_test_return != expected) {
        fprintf(stderr, "case%u action%u expected%u got%u phase%u polls%u\n", calls, action,
                expected, mac_time_test_return, d->phase, d->polls);
        assert(mac_time_test_return == expected);
    }
    assert(mac_time_test_diagnostic == d && !write_count && !xwrite_count && !st_byte);
    for (i = 0; i < 4; i++)
        assert(mac_time_test_output.before[i] == 0xa5 && mac_time_test_output.after[i] == 0xa5);
    if (action && expected == MAC_TIME_OK) {
        assert(mac_time_test_output.value.fine == oracle.fine &&
               mac_time_test_output.value.periods == oracle.periods &&
               (oracle.fine & 255) != 255 && oracle.fine < 512 && oracle.periods < 0xffffff);
    } else assert(!memcmp(&saved, &mac_time_test_output.value, sizeof(saved)));
    if (fault || (expected >= 3 && expected <= 7))
        assert(!events && !memcmp(&prior, d, sizeof(prior)));
    if (expected == MAC_TIME_OK) {
        assert(mac_time_ready && !mac_time_fault && d->phase == 8 && d->result == MAC_TIME_OK &&
               SOC_T2CTRL == 13 && !SOC_T2MSEL && !SOC_T2IRQM && SOC_T2EVTCFG == 0x77 &&
               timer_period == 512 && overflow_period == 0xffffff);
    } else if (expected >= 8) assert(mac_time_fault == expected && d->result == expected);
    if (tracing)
        printf("],\"result\":%u,\"fault\":%u,\"ready\":%u,\"stamp\":[%u,%lu],"
               "\"diagnostics\":[%lu,%u,%u,%u,%u,%u,%u,%u,%u]}",
            expected, mac_time_fault, mac_time_ready, mac_time_test_output.value.fine,
            (unsigned long)mac_time_test_output.value.periods, (unsigned long)d->elapsed_ticks,
            d->polls, d->discarded, d->result, d->phase, d->control, d->select, d->irq_flags, d->timebase_status);
}
static void retained(void)
{
    unsigned r = mac_time_fault;
    assert(r); mac_time_test_timeout = mac_time_test_limit = mac_time_test_target = 0;
    step(0, r); step(1, r);
}
static void scenarios(void)
{
    static const uint32_t coarse[] = {0, 0xff, 0xffff, 0x123456, 0xfffffe};
    unsigned i;
    reset(); begin("cold state and one shot initialization");
    step(1, 6); step(0, 1); step(0, 7); step(1, 1); step(1, 1); finish();
    reset(); ticks = 0xfffffc; start_delay = 3; begin("delayed asynchronous start and Sleep Timer wrap");
    step(0, 1); step(1, 1); finish();
    for (i = 0; i < sizeof(coarse)/sizeof(coarse[0]); i++) {
        reset(); begin("live rollover and low FF erratum");
        step(0, 1); mac_step = 0; fine = 255; overflow = coarse[i];
        step(1, 1); assert(mac_time_diagnostic()->discarded == 1);
        fine = 511; overflow = coarse[i]; step(1, 1);
        assert(mac_time_diagnostic()->discarded == 1);
        finish();
    }
    reset(); begin("latched overflow FF is not a destructive MOVF latch");
    step(0, 1); fine = 400; overflow = 0x12ff; mac_step = 0; step_after_low = 100;
    step(1, 1); assert(mac_time_test_output.value.fine == 400 &&
                       mac_time_test_output.value.periods == 0x12ff && overflow != 0x12ff); finish();
    reset(); begin("latched overflow FFFF remains coherent across live carry");
    step(0, 1); fine = 400; overflow = 0xffff; mac_step = 0; step_after_low = 100;
    step(1, 1); assert(mac_time_test_output.value.periods == 0xffff && overflow != 0xffff); finish();
    reset(); begin("stopped LF uses work budget");
    step(0, 1); tick_step = 0; force_ff = 65535; mac_time_test_limit = 4;
    step(1, 11); retained(); finish();
    reset(); begin("repeated FF deadline equality");
    step(0, 1); force_ff = 30; mac_time_test_timeout = 4;
    step(1, 10); retained(); finish();
    reset(); begin("valid sample observed at deadline is not publication");
    step(0, 1); mac_time_test_timeout = 2; step(1, 10); retained(); finish();
    reset(); begin("last allowed sample can succeed");
    step(0, 1); mac_time_test_limit = 2; step(1, 1); finish();
    reset(); begin("no latch read without confirmation budget");
    step(0, 1); mac_time_test_limit = 1; step(1, 11); retained(); finish();
    reset(); begin("init deadline before first write");
    mac_time_test_timeout = 1; step(0, 10); retained(); finish();
    reset(); stuck = 1; begin("RUN echo is not STATE");
    mac_time_test_limit = 10; step(0, 11); retained(); finish();
    reset(); stuck = 1; begin("STATE never starts before deadline");
    mac_time_test_timeout = 10; step(0, 10); retained(); finish();
    for (i = 0; i < 3; i++) {
        reset(); begin("post latch ownership loss"); step(0, 1);
        change_after_low = i+1; step(1, 9); retained(); finish();
    }
    reset(); begin("STATE loss in pre read poll");
    step(0, 1); fail_poll = 2; fail_address = 0x94; fail_value = 9;
    step(1, 9); retained(); finish();
    reset(); begin("malformed fine range"); step(0, 1);
    malformed_fine = 1; step(1, 14); retained(); finish();
    reset(); begin("malformed overflow range"); step(0, 1);
    malformed_overflow = 1; step(1, 14); retained(); finish();
    reset(); begin("masked flags are not epochs or acknowledgments");
    step(0, 1); SOC_T2IRQF = 0x3f; step(1, 1); assert(SOC_T2IRQF == 0x3f); finish();
    reset(); begin("CC2541-only flag bits are not admitted");
    step(0, 1); SOC_T2IRQF = 0x40; step(1, 9); retained(); finish();
    for (i = 0; i < 7; i++) {
        static const uint8_t addresses[] = {0x9c, 0x94, 0xc3, 0xa3, 0xa4, 0xa5, 0xa6};
        reset(); ignore_address = addresses[i]; ignore_occurrence = 1;
        begin("unconfirmed configuration"); step(0, 9); retained(); finish();
    }
    reset(); ignore_address = 0xc3; ignore_occurrence = 2; begin("unconfirmed return to live selector");
    step(0, 9); retained(); finish();
    reset(); ignore_address = 0x94; ignore_occurrence = 2; begin("ignored RUN request");
    step(0, 9); retained(); finish();
    reset(); SOC_T2CTRL = 13; timer_period = 512; overflow_period = 0xffffff;
    begin("cannot adopt or stop running timer");
    step(0, 9); retained(); finish();
    reset(); fine = 1; begin("cannot rewind an old counter");
    step(0, 9); retained(); finish();
    reset(); overflow = 1; begin("cannot import an old overflow epoch");
    step(0, 9); retained(); finish();
    reset(); SOC_T2IRQM = 1; begin("IRQ masks not silently cleared"); step(0, 8); retained(); finish();
    reset(); SOC_T2IRQF = 1; begin("old interrupt history not cleared"); step(0, 9); retained(); finish();
    reset(); SOC_IEN1 = 4; begin("IRQ ownership"); step(0, 8); retained(); finish();
    reset(); SOC_DMAARM = 1; begin("DMA ownership"); step(0, 8); retained(); finish();
    reset(); XR(0x61e1) = 0x20; begin("CSP ownership"); step(0, 8); retained(); finish();
    reset(); XR(0x6193) = 2; begin("radio ownership"); step(0, 8); retained(); finish();
    reset(); SOC_CLKCONSTA = 0xc9; begin("clock not established"); step(0, 8); retained(); finish();
    reset(); SOC_SLEEPCMD = 5; begin("power mode unsupported"); step(0, 8); retained(); finish();
    reset(); begin("half range ambiguity"); tick_step = 0x802710; step(0, 12); retained(); finish();
    reset(); begin("backwards raw deadline clock"); tick_step = 0xffffff; step(0, 13); retained(); finish();
    reset(); begin("invalid arguments and buffers");
    mac_time_test_timeout = 0; step(0, 3); mac_time_test_timeout = 10000; step(0, 1);
    mac_time_test_target = 0; step(1, 3);
    mac_time_test_target = 0x1dfb; step(1, 4);
    mac_time_test_target = 0x1e00; step(1, 4);
    mac_time_test_target = 0x1f00; step(1, 4);
    mac_time_test_target = 0x300; step(1, 5);
    mac_time_test_target = 0x700; step(1, 5);
    mac_time_test_target = 1; step(1, 5); finish();
}
int main(int argc, char **argv)
{
    unsigned i, j;
    static const uint32_t coarse[] = {0, 0xfe, 0xff, 0x100, 0xfffe, 0xffff, 0x10000, 0xfffffe};
    tracing = argc == 2 && !strcmp(argv[1], "--vectors");
    scenarios(); if (tracing) return 0;
    for (i = 0; i < 512; i++) for (j = 0; j < sizeof(coarse)/sizeof(coarse[0]); j++) {
        reset(); step(0, 1); mac_step = 0; fine = (uint16_t)i; overflow = coarse[j]; step(1, 1);
    }
    for (i = 23; i < 32; i++) {
        reset(); mac_time_test_timeout = 1UL << i; step(0, 3);
        mac_time_test_timeout = 10000; step(0, 1); mac_time_test_timeout = 1UL << i; step(1, 3);
    }
    reset(); mac_time_test_limit = 0; step(0, 3);
    mac_time_test_limit = 1000; step(0, 1); mac_time_test_limit = 0; step(1, 3);
    reset(); step(0, 1); force_ff = 65535; tick_step = 0; mac_time_test_limit = 65535;
    step(1, 11); assert(mac_time_diagnostic()->polls == 65535 &&
                       mac_time_diagnostic()->discarded == 65534); retained();
    printf("MAC time: %u native calls, real timebase, coherent latch/erratum/bounds/retained failures PASS; synthetic only.\n", calls);
    return 0;
}
#endif
