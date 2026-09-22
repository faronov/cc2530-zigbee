/* SPDX-License-Identifier: BSD-3-Clause
 * Host-only reuse of the original synthetic owner model with real services.
 */
#define main radio_autoack_component_main
#include "test_radio_autoack.c"
#undef main
#include "radio_link_fixture.h"

extern uint8_t radio_link_fixture_initialized;
#define state radio_link_fixture_state
static unsigned exporting, starting, clock_failure, accelerated, extra_frames, late_receive, fixture_calls, first;
static uint16_t body_address = 0x900;

static uint16_t fixture_address(const volatile void *p)
{
    if (p == &radio_link_fixture_config) return normal_config;
    if (p == &radio_link_fixture_frames[0]) return normal_output;
    if (p == &radio_link_fixture_frames[1]) return normal_output + 128u;
    if (p == radio_link_fixture_body) return body_address;
    return xaddress(p);
}
static void fixture_store(uint8_t address, uint8_t before, uint8_t value)
{
    if (starting || address == SOC_CLKCONCMD_ADDRESS) {
        assert(write_count == 1 && writes[0].address == address && writes[0].after == value);
        write_count = 0; logs();
        if (!starting) {
            assert(value == 0x88 || (clock_failure && value == 0xc9));
            if (!clock_failure) SOC_CLKCONSTA = value;
            trace('w', address, value);
        }
        return;
    }
    store(address, before, value);
    if (address == SOC_RFST_ADDRESS && value == 0xea && accelerated)
        tick_step = accelerated;
}
static uint8_t fixture_xload(uint16_t address)
{
    uint8_t value = xload(address);
    if (address == 0x624a && mode == 2 && extra_frames) {
        while (extra_frames) { enqueue(5, 128, 0x51); extra_frames--; }
    }
    return value;
}
static void fixture_xstore(uint16_t address, uint8_t value)
{
    if (address == 0x618d && state.stage == 6 && late_receive) stop_receive = 1;
    xstore(address, value);
}
static void fixture_begin(void)
{
    unsigned i;
    reset(); printing = 0; radio_link_fixture_initialized = 0;
    clock_failure = accelerated = extra_frames = late_receive = 0;
    memset(&radio_link_fixture_clock, 0, sizeof(radio_link_fixture_clock));
    memset(&radio_link_fixture_radio, 0, sizeof(radio_link_fixture_radio));
    memset(radio_link_fixture_frames, 0, sizeof(radio_link_fixture_frames));
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9;
    SOC_P0 = SOC_P1 = SOC_P2 = 255;
    host_mmio_write_hook = fixture_store; host_mmio_xread_hook = fixture_xload;
    host_mmio_xwrite_hook = fixture_xstore;
    host_mmio_xaddress_hook = fixture_address;
    starting = 1; assert(_sdcc_external_startup() == 0); starting = 0;
    memset((void *)radio_link_fixture_mailbox, 0xa9, 8);
    radio_link_fixture_initialize();
    assert(state.phase == LNK_DISARMED && !state.consumed && !tx_attempts);
    for (i = 0; i < 8; i++) assert(!radio_link_fixture_mailbox[i]);
    first = 1;
}
static void packet_write(unsigned run)
{
    uint8_t packet[8] = {0xa9, 0x56, LNK_CHANNEL, (uint8_t)~LNK_CHANNEL, 0x36, 0xc9, 0x4c, 0xb3};
    unsigned i;
    if (run) { packet[0] = 0x56; packet[1] = 0xa9; packet[4] = 0xc9; packet[5] = 0x36; }
    for (i = 0; i < 8; i++) radio_link_fixture_mailbox[i] = packet[i];
}
static void begin_vector(unsigned n)
{
    unsigned i;
    if (!exporting) return;
    printf("{\"case\":%u,\"initial\":{", n);
#define REG(name, address) printf("\"%u\":%u,", address, name);
    CC2530_REGISTER_LIST(REG)
#undef REG
    for (i = 0; i < sizeof(xregs); i++)
        printf("\"%u\":%u%s", 0x6100u+i, xregs[i], i+1 == sizeof(xregs) ? "" : ",");
    printf("},\"steps\":[");
}
static void checkpoint(unsigned repeat)
{
    unsigned i, before = accesses;
    if (exporting) {
        printf("%s{\"repeat\":%u,\"packet\":\"", first ? "" : ",", repeat);
        hex((const void *)radio_link_fixture_mailbox, 8);
        printf("\",\"events\":["); first = 0; trace_count = 0;
    }
    printing = exporting;
    for (i = 0; i < repeat; i++) { radio_link_fixture_poll(); fixture_calls++; }
    printing = 0; logs();
    assert(!write_count && !xwrite_count && state.attempts <= 1 && tx_attempts <= 1);
    assert(state.reason == LNK_INVARIANT || m0_status.heartbeat == state.completed);
    if (state.phase <= LNK_ADMITTED) assert(accesses == before);
    if (state.phase == LNK_END) {
        assert(state.completed == 1 && state.consumed == 1 && !XR(0x618b) && !count &&
               radio_autoack_state == RADIO_AUTOACK_OFF_NOACK && !radio_autoack_fault &&
               config_writes == 25 && tx_flushes == 1 && tx_attempts == 1 &&
               tx_count == 14 && !memcmp(tx_fifo+1, radio_link_fixture_body, LNK_LENGTH));
    }
    if (!exporting) return;
    printf("],\"state\":\""); hex((const void *)&state, LNK_SIZE);
    printf("\",\"boot\":\""); hex((const void *)&m0_status, 32);
    printf("\",\"frames\":\""); hex(radio_link_fixture_frames, sizeof(radio_link_fixture_frames));
    printf("\",\"clock\":[%lu,%u,%u,%lu,%u,%u,%u,%u,%u,%u,%u],\"radio\":[",
        (unsigned long)radio_link_fixture_clock.request.elapsed_ticks,
        radio_link_fixture_clock.request.polls, radio_link_fixture_clock.request.timebase_status,
        (unsigned long)radio_link_fixture_clock.rollback.elapsed_ticks,
        radio_link_fixture_clock.rollback.polls, radio_link_fixture_clock.rollback.timebase_status,
        radio_link_fixture_clock.saved_command, radio_link_fixture_clock.requested_command,
        radio_link_fixture_clock.observed_command, radio_link_fixture_clock.observed_status,
        radio_link_fixture_clock.rollback_result);
    emit_diagnostics(); printf("]}");
}
static void admitted(void)
{
    unsigned before = accesses;
    packet_write(0); checkpoint(1); assert(state.phase == LNK_ARMED);
    packet_write(1); checkpoint(1); assert(state.phase == LNK_ADMITTED);
    assert(accesses == before && !state.consumed && !tx_attempts);
}
static void finish(void)
{
    radio_link_fixture_t saved;
    unsigned before = accesses;
    assert(state.phase == LNK_END || state.phase == LNK_FAULT);
    memcpy(&saved, (const void *)&state, sizeof(saved));
    radio_link_fixture_initialize(); radio_link_fixture_poll();
    assert(accesses == before && !memcmp(&saved, (const void *)&state, sizeof(saved)));
    if (exporting) puts("]}");
}

