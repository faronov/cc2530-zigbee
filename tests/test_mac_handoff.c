/* SPDX-License-Identifier: BSD-3-Clause
 * Original synthetic MMIO caller. Never flash or access equipment.
 */
#include "mac_attempt.h"
#include <string.h>

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t mac_attempt_test_result[8];
MCU_XDATA radio_autoack_config_t mac_attempt_test_config;
MCU_XDATA radio_autoack_frame_t mac_attempt_test_frame;
MCU_XDATA mac_attempt_record_t mac_attempt_test_record;
MCU_XDATA mac_epoch_stamp_t mac_attempt_test_clock;
MCU_XDATA uint8_t mac_attempt_test_operation, mac_attempt_test_return, mac_attempt_test_length;
MCU_XDATA uint16_t mac_attempt_test_input, mac_attempt_test_output, mac_attempt_test_limit;
MCU_XDATA uint16_t mac_attempt_test_window;
MCU_XDATA uint32_t mac_attempt_test_timeout;

void mac_attempt_test_cycle(void)
{
    __asm
        .globl _mac_attempt_test_before
    _mac_attempt_test_before:
        nop
    __endasm;
    switch (mac_attempt_test_operation) {
    case 0: mac_attempt_test_return = mac_attempt_init(
        (const radio_autoack_config_t MCU_XDATA *)mac_attempt_test_input,
        mac_attempt_test_timeout, mac_attempt_test_limit); break;
    case 1: mac_attempt_test_return = mac_attempt_stop(mac_attempt_test_timeout, mac_attempt_test_limit); break;
    case 2: mac_attempt_test_return = mac_attempt_prepare(
        (const uint8_t MCU_XDATA *)mac_attempt_test_input, mac_attempt_test_length,
        mac_attempt_test_timeout, mac_attempt_test_limit); break;
    case 3: mac_attempt_test_return = mac_attempt_run(mac_attempt_test_window,
        mac_attempt_test_timeout, mac_attempt_test_limit,
        (mac_attempt_record_t MCU_XDATA *)mac_attempt_test_output); break;
    case 4: mac_attempt_test_return = mac_attempt_receive(mac_attempt_test_timeout, mac_attempt_test_limit,
        (radio_autoack_frame_t MCU_XDATA *)mac_attempt_test_output); break;
    case 5: mac_attempt_test_return = mac_attempt_resume(mac_attempt_test_timeout, mac_attempt_test_limit); break;
    case 6: mac_attempt_test_return = mac_attempt_now(mac_attempt_test_timeout, mac_attempt_test_limit,
        (mac_epoch_stamp_t MCU_XDATA *)mac_attempt_test_output); break;
    default: mac_attempt_test_return = mac_attempt_handoff(mac_attempt_test_timeout, mac_attempt_test_limit); break;
    }
    __asm
        .globl _mac_attempt_test_done
    _mac_attempt_test_done:
        nop
    __endasm;
}

void main(void)
{
    memset(mac_attempt_test_result, 0, 8); memcpy(mac_attempt_test_result, "MAH1", 4);
    mac_attempt_test_result[4] = 1; mac_attempt_test_result[5] = 8;
    memset(&mac_attempt_test_frame, 0x69, sizeof(mac_attempt_test_frame));
    memset(&mac_attempt_test_record, 0x69, sizeof(mac_attempt_test_record));
    memset(&mac_attempt_test_clock, 0x69, sizeof(mac_attempt_test_clock));
    for (;;) mac_attempt_test_cycle();
}
#else
#define MAC_ATTEMPT_MAIN mac_attempt_component_main
#include "test_mac_attempt.c"

static mac_epoch_stamp_t handoff_clock;
static uint16_t clock_address = 0xf00;
extern radio_autoack_handoff_clock_t mac_radio_handoff_clock;
static uint16_t handoff_staging_address = 0x650;
static unsigned guard_span;
static unsigned guard_ff, guard_discard, guard_discarded;
static unsigned guard_bad;
static unsigned handoff_started, handoff_sfd_reads, handoff_configured, injection, injected;
static unsigned automatic_acks, ack_active;
static uint64_t ack_end;
static uint16_t response_fcf = 2;
static unsigned rewrite_reply;

