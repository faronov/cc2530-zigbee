/* SPDX-License-Identifier: BSD-3-Clause
 * Synthetic real-service composition. NEVER flash this image.
 */
#include "mac_attempt.h"
#include <stddef.h>
#include <string.h>
#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t mac_attempt_test_result[8];
MCU_XDATA radio_autoack_config_t mac_attempt_test_config;
MCU_XDATA radio_autoack_frame_t mac_attempt_test_frame;
MCU_XDATA mac_attempt_record_t mac_attempt_test_record;
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
    default: mac_attempt_test_return = mac_attempt_resume(mac_attempt_test_timeout, mac_attempt_test_limit); break;
    }
    __asm
        .globl _mac_attempt_test_done
    _mac_attempt_test_done:
        nop
    __endasm;
}
void main(void)
{
    memset(mac_attempt_test_result, 0, 8); memcpy(mac_attempt_test_result, "MAT1", 4);
    mac_attempt_test_result[4] = 1; mac_attempt_test_result[5] = 8;
    memset(&mac_attempt_test_frame, 0x69, sizeof(mac_attempt_test_frame));
    memset(&mac_attempt_test_record, 0x69, sizeof(mac_attempt_test_record));
    for (;;) mac_attempt_test_cycle();
}
#else
#define main radio_autoack_component_main
#include "test_radio_autoack.c"
#undef main

extern uint8_t mac_time_ready, mac_time_fault, mac_time_attempt_active;
extern uint8_t mac_radio_shared_end, mac_radio_reserved_end, mac_attempt_reserved_end, mac_attempt_fault;
extern uint16_t mac_attempt_slot;
extern mac_time_stamp_t mac_radio_raw;
extern mac_epoch_t mac_radio_epoch, mac_attempt_first;
extern radio_autoack_config_t mac_radio_config;
extern radio_autoack_attempt_t mac_attempt_raw;
static mac_attempt_record_t receipt;
static uint16_t fine, fine_latch, fine_period, fine_write;
static uint32_t coarse, coarse_latch, coarse_period, coarse_write;
static unsigned pending, order, ff, broken, clock_failure, printing_vector, calls;
static uint32_t clocks, tx_remaining, reply_remaining;
static uint64_t model_clocks, epoch_origin, model_tx_end, model_rx_end, model_arm;
static uint32_t mmio_clocks, extra_arm, extra_rx, window = 64, bound = 10000;
static uint16_t cap = 1000;
static unsigned first_call, case_number, fail_after_arm;
static unsigned ff_after_arm;
static unsigned equal_arm, arm_pending;
static uint16_t shared_address = 0x500, radio_end = 0x700, owner_end = 0xb00;
static uint16_t raw_address = 0x510, config_staging = 0x530, attempt_raw_address = 0x710;
static uint16_t epoch_address = 0x900, receipt_address = 0xc00, normal_receipt = 0xc00;

