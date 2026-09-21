/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic test image only. NEVER flash it.
 */
#include "radio_noise.h"
#include "noise_health.h"
#include "timebase.h"

MCU_XDATA radio_noise_request_t radio_noise_test_request;
MCU_XDATA radio_noise_capture_t radio_noise_test_capture;
MCU_XDATA noise_health_t radio_noise_test_health;
MCU_XDATA uint16_t radio_noise_test_rct, radio_noise_test_apt, radio_noise_test_first_failure;
MCU_XDATA uint8_t radio_noise_test_return, radio_noise_test_health_return, radio_noise_test_processed;
radio_noise_request_t MCU_XDATA * MCU_XDATA radio_noise_test_req_ptr;
radio_noise_capture_t MCU_XDATA * MCU_XDATA radio_noise_test_cap_ptr;
extern MCU_XDATA uint8_t radio_noise_used, radio_noise_fault, radio_noise_reserved_end;

static void health(void)
{
    uint16_t base, i;
    radio_noise_test_health_return = noise_health_start(
        &radio_noise_test_health, radio_noise_test_rct, radio_noise_test_apt);
    for (base = 0; base < radio_noise_test_capture.samples; base += 17) {
        for (i = base; i < base + 17 && i < radio_noise_test_capture.samples; i++) {
            radio_noise_test_health_return = noise_health_push(&radio_noise_test_health,
                (uint8_t)((radio_noise_test_capture.data[i >> 3] >> (i & 7u)) & 1u));
            if (radio_noise_test_health_return && !radio_noise_test_first_failure)
                radio_noise_test_first_failure = i + 1;
        }
    }
}

void radio_noise_test_cycle(void)
{
#if defined(__SDCC)
    __asm
        .globl _radio_noise_test_before
    _radio_noise_test_before:
        nop
    __endasm;
#endif
    radio_noise_test_return = radio_noise_collect(radio_noise_test_req_ptr, radio_noise_test_cap_ptr);
    if (radio_noise_used && !radio_noise_test_processed) {
        health();
        radio_noise_test_processed = 1;
    }
#if defined(__SDCC)
    __asm
        .globl _radio_noise_test_done
    _radio_noise_test_done:
        nop
    __endasm;
#endif
}

