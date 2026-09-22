/* SPDX-License-Identifier: BSD-3-Clause
 * Synthetic combined peripherals. This component must NEVER be flashed.
 */
#include "mac_radio.h"
#include <stddef.h>
#include <string.h>

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t mac_radio_test_result[8];
MCU_XDATA radio_autoack_config_t mac_radio_test_config;
MCU_XDATA radio_autoack_frame_t mac_radio_test_frame;
MCU_XDATA mac_epoch_stamp_t mac_radio_test_stamp;
MCU_XDATA uint8_t mac_radio_test_operation, mac_radio_test_return, mac_radio_test_length;
MCU_XDATA uint16_t mac_radio_test_input, mac_radio_test_output, mac_radio_test_limit;
MCU_XDATA uint32_t mac_radio_test_timeout;

void mac_radio_test_cycle(void)
{
    __asm
        .globl _mac_radio_test_before
    _mac_radio_test_before:
        nop
    __endasm;
    switch (mac_radio_test_operation) {
    case 0: mac_radio_test_return = mac_radio_init(
        (const radio_autoack_config_t MCU_XDATA *)mac_radio_test_input,
        mac_radio_test_timeout, mac_radio_test_limit); break;
    case 1: mac_radio_test_return = mac_radio_now(mac_radio_test_timeout, mac_radio_test_limit,
        (mac_epoch_stamp_t MCU_XDATA *)mac_radio_test_output); break;
    case 2: mac_radio_test_return = mac_radio_receive(mac_radio_test_timeout, mac_radio_test_limit,
        (radio_autoack_frame_t MCU_XDATA *)mac_radio_test_output); break;
    case 3: mac_radio_test_return = mac_radio_stop(mac_radio_test_timeout, mac_radio_test_limit); break;
    case 4: mac_radio_test_return = mac_radio_send(
        (const uint8_t MCU_XDATA *)mac_radio_test_output, mac_radio_test_length,
        mac_radio_test_timeout, mac_radio_test_limit); break;
    case 5: mac_radio_test_return = mac_radio_resume(mac_radio_test_timeout, mac_radio_test_limit); break;
    case 6: mac_radio_test_return = mac_time_read_live(mac_radio_test_timeout, mac_radio_test_limit,
        (mac_time_stamp_t MCU_XDATA *)mac_radio_test_output); break;
    default: mac_radio_test_return = mac_time_read_radio(mac_radio_test_timeout, mac_radio_test_limit,
        (mac_time_stamp_t MCU_XDATA *)mac_radio_test_output); break;
    }
    __asm
        .globl _mac_radio_test_done
    _mac_radio_test_done:
        nop
    __endasm;
}
void main(void)
{
    memset(mac_radio_test_result, 0, 8);
    memcpy(mac_radio_test_result, "MRC1", 4);
    mac_radio_test_result[4] = 1; mac_radio_test_result[5] = 8;
    memset(&mac_radio_test_frame, 0x69, sizeof(mac_radio_test_frame));
    memset(&mac_radio_test_stamp, 0x69, sizeof(mac_radio_test_stamp));
    for (;;) mac_radio_test_cycle();
}
#else
#define main radio_autoack_component_main
#include "test_radio_autoack.c"
#undef main

extern uint8_t mac_time_ready, mac_time_fault, mac_radio_shared_end, mac_radio_reserved_end;
extern mac_time_stamp_t mac_radio_raw;
extern mac_epoch_t mac_radio_epoch;
extern radio_autoack_config_t mac_radio_config;
static mac_epoch_stamp_t stamp;
static uint16_t fine, fine_latch, timer_period, fine_write;
static uint32_t overflow, overflow_latch, overflow_period, overflow_write, mac_step;
static unsigned pending, stuck, force_ff, order, malformed, clock_failure, first;
static unsigned exported, vectors, combined_calls;
static uint16_t shared_address = 0x400, raw_address = 0x410, staging_address = 0x430;
static uint16_t stamp_address = 0x800, normal_stamp = 0x800, wrapper_end = 0x500;
static uint32_t timeout = 1000;
static uint16_t limit = 100;
static uint64_t prior_raw;
static uint32_t oracle_symbols;
static unsigned oracle_ready;

