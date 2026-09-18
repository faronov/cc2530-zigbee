/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic controller only. Component executable: NEVER flash or transmit.
 */
#include "radio_tx.h"
#include "radio_fifo.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>

MCU_XDATA struct {
    uint8_t before[4];
    radio_tx_diagnostics_t d;
    uint8_t after[4];
} radio_tx_test_guard;
#define diag radio_tx_test_guard.d
MCU_XDATA radio_fifo_diagnostics_t radio_tx_test_fifo;
MCU_XDATA uint8_t radio_tx_test_body[125];
volatile MCU_XDATA uint8_t radio_tx_test_action, radio_tx_test_mode, radio_tx_test_channel;
volatile MCU_XDATA uint8_t radio_tx_test_power, radio_tx_test_length, radio_tx_test_return;
volatile MCU_XDATA uint16_t radio_tx_test_limit, radio_tx_test_target;
volatile MCU_XDATA uint32_t radio_tx_test_timeout;
extern MCU_XDATA uint8_t radio_tx_fault, radio_tx_reserved_end;

void radio_tx_test_cycle(void)
{
    radio_tx_diagnostics_t MCU_XDATA *d;
#if defined(__SDCC)
    __asm
        .globl _radio_tx_test_before
    _radio_tx_test_before:
        nop
    __endasm;
    d = (radio_tx_diagnostics_t MCU_XDATA *)radio_tx_test_target;
#else
    d = radio_tx_test_target ? &diag : NULL;
#endif
    if (radio_tx_test_action == 0)
        radio_tx_test_return = radio_tx_send_init((radio_tx_mode_t)radio_tx_test_mode,
            radio_tx_test_channel, radio_tx_test_power, radio_tx_test_length,
            radio_tx_test_timeout, radio_tx_test_limit, d);
    else if (radio_tx_test_action == 1)
        radio_tx_test_return = radio_tx_cca_init(radio_tx_test_channel,
            radio_tx_test_timeout, radio_tx_test_limit, d);
    else if (radio_tx_test_action == 2)
        radio_tx_test_return = radio_fifo_preload_init(radio_tx_test_body, radio_tx_test_length,
            radio_tx_test_timeout, radio_tx_test_limit, &radio_tx_test_fifo);
    else
        radio_tx_test_return = radio_fifo_clear_init(radio_tx_test_timeout,
            radio_tx_test_limit, &radio_tx_test_fifo);
#if defined(__SDCC)
    __asm
        .globl _radio_tx_test_done
    _radio_tx_test_done:
        nop
    __endasm;
#endif
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t radio_tx_test_result[8];
void main(void)
{
    uint8_t i;
    for (i = 0; i < 125; i++) radio_tx_test_body[i] = i ^ 0x69;
    for (i = 0; i < sizeof(radio_tx_test_guard); i++)
        ((uint8_t MCU_XDATA *)&radio_tx_test_guard)[i] = 0x69;
    for (i = 0; i < sizeof(radio_tx_test_fifo); i++)
        ((uint8_t MCU_XDATA *)&radio_tx_test_fifo)[i] = 0x69;
    radio_tx_test_result[0] = 'T'; radio_tx_test_result[1] = 'X';
    radio_tx_test_result[2] = 'O'; radio_tx_test_result[3] = '1';
    radio_tx_test_result[4] = 1; radio_tx_test_result[5] = 8;
    radio_tx_test_result[6] = radio_tx_test_result[7] = 0;
    for (;;) radio_tx_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>

/* Never linked into firmware: original synthetic controller, not a silicon
 * timing model. All shared 32-entry MMIO logs are consumed one event at a time.
 */
MCU_XDATA uint8_t _gptrput_PARM_2;
static uint8_t xregs[1024];
#define XR(a) xregs[(a)-0x6000u]
static uint16_t last_address;
static uint8_t last_value;
static unsigned have_read, events, cases, tracing, first_step;
static uint32_t ticks, tick_step, latched;
static unsigned rx_pending, tx_pending, stop_pending, tx_commands, rx_commands, sample_commands;
static unsigned cca_clear, rx_delay, tx_delay, stop_delay, stuck_rx, stuck_tx, stuck_stop;
static unsigned ignored_command, missing_done, tx_error, rx_bytes, ignored_setting, ignored_ack;
static unsigned stale_sample, postcal, clock_fault, count_fault, keep_active, done_at_restore;
static unsigned settle_count, phase, inject, inject_address, inject_value, tx_drained;

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
static void record(char kind, uint16_t a, uint8_t v)
{
    if (tracing) printf("%s[\"%c\",%u,%u]", events ? "," : "", kind, a, v);
    events++;
}
static void progress(void)
{
    if (rx_pending && !stuck_rx && !--rx_pending) {
        XR(0x6192) = 0; XR(0x6193) = (uint8_t)((XR(0x6193) & 8) | 5 | (cca_clear ? 16 : 0));
        XR(0x6199) = 1; XR(0x61ae) = (uint8_t)postcal;
    }
    if (tx_pending && !stuck_tx) {
        tx_pending--;
        if (!tx_pending) {
            if (!missing_done) SOC_RFIRQF1 |= 2;
            SOC_RFERRF |= (uint8_t)tx_error;
            XR(0x6192) = 0;
            XR(0x6193) = (uint8_t)((XR(0x6193) & 0xd8) |
                                 (XR(0x618b) ? 5 : 0) | (keep_active ? 2 : 0));
            if (tx_drained) { XR(0x61a1) = XR(0x61a2); XR(0x619c) = 0; }
            if (!XR(0x618b)) SOC_RFIRQF1 |= 4;
        } else if (tx_pending == 1) { XR(0x6192) = 0; XR(0x6193) |= 0x26; }
    }
    if (stop_pending && !stuck_stop && !tx_pending && !--stop_pending) {
        XR(0x6192) = 0; XR(0x6193) &= 0xd8; XR(0x6199) = 0;
        SOC_RFIRQF0 |= 0x80; SOC_RFIRQF1 |= 4;
        if (rx_bytes) {
            XR(0x619b) = XR(0x619e) = (uint8_t)rx_bytes; XR(0x6193) |= 0xc0;
        }
    }
    if (inject && ++phase == inject) {
        if (inject_address == 0xbf) SOC_RFERRF = (uint8_t)inject_value;
        else XR(inject_address) = (uint8_t)inject_value;
    }
}
static uint8_t load(uint8_t a, uint8_t v)
{
    consume();
    if (a == 0xa8) progress();
    if (a == 0x95) { latched = ticks; ticks = (ticks + tick_step) & TIMEBASE_TICKS_MASK; }
    if (a >= 0x95 && a <= 0x97) v = (uint8_t)(latched >> (8*(a-0x95)));
    assert(a != 0xd9 && a != 0xe1);
    record('r', a, v); last_address = a; last_value = v; have_read = 1;
    return v;
}
static uint8_t xload(uint16_t a)
{
    uint8_t v;
    consume(); assert(a >= 0x6000 && a < 0x6400);
    v = XR(a);
    record('r', a, v); last_address = a; last_value = v; have_read = 1;
    return v;
}
static void store(uint8_t a, uint8_t before, uint8_t v)
{
    consume();
    assert(write_count == 1 && writes[0].address == a &&
           writes[0].before == before && writes[0].after == v);
    write_count = 0; record('w', a, v);
    if (a == 0xd9) {
        assert(!tx_pending && XR(0x619c) < 128);
        XR(0x6080 + XR(0x619c)) = v; XR(0x619c)++; XR(0x61a2)++;
    } else if (a == 0x91) {
        assert(v == 0x3d); SOC_RFIRQF1 = ignored_ack ? before : (uint8_t)(before & v);
    } else {
        assert(a == 0xe1);
        if (v == 0xed || v == 0xee) {
            assert(!XR(0x618b) && !(XR(0x6193) & 0x27) && !(XR(0x6192) & 0x40));
            if (v == 0xed) {
                XR(0x619b) = XR(0x619d) = XR(0x619e) = XR(0x619f) = 0;
                XR(0x6193) &= 0x3f;
            } else XR(0x619c) = XR(0x61a1) = XR(0x61a2) = 0;
        } else if (v == 0xe3) {
            rx_commands++;
            if (ignored_command != v) {
                rx_pending = rx_delay + 1; XR(0x618b) = 0x80; XR(0x6192) = 0x40;
                XR(0x6199) = 0; XR(0x6193) &= 8; /* SRXON does not sample CCA. */
            }
        } else if (v == 0xeb) {
            sample_commands++; assert(settle_count && XR(0x6199) == 1);
            XR(0x6193) = (uint8_t)((XR(0x6193) & 0xf7) | ((XR(0x6193) & 16) >> 1));
        } else {
            assert(v == 0xe9 || v == 0xea); tx_commands++;
            if (v == 0xea) {
                assert(settle_count && XR(0x6199) == 1);
                if (stale_sample) XR(0x6193) &= 0xe7; /* busy at strobe, not earlier */
                XR(0x6193) = (uint8_t)((XR(0x6193) & 0xf7) | ((XR(0x6193) & 16) >> 1));
            }
            if (ignored_command == v || (v == 0xea && !(XR(0x6193) & 8))) return;
            tx_pending = tx_delay + 2; XR(0x6192) = 0x40;
            XR(0x6193) = (uint8_t)((XR(0x6193) & 0xd8) | 2);
            if (clock_fault) SOC_CLKCONCMD = SOC_CLKCONSTA = 0x98;
            if (count_fault) XR(0x619c) = 129;
        }
    }
}
static void xstore(uint16_t a, uint8_t v)
{
    consume(); assert(xwrite_count == 1 && xwrites[0].address == a && xwrites[0].value == v);
    xwrite_count = 0; record('w', a, v);
    assert(a >= 0x6180 && a < 0x6200);
    if (a == 0x618d) {
        assert(v == 128); XR(0x618b) &= 0x7f; stop_pending = stop_delay + 1;
    } else if (a != ignored_setting) {
        XR(a) = v;
        if (a == 0x618a && v == 1 && done_at_restore) SOC_RFIRQF1 |= 2;
    }
}
static uint16_t xaddress(const volatile void *object)
{
    if (object == &diag) return radio_tx_test_target;
    if (object == &radio_tx_reserved_end) return 0x300;
    if (object == &_gptrput_PARM_2) return 0x700;
    assert(0); return 0;
}
static void cycles(uint8_t n) { consume(); assert(n == 4); settle_count++; }
static void reset(void)
{
    unsigned i;
    host_mmio_reset(); radio_tx_fault = 0;
    memset(xregs, 0, sizeof(xregs));
    memset(&radio_tx_test_guard, 0x69, sizeof(radio_tx_test_guard));
    memset(&radio_tx_test_fifo, 0x69, sizeof(radio_tx_test_fifo));
    for (i = 0; i < 125; i++) radio_tx_test_body[i] = (uint8_t)(i ^ 0x69);
    ticks = latched = 0; tick_step = 1;
    rx_pending = tx_pending = stop_pending = tx_commands = rx_commands = sample_commands = 0;
    rx_delay = tx_delay = stop_delay = stuck_rx = stuck_tx = stuck_stop = ignored_command = 0;
    missing_done = tx_error = rx_bytes = ignored_setting = ignored_ack = stale_sample = 0;
    clock_fault = count_fault = keep_active = done_at_restore = settle_count = 0;
    phase = inject = inject_address = inject_value = tx_drained = 0;
    cca_clear = 1; postcal = 0x30; have_read = events = 0; first_step = 1;
    SOC_SLEEPCMD = 4; SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88;
    XR(0x624a) = 0xa5; XR(0x6189) = 0x40; XR(0x618a) = 1;
    XR(0x6180) = 0x0d; XR(0x6182) = 7; XR(0x618e) = 15; XR(0x618f) = 11;
    XR(0x6190) = 0xf5; XR(0x6191) = 0x69; XR(0x6194) = 0x40; XR(0x6195) = 1;
    XR(0x6196) = 0xe0; XR(0x6197) = 0x1a;
    XR(0x61a8) = 0x85; XR(0x61a9) = 0x14; XR(0x61b8) = 0x75; XR(0x61b9) = 8;
    XR(0x61b2) = 0x11; XR(0x61fa) = 0x0f; XR(0x61ae) = 0x2b;
    radio_tx_test_mode = RADIO_TX_DIRECT; radio_tx_test_channel = 15;
    radio_tx_test_power = RADIO_TX_POWER_05; radio_tx_test_length = 3;
    radio_tx_test_timeout = 10000; radio_tx_test_limit = 1000; radio_tx_test_target = 0x500;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xread_hook = xload; host_mmio_xwrite_hook = xstore;
    host_mmio_xaddress_hook = xaddress; host_mmio_cycles_hook = cycles;
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
    unsigned i, before;
    radio_tx_diagnostics_t saved;
    radio_tx_test_action = (uint8_t)action; cases++; events = 0;
    before = radio_tx_fault; memcpy(&saved, &diag, sizeof(saved));
    if (tracing) {
        printf("%s{\"action\":%u,\"mode\":%u,\"channel\":%u,\"power\":%u,\"length\":%u,"
               "\"timeout\":%lu,\"limit\":%u,\"target\":%u,\"events\":[",
               first_step ? "" : ",", action, radio_tx_test_mode, radio_tx_test_channel,
               radio_tx_test_power, radio_tx_test_length, (unsigned long)radio_tx_test_timeout,
               radio_tx_test_limit, radio_tx_test_target == 0x500 ? 65535 :
               radio_tx_test_target == 0x700 ? 65534 :
               radio_tx_test_target == 0x300 ? 65533 : radio_tx_test_target);
        first_step = 0;
    }
    radio_tx_test_cycle(); consume();
    if (radio_tx_test_return != expected) {
        fprintf(stderr, "case%u action%u expected%u got%u phase%u polls%u\n", cases, action, expected,
                radio_tx_test_return, diag.phase, diag.polls);
        assert(radio_tx_test_return == expected);
    }
    assert(!write_count && !xwrite_count);
    for (i = 0; i < 4; i++)
        assert(radio_tx_test_guard.before[i] == 0x69 && radio_tx_test_guard.after[i] == 0x69);
    for (i = 0; i < 125; i++) assert(radio_tx_test_body[i] == (uint8_t)(i ^ 0x69));
    if (action < 2) {
        if (before || (expected >= 3 && expected <= 5)) {
            assert(!events && !memcmp(&saved, &diag, sizeof(saved)));
        } else if (expected <= 2) {
            assert(!radio_tx_fault && diag.phase == 7 && diag.radio_idle && XR(0x618a) == 1);
            assert(!XR(0x618b) && !(XR(0x6193) & 0x27) && !(XR(0x6192) & 0x40));
            assert(diag.writes == 10 && diag.verified == 10 && diag.polls <= radio_tx_test_limit);
            assert(expected != RADIO_TX_PHY_DONE || (diag.txdone && tx_commands));
            assert(action != 1 || (!tx_commands && settle_count));
        } else assert(radio_tx_fault == expected);
    }
    if (tracing) {
        radio_fifo_diagnostics_t *f = &radio_tx_test_fifo;
        printf("],\"result\":%u,\"fault\":%u,\"diagnostics\":[%lu,%u",
               expected, radio_tx_fault, (unsigned long)diag.elapsed_ticks, diag.polls);
#define D(n) printf(",%u", diag.n);
        D(phase) D(writes) D(verified) D(actions) D(sample_valid) D(cca) D(txdone) D(radio_idle)
        D(timebase_status) D(errors) D(flags0) D(flags1) D(rx_enable) D(fsm0) D(signals) D(rssi_valid)
        D(rx_count) D(tx_count) D(rx_first) D(rx_last) D(rx_packet) D(tx_first) D(tx_last)
#undef D
        printf("],\"fifo\":[%lu,%u", (unsigned long)f->elapsed_ticks, f->polls);
#define F(n) printf(",%u", f->n);
        F(timebase_status) F(strobes) F(confirmed) F(bytes_written) F(bytes_verified) F(errors)
        F(rx_count) F(tx_count) F(rx_first) F(rx_last) F(rx_packet) F(tx_first) F(tx_last)
        F(fifo_signals) F(sample_valid)
#undef F
        printf("]}");
    }
}
static void retained(void)
{
    unsigned result = radio_tx_fault;
    assert(result);
    radio_tx_test_mode = 255; radio_tx_test_channel = 0; radio_tx_test_timeout = 0;
    radio_tx_test_target = 0; step(0, result); step(1, result);
}
static void scenarios(void)
{
    unsigned i;
    reset(); begin("direct minimum"); radio_tx_test_length = 1;
    step(2, 0); step(0, 0); step(3, 0); finish();
    reset(); ticks = 0xffffeb; begin("TX deadline across natural wrap");
    step(2, 0); assert(ticks == 0xfffff0);
    step(0, 0); assert(ticks < 256 && diag.elapsed_ticks == 24); finish();
    reset(); begin("conditional maximum delayed wrap");
    radio_tx_test_length = 125; radio_tx_test_mode = 2;
    rx_delay = 2; tx_delay = 3; stop_delay = 2; ticks = 0xfffff0;
    step(2, 0); step(0, 0); step(3, 0); finish();
    reset(); begin("CCA clear"); step(1, 1); step(3, 1); finish();
    reset(); cca_clear = 0; begin("CCA busy"); step(1, 2); finish();
    reset(); cca_clear = 0; XR(0x6193) = 8; begin("old sampled clear cannot override fresh busy");
    step(1, 2); finish();
    reset(); cca_clear = 0; radio_tx_test_mode = 2; begin("conditional busy");
    step(2, 0); step(0, 2); step(3, 0); cca_clear = 1;
    step(2, 0); step(0, 0); step(3, 0); finish();
    reset(); stale_sample = 1; radio_tx_test_mode = 2; begin("became busy at strobe");
    step(2, 0); step(0, 2); finish();
    reset(); rx_bytes = 7; begin("CCA backlog requires explicit quiescent flush");
    step(1, 1); step(3, 0); finish();
    reset(); tx_drained = 1; begin("post TX pointer is not assumed reset");
    step(2, 0); step(0, 0); step(3, 0); finish();
    reset(); SOC_RFIRQF1 = 2; begin("clear stale TXDONE");
    step(2, 0); step(0, 0); finish();
    reset(); SOC_RFIRQF1 = 6; ignored_ack = 1; begin("unconfirmed stale flag clear");
    step(2, 0); step(0, 7); retained(); finish();
    reset(); ignored_command = 0xe9; begin("ignored TX not completion");
    step(2, 0); radio_tx_test_limit = 28; step(0, 11); retained(); finish();
    reset(); ignored_command = 0xe3; begin("ignored RX not valid CCA");
    radio_tx_test_limit = 26; step(1, 11); retained(); finish();
    reset(); stuck_rx = 1; begin("calibration never ready");
    radio_tx_test_timeout = 26; step(1, 10); retained(); finish();
    reset(); stuck_tx = 1; begin("TX stuck after command");
    step(2, 0); radio_tx_test_limit = 26; step(0, 11); retained(); finish();
    reset(); missing_done = 1; begin("idle without TXDONE is not completion");
    step(2, 0); radio_tx_test_timeout = 26; step(0, 10); retained(); finish();
    reset(); begin("TXDONE at deadline is still timeout");
    step(2, 0); radio_tx_test_timeout = 23; step(0, 10);
    assert(diag.txdone && !diag.radio_idle && XR(0x618a) == 0); retained(); finish();
    reset(); SOC_RFIRQF1 = 1; begin("TXACKDONE is not TXDONE");
    step(2, 0); step(0, 7); retained(); finish();
    reset(); keep_active = 1; begin("TXDONE without idle is not completion");
    step(2, 0); radio_tx_test_limit = 28; step(0, 11); retained(); finish();
    reset(); stuck_stop = 1; begin("busy but shutdown stuck");
    cca_clear = 0; radio_tx_test_limit = 29; step(1, 11); retained(); finish();
    reset(); done_at_restore = 1; begin("uncommanded TXDONE after CCA");
    step(1, 7); retained(); finish();
    reset(); begin("deadline equality suppresses configuration write");
    radio_tx_test_timeout = 1; step(1, 10); retained(); finish();
    reset(); begin("no action without confirmation budget");
    radio_tx_test_limit = 1; tick_step = 0; step(1, 11); retained(); finish();
    reset(); begin("half-range ambiguity");
    tick_step = 0x802710; step(1, 12); retained(); finish();
    reset(); begin("backward counter");
    tick_step = 0xffffff; step(1, 13); retained(); finish();
    reset(); ignored_setting = 0x6180; begin("configuration readback failure");
    step(1, 7); retained(); finish();
    reset(); postcal = 0xfc; begin("FSCAL reserved bits after calibration"); step(1, 1); finish();
    reset(); postcal = 0x31; begin("FSCAL low bits after calibration"); step(1, 7); retained(); finish();
    reset(); clock_fault = 1; begin("clock changed after TX command");
    step(2, 0); step(0, 7); retained(); finish();
    reset(); count_fault = 1; begin("FIFO counter overflow");
    step(2, 0); step(0, 9); retained(); finish();
    reset(); begin("RX overflow while sensing is not recoverable busy");
    inject = 23; inject_address = 0x6193; inject_value = 0x45;
    step(1, 8); retained(); finish();
    reset(); begin("PHY header mismatch is not an accepted payload");
    step(2, 0); XR(0x6080) ^= 1; step(0, 14); retained(); finish();
    reset(); begin("preload length mismatch");
    step(2, 0); radio_tx_test_length = 4; step(0, 14); retained(); finish();
    reset(); SOC_IEN0 = 0x80; begin("IRQs are not silently disabled");
    step(1, 6); retained(); finish();
    reset(); SOC_CLKCONSTA = 0xc9; begin("XOSC command status mismatch");
    step(1, 6); retained(); finish();
    reset(); XR(0x61e1) = 0x20; begin("running CSP is not silently cancelled");
    step(1, 6); retained(); finish();
    for (i = 0; i < 7; i++) {
        reset(); tx_error = 1u << i; begin("RFERR retained even with TXDONE");
        step(2, 0); step(0, 8); retained(); finish();
    }
    reset(); XR(0x6190) = 0xff; begin("explicit power overrides reset"); step(1, 1); finish();
    reset(); XR(0x6191) = 9; begin("unsupported TXCTRL profile"); step(1, 6); retained(); finish();
    reset(); XR(0x6197) = 0xff; begin("explicit CCA profile overrides reset"); step(1, 1); finish();
    reset(); XR(0x618a) = 0; begin("legacy RX history not admitted"); step(1, 14); retained(); finish();
    reset(); begin("transmit without preload"); step(0, 14); retained(); finish();
    reset(); begin("CCA with loaded TX is unsupported"); step(2, 0); step(1, 14); retained(); finish();
    reset(); begin("invalid public inputs have no MMIO");
    radio_tx_test_mode = 3; step(0, 3);
    radio_tx_test_mode = 1; radio_tx_test_power = 0xf5; step(0, 3);
    radio_tx_test_power = 5; radio_tx_test_channel = 27; step(0, 3); step(1, 3);
    radio_tx_test_channel = 11; radio_tx_test_target = 0; step(0, 3); step(1, 3);
    radio_tx_test_target = 0x1f00; step(0, 4); step(1, 4);
    radio_tx_test_target = 1; step(0, 5); step(1, 5);
    radio_tx_test_target = 0x300; step(0, 5); step(1, 5);
    radio_tx_test_target = 0x700; step(0, 5); step(1, 5);
    radio_tx_test_target = 0x1df0; step(0, 4); step(1, 4); finish();
}
int main(int argc, char **argv)
{
    unsigned channel, length, i;
    tracing = argc == 2 && !strcmp(argv[1], "--vectors");
    scenarios();
    if (tracing) return 0;
    for (channel = 11; channel <= 26; channel++) for (length = 1; length <= 125; length++) {
        reset(); radio_tx_test_channel = (uint8_t)channel; radio_tx_test_length = (uint8_t)length;
        radio_tx_test_mode = (length & 1) ? 1 : 2;
        step(2, 0); step(0, 0); step(3, 0);
        assert(XR(0x618f) == 11+5*(channel-11) && XR(0x6190) == 5);
    }
    for (i = 0; i < 256; i++) {
        reset(); radio_tx_test_power = (uint8_t)i;
        if (i != 5) step(0, 3);
        reset(); radio_tx_test_mode = (uint8_t)i;
        if (i != 1 && i != 2) step(0, 3);
        reset(); radio_tx_test_channel = (uint8_t)i;
        if (i < 11 || i > 26) step(1, 3);
        reset(); radio_tx_test_length = (uint8_t)i;
        if (!i || i > 125) step(0, 3);
    }
    for (i = 23; i < 32; i++) {
        reset(); radio_tx_test_timeout = 1UL << i; step(0, 3); step(1, 3);
    }
    reset(); radio_tx_test_timeout = 0; step(0, 3); step(1, 3);
    reset(); radio_tx_test_limit = 0; step(0, 3); step(1, 3);
    reset(); radio_tx_test_limit = 65535; stuck_rx = 1; tick_step = 0; step(1, 11);
    assert(diag.polls == 65535); retained();
    printf("TX/CCA: %u native calls, real FIFO/timebase, bounds and retained failures PASS; synthetic only.\n", cases);
    return 0;
}
#endif