#define FIXTURE_CASES 35u
static void fixture_case(unsigned n)
{
    unsigned stage;
    fixture_begin();
    if (n == 8) { clock_failure = 1; tick_step = 512; }
    if (n == 9) SOC_RFERRF = 4;
    if (n == 10) ignored_enable = 1;
    if (n == 11) tx_write_fault = 12;
    if (n == 12) tx_error = 1;
    if (n == 13) { ignored_tx_clear = 1; SOC_RFIRQF1 = 7; }
    if (n == 14) { hold_stop = 1; tick_step = 0; }
    if (n == 15) extra_frames = 2;
    if (n == 16) extra_frames = 3;
    if (n == 5) cca_clear = 0;
    if (n == 6) accelerated = 32;
    if (n == 34) { accelerated = 32; late_receive = 1; }
    if (n == 7) arrival_on_ready = 1;
    if (n == 3 || n == 4 || n == 7 || n == 17) reply_after_tx = 1;
    if (n == 4) bad_reply = 1;
    begin_vector(n);
    if (n == 0 || n == 1) {
        if (n == 1) { packet_write(0); checkpoint(1); }
        checkpoint(LNK_ADMISSION_POLLS);
        assert(state.reason == LNK_EXHAUSTED && !state.consumed);
    } else if (n == 2) {
        packet_write(1); checkpoint(1); assert(state.reason == LNK_PACKET);
    } else if (n >= 18 && n < 34) {
        stage = (n - 18) / 8;
        if (stage) { packet_write(0); checkpoint(1); }
        packet_write(stage); radio_link_fixture_mailbox[(n - 18) % 8] ^= 1;
        checkpoint(1); assert(state.reason == LNK_PACKET);
    } else {
        if (n == 17) {
            checkpoint(LNK_ADMISSION_POLLS - 1); packet_write(0); checkpoint(1);
            checkpoint(LNK_ADMISSION_POLLS - 1); packet_write(1); checkpoint(1);
        } else admitted();
        checkpoint(1);
        if (n <= 7 || n == 17 || n == 34) {
            assert(state.phase == LNK_END && state.completed);
            assert(state.outcome == (n == 4 ? LNK_RX_BAD : n == 5 ? LNK_CCA_BUSY :
                                    n == 6 || n == 34 ? LNK_RX_TIMEOUT : LNK_RX_GOOD));
            assert(state.frames == (n == 5 || n == 6 ? 0 : n == 7 ? 2 : 1));
            assert(state.before_tx == (n == 7 ? 1 : 0));
            if (state.outcome == LNK_RX_GOOD || state.outcome == LNK_RX_BAD) {
                radio_autoack_frame_t *f = &radio_link_fixture_frames[state.before_tx];
                assert(f->length == 3 && f->body[0] == 2 && f->body[1] == 0 && f->body[2] == 0x5a);
            }
        } else {
            assert(state.phase == LNK_FAULT && !state.completed);
            assert(state.reason == (n == 8 ? LNK_CLOCK : n <= 10 ? LNK_ACQUIRE :
                                    n <= 13 ? LNK_TRANSMIT : n == 14 ? LNK_STOP : LNK_CAPACITY));
            if (n >= 15) assert(state.frames == 2 && state.before_tx == 2 && !state.attempts);
        }
    }
    finish();
}