static void timer_advance(uint32_t clocks)
{
    uint64_t sum, periods;
    if (!(SOC_T2CTRL & 4)) return;
    assert(timer_period == 512 && overflow_period == 0xffffff);
    sum = (uint64_t)fine + clocks; periods = sum / timer_period;
    fine = (uint16_t)(sum % timer_period);
    if (periods) SOC_T2IRQF |= 1;
    sum = (uint64_t)overflow + periods;
    if (sum >= overflow_period) SOC_T2IRQF |= 8;
    overflow = (uint32_t)(sum % overflow_period);
}
static uint8_t timer_selected(uint8_t a)
{
    uint8_t selection = a <= 0xa3 ? SOC_T2MSEL & 7 : (SOC_T2MSEL >> 4) & 7;
    if (selection == 2)
        return (uint8_t)(a <= 0xa3 ? (uint32_t)timer_period >> (8*(a-0xa2)) :
                        overflow_period >> (8*(a-0xa4)));
    assert(!selection);
    if (a == 0xa2) {
        uint8_t low;
        assert(!order);
        if ((SOC_T2CTRL & 4) && force_ff) {
            timer_advance((255u + 512u - fine) % 512u); force_ff--;
        }
        low = (uint8_t)fine;
        if (SOC_T2CTRL & 4) {
            order = low == 255 ? 0 : 1;
            if (low == 255) timer_advance(1);
        }
        fine_latch = malformed == 1 ? 512 : fine;
        overflow_latch = malformed == 2 ? 0xffffff : overflow;
        return malformed == 1 ? 0 : low;
    }
    if ((SOC_T2CTRL & 4) && order) {
        assert(a == 0xa2 + order); order = (order + 1) % 5;
    }
    return (uint8_t)(a == 0xa3 ? fine_latch >> 8 : overflow_latch >> (8*(a-0xa4)));
}
static uint8_t combined_load(uint8_t a, uint8_t value)
{
    timer_advance(mac_step);
    if (a == 0x94) {
        if (pending && !stuck && !--pending) SOC_T2CTRL |= 4;
        value = SOC_T2CTRL;
    } else if (a == 0xa1) value = SOC_T2IRQF;
    else if (a >= 0xa2 && a <= 0xa6) value = timer_selected(a);
    return load(a, value);
}
static uint8_t combined_xload(uint16_t a)
{
    timer_advance(mac_step); return xload(a);
}
static void combined_store(uint8_t a, uint8_t before, uint8_t value)
{
    timer_advance(mac_step);
    if (a != 0xc6 && a != 0x94 && a != 0xc3 && a != 0x9c && !(a >= 0xa2 && a <= 0xa6)) {
        store(a, before, value); return;
    }
    assert(write_count == 1 && writes[0].address == a && writes[0].after == value);
    write_count = 0; logs(); trace('w', a, value);
    if (a == 0xc6) {
        if (!clock_failure) SOC_CLKCONSTA = value;
    } else if (a == 0x94) {
        assert(!(before & 4) && (value == 8 || value == 9));
        if (value == 9) { assert(!SOC_T2MSEL && SOC_T2EVTCFG == 0x77); pending = 1; }
    } else if (a == 0xc3) assert(SOC_T2CTRL == 8 && (value == 0x22 || !value));
    else if (a == 0x9c) assert(SOC_T2CTRL == 2 && value == 0x77);
    else {
        assert(SOC_T2CTRL == 8 && SOC_T2MSEL == 0x22);
        if (a == 0xa2) fine_write = value;
        else if (a == 0xa3) timer_period = fine_write | ((uint16_t)value << 8);
        else if (a == 0xa4) overflow_write = value;
        else if (a == 0xa5) overflow_write |= (uint32_t)value << 8;
        else overflow_period = overflow_write | ((uint32_t)value << 16);
    }
}
static void combined_xstore(uint16_t a, uint8_t value)
{
    timer_advance(mac_step); xstore(a, value);
}
static uint16_t combined_address(const volatile void *p)
{
    if (p == &mac_radio_shared_end) return shared_address;
    if (p == &mac_radio_reserved_end) return wrapper_end;
    if (p == &mac_radio_raw) return raw_address;
    if (p == &mac_radio_config) return staging_address;
    if (p == &stamp) return stamp_address;
    return xaddress(p);
}
static void combined_reset(void)
{
    reset(); printing = 0; mac_time_ready = mac_time_fault = 0;
    memset((void *)mac_time_diagnostic(), 0, sizeof(mac_time_diagnostics_t));
    memset((void *)mac_radio_diagnostic(), 0, sizeof(mac_radio_diagnostics_t));
    memset(&mac_radio_raw, 0, sizeof(mac_radio_raw));
    memset(&mac_radio_epoch, 0, sizeof(mac_radio_epoch));
    memset(&stamp, 0x69, sizeof(stamp));
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_T2CTRL = 2;
    fine = fine_latch = fine_write = timer_period = 0;
    overflow = overflow_latch = overflow_write = overflow_period = 0;
    pending = stuck = force_ff = order = malformed = clock_failure = 0;
    mac_step = 7; first = 1; timeout = 1000; limit = 100;
    stamp_address = normal_stamp; oracle_ready = 0; prior_raw = 0; oracle_symbols = 0;
    host_mmio_read_hook = combined_load; host_mmio_write_hook = combined_store;
    host_mmio_xread_hook = combined_xload; host_mmio_xwrite_hook = combined_xstore;
    host_mmio_xaddress_hook = combined_address;
}
static void emit_stamp(const mac_epoch_stamp_t *s)
{
    printf("%02x%02x%02x%02x%02x%02x", (unsigned)(s->symbols & 255),
        (unsigned)((s->symbols >> 8) & 255), (unsigned)((s->symbols >> 16) & 255),
        (unsigned)(s->symbols >> 24), s->fine & 255, s->fine >> 8);
}
static void combined_call(unsigned operation, unsigned expected)
{
    const mac_radio_diagnostics_t *d = mac_radio_diagnostic();
    mac_radio_diagnostics_t saved = *d;
    radio_autoack_frame_t old_frame = frame.value;
    mac_epoch_stamp_t old_stamp = stamp;
    unsigned result, before = accesses, i;
    if (exported) {
        printf("%s{\"operation\":%u,\"timeout\":%lu,\"limit\":%u,\"input\":%u,\"output\":%u,"
            "\"length\":%u,\"configuration\":\"", first ? "" : ",", operation,
            (unsigned long)timeout, limit, config_address,
            operation == 1 || operation >= 6 ? stamp_address : output_address, body_length);
        emit_configuration(); printf("\",\"initial\":{");
#define REG(name, address) printf("\"%u\":%u,", address, name);
        CC2530_REGISTER_LIST(REG)
#undef REG
        for (i = 0; i < sizeof(xregs); i++)
            printf("\"%u\":%u%s", 0x6100u+i, xregs[i], i+1 == sizeof(xregs) ? "" : ",");
        printf("},\"events\":["); trace_count = 0;
    }
    printing = exported; first = 0;
    switch (operation) {
    case 0: result = mac_radio_init(config_address ? &config.value : NULL, timeout, limit); break;
    case 1: result = mac_radio_now(timeout, limit, stamp_address ? &stamp : NULL); break;
    case 2: result = mac_radio_receive(timeout, limit, output_address ? &frame.value : NULL); break;
    case 3: result = mac_radio_stop(timeout, limit); break;
    case 4: result = mac_radio_send(output_address ? (const uint8_t *)&frame.value : NULL,
                                   body_length, timeout, limit); break;
    case 5: result = mac_radio_resume(timeout, limit); break;
    case 6: result = mac_time_read_live(timeout, limit, (mac_time_stamp_t *)&stamp); break;
    default: result = mac_time_read_radio(timeout, limit, (mac_time_stamp_t *)&stamp); break;
    }
    printing = 0; logs(); combined_calls++;
    if (result != expected) fprintf(stderr, "case%u op%u got%u want%u timer%u radio%u\n",
                                    vectors, operation, result, expected, d->timer_result, d->radio_result);
    assert(result == expected);
    if (operation < 6 && (saved.fault || (result >= 8 && result <= 11))) {
        assert(before == accesses && !memcmp(&saved, d, sizeof(saved)));
    }
    if (operation != 2 || (result != MAC_RADIO_FRAME && result != MAC_RADIO_BAD_CRC))
        assert(!memcmp(&old_frame, &frame.value, sizeof(old_frame)));
    if (operation != 1 || result != MAC_RADIO_READY) assert(!memcmp(&old_stamp, &stamp, sizeof(stamp)));
    if (operation < 6 && result < 8 && d->has_time) {
        uint64_t raw = (uint64_t)mac_radio_epoch.periods * 512u + mac_radio_epoch.fine;
        if (!oracle_ready) {
            oracle_symbols = d->last_live.symbols; oracle_ready = 1;
        } else {
            uint64_t delta = (raw + UINT64_C(0x1fffffe00) - prior_raw) % UINT64_C(0x1fffffe00);
            assert(delta < UINT64_C(0xffffff00));
            oracle_symbols += (uint32_t)((delta + (prior_raw % 512u)) / 512u);
            assert(d->last_live.symbols == oracle_symbols);
        }
        prior_raw = raw; assert(d->last_live.fine == raw % 512u);
        if (operation == 1) assert(stamp.symbols == oracle_symbols && stamp.fine == raw % 512u);
    }
    if (exported) {
        printf("],\"result\":%u,\"status\":\"", result); emit_stamp(&d->last_live);
        printf("%02x%02x%02x%02x%02x%02x%02x%02x", d->phase, d->result, d->fault, d->has_time,
               d->clock_result, d->timer_result, d->epoch_result, d->radio_result);
        printf("\",\"stamp\":\""); emit_stamp(&stamp);
        printf("\",\"frame\":\""); hex(&frame.value, sizeof(frame.value));
        printf("\",\"radio\":["); emit_diagnostics();
        printf("],\"timer\":[%lu,%u,%u,%u,%u,%u,%u,%u,%u]}",
            (unsigned long)mac_time_diagnostic()->elapsed_ticks, mac_time_diagnostic()->polls,
            mac_time_diagnostic()->discarded, mac_time_diagnostic()->result,
            mac_time_diagnostic()->phase, mac_time_diagnostic()->control,
            mac_time_diagnostic()->select, mac_time_diagnostic()->irq_flags,
            mac_time_diagnostic()->timebase_status);
    }
}
#define COMBINED_CASES 52u
static void combined_case(unsigned n)
{
    unsigned op;
    combined_reset(); vectors = n;
    if (exported) printf("{\"case\":%u,\"steps\":[", n);
    if (n == 1) { clock_failure = 1; tick_step = 512; }
    if (n == 2) stuck = 1;
    if (n == 3) ignored_enable = 1;
    if (n == 4) XR(0x618b) = 1;
    if (n >= 1 && n <= 4) {
        combined_call(0, n == 1 ? MAC_RADIO_CLOCK_ERROR : n == 3 ? MAC_RADIO_DRIVER_ERROR : MAC_RADIO_TIMER_ERROR);
        for (op = 0; op < 6; op++) combined_call(op, mac_radio_diagnostic()->fault);
    } else if (n >= 30 && n <= 37) {
        if (n == 30) config_address = 0;
        if (n == 31) config.value.channel = 27;
        if (n == 32) config.value.power = 6;
        if (n == 33) timeout = 0;
        if (n == 34) limit = 0;
        if (n == 35) timeout = TIMEBASE_HALF_RANGE;
        if (n == 36) config_address = wrapper_end;
        if (n == 37) config_address = 0x1df3;
        combined_call(0, n == 36 ? MAC_RADIO_BUFFER_OWNERSHIP : n == 37 ? MAC_RADIO_INVALID_RANGE : MAC_RADIO_INVALID_ARGUMENT);
    } else if (n == 38) {
        for (op = 1; op < 6; op++) combined_call(op, MAC_RADIO_STATE);
    } else {
        combined_call(0, MAC_RADIO_READY);
        assert(mac_time_ready && timer_period == 512 && overflow_period == 0xffffff && XR(0x618b) == 1);
        if (n == 0) {
            combined_call(1, MAC_RADIO_READY); combined_call(2, MAC_RADIO_EMPTY);
            enqueue(11, 0xe9, 0x38); combined_call(2, MAC_RADIO_FRAME);
            enqueue(5, 0x69, 0x55); combined_call(3, MAC_RADIO_DRAIN);
            combined_call(1, MAC_RADIO_READY); combined_call(2, MAC_RADIO_BAD_CRC);
            combined_call(3, MAC_RADIO_STOPPED); combined_call(1, MAC_RADIO_READY);
            reply_after_tx = 1; combined_call(4, MAC_RADIO_TX_DONE);
            combined_call(1, MAC_RADIO_READY); combined_call(2, MAC_RADIO_FRAME);
            combined_call(3, MAC_RADIO_STOPPED); combined_call(5, MAC_RADIO_READY);
            assert(tx_attempts == 1 && XR(0x6189) == 0x60);
        } else if (n == 5 || n == 6 || n == 7) {
            combined_call(3, MAC_RADIO_STOPPED);
            if (n == 5) cca_clear = 0;
            if (n == 6) tx_error = 1;
            if (n == 7) tx_write_fault = 3;
            combined_call(4, n == 5 ? MAC_RADIO_CCA_BUSY : MAC_RADIO_DRIVER_ERROR);
        } else if (n == 8) {
            stop_receive = stop_ack = 1; combined_call(3, MAC_RADIO_DRAIN);
            combined_call(1, MAC_RADIO_READY); combined_call(2, MAC_RADIO_FRAME);
            combined_call(3, MAC_RADIO_STOPPED);
        } else if (n >= 9 && n <= 24) {
            if (n == 9) force_ff = 2;
            if (n == 10) { force_ff = 1000; limit = 3; tick_step = 0; }
            if (n == 11) { force_ff = 1000; timeout = 2; }
            if (n == 12) SOC_T2MSEL = 0x11;
            if (n == 13) SOC_T2CTRL = 9;
            if (n == 14) SOC_T2EVTCFG = 0;
            if (n == 15) SOC_T2IRQM = 1;
            if (n == 16) SOC_IEN0 = 128;
            if (n == 17) SOC_DMAARM = 1;
            if (n == 18) XR(0x61e1) = 1;
            if (n == 19) SOC_RFERRF = 4;
            if (n == 20) XR(0x6192) = 128;
            if (n == 21) SOC_CLKCONCMD = SOC_CLKCONSTA = 0x98;
            if (n == 22 || n == 23) malformed = n - 21;
            if (n == 24) tick_step = TIMEBASE_HALF_RANGE;
            combined_call(1, n == 9 ? MAC_RADIO_READY : MAC_RADIO_TIMER_ERROR);
            if (n == 9) assert(mac_time_diagnostic()->discarded == 2);
        } else if (n == 25 || n == 26 || n == 27) {
            uint64_t raw = (uint64_t)mac_radio_epoch.periods * 512u + mac_radio_epoch.fine;
            raw += n == 25 ? UINT64_C(0xffffff00) : n == 26 ? UINT64_C(0xffffff01) : UINT64_C(0x1fffffdff);
            raw %= UINT64_C(0x1fffffe00);
            mac_step = 0; fine = (uint16_t)(raw % 512u); overflow = (uint32_t)(raw / 512u);
            combined_call(1, MAC_RADIO_EPOCH_ERROR);
        } else if (n == 28) {
            unsigned i;
            mac_step = 0;
            for (i = 0; i < (exported ? 4u : 520u); i++) {
                timer_advance(0xfffffe00u); combined_call(1, MAC_RADIO_READY);
            }
        } else if (n == 29) combined_call(6, MAC_TIME_UNSUPPORTED_STATE);
        else if (n >= 39 && n <= 47) {
            if (n == 39) stamp_address = 0;
            if (n == 40) stamp_address = wrapper_end;
            if (n == 41) stamp_address = shared_address;
            if (n == 42) stamp_address = helper - 11;
            if (n == 43) stamp_address = helper;
            if (n == 44) stamp_address = 0x1e00;
            if (n == 45) stamp_address = 0x1f00;
            if (n == 46) { output_address = wrapper_end; combined_call(2, MAC_RADIO_BUFFER_OWNERSHIP); }
            if (n == 47) { body_length = 126; combined_call(4, MAC_RADIO_INVALID_ARGUMENT); }
            if (n < 46) combined_call(1, n == 39 ? MAC_RADIO_INVALID_ARGUMENT :
                n < 44 ? MAC_RADIO_BUFFER_OWNERSHIP : MAC_RADIO_INVALID_RANGE);
        } else if (n == 48) {
            combined_call(0, MAC_RADIO_STATE); combined_call(4, MAC_RADIO_STATE);
            combined_call(5, MAC_RADIO_STATE); combined_call(3, MAC_RADIO_STOPPED);
            combined_call(2, MAC_RADIO_STATE); combined_call(3, MAC_RADIO_STATE);
            output_address = helper; combined_call(4, MAC_RADIO_BUFFER_OWNERSHIP);
        } else if (n == 49 || n == 50) {
            stamp_address = n == 49 ? shared_address : helper - 11;
            combined_call(7, MAC_TIME_BUFFER_OWNERSHIP);
        } else if (n == 51) {
            XR(0x6192) = 0x40; XR(0x6193) = 0x27;
            combined_call(1, MAC_RADIO_READY);
        }
        if (mac_radio_diagnostic()->fault)
            for (op = 0; op < 6; op++) combined_call(op, mac_radio_diagnostic()->fault);
    }
    if (exported) puts("]}");
}
int main(int argc, char **argv)
{
    unsigned n;
    (void)radio_autoack_component_main;
    assert(argc == 1 || (argc == 11 && !strcmp(argv[1], "--vector")));
    if (argc == 11) {
        n = (unsigned)strtoul(argv[2], NULL, 0);
        normal_config = (uint16_t)strtoul(argv[3], NULL, 0);
        normal_output = (uint16_t)strtoul(argv[4], NULL, 0);
        normal_stamp = (uint16_t)strtoul(argv[5], NULL, 0);
        shared_address = (uint16_t)strtoul(argv[6], NULL, 0);
        wrapper_end = (uint16_t)strtoul(argv[7], NULL, 0);
        raw_address = (uint16_t)strtoul(argv[8], NULL, 0);
        staging_address = (uint16_t)strtoul(argv[9], NULL, 0);
        helper = (uint16_t)strtoul(argv[10], NULL, 0);
        assert(n < COMBINED_CASES); exported = 1; combined_case(n); return 0;
    }
    for (n = 0; n < COMBINED_CASES; n++) combined_case(n);
    combined_reset(); combined_call(0, MAC_RADIO_READY);
    for (n = 1; n <= wrapper_end; n++) {
        stamp_address = output_address = (uint16_t)n;
        combined_call(1, MAC_RADIO_BUFFER_OWNERSHIP);
        combined_call(2, MAC_RADIO_BUFFER_OWNERSHIP);
    }
    for (n = helper - 11u; n <= helper; n++) {
        stamp_address = output_address = (uint16_t)n;
        combined_call(1, MAC_RADIO_BUFFER_OWNERSHIP);
        combined_call(2, MAC_RADIO_BUFFER_OWNERSHIP);
    }
    for (n = 0x1e00; n <= 0xffff; n++) {
        stamp_address = (uint16_t)n; combined_call(1, MAC_RADIO_INVALID_RANGE);
    }
    stamp_address = normal_stamp; output_address = normal_output;
    combined_call(3, MAC_RADIO_STOPPED);
    for (n = 1; n <= wrapper_end; n++) {
        output_address = (uint16_t)n; combined_call(4, MAC_RADIO_BUFFER_OWNERSHIP);
    }
    for (n = helper - 11u; n <= helper; n++) {
        output_address = (uint16_t)n; combined_call(4, MAC_RADIO_BUFFER_OWNERSHIP);
    }
    printf("MAC radio: %u combined cases/%u calls; genuine services, live fractional time, "
           "radio phases and retained faults PASS (synthetic only).\n", COMBINED_CASES, combined_calls);
    return 0;
}
#endif