static void advance_clocks(uint32_t delta)
{
    uint64_t sum;
    if (!(SOC_T2CTRL & 4)) return;
    assert(fine_period == 512 && coarse_period == 0xffffff);
    model_clocks += delta;
    clocks += delta;
    sum = (uint64_t)fine + delta;
    if (sum >= 512) SOC_T2IRQF |= 1;
    fine = (uint16_t)(sum % 512);
    sum = coarse + sum/512;
    if (sum >= 0xffffff) SOC_T2IRQF |= 8;
    coarse = (uint32_t)(sum % 0xffffff);
    if (tx_remaining) {
        if (delta >= tx_remaining && !hold_tx) {
            model_tx_end = model_clocks-delta+tx_remaining;
            tx_remaining = 0; SOC_RFIRQF1 |= 2;
            XR(0x6192) = 0x40; XR(0x6193) = (XR(0x6193) & 0xc8) | 1;
            XR(0x6199) = 0; XR(0x61a1) = (uint8_t)tx_count;
            mode = 7; reply_remaining = 34u*512u;
        } else if (!hold_tx) tx_remaining -= delta;
    } else if (reply_remaining) {
        if (delta >= reply_remaining) {
            model_rx_end = model_clocks-delta+reply_remaining;
            reply_remaining = 0; mode = 2; XR(0x6192) = 0;
            XR(0x6193) = (XR(0x6193) & 0xc8) | 5; XR(0x6199) = 1;
            if (reply_after_tx && XR(0x6189) == 0x40) {
                reply_after_tx = 0; enqueue(5, bad_reply ? 0x69 : 0xe9, 0x5a);
            }
        } else reply_remaining -= delta;
    }
}
static uint8_t timer_load(uint8_t a)
{
    unsigned selection = a <= 0xa3 ? SOC_T2MSEL & 7 : (SOC_T2MSEL >> 4) & 7;
    if (selection == 2)
        return (uint8_t)(a <= 0xa3 ? (uint32_t)fine_period >> (8*(a-0xa2)) :
                        coarse_period >> (8*(a-0xa4)));
    assert(!selection);
    if (a == 0xa2) {
        uint8_t low;
        assert(!order);
        if ((SOC_T2CTRL & 4) && ff) { advance_clocks((255u+512u-fine)%512u); ff--; }
        low = (uint8_t)fine;
        if (SOC_T2CTRL & 4) {
            order = low == 255 ? 0 : 1;
            if (low == 255) advance_clocks(1);
        }
        fine_latch = broken == 1 ? 512 : fine;
        coarse_latch = broken == 2 ? 0xffffff : coarse;
        return broken == 1 ? 0 : low;
    }
    if ((SOC_T2CTRL & 4) && order) { assert(a == 0xa2+order); order = (order+1)%5; }
    return (uint8_t)(a == 0xa3 ? fine_latch >> 8 : coarse_latch >> (8*(a-0xa4)));
}
static uint8_t attempt_load(uint8_t a, uint8_t value)
{
    advance_clocks(mmio_clocks);
    if (a == 0xa2 && arm_pending) {
        uint64_t target = (uint64_t)mac_attempt_raw.before.periods*512u +
            mac_attempt_raw.before.fine + (16u+2u*body_length)*512u;
        uint64_t now = (uint64_t)coarse*512u+fine;
        uint64_t delta = (target + UINT64_C(0x1fffffe00)-now) % UINT64_C(0x1fffffe00);
        assert(delta < UINT64_C(0xffffff00)); advance_clocks((uint32_t)delta);
        arm_pending = 0;
    }
    if (a == 0x94) {
        if (pending && !--pending) SOC_T2CTRL |= 4;
        value = SOC_T2CTRL;
    } else if (a == 0xa1) value = SOC_T2IRQF;
    else if (a >= 0xa2 && a <= 0xa6) value = timer_load(a);
    return load(a, value);
}
static uint8_t attempt_xload(uint16_t a)
{
    advance_clocks(mmio_clocks);
    if ((mode == 6 || mode == 7) && a == 0x624a) {
        logs(); trace('r', a, XR(a)); return XR(a);
    }
    if (a == 0x619b && packets && extra_rx) {
        advance_clocks(extra_rx); extra_rx = 0;
    }
    return xload(a);
}
static void attempt_store(uint8_t a, uint8_t before, uint8_t value)
{
    advance_clocks(mmio_clocks);
    if ((a == 0xd9 || (a == 0xe1 && value == 0xee)) && XR(0x6189) == 0x4c) {
        assert(write_count == 1 && writes[0].address == a && writes[0].after == value);
        write_count = 0; logs(); trace('w', a, value);
        assert(mode == 5 && !XR(0x618b) && XR(0x6180) == 0x0c && !SOC_RFERRF);
        if (a == 0xd9) {
            assert(tx_count < 128);
            tx_fifo[tx_count++] = value;
            XR(0x619c) = XR(0x61a2) = (uint8_t)tx_count;
        } else {
            tx_flushes++; tx_count = 0; XR(0x619c) = XR(0x61a1) = XR(0x61a2) = 0;
        }
        return;
    }
    if (a == 0xe1 && value == 0xea && XR(0x6189) == 0x4c) {
        assert(write_count == 1 && writes[0].address == a && writes[0].after == value);
        write_count = 0; logs(); trace('w', a, value);
        assert(mode == 2 && XR(0x6197) == 0x0a && XR(0x6196) == 0xf8 &&
               XR(0x6199) == 1 && cca_cycles >= 4 && !(SOC_RFIRQF1 & 2));
        assert(tx_count == body_length+1u && tx_fifo[0] == body_length+2u);
        cca_cycles = 0; tx_attempts++;
        if (cca_clear) {
            XR(0x6193) |= 8;
            if (!lost_tx) {
                tx_started++; mode = 6; XR(0x6192) = 0x40;
                XR(0x6193) = (XR(0x6193) & 0xc8) | 6;
                tx_remaining = (12u+16u+2u*body_length)*512u;
            }
        } else XR(0x6193) &= 0xf7;
        return;
    }
    if (a != 0xc6 && a != 0x94 && a != 0xc3 && a != 0x9c && !(a >= 0xa2 && a <= 0xa6)) {
        store(a, before, value); return;
    }
    assert(write_count == 1 && writes[0].address == a && writes[0].after == value);
    write_count = 0; logs(); trace('w', a, value);
    if (a == 0xc6) { if (!clock_failure) SOC_CLKCONSTA = value; }
    else if (a == 0x94) { assert(!(before & 4) && (value == 8 || value == 9)); pending = value == 9; }
    else if (a == 0xc3) assert(SOC_T2CTRL == 8 && (value == 0x22 || !value));
    else if (a == 0x9c) assert(SOC_T2CTRL == 2 && value == 0x77);
    else {
        assert(SOC_T2CTRL == 8 && SOC_T2MSEL == 0x22);
        if (a == 0xa2) fine_write = value;
        else if (a == 0xa3) fine_period = fine_write | ((uint16_t)value << 8);
        else if (a == 0xa4) coarse_write = value;
        else if (a == 0xa5) coarse_write |= (uint32_t)value << 8;
        else coarse_period = coarse_write | ((uint32_t)value << 16);
    }
}
static void attempt_xstore(uint16_t a, uint8_t value)
{
    advance_clocks(mmio_clocks);
    if ((a == 0x6189 && (value == 0x4c || XR(a) == 0x4c)) ||
        (a == 0x6196 && value == 0xf8) || (a == 0x6197 && value == 0x0a) ||
        (a == 0x618c && XR(0x6189) == 0x4c)) {
        assert(xwrite_count == 1 && xwrites[0].address == a && xwrites[0].value == value);
        xwrite_count = 0; logs(); trace('w', a, value);
        if (a == 0x618c) {
            assert(mode == 5 && !count && !packets && !XR(0x618b));
            XR(0x618b) = 1; XR(0x6192) = 0x40; XR(0x6193) = (XR(0x6193)&8u)|1u; XR(0x6199) = 0;
            mode = 1; phase = 0;
        } else {
            assert(mode == 5 || (a == 0x6189 && value == 0x40 && (mode == 6 || mode == 2)));
            XR(a) = value ^ (a == phase_write_fault ? 1u : 0u);
            if (a == 0x6189 && value == 0x40) {
                model_arm = model_clocks; arm_pending = equal_arm;
                advance_clocks(extra_arm);
                ff = ff_after_arm;
                if (fail_after_arm) SOC_RFERRF = 8;
            }
        }
        return;
    }
    xstore(a, value);
}
static uint16_t attempt_address(const volatile void *p)
{
    if (p == &mac_radio_shared_end) return shared_address;
    if (p == &mac_radio_reserved_end) return radio_end;
    if (p == &mac_attempt_reserved_end) return owner_end;
    if (p == &mac_radio_raw) return raw_address;
    if (p == &mac_radio_config) return config_staging;
    if (p == &mac_attempt_raw || p == &mac_attempt_raw.before) return attempt_raw_address;
    if (p == &mac_attempt_raw.armed) return attempt_raw_address+6;
    if (p == &mac_attempt_raw.tx) return attempt_raw_address+12;
    if (p == &mac_attempt_raw.rx) return attempt_raw_address+18;
    if (p == &mac_attempt_raw.last) return attempt_raw_address+24;
    if (p == &mac_attempt_raw.frame) return attempt_raw_address+30;
    if (p == &mac_attempt_first) return epoch_address;
    if (p == &receipt) return receipt_address;
    return xaddress(p);
}
static void attempt_reset(void)
{
    reset(); printing = 0; mac_time_ready = mac_time_fault = mac_time_attempt_active = 0;
    mac_attempt_fault = 0; mac_attempt_slot = 0;
    memset((void *)mac_time_diagnostic(), 0, sizeof(mac_time_diagnostics_t));
    memset((void *)mac_radio_diagnostic(), 0, sizeof(mac_radio_diagnostics_t));
    memset(&mac_radio_raw, 0, sizeof(mac_radio_raw)); memset(&mac_radio_epoch, 0, sizeof(mac_radio_epoch));
    memset(&mac_attempt_raw, 0, sizeof(mac_attempt_raw)); memset(&mac_attempt_first, 0, sizeof(mac_attempt_first));
    memset(&receipt, 0x69, sizeof(receipt));
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_T2CTRL = 2;
    fine = fine_latch = fine_write = fine_period = 0;
    coarse = coarse_latch = coarse_write = coarse_period = 0;
    pending = order = ff = broken = clock_failure = fail_after_arm = ff_after_arm = 0;
    equal_arm = arm_pending = 0;
    model_clocks = epoch_origin = model_tx_end = model_rx_end = model_arm = 0;
    clocks = tx_remaining = reply_remaining = extra_arm = extra_rx = 0;
    mmio_clocks = 32; first_call = 1; bound = 10000; cap = 1000; window = 64;
    receipt_address = normal_receipt; reply_after_tx = 1;
    host_mmio_read_hook = attempt_load; host_mmio_write_hook = attempt_store;
    host_mmio_xread_hook = attempt_xload; host_mmio_xwrite_hook = attempt_xstore;
    host_mmio_xaddress_hook = attempt_address;
}
static void stamp_hex(const mac_epoch_stamp_t *s)
{
    printf("%02x%02x%02x%02x%02x%02x", (unsigned)(s->symbols & 255),
        (unsigned)((s->symbols >> 8) & 255), (unsigned)((s->symbols >> 16) & 255),
        (unsigned)(s->symbols >> 24), s->fine & 255, s->fine >> 8);
}
static void receipt_hex(void)
{
    stamp_hex(&receipt.tx_lower); stamp_hex(&receipt.tx_upper); stamp_hex(&receipt.rx_upper);
    stamp_hex(&receipt.armed); stamp_hex(&receipt.last); hex(&receipt.frame, 128);
    printf("%02x%02x%02x%02x%02x%02x", receipt.slot & 255, receipt.slot >> 8,
        receipt.length, receipt.transmitted, receipt.received, receipt.within_window);
}
static void attempt_call(unsigned op, unsigned expected)
{
    unsigned result, i, before = accesses;
    mac_attempt_record_t saved = receipt;
    mac_radio_diagnostics_t diagnostic = *mac_attempt_diagnostic();
    radio_autoack_frame_t old_frame = frame.value;
    if (printing_vector) {
        printf("%s{\"operation\":%u,\"input\":%u,\"output\":%u,\"timeout\":%lu,"
            "\"limit\":%u,\"window\":%lu,\"length\":%u,\"configuration\":\"",
            first_call ? "" : ",", op, op == 2 ? output_address : config_address,
            op == 3 ? receipt_address : output_address, (unsigned long)bound, cap,
            (unsigned long)window, body_length);
        emit_configuration(); printf("\",\"initial\":{");
#define REG(name, address) printf("\"%u\":%u,", address, name);
        CC2530_REGISTER_LIST(REG)
#undef REG
        for (i = 0; i < sizeof(xregs); i++)
            printf("\"%u\":%u%s", 0x6100u+i, xregs[i], i+1 == sizeof(xregs) ? "" : ",");
        printf("},\"events\":["); trace_count = 0;
    }
    printing = printing_vector; first_call = 0;
    switch (op) {
    case 0: result = mac_attempt_init(config_address ? &config.value : NULL, bound, cap); break;
    case 1: result = mac_attempt_stop(bound, cap); break;
    case 2: result = mac_attempt_prepare(output_address ? (const uint8_t *)&frame.value : NULL,
                                         body_length, bound, cap); break;
    case 3: result = mac_attempt_run((uint16_t)window, bound, cap, receipt_address ? &receipt : NULL); break;
    case 4: result = mac_attempt_receive(bound, cap, output_address ? &frame.value : NULL); break;
    default: result = mac_attempt_resume(bound, cap); break;
    }
    printing = 0; logs(); calls++;
    if (result != expected)
        fprintf(stderr, "case%u op%u got%u expected%u radio%u timer%u phase%u\n", case_number,
            op, result, expected, radio_autoack_diagnostic()->result, mac_time_diagnostic()->result,
            radio_autoack_diagnostic()->phase);
    assert(result == expected);
    if (op == 0 && result == MAC_RADIO_READY)
        epoch_origin = ((uint64_t)mac_radio_epoch.periods-mac_radio_epoch.symbols)*512u;
    if (result >= 8 || op != 3) assert(!memcmp(&saved, &receipt, sizeof(saved)));
    if (op != 4 || (result != 1 && result != 2)) assert(!memcmp(&old_frame, &frame.value, sizeof(old_frame)));
    if (diagnostic.fault || (result >= 8 && result <= 11)) {
        assert(accesses == before);
        assert(!memcmp(&diagnostic, mac_attempt_diagnostic(), sizeof(diagnostic)));
    }
    if (op == 3 && result < 8 && receipt.transmitted) {
        uint64_t lower = (uint64_t)receipt.tx_lower.symbols*512+receipt.tx_lower.fine;
        uint64_t upper = (uint64_t)receipt.tx_upper.symbols*512+receipt.tx_upper.fine;
        uint64_t armed = (uint64_t)receipt.armed.symbols*512+receipt.armed.fine;
        assert(armed < lower && lower <= upper && receipt.slot == mac_attempt_slot);
        assert(lower <= model_tx_end-epoch_origin && model_tx_end-epoch_origin <= upper);
        assert(model_arm-epoch_origin <= armed && model_arm < model_tx_end);
        if (receipt.received) {
            uint64_t rx = (uint64_t)receipt.rx_upper.symbols*512+receipt.rx_upper.fine;
            assert(receipt.frame.length == 3 && receipt.frame.body[0] == 2 && receipt.frame.body[2] == 0x5a);
            assert(receipt.within_window == (rx <= lower+window*512));
            assert(model_tx_end < model_rx_end && model_rx_end-epoch_origin <= rx);
        }
    }
    if (printing_vector) {
        const mac_radio_diagnostics_t *d = mac_attempt_diagnostic();
        printf("],\"return\":%u,\"frame\":\"", result); hex(&frame.value, 128);
        printf("\",\"record\":\""); receipt_hex();
        printf("\",\"diagnostic\":\""); stamp_hex(&d->last_live);
        printf("%02x%02x%02x%02x%02x%02x%02x%02x\"}", d->phase, d->result, d->fault,
            d->has_time, d->clock_result, d->timer_result, d->epoch_result, d->radio_result);
    }
}
#define ATTEMPT_CASES 28u
static void attempt_case(unsigned n)
{
    unsigned op;
    case_number = n; attempt_reset();
    if (printing_vector) printf("{\"case\":%u,\"steps\":[", n);
    attempt_call(3, MAC_RADIO_STATE);
    attempt_call(0, MAC_RADIO_READY);
    if (n == 26) {
        for (op = 0; op < 3; op++) {
            advance_clocks(0x7ffff000UL); attempt_call(4, MAC_RADIO_EMPTY);
        }
    }
    attempt_call(2, MAC_RADIO_STATE);
    attempt_call(1, MAC_RADIO_STOPPED);
    if (n == 25) SOC_RFIRQF1 |= 1;
    if (n == 14) body_length = 125;
    if (n == 15) body_length = 1;
    if (n == 20) { cap = 1; attempt_call(2, MAC_RADIO_TIMER_ERROR); goto finished; }
    if (n == 21) {
        phase_write_fault = 0x6196; attempt_call(2, MAC_RADIO_DRIVER_ERROR); goto finished;
    }
    attempt_call(2, MAC_RADIO_READY);
    attempt_call(2, MAC_RADIO_STATE);
    if (n == 1) { cca_clear = 0; XR(0x6193) |= 8; }
    if (n == 2) bad_reply = 1;
    if (n == 3) reply_after_tx = 0;
    if (n == 4) extra_arm = 65536;
    if (n == 5) extra_rx = 65536;
    if (n == 6) fail_after_arm = 1;
    if (n == 7) ff = 2;
    if (n == 8) broken = 1;
    if (n == 9) broken = 2;
    if (n == 10) { hold_tx = 1; cap = 120; }
    if (n == 11) { lost_tx = 1; cap = 120; }
    if (n == 12) XR(0x61a8) ^= 1;
    if (n == 13) SOC_T2MSEL = 1;
    if (n == 16) window = 1;
    if (n == 17) arrive_after = 1;
    if (n == 18) after_reads = 2;
    if (n == 19) {
        window = 0; attempt_call(3, MAC_RADIO_INVALID_ARGUMENT);
        window = 64; receipt_address = owner_end; attempt_call(3, MAC_RADIO_BUFFER_OWNERSHIP);
        receipt_address = helper-11; attempt_call(3, MAC_RADIO_BUFFER_OWNERSHIP);
        receipt_address = 0x1e00; attempt_call(3, MAC_RADIO_INVALID_RANGE);
        receipt_address = normal_receipt;
    }
    if (n == 22) { ff_after_arm = 2; }
    if (n == 23) { ff_after_arm = 200; cap = 100; }
    if (n == 24) { attempt_call(1, MAC_RADIO_STOPPED); attempt_call(2, MAC_RADIO_READY); }
    if (n == 26) {
        uint64_t now = (uint64_t)coarse*512u+fine;
        uint64_t delta = UINT64_C(0x1fffffe00)-55000u-now;
        assert(delta < UINT64_C(0xffffff00)); advance_clocks((uint32_t)delta);
    }
    if (n == 27) equal_arm = 1;
    attempt_call(3, n == 1 ? MAC_RADIO_CCA_BUSY : n == 2 ? MAC_RADIO_BAD_CRC :
        n == 3 || n == 16 ? MAC_RADIO_EMPTY : n == 8 || n == 9 || n == 13 ? MAC_RADIO_TIMER_ERROR :
        n == 4 || n == 6 || n == 10 || n == 11 || n == 12 || n == 18 || n == 23 || n == 27 ?
        MAC_RADIO_DRIVER_ERROR :
        MAC_RADIO_FRAME);
    if (n == 26) assert(mac_attempt_raw.before.periods > 0xffff00 && mac_attempt_raw.rx.periods < 100);
    if (!mac_attempt_diagnostic()->fault) {
        attempt_call(3, MAC_RADIO_STATE);
        if (n == 17) {
            attempt_call(1, MAC_RADIO_DRAIN);
            attempt_call(4, MAC_RADIO_FRAME); attempt_call(1, MAC_RADIO_STOPPED);
        } else {
            reply_after_tx = 0; reply_remaining = 0;
            if (mode == 7) { mode = 2; XR(0x6192) = 0; XR(0x6193) = (XR(0x6193)&0xc8)|5; XR(0x6199) = 1; }
            attempt_call(1, MAC_RADIO_STOPPED);
        }
        attempt_call(5, MAC_RADIO_READY);
    }
finished:
    if (mac_attempt_diagnostic()->fault)
        for (op = 0; op < 6; op++) attempt_call(op, mac_attempt_diagnostic()->fault);
    if (printing_vector) puts("]}");
}
int main(int argc, char **argv)
{
    unsigned n;
    (void)radio_autoack_component_main;
    assert(argc == 1 || (argc == 14 && !strcmp(argv[1], "--vector")));
    if (argc == 14) {
        n = (unsigned)strtoul(argv[2], NULL, 0);
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
        assert(n < ATTEMPT_CASES); printing_vector = 1; attempt_case(n); return 0;
    }
    normal_config = 0xe00; normal_output = 0xd00; helper = 0x1200;
    for (n = 0; n < ATTEMPT_CASES; n++) attempt_case(n);
    attempt_reset(); attempt_call(0, MAC_RADIO_READY);
    for (n = 1; n <= owner_end; n++) {
        output_address = receipt_address = (uint16_t)n;
        attempt_call(4, MAC_RADIO_BUFFER_OWNERSHIP); attempt_call(3, MAC_RADIO_BUFFER_OWNERSHIP);
    }
    for (n = helper-11; n <= helper; n++) {
        output_address = receipt_address = (uint16_t)n;
        attempt_call(4, MAC_RADIO_BUFFER_OWNERSHIP); attempt_call(3, MAC_RADIO_BUFFER_OWNERSHIP);
    }
    printf("MAC attempt: %u sequences/%u real-service calls PASS; synthetic hardware only.\n",
           ATTEMPT_CASES, calls);
    return 0;
}
#endif
