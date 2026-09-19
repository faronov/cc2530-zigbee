/* SPDX-License-Identifier: BSD-3-Clause
 * Original shared synthetic controller, not an independent silicon model.
 * Host ONLY: real bootstrap/clock/FIFO/TX; never link this file into firmware.
 */
#define main radio_tx_component_main
#include "test_radio_tx.c"
#undef main
#include "radio_tx_fixture.h"
#define state radio_tx_fixture_state
extern uint8_t radio_tx_fixture_initialized;
static unsigned exporting, starting, clock_failure, fixture_calls, first;
static unsigned fail_preload, fail_clear, clears;

static uint16_t fixture_address(const volatile void *p)
{
    if (p == &radio_tx_fixture_tx) return 0x500;
    return xaddress(p);
}
static void fixture_store(uint8_t a, uint8_t before, uint8_t v)
{
    if (starting || a == SOC_CLKCONCMD_ADDRESS) {
        consume(); assert(write_count == 1 && writes[0].address == a && writes[0].after == v);
        write_count = 0;
        if (!starting) {
            assert(v == 0x88 || (clock_failure && v == 0xc9));
            if (!clock_failure) SOC_CLKCONSTA = v;
            record('w', a, v);
        }
        return;
    }
    store(a, before, v);
    if (a == 0xd9 && fail_preload) XR(0x619c) = 129;
    if (a == 0xe1 && (v == 0xed || v == 0xee)) {
        clears++;
        if (fail_clear) SOC_RFERRF = 4;
    }
}
static void fixture_begin(const char *name)
{
    unsigned i;
    reset(); tracing = 0; radio_tx_fixture_initialized = 0;
    clock_failure = fail_preload = fail_clear = clears = 0;
    memset(&radio_tx_fixture_clock, 0, sizeof(radio_tx_fixture_clock));
    memset(&radio_tx_fixture_fifo, 0, sizeof(radio_tx_fixture_fifo));
    memset(&radio_tx_fixture_tx, 0, sizeof(radio_tx_fixture_tx));
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9;
    SOC_P0 = SOC_P1 = SOC_P2 = 255;
    host_mmio_write_hook = fixture_store; host_mmio_xaddress_hook = fixture_address;
    starting = 1; assert(_sdcc_external_startup() == 0); starting = 0;
    /* A stale packet before initialization must be cleared, never admitted. */
    memset((void *)radio_tx_fixture_mailbox, 0xa6, 8);
    radio_tx_fixture_initialize();
    assert(state.phase == TXF_DISARMED && !state.attempts && !events);
    for (i = 0; i < 8; i++) assert(!radio_tx_fixture_mailbox[i]);
    first = 1;
    if (!exporting) return;
    printf("{\"name\":\"%s\",\"initial\":{", name);
#define REG(n, a) printf("\"%u\":%u,", a, n);
    CC2530_REGISTER_LIST(REG)
#undef REG
    for (i = 0; i < sizeof(xregs); i++)
        printf("\"%u\":%u%s", 0x6000u+i, xregs[i], i+1 == sizeof(xregs) ? "" : ",");
    printf("},\"steps\":[");
}
static void packet_write(unsigned run)
{
    unsigned i;
    uint8_t p[8] = {0xa6, 0x59, 15, 0xf0, 0x3c, 0xc3, 0x69, 0x96};
    if (run) { p[0] = 0x59; p[1] = 0xa6; p[4] = 0xc3; p[5] = 0x3c; }
    for (i = 0; i < 8; i++) radio_tx_fixture_mailbox[i] = p[i];
}
static void hex(const volatile void *object, unsigned size)
{
    unsigned i; const volatile uint8_t *p = object;
    for (i = 0; i < size; i++) printf("%02x", p[i]);
}
static void checkpoint(unsigned repeat)
{
    unsigned i;
    if (exporting) {
        printf("%s{\"repeat\":%u,\"packet\":\"", first ? "" : ",", repeat);
        hex(radio_tx_fixture_mailbox, 8); printf("\",\"events\":["); first = 0;
    }
    events = 0; tracing = exporting;
    for (i = 0; i < repeat; i++) { radio_tx_fixture_poll(); fixture_calls++; }
    consume(); tracing = 0;
    assert(!write_count && !xwrite_count && state.attempts <= 1 && tx_commands <= 1);
    assert(state.reason == TXF_INVARIANT || m0_status.heartbeat == state.completed);
    if (!exporting) return;
    printf("],\"state\":\""); hex(&state, TXF_SIZE);
    printf("\",\"boot\":\""); hex(&m0_status, 32);
    printf("\",\"clock\":[%lu,%u,%u,%lu,%u,%u,%u,%u,%u,%u,%u],\"fifo\":[%lu,%u",
        (unsigned long)radio_tx_fixture_clock.request.elapsed_ticks,
        radio_tx_fixture_clock.request.polls, radio_tx_fixture_clock.request.timebase_status,
        (unsigned long)radio_tx_fixture_clock.rollback.elapsed_ticks,
        radio_tx_fixture_clock.rollback.polls, radio_tx_fixture_clock.rollback.timebase_status,
        radio_tx_fixture_clock.saved_command, radio_tx_fixture_clock.requested_command,
        radio_tx_fixture_clock.observed_command, radio_tx_fixture_clock.observed_status,
        radio_tx_fixture_clock.rollback_result,
        (unsigned long)radio_tx_fixture_fifo.elapsed_ticks, radio_tx_fixture_fifo.polls);
#define F(n) printf(",%u", radio_tx_fixture_fifo.n);
    F(timebase_status) F(strobes) F(confirmed) F(bytes_written) F(bytes_verified) F(errors)
    F(rx_count) F(tx_count) F(rx_first) F(rx_last) F(rx_packet) F(tx_first) F(tx_last)
    F(fifo_signals) F(sample_valid)
#undef F
    printf("],\"tx\":[%lu,%u", (unsigned long)radio_tx_fixture_tx.elapsed_ticks, radio_tx_fixture_tx.polls);
#define D(n) printf(",%u", radio_tx_fixture_tx.n);
    D(phase) D(writes) D(verified) D(actions) D(sample_valid) D(cca) D(txdone) D(radio_idle)
    D(timebase_status) D(errors) D(flags0) D(flags1) D(rx_enable) D(fsm0) D(signals) D(rssi_valid)
    D(rx_count) D(tx_count) D(rx_first) D(rx_last) D(rx_packet) D(tx_first) D(tx_last)
#undef D
    printf("]}");
}
static void admitted(void)
{
    packet_write(0); checkpoint(1); assert(state.phase == TXF_ARMED && !events);
    packet_write(1); checkpoint(1); assert(state.phase == TXF_ADMITTED && !events && !tx_commands);
}
static void retained_fixture(void)
{
    radio_tx_fixture_t copy;
    assert(state.phase == TXF_FAULT || state.phase == TXF_END);
    memcpy(&copy, (const void *)&state, sizeof(copy));
    events = 0; radio_tx_fixture_initialize(); radio_tx_fixture_poll();
    assert(!events && !memcmp(&copy, (const void *)&state, sizeof(copy)));
}
static void fixture_finish(void) { retained_fixture(); if (exporting) puts("]}"); }
int main(int argc, char **argv)
{
    unsigned i, stage, byte, bit;
    (void)radio_tx_component_main;
    assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "--vectors")));
    exporting = argc == 2;
    fixture_begin("default no command"); checkpoint(256);
    assert(state.reason == TXF_EXHAUSTED && !events); fixture_finish();
    fixture_begin("armed exhaustion"); packet_write(0); checkpoint(1); checkpoint(256);
    assert(state.reason == TXF_EXHAUSTED && !events); fixture_finish();
    fixture_begin("last admission poll"); checkpoint(255); packet_write(0); checkpoint(1);
    checkpoint(255); packet_write(1); checkpoint(1); checkpoint(1);
    assert(state.phase == TXF_END && tx_commands == 1); fixture_finish();
    for (stage = 0; stage < 2; stage++) for (byte = 0; byte < 8; byte++) {
        fixture_begin("damaged mailbox byte");
        if (stage) { packet_write(0); checkpoint(1); }
        packet_write(stage); radio_tx_fixture_mailbox[byte] ^= 1; checkpoint(1);
        assert(state.reason == TXF_PACKET && !events); fixture_finish();
    }
    fixture_begin("RUN before ARM"); packet_write(1); checkpoint(1);
    assert(state.reason == TXF_PACKET && !events); fixture_finish();
    fixture_begin("ARM replay"); packet_write(0); checkpoint(1); packet_write(0); checkpoint(1);
    assert(state.reason == TXF_PACKET && !events); fixture_finish();
    fixture_begin("new packet after admission"); admitted(); packet_write(1); checkpoint(1);
    assert(state.reason == TXF_PACKET && !events); fixture_finish();
    for (i = 0; i < 14; i++) {
        fixture_begin("real service outcome"); admitted();
        switch (i) {
        case 0: break;
        case 1: cca_clear = 0; break;
        case 2: stale_sample = 1; break;
        case 3: rx_bytes = 7; break;
        case 4: clock_failure = 1; tick_step = 512; break;
        case 5: SOC_RFERRF = 4; break;
        case 6: fail_preload = 1; break;
        case 7: ignored_setting = 0x6180; break;
        case 8: tx_error = 4; break;
        case 9: missing_done = 1; tick_step = 32; break;
        case 10: stuck_rx = 1; tick_step = 0; break;
        case 11: stuck_stop = 1; cca_clear = 0; tick_step = 0; break;
        case 12: fail_clear = 1; break;
        default: tx_drained = 1; break;
        }
        checkpoint(1);
        if (i < 4 || i == 13) {
            assert(state.phase == TXF_END && state.completed == 1 && state.attempts == 1);
            assert(!XR(0x618b) && !XR(0x619b) && !XR(0x619c));
            assert(state.tx_result == ((i == 1 || i == 2) ? RADIO_TX_CCA_BUSY : RADIO_TX_PHY_DONE));
        } else assert(state.phase == TXF_FAULT && !state.completed);
        if (i == 4) assert(state.reason == TXF_CLOCK && state.clock_result == CLOCK_TIMEOUT);
        if (i == 5) assert(state.reason == TXF_CLEAR);
        if (i == 6) assert(state.reason == TXF_PRELOAD);
        if (i == 10 || i == 11) assert(radio_tx_fixture_tx.polls == TXF_LIMIT);
        if (i == 12) assert(state.reason == TXF_FINAL_CLEAR);
        fixture_finish();
    }
    if (exporting) return 0;
    /* Every one-bit corruption, both phases, and every incomplete write. */
    for (stage = 0; stage < 2; stage++) for (byte = 0; byte < 8; byte++) for (bit = 0; bit < 8; bit++) {
        fixture_begin("single bit");
        if (stage) { packet_write(0); checkpoint(1); }
        packet_write(stage); radio_tx_fixture_mailbox[byte] ^= (uint8_t)(1u << bit);
        checkpoint(1); assert(state.reason == TXF_PACKET && !events); fixture_finish();
    }
    for (stage = 0; stage < 2; stage++) for (byte = 0; byte < 8; byte++) {
        fixture_begin("partial packet");
        if (stage) { packet_write(0); checkpoint(1); }
        packet_write(stage);
        for (i = byte; i < 8; i++) radio_tx_fixture_mailbox[i] = 0;
        checkpoint(1);
        assert(!events && state.phase != TXF_ADMITTED);
    }
    for (i = 6; i < 21; i++) {
        fixture_begin("public state cannot authorize");
        ((volatile uint8_t *)&state)[i] ^= 1; checkpoint(1);
        assert(!events && !tx_commands);
    }
    printf("TX fixture: %u native polls, real clock/FIFO/TX, admission/failures/caps PASS; synthetic only.\n",
           fixture_calls);
    return 0;
}