static void handoff_tick(void)
{
    if (!ack_active || model_clocks < ack_end) return;
    ack_active = 0; automatic_acks++;
    SOC_RFIRQF1 |= 1;
    XR(0x6192) = 0;
    if (XR(0x618b)) {
        mode = 2; XR(0x6193) = (XR(0x6193) & 0xc8) | 5;
    } else {
        mode = 5; XR(0x6193) &= 0xc8; SOC_RFIRQF1 |= 4;
    }
}

static void start_ack(void)
{
    assert(!ack_active);
    ack_active = 1; ack_end = model_clocks + 34u*512u;
    mode = 8; XR(0x6192) = 0x40;
    XR(0x6193) = (XR(0x6193) & 0xc8) | 6;
}

static void deliver_data(void)
{
    unsigned first = tail;
    enqueue(13, 0xe9, 0x20);
    fifo[(first+1)&127] = 0x61;
    fifo[(first+2)&127] = 0x98;
    fifo[(first+8)&127] = 0x78;
    fifo[(first+9)&127] = 0x56;
    if ((XR(0x6180) & 1) && ((XR(0x6180) >> 2) & 3) >= 1 &&
        (XR(0x6189) & 0x20) && (XR(0x6181) & 0x10) &&
        (fifo[(first+1)&127] & 0x27u) == 0x21u &&
        fifo[(first+4)&127] == XR(0x6172) && fifo[(first+5)&127] == XR(0x6173) &&
        fifo[(first+6)&127] == XR(0x6174) && fifo[(first+7)&127] == XR(0x6175)) {
        start_ack();
    }
}

static void deliver_command(uint8_t status, uint8_t crc)
{
    unsigned first = tail, i;
    uint8_t matches = 1;
    enqueue(27, crc, 0x20);
    fifo[(first+1)&127] = 0x63;
    fifo[(first+2)&127] = 0xcc;
    for (i = 0; i < 8; i++) {
        fifo[(first+6+i)&127] = config.value.ieee[i];
        fifo[(first+14+i)&127] = (uint8_t)(0x40u+i);
        if (config.value.ieee[i] != XR(0x616a+i)) matches = 0;
    }
    fifo[(first+22)&127] = 2;
    fifo[(first+23)&127] = status ? 255 : 0x78;
    fifo[(first+24)&127] = status ? 255 : 0x56;
    fifo[(first+25)&127] = status;
    if (matches && (crc & 0x80) && (XR(0x6180) & 1) && (XR(0x6189) & 0x20) &&
        (XR(0x6181) & 0x40) && (fifo[(first+1)&127] & 0x27u) == 0x23u &&
        fifo[(first+4)&127] == XR(0x6172) &&
        fifo[(first+5)&127] == XR(0x6173))
        start_ack();
}

static uint16_t handoff_address(const volatile void *p)
{
    if (p == &handoff_clock) return clock_address;
    if (p == &mac_radio_handoff_clock || p == &mac_radio_handoff_clock.before)
        return handoff_staging_address;
    if (p == &mac_radio_handoff_clock.after) return handoff_staging_address+6;
    if (p == &mac_radio_handoff_clock.last) return handoff_staging_address+12;
    return attempt_address(p);
}

static void handoff_inject(void)
{
    if (injected) return;
    injected = 1;
    if (injection == 8)
        deliver_data();
    else if (injection == 1 || injection == 4 || injection == 6)
        enqueue(11, 0xe9, 0x38);
    else if (injection == 2 || injection == 3 || injection == 5 || injection == 7) {
        XR(0x6193) |= 0x20;
        SOC_RFIRQF0 |= 2;
    }
    else if (injection == 9)
        SOC_RFERRF = 4;
}

