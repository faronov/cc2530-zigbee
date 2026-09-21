/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Native-only synthetic peripheral stimulus. NEVER linked into board firmware.
 */
#include "radio_noise_fixture.h"
#include "host_mmio.h"
#include "timebase.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern MCU_XDATA uint8_t radio_noise_used, radio_noise_fault, radio_noise_reserved_end;
uint8_t _gptrput_PARM_2, memset_PARM_2, __memcpy_PARM_2[3];
#define S radio_noise_fixture_state
#define C radio_noise_fixture_capture
#define H radio_noise_fixture_health
#define K radio_noise_fixture_clock
#define XR(a) xregs[(a) - 0x6100u]
static uint8_t xregs[0x300], old_x[0x300], old_sfr[256];
static uint32_t ticks, latched, stride;
static unsigned vectors, events, steps, cases, reads_total, writes_total;
static unsigned mode, configs, raw_reads, scenario, clock_stuck, clock_writes, observations;
static const uint16_t settings[] = {
    0x6189, 0x618a, 0x6180, 0x6182, 0x6194, 0x6195, 0x61b2, 0x61fa, 0x61ae, 0x618f
};
static const uint8_t values[] = {0x4c, 0, 0x0c, 0, 0x7f, 1, 0x15, 9, 0, 86};