int main(int argc, char **argv)
{
    unsigned n, stage, byte, bit, before;
    (void)radio_autoack_component_main;
    assert(argc == 1 || (argc == 8 && !strcmp(argv[1], "--vector")));
    if (argc == 8) {
        n = (unsigned)strtoul(argv[2], NULL, 0);
        normal_config = (uint16_t)strtoul(argv[3], NULL, 0);
        normal_output = (uint16_t)strtoul(argv[4], NULL, 0);
        body_address = (uint16_t)strtoul(argv[5], NULL, 0);
        reserved = (uint16_t)strtoul(argv[6], NULL, 0);
        helper = (uint16_t)strtoul(argv[7], NULL, 0);
        assert(n < FIXTURE_CASES); exporting = 1; fixture_case(n); return 0;
    }
    for (n = 0; n < FIXTURE_CASES; n++) fixture_case(n);
    for (stage = 0; stage < 2; stage++) for (byte = 0; byte < 8; byte++) for (bit = 0; bit < 8; bit++) {
        fixture_begin(); before = accesses;
        if (stage) { packet_write(0); checkpoint(1); }
        packet_write(stage); radio_link_fixture_mailbox[byte] ^= (uint8_t)(1u << bit);
        checkpoint(1); assert(state.reason == LNK_PACKET && accesses == before); finish();
    }
    for (stage = 0; stage < 2; stage++) for (byte = 0; byte < 8; byte++) {
        fixture_begin(); before = accesses;
        if (stage) { packet_write(0); checkpoint(1); }
        packet_write(stage);
        for (n = byte; n < 8; n++) radio_link_fixture_mailbox[n] = 0;
        checkpoint(1); assert(accesses == before && state.phase != LNK_ADMITTED);
    }
    for (n = 0; n < LNK_LENGTH + sizeof(radio_link_fixture_config); n++) {
        fixture_begin(); before = accesses;
        if (n < LNK_LENGTH) radio_link_fixture_body[n] ^= 1;
        else ((uint8_t *)&radio_link_fixture_config)[n - LNK_LENGTH] ^= 1;
        checkpoint(1); assert(state.reason == LNK_INVARIANT && accesses == before); finish();
    }
    fixture_begin(); tick_step = 0; admitted(); checkpoint(1);
    assert(state.reason == LNK_WORK && state.rx_polls[0] == 0 && state.rx_polls[1] == 16);
    assert(!state.completed && XR(0x618b) == 1); finish();
    printf("Link fixture: %u native polls; real startup/clock/owner, admission, TX/RX/drain "
           "and retained failures PASS (synthetic only).\n", fixture_calls);
    return 0;
}