static uint8_t handoff_load(uint8_t a, uint8_t value)
{
    uint8_t result;
    handoff_tick();
    if (a == SOC_RFIRQF1_ADDRESS) value = SOC_RFIRQF1;
    if (a == SOC_RFD_ADDRESS && rewrite_reply && packets && remaining == 6 && fifo[head] == 5) {
        fifo[(head+1)&127] = (uint8_t)response_fcf;
        fifo[(head+2)&127] = (uint8_t)(response_fcf >> 8);
        rewrite_reply = 0;
    }
    if (a == SOC_RFIRQF0_ADDRESS && handoff_started && injection == 9)
        handoff_inject();
    if (handoff_started && guard_discard) assert(a < 0xa3 || a > 0xa6);
    result = attempt_load(a, value);
    if (handoff_started && a == 0xa2 && !(SOC_T2MSEL & 7)) {
        guard_discard = result == 255;
        guard_discarded += guard_discard;
    }
    handoff_tick();
    return result;
}

static void handoff_store(uint8_t a, uint8_t before, uint8_t value)
{
    handoff_tick();
    if (a == SOC_RFIRQF1_ADDRESS && mode == 8) {
        advance_clocks(mmio_clocks);
        assert(write_count == 1 && writes[0].address == a && writes[0].after == value && value == 0x3b);
        write_count = 0; logs(); trace('w', a, value);
        SOC_RFIRQF1 = before & value;
        return;
    }
    if (a == SOC_RFIRQF0_ADDRESS) {
        advance_clocks(mmio_clocks);
        assert(write_count == 1 && writes[0].address == a && writes[0].after == value && value == 0xfd);
        write_count = 0; logs();
        if (guard_span) {
            uint64_t desired = (uint64_t)mac_radio_handoff_clock.before.periods * 512u +
                mac_radio_handoff_clock.before.fine + guard_span;
            assert(desired >= model_clocks + 2u*mmio_clocks &&
                   desired-model_clocks-2u*mmio_clocks <= UINT32_MAX);
            advance_clocks((uint32_t)(desired-model_clocks-2u*mmio_clocks));
        }
        if (injection == 3 || injection == 4) handoff_inject();
        trace('w', a, value);
        SOC_RFIRQF0 = injection == 10 ? before : before & value;
        ff = guard_ff;
        broken = guard_bad;
        if (injection == 5 || injection == 6) handoff_inject();
        return;
    }
    attempt_store(a, before, value);
}

static uint8_t handoff_xload(uint16_t a)
{
    uint8_t value;
    handoff_tick();
    if (a == 0x6081 || a == 0x6083) {
        advance_clocks(mmio_clocks); logs();
        assert(handoff_started && tx_count >= 4 && tx_fifo[0] == tx_count + 1);
        value = tx_fifo[a - 0x6080];
        trace('r', a, value);
        return value;
    }
    if (a == 0x6193 && handoff_started) {
        handoff_sfd_reads++;
        if (injection == 1 || injection == 2) handoff_inject();
    }
    value = attempt_xload(a);
    handoff_tick();
    return value;
}

static void handoff_xstore(uint16_t a, uint8_t value)
{
    handoff_tick();
    if (mode == 8 && a == 0x618d) {
        assert(xwrite_count == 1 && xwrites[0].address == a && xwrites[0].value == value && value == 1);
        xwrite_count = 0; logs(); trace('w', a, value);
        XR(0x618b) = 0;
        return;
    }
    if (handoff_started && (a == 0x6189 || a == 0x6180)) {
        advance_clocks(mmio_clocks);
        assert(xwrite_count == 1 && xwrites[0].address == a && xwrites[0].value == value);
        xwrite_count = 0; logs(); trace('w', a, value);
        assert(mode == 2 && XR(0x618b) == 1);
        assert((a == 0x6189 && value == 0x60 && !handoff_configured) ||
               (a == 0x6180 && value == RADIO_AUTOACK_NORMAL_FILTER && handoff_configured == 1));
        handoff_configured++;
        XR(a) = value ^ (injection == (a == 0x6189 ? 11u : 12u) ? 1u : 0u);
        if ((a == 0x6189 && injection == 7) || (a == 0x6180 && injection == 8))
            handoff_inject();
        return;
    }
    attempt_xstore(a, value);
}