static void shadow(void)
{
#define SAVE(name, address) old_sfr[address] = name;
    CC2530_REGISTER_LIST(SAVE)
#undef SAVE
    memcpy(old_x, xregs, sizeof(xregs));
}
static void event(char kind, unsigned address, unsigned long value)
{
    unsigned i, count = 0;
    if (!vectors) return;
    printf("%s[\"%c\",%u,%lu,[", events++ ? "," : "", kind, address, value);
#define DELTA(name, address) if (old_sfr[address] != name) \
    printf("%s[%u,%u]", count++ ? "," : "", address, name);
    CC2530_REGISTER_LIST(DELTA)
#undef DELTA
    for (i = 0; i < sizeof(xregs); i++)
        if (old_x[i] != xregs[i])
            printf("%s[%u,%u]", count++ ? "," : "", 0x6100u + i, xregs[i]);
    printf("]]"); shadow();
}
static void consume(void)
{
    assert(read_count + xread_count <= 1);
    read_count = xread_count = 0;
}
static uint8_t load(uint8_t address, uint8_t value)
{
    consume(); reads_total++;
    assert(address != SOC_RFD_ADDRESS && address != SOC_RNDL_ADDRESS && address != SOC_RNDH_ADDRESS);
    if (address == SOC_ST0_ADDRESS) {
        latched = ticks; ticks = (ticks + stride) & TIMEBASE_TICKS_MASK;
        SOC_ST0 = (uint8_t)latched; SOC_ST1 = (uint8_t)(latched >> 8); SOC_ST2 = (uint8_t)(latched >> 16);
        event('t', address, latched); return SOC_ST0;
    }
    if (address == SOC_ST1_ADDRESS) return (uint8_t)(latched >> 8);
    if (address == SOC_ST2_ADDRESS) return (uint8_t)(latched >> 16);
    if (!S.attempts) event('c', address, value);
    return value;
}
static void store(uint8_t address, uint8_t before, uint8_t value)
{
    consume(); writes_total++;
    assert(write_count == 1 && writes[0].before == before); write_count = 0;
    if (address == SOC_CLKCONCMD_ADDRESS) {
        clock_writes++;
        assert(!S.attempts && !raw_reads && !configs);
        assert((clock_writes == 1 && value == 0x88) || (clock_writes == 2 && value == 0xc9));
        if (!clock_stuck) SOC_CLKCONSTA = value;
    } else {
        assert(address == SOC_RFST_ADDRESS && value == 0xe3 && !mode && configs == 10);
        mode = 1; XR(0x618b) = 0x80; XR(0x6192) = 0x40; XR(0x6193) = 1;
    }
    event('w', address, value);
}
static uint8_t raw_bit(unsigned index)
{
    if (scenario == 1) return 0;
    if (scenario == 2) return (uint8_t)(index % 3 == 2); /* APT failure, no long run */
    return (uint8_t)((0x96u >> (index & 7u)) & 1u);
}
static uint8_t xload(uint16_t address)
{
    consume(); reads_total++;
    assert(address >= 0x6180 && address < 0x6300);
    if (address == 0x624a) {
        observations++;
        if (mode == 1 && scenario != 5) {
            XR(0x6192) = 0; XR(0x6193) = 5; XR(0x6199) = 1; XR(0x61ae) = 0x30;
        }
        if (mode == 2 && scenario != 6) { XR(0x6193) = 0; SOC_RFIRQF1 = 4; mode = 3; }
        if (scenario == 4 && raw_reads == 17) SOC_RFERRF = 1;
        event('o', address, XR(address));
    }
    if (address == 0x61a7) {
        assert(mode == 1 && XR(0x6189) == 0x4c && XR(0x6193) == 5 && XR(0x6199) == 1);
        XR(address) = raw_bit(raw_reads) | (uint8_t)((raw_reads & 1u) << 1);
        raw_reads++;
        if (scenario == 3 && raw_reads == 17) XR(address) |= 4;
        event('r', address, XR(address));
    }
    return XR(address);
}
static void xstore(uint16_t address, uint8_t value)
{
    consume(); writes_total++;
    assert(xwrite_count == 1); xwrite_count = 0;
    if (address == 0x618d) {
        assert(value == 0x80 && mode == 1 && raw_reads == 1024);
        mode = 2; XR(0x618b) = 0; SOC_RFIRQF0 = 0x80;
        if (scenario == 6) stride = 0; /* stopped raw clock during unconfirmed stop */
    } else {
        assert(configs < 10 && !mode && settings[configs] == address && values[configs] == value);
        configs++; XR(address) = value;
    }
    event('w', address, value);
}
static uint16_t xaddress(const volatile void *object)
{
    /* Native structures include native padding. These are synthetic disjoint
     * ordinary-SRAM addresses; the linked proof checks actual SDCC ownership.
     */
    if (object == &radio_noise_reserved_end) return 0x300;
    if (object == &radio_noise_fixture_request) return 0x400;
    if (object == &C) return 0x500;
    if (object == __memcpy_PARM_2) return 0x1d00;
    if (object == &memset_PARM_2) return 0x1d08;
    if (object == &_gptrput_PARM_2) return 0x1d10;
    assert(0); return 0;
}
static void hex(const volatile void *object, unsigned size)
{
    const volatile uint8_t *p = object;
    unsigned i;
    putchar('"'); for (i = 0; i < size; i++) printf("%02x", p[i]); putchar('"');
}
static void capture_json(void)
{
    printf(",\"capture\":[%lu,%lu,%lu,%lu,%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",
        (unsigned long)C.elapsed_ticks, (unsigned long)C.first_before, (unsigned long)C.last_after,
        (unsigned long)C.min_gap, (unsigned long)C.max_gap, (unsigned long)C.max_span,
        C.samples, C.timed_samples, C.polls, C.phase, C.writes, C.verified, C.actions, C.timebase_status,
        C.rx_enable, C.calibration, C.signals, C.rssi_valid, C.errors, C.flags0, C.flags1, C.last_raw);
    printf(",\"data\":"); hex(C.data, 128);
    printf(",\"health\":[%u,%u,%u,%u,%u,%u,%u,%u,%u]", H.rct_cutoff, H.apt_cutoff,
        H.run, H.matches, H.window_count, H.startup_remaining, H.last, H.reference, H.state);
    printf(",\"clock\":[%lu,%u,%u,%lu,%u,%u,%u,%u,%u,%u,%u]",
        (unsigned long)K.request.elapsed_ticks, K.request.polls, K.request.timebase_status,
        (unsigned long)K.rollback.elapsed_ticks, K.rollback.polls, K.rollback.timebase_status,
        K.saved_command, K.requested_command, K.observed_command, K.observed_status, K.rollback_result);
}
static void begin(const char *name, unsigned which)
{
    host_mmio_reset();
    SOC_P0 = SOC_P1 = SOC_P2 = 255; SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_SLEEPCMD = 4;
    memset(xregs, 0, sizeof(xregs));
    XR(0x624a) = 0xa5; XR(0x6189) = 0x40; XR(0x618a) = 1; XR(0x6180) = 0x0d;
    XR(0x6182) = 7; XR(0x6194) = 0x40; XR(0x6195) = 1; XR(0x618e) = 0x0f;
    XR(0x61a8) = 0x85; XR(0x61a9) = 0x14; XR(0x61b8) = 0x75; XR(0x61b9) = 8;
    XR(0x61b2) = 0x11; XR(0x61fa) = 0x0f; XR(0x61ae) = 0x2b; XR(0x618f) = 0x0b;
    radio_noise_fixture_initialized = radio_noise_used = radio_noise_fault = 0;
    memset((void *)radio_noise_fixture_command, 0xa6, 16); /* reset must disarm stale SRAM */
    ticks = latched = 0; stride = 1; scenario = which;
    mode = configs = raw_reads = events = steps = clock_stuck = clock_writes = observations = 0;
    assert(_sdcc_external_startup() == 0);
    radio_noise_fixture_initialize();
    assert(S.phase == RNF_DISARMED && S.result == 255 && S.health_result == 255 && !S.attempts);
    assert(!SOC_IEN0 && !SOC_IEN1 && !SOC_IEN2 && !m0_status.heartbeat);
    assert(m0_status.clock_request == 0xc9 && m0_status.clock_status == 0xc9);
    assert(m0_status.board == CC2530_BOARD);
#if CC2530_BOARD == BOARD_LG_ESL29_REV03
    assert(SOC_P0 == 0x43 && SOC_P1 == 0xfd && SOC_P0DIR == 0xbc && SOC_P1DIR == 2);
    assert(SOC_P0INP == 0xbc && !SOC_APCFG && !SOC_P0SEL && !SOC_P1SEL);
#else
    assert(SOC_P0 == 255 && SOC_P1 == 255 && !SOC_P0DIR && !SOC_P1DIR);
#endif
    write_count = read_count = xread_count = xwrite_count = 0;
    reads_total = writes_total = 0;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xread_hook = xload; host_mmio_xwrite_hook = xstore; host_mmio_xaddress_hook = xaddress;
    shadow(); cases++;
    if (vectors) {
        unsigned i;
        printf("{\"name\":\"%s\",\"pattern\":%u,\"initial\":{", name, which);
        printf("\"198\":201,\"158\":201,\"190\":4");
        for (i = 0; i < sizeof(xregs); i++) printf(",\"%u\":%u", 0x6100u + i, xregs[i]);
        printf("},\"steps\":[");
    }
}
static void step(const uint8_t *packet, unsigned repeat, unsigned corrupt)
{
    unsigned i;
    uint8_t empty[16] = {0};
    if (!packet) packet = empty;
    for (i = 0; i < 16; i++) radio_noise_fixture_command[i] = packet[i];
    if (corrupt) S.attempts = 1;
    if (vectors) {
        printf("%s{\"packet\":", steps++ ? "," : ""); hex(packet, 16);
        printf(",\"repeat\":%u,\"corrupt\":%u,\"events\":[", repeat, corrupt); events = 0;
    }
    for (i = 0; i < repeat; i++) radio_noise_fixture_poll();
    for (i = 0; i < 16; i++) assert(!radio_noise_fixture_command[i]);
    if (vectors) {
        printf("],\"state\":"); hex(&S, sizeof(S));
        printf(",\"boot\":"); hex(&m0_status, sizeof(m0_status));
        capture_json();
        printf(",\"used\":%u,\"fault\":%u}", radio_noise_used, radio_noise_fault);
    }
}
static void finish(void)
{
    radio_noise_fixture_t state;
    radio_noise_capture_t capture;
    noise_health_t health;
    clock_diagnostics_t clock;
    unsigned r = reads_total, w = writes_total;
    memcpy(&state, (const void *)&S, sizeof(state)); capture = C; health = H; clock = K;
    assert(S.phase == RNF_END || S.phase == RNF_FAULT);
    memset((void *)radio_noise_fixture_command, 0xa6, 16);
    radio_noise_fixture_initialize(); radio_noise_fixture_poll(); radio_noise_fixture_poll();
    assert(reads_total == r && writes_total == w && !memcmp(&state, (const void *)&S, sizeof(state)));
    assert(!memcmp(&capture, &C, sizeof(C)) && !memcmp(&health, &H, sizeof(H)) && !memcmp(&clock, &K, sizeof(K)));
    assert(radio_noise_fixture_command[0] == 0xa6 && radio_noise_fixture_command[15] == 0xa6);
    if (vectors) puts("]}");
}
static void arm(void)
{
    step(radio_noise_fixture_arm, 1, 0);
    assert(S.phase == RNF_ADMITTED && S.clock_result == 0 && !S.attempts && !radio_noise_used);
    assert(configs == 0 && raw_reads == 0 && mode == 0 && clock_writes == 1 && !m0_status.heartbeat);
}
int main(int argc, char **argv)
{
    unsigned i, stage, byte, value;
    uint8_t bad[16];
    vectors = argc == 2 && !strcmp(argv[1], "--vectors");
    for (i = 0; i < 10; i++) {
        static const char *const names[] = {"success", "rct", "apt", "bad-raw-prefix",
            "controller-prefix", "frozen-warm-limit", "stop-limit", "timeout", "raw-wrap", "half-range"};
        begin(names[i], i);
        if (i == 8) ticks = 0xfffff0UL;
        step(NULL, 3, 0); assert(!reads_total && !writes_total);
        arm(); step(NULL, 2, 0); assert(clock_writes == 1 && !configs);
        if (i == 5) stride = 0;
        if (i == 7) stride = 500;
        if (i == 9) stride = TIMEBASE_HALF_RANGE;
        step(radio_noise_fixture_run, 1, 0);
        assert(S.attempts == 1 && radio_noise_used == 1 && C.samples == raw_reads && C.polls <= 10000);
        for (byte = 0; byte < 1024; byte++)
            assert(((C.data[byte >> 3] >> (byte & 7u)) & 1u) == (byte < raw_reads ? raw_bit(byte) : 0));
        if (i < 3 || i == 8) {
            assert(S.phase == RNF_END && !S.result && m0_status.heartbeat == 1 && C.samples == 1024);
            assert(C.timed_samples == 1024 && C.phase == 5 && C.actions == 3 && mode == 3);
            assert(S.health_result == (i == 1 ? 3 : i == 2 ? 4 : 0));
            if (i == 1) assert(S.first_failure[0] == 21 && !S.first_failure[1]);
            if (!i) assert(!S.first_failure[0] && !S.first_failure[1] && H.state == 2);
        } else {
            assert(S.phase == RNF_FAULT && S.reason == RNF_ACQUISITION && !m0_status.heartbeat);
            assert(S.result == (i == 3 ? 12 : i == 4 ? 8 : i == 7 ? 9 : i == 9 ? 11 : 10));
            if (i == 3 || i == 4) assert(C.samples == 17 && C.timed_samples == 16 && C.actions == 1);
            if (i == 5 || i == 6) assert(C.polls == 10000);
        }
        finish();
    }
    for (i = 0; i < 2; i++) {
        begin(i ? "clock-work-limit" : "clock-timeout", 0);
        clock_stuck = 1; stride = i ? 0 : 512;
        step(radio_noise_fixture_arm, 1, 0);
        assert(S.phase == RNF_FAULT && S.reason == RNF_CLOCK && !S.attempts && S.result == 255);
        assert(S.clock_result == (i ? 4 : 3) && K.rollback_result == 9 && clock_writes == 2 && !configs);
        assert(K.request.polls == (i ? 4096 : 2) && K.rollback.polls == K.request.polls);
        finish();
    }
    begin("premature-run", 0); step(radio_noise_fixture_run, 1, 0);
    assert(S.reason == RNF_PACKET && !reads_total && !writes_total); finish();
    begin("stale-arm", 0); arm(); step(radio_noise_fixture_arm, 1, 0);
    assert(S.reason == RNF_PACKET && !configs); finish();
    begin("invariant", 0); step(NULL, 1, 1);
    assert(S.reason == RNF_INVARIANT && !reads_total); finish();
    begin("nonreset-clock", 0); SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88;
    step(radio_noise_fixture_arm, 1, 0);
    assert(S.reason == RNF_INVARIANT && !writes_total && !configs && !S.attempts); finish();
    begin("changed-admitted-clock", 0); arm(); SOC_CLKCONSTA = 0xc9;
    step(radio_noise_fixture_run, 1, 0);
    assert(S.reason == RNF_INVARIANT && !configs && !S.attempts && S.result == 255); finish();
    for (stage = 0; stage < 2; stage++) for (byte = 0; byte < 16; byte++)
        for (value = 0; value < (vectors ? 1u : 256u); value++) {
            const uint8_t *good = stage ? radio_noise_fixture_run : radio_noise_fixture_arm;
            if (!vectors && value == good[byte]) continue;
            begin(stage ? "invalid-run" : "invalid-arm", 0);
            if (stage) arm();
            memcpy(bad, good, 16); bad[byte] = vectors ? bad[byte] ^ 1 : (uint8_t)value;
            step(bad, 1, 0);
            assert(S.phase == RNF_FAULT && S.reason == RNF_PACKET && !configs && !raw_reads);
            finish();
        }
    if (!vectors) printf("Raw IRND board fixture: %u native cases PASS; synthetic only.\n", cases);
    return 0;
}
