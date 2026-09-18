/* SPDX-License-Identifier: BSD-3-Clause
 * Original shared synthetic RX model; not an independent silicon oracle.
 * Including the host-only foundation avoids a second successful RX stub.
 */
#define main radio_rx_foundation_main
#include "test_radio_rx.c"
#undef main
#include "radio_rx_fixture.h"

#define DEFINE(name, address) volatile uint8_t name;
RXF_REGISTERS(DEFINE)
#undef DEFINE
#define state radio_rx_fixture_state
static unsigned exporting, starting, fixture_cases, clock_failure;
static const char *scenario;

static uint16_t fixture_address(const volatile void *p)
{
    if (p == &radio_rx_fixture_frame) return 0x400;
    if (p == &radio_rx_fixture_diagnostics) return 0x500;
    return xaddress(p);
}
static void fixture_store(uint8_t address, uint8_t before, uint8_t value)
{
    if (address == SOC_RFST_ADDRESS) { store(address, before, value); return; }
    consume_reads();
    assert(write_count == 1 && writes[0].address == address && writes[0].after == value);
    write_count = 0;
    if (!starting) {
        assert(address == SOC_CLKCONCMD_ADDRESS && (value == 0x88 || (clock_failure && value == 0xc9)));
        if (!clock_failure) SOC_CLKCONSTA = value;
        trace('w', address, value);
    }
}
static void begin(const char *name, unsigned length, uint8_t crc)
{
    reset(length, 0x81, crc, 0);
    scenario = name;
    clock_failure = 0;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9;
    SOC_P0 = SOC_P1 = SOC_P2 = 255;
    RXF_IP0 = RXF_IP1 = RXF_TCON = RXF_S0CON = RXF_S1CON = RXF_IRCON2 = 0;
    host_mmio_write_hook = fixture_store;
    host_mmio_xaddress_hook = fixture_address;
    starting = 1; assert(_sdcc_external_startup() == 0); starting = 0;
}
static void emit_start(unsigned first)
{
    unsigned i;
    if (!exporting) return;
    printf("{\"name\":\"%s\",\"first\":%u,\"initial\":{", scenario, first);
#define REG(name, address) printf("\"%u\":%u,", address, name);
    CC2530_REGISTER_LIST(REG)
    RXF_REGISTERS(REG)
#undef REG
    for (i = 0; i < sizeof(xregs); i++)
        printf("\"%u\":%u%s", 0x6100u+i, xregs[i], i+1 == sizeof(xregs) ? "" : ",");
    printf("},\"events\":["); trace_count = 0; trace_enabled = 1;
}
static void emit_end(void)
{
    unsigned i;
    consume_reads(); fixture_cases++;
    assert(!write_count && !xwrite_count);
    assert(m0_status.heartbeat == state.completed && state.completed <= state.attempt &&
           state.attempt <= RXF_ATTEMPTS);
    if (state.result != RADIO_RX_OK)
        for (i = 0; i < 128; i++) assert(((uint8_t *)&radio_rx_fixture_frame)[i] == 0xa5);
    else {
        assert(radio_rx_fixture_frame.length == expected_length - 2);
        assert(radio_rx_fixture_frame.rssi_raw == wire[expected_length-1]);
        assert(radio_rx_fixture_frame.correlation == (wire[expected_length] & 127));
        assert(!memcmp(radio_rx_fixture_frame.body, wire + 1, expected_length-2));
        for (i = expected_length-2; i < 125; i++) assert(radio_rx_fixture_frame.body[i] == 0xa5);
    }
    if (!exporting) return;
    trace_enabled = 0;
    printf("],\"state\":\"");
    for (i = 0; i < RXF_SIZE; i++) printf("%02x", ((volatile uint8_t *)&state)[i]);
    printf("\",\"frame\":\"");
    for (i = 0; i < 128; i++) printf("%02x", ((uint8_t *)&radio_rx_fixture_frame)[i]);
    printf("\",\"boot\":\"");
    for (i = 0; i < 32; i++) printf("%02x", ((volatile uint8_t *)&m0_status)[i]);
    puts("\"}");
}
static void initialize(void)
{
    emit_start(1); radio_rx_fixture_initialize(); emit_end();
}
static void step(void)
{
    config_writes = on_writes = stop_writes = flush_writes = 0;
    emit_start(0); radio_rx_fixture_step(); emit_end();
}
static void retained(void)
{
    radio_rx_fixture_t copy;
    unsigned r = reads_total, w = writes_total;
    memcpy(&copy, (const void *)&state, sizeof(copy));
    radio_rx_fixture_step();
    assert(r == reads_total && w == writes_total && !memcmp(&copy, (const void *)&state, sizeof(copy)));
}
int main(int argc, char **argv)
{
    unsigned i, bit, low;
    (void)radio_rx_foundation_main;
    assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "--vectors")));
    exporting = argc == 2;
    begin("minimum", 3, 0xff); initialize(); step(); step();
    assert(state.phase == RXF_READY && state.completed == 1);
    begin("maximum", 127, 0x80); initialize(); step(); step();
    assert(state.completed == 1);
    begin("CRC reuse and hard cap", 5, 0x69); initialize(); step(); step();
    assert(state.result == RADIO_RX_BAD_CRC && !state.completed && !radio_rx_fault);
    wire[5] |= 128;
    SOC_IRCON = 0x80;
    for (i = 1; i < 16; i++) step();
    assert(state.completed == 15 && state.attempt == 16 && state.phase == RXF_READY);
    step(); assert(state.phase == RXF_END); retained();
    begin("timeout retained", 5, 0x80); initialize(); step();
    tick_step = 2048; silent_channel = 1; step();
    assert(state.result == RADIO_RX_TIMEOUT && state.phase == RXF_FAULT &&
           state.fault_latch == RADIO_RX_TIMEOUT && XR(0x618b) == 128);
    retained();
    begin("bad initial sleep", 5, 0x80); SOC_SLEEPCMD = 5; initialize();
    assert(state.phase == RXF_FAULT && state.reason == RXF_ENTRY); retained();
    begin("clock failure", 5, 0x80); initialize();
    clock_failure = 1; tick_step = 2048; step();
    assert(state.phase == RXF_FAULT && state.reason == RXF_CLOCK_ERROR &&
           state.clock_result == CLOCK_TIMEOUT && !state.attempt); retained();
    begin("entry TXACKDONE", 5, 0x80); initialize(); step(); SOC_RFIRQF1 = 1; step();
    assert(state.result == RADIO_RX_STATE_CHANGED && !on_writes); retained();
    begin("entry RXP1 high bit", 5, 0x80); initialize(); step(); XR(0x619f) = 128; step();
    assert(state.result == RADIO_RX_NOT_EMPTY && !on_writes); retained();
    begin("configuration readback", 5, 0x80); initialize(); step(); ignored_config = 2; step();
    assert(state.result == RADIO_RX_STATE_CHANGED && !on_writes); retained();
    begin("entry overflow", 5, 0x80); initialize(); step(); XR(0x619b) = 129; step();
    assert(state.result == RADIO_RX_COUNT_ERROR && !on_writes); retained();
    begin("controller error", 5, 0x80); initialize(); step(); SOC_RFERRF = 4; step();
    assert(state.result == RADIO_RX_CONTROLLER_ERROR && !on_writes); retained();
    begin("STIF deassertion", 5, 0x80); SOC_IRCON = 128; initialize(); step(); SOC_IRCON = 0; step();
    assert(state.reason == RXF_INVARIANT && !state.attempt); retained();
    begin("FSCAL1 postcal 30", 5, 0x80); initialize(); step(); step();
    assert(state.phase == RXF_READY && state.completed == 1 && XR(0x61ae) == 0x30);
    begin("FSCAL1 postcal FC", 5, 0x80); fscal1_after_cal = 0xfc; initialize(); step(); step();
    assert(state.phase == RXF_READY && state.completed == 1 && XR(0x61ae) == 0xfc);
    for (i = 1; i <= 3; i++) {
        begin("FSCAL1 postcal low bits", 5, 0x80); fscal1_after_cal = (uint8_t)(0x30u | i);
        initialize(); step(); step();
        assert(state.phase == RXF_FAULT && state.result == RADIO_RX_STATE_CHANGED &&
               state.fault_latch == RADIO_RX_STATE_CHANGED && !state.completed &&
               radio_rx_fixture_diagnostics.phase == 2 && !radio_rx_fixture_diagnostics.sample_valid &&
               on_writes == 1 && !stop_writes && !flush_writes && !rd);
        retained();
    }
    begin("FSCAL1 configuration low bits", 5, 0x80); initialize(); step();
    inject_sample = 19; inject_address = 0x61ae; inject_value = 0x31; step();
    assert(state.phase == RXF_FAULT && state.result == RADIO_RX_STATE_CHANGED && !on_writes);
    retained();
    if (exporting) return 0;
    for (i = 0; i < 256; i += 4) for (low = 0; low < 4; low++) {
        begin("FSCAL1 exhaustive", 5, 0x80); XR(0x61ae) = (uint8_t)(i | 3u);
        fscal1_after_write = (uint8_t)i;
        fscal1_after_cal = (uint8_t)(i | low); initialize(); step(); step();
        if (low) {
            assert(state.phase == RXF_FAULT && state.result == RADIO_RX_STATE_CHANGED &&
                   state.fault_latch == RADIO_RX_STATE_CHANGED && !state.completed &&
                   radio_rx_fixture_diagnostics.phase == 2 && !radio_rx_fixture_diagnostics.sample_valid &&
                   on_writes == 1 && !stop_writes && !flush_writes && !rd);
            retained();
        } else assert(state.phase == RXF_READY && state.completed == 1 && !state.fault_latch);
    }
    /* Genuine full fixed cap, not a shortened successful service substitute. */
    begin("stopped timer cap", 5, 0x80); initialize(); step();
    tick_step = 0; silent_channel = 1; step();
    assert(state.result == RADIO_RX_POLL_LIMIT && radio_rx_fixture_diagnostics.polls == 65535 &&
           state.phase == RXF_FAULT && state.fault_latch == RADIO_RX_POLL_LIMIT);
    retained();
    for (i = 0; i < 7; i++) for (bit = 0; bit < 8; bit++) {
        volatile uint8_t *p[] = {&RXF_IP0, &RXF_IP1, &RXF_TCON, &RXF_S0CON,
                                &RXF_S1CON, &RXF_IRCON2, &SOC_IRCON};
        begin("flag invariant", 5, 0x80); initialize(); step();
        *p[i] = (uint8_t)(1u << bit); step();
        assert(state.phase == (i == 6 && bit == 7 ? RXF_READY : RXF_FAULT));
        if (state.phase == RXF_FAULT) retained();
    }
    printf("RX fixture: %u real bootstrap/clock/RX host checkpoints PASS; synthetic only.\n", fixture_cases);
    return 0;
}