static void handoff_reset(void)
{
    attempt_reset();
    config.value.pan = 0x1234;
    config.value.short_address = 0x9abc;
    memset(&handoff_clock, 0x69, sizeof(handoff_clock));
    memset(&mac_radio_handoff_clock, 0, sizeof(mac_radio_handoff_clock));
    handoff_started = handoff_sfd_reads = handoff_configured = injected = injection = 0;
    automatic_acks = ack_active = 0; ack_end = 0;
    rewrite_reply = 1;
    guard_span = 0;
    guard_ff = guard_discard = guard_discarded = 0;
    guard_bad = 0;
    host_mmio_read_hook = handoff_load; host_mmio_write_hook = handoff_store;
    host_mmio_xread_hook = handoff_xload; host_mmio_xwrite_hook = handoff_xstore;
    host_mmio_xaddress_hook = handoff_address;
}

static void handoff_call(unsigned op, unsigned expected)
{
    unsigned result, i, before = accesses;
    uint16_t destination = op == 6 ? clock_address : op == 3 ? receipt_address : output_address;
    mac_attempt_record_t saved;
    mac_epoch_stamp_t old_clock;
    radio_autoack_frame_t old_frame;
    mac_radio_diagnostics_t diagnostic;
    memcpy(&saved, &receipt, sizeof(saved));
    memcpy(&old_clock, &handoff_clock, sizeof(old_clock));
    memcpy(&old_frame, &frame.value, sizeof(old_frame));
    memcpy(&diagnostic, mac_attempt_diagnostic(), sizeof(diagnostic));
    if (printing_vector) {
        printf("%s{\"operation\":%u,\"input\":%u,\"output\":%u,\"timeout\":%lu,"
               "\"limit\":%u,\"window\":%lu,\"length\":%u,\"configuration\":\"",
               first_call ? "" : ",", op, op == 2 ? output_address : config_address,
               destination, (unsigned long)bound, cap, (unsigned long)window, body_length);
        emit_configuration(); printf("\",\"initial\":{");
#define REG(name, address) printf("\"%u\":%u,", address, name);
        CC2530_REGISTER_LIST(REG)
#undef REG
        printf("\"%u\":%u,\"%u\":%u,", 0x6081u, tx_fifo[1], 0x6083u, tx_fifo[3]);
        for (i = 0; i < sizeof(xregs); i++)
            printf("\"%u\":%u%s", 0x6100u+i, xregs[i], i+1 == sizeof(xregs) ? "" : ",");
        printf("}");
        if (op == 2) {
            printf(",\"body\":\""); hex(&frame.value, body_length); printf("\"");
        }
        printf(",\"events\":["); trace_count = 0;
    }
    printing = printing_vector; first_call = 0;
    switch (op) {
    case 0: result = mac_attempt_init(config_address ? &config.value : NULL, bound, cap); break;
    case 1: result = mac_attempt_stop(bound, cap); break;
    case 2: result = mac_attempt_prepare(output_address ? (const uint8_t *)&frame.value : NULL,
                                         body_length, bound, cap); break;
    case 3: result = mac_attempt_run((uint16_t)window, bound, cap, receipt_address ? &receipt : NULL); break;
    case 4: result = mac_attempt_receive(bound, cap, output_address ? &frame.value : NULL); break;
    case 5: result = mac_attempt_resume(bound, cap); break;
    case 6: result = mac_attempt_now(bound, cap, clock_address ? &handoff_clock : NULL); break;
    default: result = mac_attempt_handoff(bound, cap); break;
    }
    printing = 0; logs(); calls++;
    if (result != expected)
        fprintf(stderr, "handoff case%u op%u got%u expected%u radio%u timer%u phase%u\n",
                case_number, op, result, expected, radio_autoack_diagnostic()->result,
                mac_time_diagnostic()->result, radio_autoack_diagnostic()->phase);
    assert(result == expected);
    if (op == 0 && result == MAC_RADIO_READY)
        epoch_origin = ((uint64_t)mac_radio_epoch.periods-mac_radio_epoch.symbols)*512u;
    if (op != 3 || result >= 8) assert(!memcmp(&saved, &receipt, sizeof(saved)));
    if (op != 4 || (result != 1 && result != 2)) assert(!memcmp(&old_frame, &frame.value, sizeof(old_frame)));
    if (op != 6 || result != MAC_RADIO_READY) assert(!memcmp(&old_clock, &handoff_clock, sizeof(old_clock)));
    if (diagnostic.fault || (result >= 8 && result <= 11)) {
        assert(accesses == before);
        assert(!memcmp(&diagnostic, mac_attempt_diagnostic(), sizeof(diagnostic)));
    }
    if (printing_vector) {
        const mac_radio_diagnostics_t *d = mac_attempt_diagnostic();
        printf("],\"return\":%u,\"frame\":\"", result); hex(&frame.value, 128);
        printf("\",\"record\":\""); receipt_hex();
        printf("\",\"clock\":\""); stamp_hex(&handoff_clock);
        printf("\",\"diagnostic\":\""); stamp_hex(&d->last_live);
        printf("%02x%02x%02x%02x%02x%02x%02x%02x\"", d->phase, d->result, d->fault,
            d->has_time, d->clock_result, d->timer_result, d->epoch_result, d->radio_result);
        printf(",\"radio_diag\":["); emit_diagnostics();
        printf("],\"handoff_clock\":\"");
        for (i = 0; i < 3; i++) {
            const mac_time_stamp_t *raw = i == 0 ? &mac_radio_handoff_clock.before :
                                         i == 1 ? &mac_radio_handoff_clock.after :
                                                  &mac_radio_handoff_clock.last;
            printf("%02x%02x%02x%02x%02x%02x", raw->fine & 255, raw->fine >> 8,
                   (unsigned)(raw->periods & 255), (unsigned)((raw->periods >> 8) & 255),
                   (unsigned)((raw->periods >> 16) & 255), (unsigned)(raw->periods >> 24));
        }
        printf("\"}");
    }
}