#if defined(__SDCC)
typedef char request_size_check[sizeof(radio_noise_request_t) == 11 ? 1 : -1];
typedef char capture_size_check[sizeof(radio_noise_capture_t) == 171 ? 1 : -1];
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t radio_noise_test_result[8];
void main(void)
{
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    radio_noise_test_result[0] = 'R'; radio_noise_test_result[1] = 'N';
    radio_noise_test_result[2] = 'O'; radio_noise_test_result[3] = '1';
    radio_noise_test_result[4] = 1; radio_noise_test_result[5] = 8;
    radio_noise_test_result[6] = 0; radio_noise_test_result[7] = 0;
    radio_noise_test_req_ptr = &radio_noise_test_request;
    radio_noise_test_cap_ptr = &radio_noise_test_capture;
    for (;;) radio_noise_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint8_t _gptrput_PARM_2, memset_PARM_2, __memcpy_PARM_2[3];
static uint8_t xregs[0x300], shadow_x[0x300], shadow_sfr[256];
static uint32_t ticks, latched, step;
static unsigned mode, phase_polls, warm_delay, stop_delay, polls, raw_reads;
static unsigned pattern, bad_raw_at, writes_total, reads_total, config_writes, ignored_config;
static unsigned inject_poll, inject_address, inject_value, vectors, events, cases;
static uint16_t req_address, cap_address;
#define XR(a) xregs[(a) - 0x6100u]
static const uint16_t settings[] = {
    0x6189, 0x618a, 0x6180, 0x6182, 0x6194, 0x6195, 0x61b2, 0x61fa, 0x61ae, 0x618f
};
static const uint8_t values[] = {0x4c, 0, 0x0c, 0, 0x7f, 1, 0x15, 9, 0, 0};

static volatile uint8_t *sfr(unsigned address)
{
    switch (address) {
#define REG_CASE(name, location) case location: return &name;
        CC2530_REGISTER_LIST(REG_CASE)
#undef REG_CASE
    default: assert(0); return NULL;
    }
}

static void shadow(void)
{
#define SAVE(name, address) shadow_sfr[address] = name;
    CC2530_REGISTER_LIST(SAVE)
#undef SAVE
    memcpy(shadow_x, xregs, sizeof(xregs));
}

static void event(char kind, unsigned address, unsigned long value)
{
    unsigned i, count = 0;
    if (!vectors) return;
    printf("%s[\"%c\",%u,%lu,[", events++ ? "," : "", kind, address, value);
#define DELTA(name, address) if (shadow_sfr[address] != name) \
    printf("%s[%u,%u]", count++ ? "," : "", address, name);
    CC2530_REGISTER_LIST(DELTA)
#undef DELTA
    /* Explicitly restore a dropped write even when the model has no delta. */
    if (kind == 'w' && address >= 0x6100 && shadow_x[address - 0x6100] == XR(address))
        printf("%s[%u,%u]", count++ ? "," : "", address, XR(address));
    for (i = 0; i < sizeof(xregs); i++)
        if (shadow_x[i] != xregs[i])
            printf("%s[%u,%u]", count++ ? "," : "", 0x6100u + i, xregs[i]);
    printf("]]");
    shadow();
}

static void consume(void)
{
    assert(read_count + xread_count <= 1);
    read_count = xread_count = 0;
}

static void advance(void)
{
    polls++; phase_polls++;
    if (mode == 1 && phase_polls > warm_delay) {
        XR(0x6192) = 0; XR(0x6193) = 5; XR(0x6199) = 1;
        XR(0x61ae) = 0x30;
    }
    if (mode == 2 && phase_polls > stop_delay) {
        XR(0x6192) = 0; XR(0x6193) = 0; SOC_RFIRQF1 = 4; mode = 3;
    }
    if (inject_poll && polls == inject_poll) {
        if (inject_address < 256) *sfr(inject_address) = (uint8_t)inject_value;
        else XR(inject_address) = (uint8_t)inject_value;
    }
}

static uint8_t xload(uint16_t address)
{
    uint8_t value;
    consume(); reads_total++;
    assert(address >= 0x6180 && address < 0x6300);
    if (address == 0x624a) {
        advance();
        event('o', address, XR(address));
    }
    value = XR(address);
    if (address == 0x61a7) {
        assert(mode == 1 && XR(0x6189) == 0x4c && XR(0x618b) == 0x80);
        assert(XR(0x6193) == 5 && XR(0x6199) == 1 && !XR(0x6192));
        value = pattern == 1 ? 0 : pattern == 2 ? 1 : (uint8_t)((0x96u >> (raw_reads & 7u)) & 1u);
        value |= (uint8_t)((raw_reads & 1u) << 1);
        raw_reads++;
        if (raw_reads == bad_raw_at) value |= 4;
        XR(address) = value;
        event('r', address, value);
    }
    return value;
}

static uint8_t load(uint8_t address, uint8_t value)
{
    consume(); reads_total++;
    if (address == SOC_ST0_ADDRESS) {
        latched = ticks;
        ticks = (ticks + step) & TIMEBASE_TICKS_MASK;
        SOC_ST0 = (uint8_t)latched; SOC_ST1 = (uint8_t)(latched >> 8); SOC_ST2 = (uint8_t)(latched >> 16);
        event('t', address, latched);
        value = SOC_ST0;
    } else if (address == SOC_ST1_ADDRESS) value = (uint8_t)(latched >> 8);
    else if (address == SOC_ST2_ADDRESS) value = (uint8_t)(latched >> 16);
    assert(address != SOC_RFD_ADDRESS && address != SOC_RNDL_ADDRESS && address != SOC_RNDH_ADDRESS);
    return value;
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    consume(); writes_total++;
    assert(write_count == 1 && writes[0].address == address && writes[0].before == before);
    write_count = 0;
    assert(address == SOC_RFST_ADDRESS && value == 0xe3 && mode == 0 && config_writes == 10);
    mode = 1; phase_polls = 0;
    XR(0x618b) = 0x80; XR(0x6192) = 0x40; XR(0x6193) = 1; XR(0x6199) = 0;
    event('w', address, value);
}

static void xstore(uint16_t address, uint8_t value)
{
    consume(); writes_total++;
    assert(xwrite_count == 1 && xwrites[0].address == address && xwrites[0].value == value);
    xwrite_count = 0;
    if (address == 0x618d) {
        assert(value == 0x80 && mode == 1);
        mode = 2; phase_polls = 0; XR(0x618b) = 0; SOC_RFIRQF0 = 0x80;
    } else {
        unsigned i = config_writes++;
        assert(mode == 0 && i < 10 && address == settings[i]);
        assert(value == (i == 9 ? 11 + 5 * (radio_noise_test_request.channel - 11) : values[i]));
        if (ignored_config != i + 1) XR(address) = value;
    }
    event('w', address, value);
}

static uint16_t xaddress(const volatile void *p)
{
    if (p == &radio_noise_test_request) return req_address;
    if (p == &radio_noise_test_capture) return cap_address;
    if (p == &radio_noise_reserved_end) return 0x300;
    if (p == __memcpy_PARM_2) return 0x1d00;
    if (p == &memset_PARM_2) return 0x1d08;
    if (p == &_gptrput_PARM_2) return 0x1d10;
    assert(0); return 0;
}

static void reset(unsigned count)
{
    host_mmio_reset();
    memset(xregs, 0, sizeof(xregs));
    memset(&radio_noise_test_capture, 0xa5, sizeof(radio_noise_test_capture));
    memset(&radio_noise_test_request, 0, sizeof(radio_noise_test_request));
    memset(&radio_noise_test_health, 0, sizeof(radio_noise_test_health));
    radio_noise_fault = radio_noise_used = radio_noise_test_processed = 0;
    radio_noise_test_first_failure = radio_noise_test_health_return = 0;
    radio_noise_test_rct = 21; radio_noise_test_apt = 589;
    radio_noise_test_request.channel = 26;
    radio_noise_test_request.samples = (uint16_t)count;
    radio_noise_test_request.interval = 1;
    radio_noise_test_request.timeout = 100000;
    radio_noise_test_request.limit = 10000;
    req_address = 0x400; cap_address = 0x500;
    radio_noise_test_req_ptr = &radio_noise_test_request;
    radio_noise_test_cap_ptr = &radio_noise_test_capture;
    ticks = latched = 0; step = 1;
    mode = phase_polls = warm_delay = stop_delay = polls = raw_reads = 0;
    pattern = bad_raw_at = config_writes = ignored_config = inject_poll = 0;
    reads_total = writes_total = events = 0;
    SOC_SLEEPCMD = 4; SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88;
    XR(0x624a) = 0xa5; XR(0x6189) = 0x40; XR(0x618a) = 1; XR(0x6180) = 0x0d;
    XR(0x6182) = 7; XR(0x6194) = 0x40; XR(0x6195) = 1; XR(0x618e) = 0x0f;
    XR(0x61a8) = 0x85; XR(0x61a9) = 0x14; XR(0x61b8) = 0x75; XR(0x61b9) = 8;
    XR(0x61b2) = 0x11; XR(0x61fa) = 0x0f; XR(0x61ae) = 0x2b; XR(0x618f) = 0x0b;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xread_hook = xload; host_mmio_xwrite_hook = xstore;
    host_mmio_xaddress_hook = xaddress;
}

static void run(radio_noise_result_t expected)
{
    radio_noise_capture_t saved;
    radio_noise_request_t request = radio_noise_test_request;
    unsigned i, old_reads, old_writes;
    radio_noise_test_cycle();
    consume();
    cases++;
    assert(radio_noise_test_return == expected);
    assert(!memcmp(&request, &radio_noise_test_request, sizeof(request)));
    if (radio_noise_used) {
        assert(radio_noise_test_capture.samples == raw_reads);
        assert(radio_noise_test_capture.timed_samples <= raw_reads);
        for (i = 0; i < 1024; i++) {
            unsigned bit = i >= raw_reads ? 0 : pattern == 1 ? 0 :
                pattern == 2 ? 1 : (0x96u >> (i & 7u)) & 1u;
            assert(((radio_noise_test_capture.data[i >> 3] >> (i & 7u)) & 1u) == bit);
        }
    }
    if (expected == RADIO_NOISE_OK) {
        assert(mode == 3 && writes_total == 12 && !radio_noise_fault);
        assert(radio_noise_test_capture.samples == request.samples);
        assert(radio_noise_test_capture.timed_samples == request.samples);
        assert(radio_noise_test_capture.phase == 5 && radio_noise_test_capture.actions == 3);
        assert(radio_noise_test_capture.min_gap >= request.interval);
        assert(radio_noise_test_capture.max_gap >= radio_noise_test_capture.min_gap);
    }
    old_reads = reads_total; old_writes = writes_total;
    saved = radio_noise_test_capture;
    if (radio_noise_used) {
        assert(radio_noise_collect(NULL, NULL) == (radio_noise_fault ? expected : RADIO_NOISE_ALREADY_USED));
        assert(old_reads == reads_total && old_writes == writes_total);
        assert(!memcmp(&saved, &radio_noise_test_capture, sizeof(saved)));
    } else {
        const uint8_t *bytes = (const uint8_t *)&radio_noise_test_capture;
        assert(!reads_total && !writes_total);
        for (i = 0; i < sizeof(saved); i++) assert(bytes[i] == 0xa5);
    }
}

static void emit(const char *name, radio_noise_result_t expected)
{
    unsigned i;
    radio_noise_capture_t *c = &radio_noise_test_capture;
    radio_noise_request_t *r = &radio_noise_test_request;
    noise_health_t *h = &radio_noise_test_health;
    printf("{\"name\":\"%s\",\"request\":[%lu,%u,%u,%u,%u],\"cutoffs\":[%u,%u],\"pattern\":%u,\"pointers\":[%u,%u],\"initial\":{",
        name, (unsigned long)r->timeout, r->samples, r->interval, r->limit, r->channel,
        radio_noise_test_rct, radio_noise_test_apt, pattern, req_address, cap_address);
#define EMIT(name, address) printf("\"%u\":%u,", address, name);
    CC2530_REGISTER_LIST(EMIT)
#undef EMIT
    for (i = 0; i < sizeof(xregs); i++)
        printf("\"%u\":%u%s", 0x6100u + i, xregs[i], i + 1 == sizeof(xregs) ? "" : ",");
    printf("},\"events\":[");
    shadow(); vectors = 1; run(expected); vectors = 0;
    printf("],\"result\":%u,\"fault\":%u,\"health\":[%u,%u,%u,%u],\"capture\":[",
        expected, radio_noise_fault, radio_noise_test_health_return, radio_noise_test_health.state,
        radio_noise_test_health.startup_remaining, radio_noise_test_first_failure);
    printf("%lu,%lu,%lu,%lu,%lu,%lu,%u,%u,%u",
        (unsigned long)c->elapsed_ticks, (unsigned long)c->first_before, (unsigned long)c->last_after,
        (unsigned long)c->min_gap, (unsigned long)c->max_gap, (unsigned long)c->max_span,
        c->samples, c->timed_samples, c->polls);
#define FIELD(name) printf(",%u", c->name);
    FIELD(phase) FIELD(writes) FIELD(verified) FIELD(actions) FIELD(timebase_status)
    FIELD(rx_enable) FIELD(calibration) FIELD(signals) FIELD(rssi_valid)
    FIELD(errors) FIELD(flags0) FIELD(flags1) FIELD(last_raw)
#undef FIELD
    printf("],\"health_context\":[%u,%u,%u,%u,%u,%u,%u,%u,%u],\"data\":\"",
        h->rct_cutoff, h->apt_cutoff, h->run, h->matches, h->window_count,
        h->startup_remaining, h->last, h->reference, h->state);
    for (i = 0; i < 128; i++) printf("%02x", c->data[i]);
    puts("\"}");
}

static void scenarios(void)
{
    unsigned i;
    reset(1); emit("single", RADIO_NOISE_OK);
    reset(13); emit("partial byte", RADIO_NOISE_OK);
    reset(1024); emit("full startup across chunks", RADIO_NOISE_OK);
    reset(1024); pattern = 1; radio_noise_test_rct = 65535; radio_noise_test_apt = 1024;
    emit("failing final startup sample", RADIO_NOISE_OK);
    reset(23); pattern = 2; emit("retained RCT is not acquisition failure", RADIO_NOISE_OK);
    reset(8); ticks = 0xfffff0; radio_noise_test_request.interval = 3; warm_delay = 2; stop_delay = 2;
    emit("paced wrap and delayed phases", RADIO_NOISE_OK);
    reset(8); warm_delay = 65535; radio_noise_test_request.limit = 25;
    emit("warmup work bound", RADIO_NOISE_WORK_LIMIT);
    reset(8); step = 0; radio_noise_test_request.limit = 25;
    emit("stopped sampling clock", RADIO_NOISE_WORK_LIMIT);
    reset(8); stop_delay = 65535; radio_noise_test_request.limit = 40;
    emit("unconfirmed stop", RADIO_NOISE_WORK_LIMIT);
    reset(8); radio_noise_test_request.timeout = 25;
    emit("deadline after raw read", RADIO_NOISE_TIMEOUT);
    reset(8); step = 0x800000;
    emit("counter half range", RADIO_NOISE_TIME_ERROR);
    reset(8); bad_raw_at = 3; emit("invalid raw register", RADIO_NOISE_BAD_SAMPLE);
    reset(8); ignored_config = 1; emit("no sync profile not retained", RADIO_NOISE_STATE_CHANGED);
    reset(8); SOC_DMAARM = 1; emit("DMA ownership", RADIO_NOISE_UNSUPPORTED_STATE);
    reset(8); XR(0x619b) = 1; emit("entry FIFO occupied", RADIO_NOISE_FIFO_ERROR);
    reset(8); SOC_RFIRQF1 = 1; emit("ACK history", RADIO_NOISE_STATE_CHANGED);
    reset(8); inject_poll = 25; inject_address = SOC_CLKCONCMD_ADDRESS; inject_value = 0xc8;
    emit("changed clock", RADIO_NOISE_UNSUPPORTED_STATE);
    reset(8); inject_poll = 25; inject_address = 0x6199; inject_value = 0;
    emit("lost RSSI validity", RADIO_NOISE_STATE_CHANGED);
    reset(8); inject_poll = 25; inject_address = SOC_RFERRF_ADDRESS; inject_value = 4;
    emit("controller error with raw prefix", RADIO_NOISE_CONTROLLER_ERROR);
    reset(8); inject_poll = 25; inject_address = 0x6189; inject_value = 0x48;
    emit("FIFO looping is not no sync", RADIO_NOISE_STATE_CHANGED);
    reset(8); inject_poll = 25; inject_address = 0x61ae; inject_value = 0x31;
    emit("changed VCO configuration", RADIO_NOISE_STATE_CHANGED);
    for (i = 21; i <= 25; i++) {
        reset(1); radio_noise_test_request.limit = (uint16_t)i;
        emit("exact work boundary", i < 24 ? RADIO_NOISE_WORK_LIMIT : RADIO_NOISE_OK);
    }
    reset(1); req_address = 0x1f00; emit("request alias rejected", RADIO_NOISE_INVALID_RANGE);
    reset(1); cap_address = 0x1dff; emit("capture crosses status", RADIO_NOISE_INVALID_RANGE);
    reset(1); cap_address = 0x400; emit("overlap rejected", RADIO_NOISE_INVALID_RANGE);
    reset(1); cap_address = 1; emit("private prefix rejected", RADIO_NOISE_BUFFER_OWNERSHIP);
    reset(1); radio_noise_test_request.samples = 1025; emit("sample bound", RADIO_NOISE_INVALID_ARGUMENT);
    reset(1); cap_address = 0x300; emit("private boundary rejected", RADIO_NOISE_BUFFER_OWNERSHIP);
    for (i = 0; i < 3; i++) {
        reset(1); cap_address = (uint16_t)(0x1d00 + i * 8);
        emit("libc scratch rejected", RADIO_NOISE_BUFFER_OWNERSHIP);
    }
    reset(1); cap_address = 0x1d00 - sizeof(radio_noise_test_capture) + 1;
    emit("capture crosses libc boundary", RADIO_NOISE_BUFFER_OWNERSHIP);
    reset(1); req_address = 0x1d00 - sizeof(radio_noise_test_request) + 1;
    emit("request crosses libc boundary", RADIO_NOISE_BUFFER_OWNERSHIP);
    reset(1); req_address = 0; radio_noise_test_req_ptr = NULL;
    emit("null request", RADIO_NOISE_INVALID_ARGUMENT);
    reset(1); cap_address = 0; radio_noise_test_cap_ptr = NULL;
    emit("null capture", RADIO_NOISE_INVALID_ARGUMENT);
}

int main(int argc, char **argv)
{
    unsigned i, j;
    if (argc == 2 && !strcmp(argv[1], "--vectors")) { scenarios(); return 0; }
    assert(argc == 1);
    for (i = 1; i <= 1024; i++) { reset(i); run(RADIO_NOISE_OK); }
    for (i = 0; i < 256; i++) {
        reset(1); radio_noise_test_request.channel = (uint8_t)i;
        run(i >= 11 && i <= 26 ? RADIO_NOISE_OK : RADIO_NOISE_INVALID_ARGUMENT);
    }
    for (j = 0; j < 2; j++) for (i = 0; i < 65536UL; i++) {
        uint32_t address = i, size = j ? sizeof(radio_noise_test_capture) : sizeof(radio_noise_test_request);
        uint32_t other = j ? 0x400 : 0x500, other_size = j ? sizeof(radio_noise_test_request) : sizeof(radio_noise_test_capture);
        radio_noise_result_t expected;
        if (address >= 0x1e00 || address + size > 0x1e00) expected = RADIO_NOISE_INVALID_RANGE;
        else if (address <= 0x300 || address + size > 0x1d00) expected = RADIO_NOISE_BUFFER_OWNERSHIP;
        else if (address < other + other_size && other < address + size) expected = RADIO_NOISE_INVALID_RANGE;
        else continue;
        reset(1);
        if (j) cap_address = (uint16_t)i; else req_address = (uint16_t)i;
        run(expected);
    }
    for (i = 0; i < 10; i++) {
        reset(1); ignored_config = i + 1;
        run(i == 5 ? RADIO_NOISE_OK : RADIO_NOISE_STATE_CHANGED);
    }
    reset(1); radio_noise_test_request.samples = 0; run(RADIO_NOISE_INVALID_ARGUMENT);
    reset(1); radio_noise_test_request.samples = 1025; run(RADIO_NOISE_INVALID_ARGUMENT);
    reset(1); radio_noise_test_request.interval = 0; run(RADIO_NOISE_INVALID_ARGUMENT);
    reset(1); radio_noise_test_request.timeout = 0; run(RADIO_NOISE_INVALID_ARGUMENT);
    reset(1); radio_noise_test_request.timeout = 0x800000; run(RADIO_NOISE_INVALID_ARGUMENT);
    reset(1); radio_noise_test_request.limit = 0; run(RADIO_NOISE_INVALID_ARGUMENT);
    reset(1); assert(radio_noise_collect(NULL, &radio_noise_test_capture) == RADIO_NOISE_INVALID_ARGUMENT);
    assert(radio_noise_collect(&radio_noise_test_request, NULL) == RADIO_NOISE_INVALID_ARGUMENT);
    printf("Raw IRND: %u native cases, count/packing/cadence/ownership/retained states and real health core PASS; no entropy claim.\n", cases);
    return 0;
}
#endif