#define HANDOFF_CASES 43u
static void handoff_case(unsigned selected)
{
    static const uint16_t fields[] = {0xfffa, 0x5552, 0xaaa2, 0x888a};
    unsigned i;
    case_number = selected;
    handoff_reset();
    if (selected >= 26 && selected <= 29) response_fcf = fields[selected-26];
    if (selected == 31) response_fcf = 1;
    if (printing_vector) printf("{\"case\":%u,\"steps\":[", selected+ATTEMPT_CASES);
    handoff_call(6, MAC_RADIO_STATE); handoff_call(7, MAC_RADIO_STATE);
    handoff_call(0, MAC_RADIO_READY);
    handoff_call(6, MAC_RADIO_READY); handoff_call(7, MAC_RADIO_STATE);
    if (selected == 32) {
        uint16_t saved_address = clock_address;
        static const uint16_t invalid[] = {0, 1, 0x1dff, 0x1e00, 0x1f00};
        for (i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
            clock_address = invalid[i];
            handoff_call(6, i == 0 ? MAC_RADIO_INVALID_ARGUMENT :
                         i == 1 ? MAC_RADIO_BUFFER_OWNERSHIP : MAC_RADIO_INVALID_RANGE);
        }
        clock_address = owner_end; handoff_call(6, MAC_RADIO_BUFFER_OWNERSHIP);
        clock_address = helper-16; handoff_call(6, MAC_RADIO_BUFFER_OWNERSHIP);
        clock_address = helper; handoff_call(6, MAC_RADIO_BUFFER_OWNERSHIP);
        clock_address = saved_address;
        handoff_call(6, MAC_RADIO_READY);
        goto finished;
    }
    handoff_call(1, MAC_RADIO_STOPPED);
    handoff_call(6, MAC_RADIO_READY);
    ((uint8_t *)&frame.value)[0] = selected == 30 ? 0x41 : 0x61;
    ((uint8_t *)&frame.value)[2] = selected == 15 ? 0x5b : 0x5a;
    handoff_call(2, MAC_RADIO_READY);
    handoff_call(6, MAC_RADIO_READY);
    if (selected >= 33 && selected <= 35) {
        if (selected == 33) SOC_IEN0 = 0x80;
        if (selected == 34) broken = 1;
        if (selected == 35) {
            for (i = 0; i < 3; i++) {
                advance_clocks(0x7ffff000UL);
                handoff_call(6, MAC_RADIO_READY);
                assert(radio_autoack_state == RADIO_AUTOACK_ATTEMPT_PREPARED &&
                       !XR(0x618b) && tx_count == body_length+1u);
            }
            handoff_call(1, MAC_RADIO_STOPPED);
        } else
            handoff_call(6, MAC_RADIO_TIMER_ERROR);
        goto finished;
    }
    if (selected == 13) bad_reply = 1;
    if (selected == 14) extra_rx = 100u*512u;
    if (selected == 16) cca_clear = 0;
    if (selected == 17) reply_after_tx = 0;
    handoff_call(3, selected == 13 ? MAC_RADIO_BAD_CRC : selected == 16 ? MAC_RADIO_CCA_BUSY :
                    selected == 17 ? MAC_RADIO_EMPTY : MAC_RADIO_FRAME);
    handoff_call(6, MAC_RADIO_READY);
    if (selected == 31) {
        handoff_call(7, MAC_RADIO_STATE);
        goto finished;
    }
    if (selected >= 13 && selected <= 17 && selected != 15) {
        handoff_call(7, MAC_RADIO_STATE);
        goto finished;
    }
    if (selected == 18) {
        handoff_call(4, MAC_RADIO_EMPTY);
        handoff_call(7, MAC_RADIO_STATE);
        goto finished;
    }
    if (selected == 19) {
        handoff_call(1, MAC_RADIO_STOPPED);
        handoff_call(7, MAC_RADIO_STATE);
        goto finished;
    }
    handoff_started = 1;
    if (selected >= 36 && selected <= 38) guard_span = 511u + selected-36u;
    if (selected == 39) { guard_ff = 1; mmio_clocks = 1; }
    if (selected == 40) { guard_ff = 200; cap = 16; }
    if (selected >= 41) guard_bad = selected-40;
    if (selected < 13) injection = selected;
    if (selected == 20) bound = 0;
    if (selected == 21) cap = 0;
    if (selected == 22) cap = 2;
    if (selected == 23) broken = 1;
    if (selected == 24) { bound = 1; tick_step = 1; }
    if (selected == 25) cap = 1;
    handoff_call(7, selected == 0 || selected == 36 || selected == 39 ||
                 (selected >= 26 && selected <= 29) ? MAC_RADIO_READY :
                 selected == 20 || selected == 21 ?
                 MAC_RADIO_INVALID_ARGUMENT : selected == 23 || selected == 24 || selected == 25 ?
                 MAC_RADIO_TIMER_ERROR : MAC_RADIO_DRIVER_ERROR);
    handoff_started = 0;
    if (selected == 39 || selected == 40) assert(guard_discarded);
    if (selected == 40) {
        assert(radio_autoack_fault == RADIO_AUTOACK_WORK_LIMIT &&
               !mac_radio_handoff_clock.after.fine && !mac_radio_handoff_clock.after.periods);
    }
    if (selected >= 41) {
        assert(mac_time_fault == MAC_TIME_COUNT_ERROR &&
               radio_autoack_fault == RADIO_AUTOACK_ATTEMPT_TIMER_ERROR &&
               !mac_radio_handoff_clock.after.fine && !mac_radio_handoff_clock.after.periods);
    }
    if (selected == 0 || selected == 36 || selected == 39 || (selected >= 26 && selected <= 29)) {
        uint8_t immutable[128];
        memcpy(immutable, tx_fifo, sizeof(immutable));
        assert(radio_autoack_state == RADIO_AUTOACK_RX && handoff_configured == 2 &&
               XR(0x618b) == 1 && XR(0x6180) == 5 && XR(0x6189) == 0x60);
        handoff_call(7, MAC_RADIO_STATE);
        handoff_call(6, MAC_RADIO_READY);
        deliver_data();
        handoff_call(4, MAC_RADIO_FRAME);
        advance_clocks(40u*512u); handoff_tick();
        deliver_command(0, 0xe9);
        handoff_call(4, MAC_RADIO_FRAME);
        advance_clocks(40u*512u); handoff_tick();
        deliver_command(1, 0xe9);
        handoff_call(4, MAC_RADIO_FRAME);
        advance_clocks(40u*512u); handoff_tick();
        deliver_command(0, 0x69);
        handoff_call(4, MAC_RADIO_BAD_CRC);
        handoff_call(1, MAC_RADIO_STOPPED);
        assert((SOC_RFIRQF1 & 1) && automatic_acks == 3 && !ack_active &&
               !memcmp(immutable, tx_fifo, sizeof(immutable)));
        handoff_call(5, MAC_RADIO_READY);
    }
finished:
    if (mac_attempt_diagnostic()->fault)
        for (i = 0; i < 8; i++) handoff_call(i, mac_attempt_diagnostic()->fault);
    if (printing_vector) puts("]}");
}

#if defined(CC2530_MAC_INTERVAL)
static uint32_t handoff_mac_now(void)
{
    uint32_t next = mac_attempt_diagnostic()->last_live.symbols + 1u;
    unsigned budget = 1000;
    do {
        assert(budget--);
        assert(mac_attempt_now(bound, cap, &handoff_clock) == MAC_RADIO_READY);
    } while ((uint32_t)(handoff_clock.symbols-next) >= MAC_TX_HALF);
    return handoff_clock.symbols;
}

static void handoff_mac(void)
{
    static const uint8_t packet[] = {
        0x61, 0x98, 0, 0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xaa, 0x55, 0xcc
    };
    uint32_t now;
    unsigned budget = 1000;
    clock_address = 0x1000; response_fcf = 0xfffa;
    handoff_reset();
    handoff_call(0, MAC_RADIO_READY); handoff_call(1, MAC_RADIO_STOPPED);
    now = handoff_mac_now();
    assert(mac_tx_interval_init(&interval_tx, 0x5a, now) == MAC_TX_OK);
    assert(mac_tx_interval_submit(&interval_tx, packet, sizeof(packet), now, 100000, 1000) == MAC_TX_OK);
    assert(mac_tx_interval_copy(&interval_tx, (uint8_t *)&frame.value,
                               sizeof(frame.value), &body_length) == MAC_TX_OK);
    handoff_call(2, MAC_RADIO_READY);
    now = handoff_mac_now();
    assert(mac_tx_interval_step(&interval_tx, now, NULL, &interval_action) == MAC_TX_OK);
    assert(interval_action.control.kind == MAC_TX_ACTION_RANDOM);
    interval_source(MAC_TX_EVENT_RANDOM, now); interval_event.source.value = 7;
    assert(mac_tx_interval_step(&interval_tx, now, &interval_event, &interval_action) == MAC_TX_OK);
    assert(interval_action.control.kind == MAC_TX_ACTION_ATTEMPT);
    do {
        assert(budget--);
        now = handoff_mac_now();
    } while ((uint32_t)(now-interval_action.control.at) >= MAC_TX_HALF);
    window = MAC_TX_ACK_SYMBOLS;
    handoff_call(3, MAC_RADIO_FRAME);
    now = handoff_mac_now();
    interval_source(MAC_TX_EVENT_SENT_INTERVAL, now);
    interval_event.lower = receipt.tx_lower; interval_event.upper = receipt.tx_upper;
    assert(mac_tx_interval_step(&interval_tx, now, &interval_event, &interval_action) == MAC_TX_OK);
    assert(interval_action.control.kind == MAC_TX_ACTION_COLLECT);
    interval_source(MAC_TX_EVENT_ACK_INTERVAL, now);
    interval_event.lower = receipt.tx_lower; interval_event.upper = receipt.rx_upper;
    interval_event.source.bytes = receipt.frame.body; interval_event.source.length = receipt.frame.length;
    assert(mac_tx_interval_step(&interval_tx, now, &interval_event, &interval_action) == MAC_TX_OK);
    assert(interval_tx.engine.outcome == MAC_TX_ACKED && interval_tx.engine.pending &&
           interval_action.control.kind == MAC_TX_ACTION_QUIESCE);
    handoff_started = 1; handoff_call(7, MAC_RADIO_READY); handoff_started = 0;
    /* The one ordinary TX has completed and no caller buffer is borrowed.
     * Receiver ACKs now belong to the separately handed-off RX lease.
     */
    now = handoff_mac_now();
    interval_source(MAC_TX_EVENT_QUIESCED, now);
    assert(mac_tx_interval_step(&interval_tx, now, &interval_event, &interval_action) == MAC_TX_OK);
    assert(interval_tx.engine.phase == MAC_TX_DONE && interval_tx.engine.tx_end == 0 &&
           radio_autoack_state == RADIO_AUTOACK_RX && XR(0x618b) == 1);
    assert(mac_tx_interval_release(&interval_tx) == MAC_TX_OK);
    deliver_data(); handoff_call(4, MAC_RADIO_FRAME);
    handoff_call(1, MAC_RADIO_STOPPED);
    assert(automatic_acks == 1 && tx_started == 1 && tx_attempts == 1);
    puts("MAC handoff: genuine MAC actions driven by owned live time, real ACK bounds and independent RX ACK service.");
}
#endif

#ifndef MAC_HANDOFF_MAIN
#define MAC_HANDOFF_MAIN main
#endif
int MAC_HANDOFF_MAIN(int argc, char **argv)
{
    unsigned n, fcf;
    if (argc == 16 && !strcmp(argv[1], "--vector")) {
        n = (unsigned)strtoul(argv[2], NULL, 0);
        clock_address = (uint16_t)strtoul(argv[14], NULL, 0);
        handoff_staging_address = (uint16_t)strtoul(argv[15], NULL, 0);
        if (n < ATTEMPT_CASES)
            return mac_attempt_component_main(14, argv);
        normal_config = (uint16_t)strtoul(argv[3], NULL, 0);
        normal_output = (uint16_t)strtoul(argv[4], NULL, 0);
        normal_receipt = (uint16_t)strtoul(argv[5], NULL, 0);
        shared_address = (uint16_t)strtoul(argv[6], NULL, 0);
        radio_end = (uint16_t)strtoul(argv[7], NULL, 0);
        owner_end = (uint16_t)strtoul(argv[8], NULL, 0);
        raw_address = (uint16_t)strtoul(argv[9], NULL, 0);
        config_staging = (uint16_t)strtoul(argv[10], NULL, 0);
        attempt_raw_address = (uint16_t)strtoul(argv[11], NULL, 0);
        epoch_address = (uint16_t)strtoul(argv[12], NULL, 0);
        helper = (uint16_t)strtoul(argv[13], NULL, 0);
        assert(n < ATTEMPT_CASES + HANDOFF_CASES);
        printing_vector = 1; handoff_case(n-ATTEMPT_CASES);
        return 0;
    }
    assert(argc == 1);
    mac_attempt_component_main(argc, argv);
#if defined(CC2530_MAC_INTERVAL)
    handoff_mac();
    return 0;
#endif
    for (n = 0; n < HANDOFF_CASES; n++) {
        response_fcf = 2;
        handoff_case(n);
    }
    for (fcf = 2; fcf <= 65535u; fcf += 8) {
        response_fcf = (uint16_t)fcf;
        handoff_case(0);
    }
    handoff_reset(); handoff_call(0, MAC_RADIO_READY);
    for (n = 1; n <= owner_end; n++) {
        clock_address = (uint16_t)n;
        handoff_call(6, MAC_RADIO_BUFFER_OWNERSHIP);
    }
    for (n = helper-16; n <= helper; n++) {
        clock_address = (uint16_t)n;
        handoff_call(6, MAC_RADIO_BUFFER_OWNERSHIP);
    }
    printf("MAC handoff: %u new sequences; real owner clock, guarded live transition and retained faults.\n",
           HANDOFF_CASES);
    return 0;
}
#endif
