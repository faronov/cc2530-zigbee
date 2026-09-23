/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "security_counter.h"

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t counter_test_result[8];
MCU_XDATA uint8_t counter_test_buffer[112];
MCU_XDATA uint32_t counter_test_value, counter_test_nwk, counter_test_aps;
MCU_XDATA uint16_t counter_test_limit;
MCU_XDATA uint8_t counter_test_action, counter_test_length, counter_test_domain, counter_test_return;

void counter_test_cycle(void)
{
    __asm
        .globl _counter_before
    _counter_before:
        nop
    __endasm;
    switch (counter_test_action) {
    case 0: counter_test_return = security_counter_open(); break;
    case 1: counter_test_return = security_counter_create(counter_test_nwk, counter_test_aps,
                counter_test_buffer, counter_test_length, counter_test_limit); break;
    case 2: counter_test_return = security_counter_take(counter_test_domain, &counter_test_value,
                                                       counter_test_limit); break;
    case 3: counter_test_return = security_counter_save(counter_test_buffer, counter_test_length,
                                                       counter_test_limit); break;
    default: counter_test_return = security_counter_read(counter_test_buffer, 112, &counter_test_length);
    }
    __asm
        .globl _counter_done
    _counter_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    counter_test_result[0] = 'C'; counter_test_result[1] = 'T';
    counter_test_result[2] = 'R'; counter_test_result[3] = '1';
    counter_test_result[4] = 1; counter_test_result[5] = 8;
    counter_test_result[6] = counter_test_result[7] = 0;
    for (i = 0; i < 112; i++) counter_test_buffer[i] = i ^ 0x69;
    counter_test_limit = 3;
    for (;;) counter_test_cycle();
}
#else
#define NV_RECORD_TEST_ENTRY nv_record_reference_main
#include "test_nv_record.c"
#undef NV_RECORD_TEST_ENTRY

extern security_counter_status_t security_counter_diagnostic;
extern uint8_t security_counter_blob[128], security_counter_check[128], security_counter_reserved_end;
static uint8_t counter_payload[112], counter_output[112], counter_length;
static uint32_t counter_value;
static unsigned counter_checks;
static uint16_t counter_address = 0xc00;

#define CHECK(test) do { assert(test); counter_checks++; } while (0)
#define STATUS security_counter_diagnostic

static uint16_t counter_xaddress(const volatile void *p)
{
    if (p == &security_counter_reserved_end) return 0xaff;
    if (p == security_counter_blob) return 0x800;
    if (p == security_counter_check) return 0x880;
    if (p == counter_payload || p == counter_output || p == &counter_length || p == &counter_value)
        return counter_address;
    return nv_address(p);
}

static void counter_reset(void)
{
    unsigned i;
    reboot();
    memset(&STATUS, 0, sizeof(STATUS));
    memset(security_counter_blob, 0, sizeof(security_counter_blob));
    memset(security_counter_check, 0, sizeof(security_counter_check));
    for (i = 0; i < 112; i++) counter_payload[i] = (uint8_t)(i ^ 0x69);
    memset(counter_output, 0xa5, sizeof(counter_output));
    counter_value = 0xa5a5a5a5UL; counter_length = 0xa5;
    counter_address = 0xc00;
    host_mmio_xaddress_hook = counter_xaddress;
}

static void counter_record(uint8_t page, uint32_t generation, uint32_t nwk, uint32_t aps, uint8_t length)
{
    memset(input, 0, sizeof(input));
    memcpy(input, "CTR1\1", 5);
    input[5] = length;
    set_u32(input+8, nwk); set_u32(input+12, aps);
    memcpy(input+16, counter_payload, length);
    record(page, generation, (uint8_t)(16+length));
}

static void starting(uint32_t nwk, uint32_t aps, uint8_t length)
{
    counter_reset(); memset(nv, 255, sizeof(nv));
    counter_record(0, 1, nwk, aps, length);
    CHECK(security_counter_open() == SECURITY_COUNTER_OK);
    CHECK(STATUS.next[0] == nwk && STATUS.until[0] == nwk &&
          STATUS.next[1] == aps && STATUS.until[1] == aps);
}

static void take(uint8_t domain, uint32_t expected)
{
    CHECK(security_counter_take(domain, &counter_value, 3) == SECURITY_COUNTER_OK);
    CHECK(counter_value == expected && STATUS.next[domain] == expected+1 &&
          STATUS.until[domain] > expected && STATUS.state == SECURITY_COUNTER_READY);
    consume();
}

static void basic(void)
{
    unsigned i, before;
    counter_reset(); memset(nv, 255, sizeof(nv));
    CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_STATE);
    CHECK(security_counter_open() == SECURITY_COUNTER_EMPTY && commands == 0);
    CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_STATE);
    CHECK(security_counter_create(17, 9, counter_payload, 112, 3) == SECURITY_COUNTER_OK);
    CHECK(STATUS.generation == 1 && STATUS.until[0] == 17 && STATUS.until[1] == 9);
    CHECK(security_counter_status() == &STATUS);
    CHECK(security_counter_read(counter_output, 112, &counter_length) == SECURITY_COUNTER_OK);
    CHECK(counter_length == 112 && !memcmp(counter_output, counter_payload, 112));
    take(0, 17);
    CHECK(STATUS.until[0] == 273 && STATUS.generation == 2);
    before = events;
    for (i = 18; i < 273; i++) take(0, i);
    CHECK(events == before);
    take(0, 273); take(1, 9);
    CHECK(STATUS.until[0] == 529 && STATUS.until[1] == 265);
    counter_reset();
    CHECK(security_counter_open() == SECURITY_COUNTER_OK);
    take(0, 529); take(1, 265);
    CHECK(security_counter_save(NULL, 0, 3) == SECURITY_COUNTER_OK);
    CHECK(STATUS.payload_length == 0 && STATUS.next[0] == 785 && STATUS.next[1] == 521);
    take(0, 785);
    counter_reset();
    CHECK(security_counter_open() == SECURITY_COUNTER_OK && STATUS.payload_length == 0);
    CHECK(security_counter_read(NULL, 0, &counter_length) == SECURITY_COUNTER_OK && !counter_length);
    take(0, 1041); take(1, 521);
    CHECK(security_counter_create(0, 0, NULL, 0, 3) == SECURITY_COUNTER_STATE);
    CHECK(security_counter_open() == SECURITY_COUNTER_STATE);
}

static void formats(void)
{
    unsigned i, before;
    uint8_t baseline[4096];
    for (i = 0; i <= 112; i++) {
        starting(100, 1000, (uint8_t)i);
        CHECK(security_counter_read(counter_output, 112, &counter_length) == SECURITY_COUNTER_OK);
        CHECK(counter_length == i && !memcmp(counter_output, counter_payload, i));
        CHECK(i == 112 || counter_output[i] == 0xa5);
        CHECK(security_counter_save(counter_payload, (uint8_t)(112-i), 3) == SECURITY_COUNTER_OK);
        counter_reset();
        CHECK(security_counter_open() == SECURITY_COUNTER_OK && STATUS.payload_length == 112-i);
        take(0, 100); take(1, 1000);
    }
    for (i = 0; i < 8; i++) {
        counter_reset(); memset(nv, 255, sizeof(nv)); counter_record(0, 1, 7, 11, 0);
        nv[12+i] ^= 0x80;
        set_u32(nv+2040, crc_reference(nv, 2040));
        CHECK(security_counter_open() == (i == 4 ? SECURITY_COUNTER_VERSION : SECURITY_COUNTER_FORMAT));
        before = events;
        CHECK(security_counter_take(0, &counter_value, 3) != SECURITY_COUNTER_OK);
        CHECK(events == before && counter_value == 0xa5a5a5a5UL);
    }
    starting(7, 11, 0); counter_record(1, 2, 263, 11, 0);
    memcpy(baseline, nv, sizeof(nv));
    for (i = 0; i < sizeof(nv); i++) {
        counter_reset(); memcpy(nv, baseline, sizeof(nv)); nv[i] ^= 1;
        CHECK(security_counter_open() != SECURITY_COUNTER_OK);
        before = events;
        CHECK(security_counter_take(0, &counter_value, 3) != SECURITY_COUNTER_OK);
        CHECK(events == before && counter_value == 0xa5a5a5a5UL);
    }
    starting(7, 11, 0); counter_record(0, 1, 3, 11, 0);
    CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_ROLLBACK);
    CHECK(counter_value == 0xa5a5a5a5UL);
    starting(7, 11, 0); counter_record(0, 9, 7, 11, 0);
    CHECK(security_counter_save(NULL, 0, 3) == SECURITY_COUNTER_ROLLBACK);
    starting(7, 11, 0); fail_read = reads_nv+1;
    CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_NV);
    before = events;
    CHECK(security_counter_open() == SECURITY_COUNTER_NV);
    CHECK(security_counter_save(NULL, 0, 3) == SECURITY_COUNTER_NV);
    CHECK(events == before && counter_value == 0xa5a5a5a5UL);
}

static void bounds(void)
{
    unsigned i, before;
    starting(0xfffffffeUL, 0xffffff00UL, 1);
    take(0, 0xfffffffeUL);
    before = events; counter_value = 0xa5a5a5a5UL;
    CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_EXHAUSTED);
    CHECK(events == before && counter_value == 0xa5a5a5a5UL);
    for (i = 0; i < 255; i++) take(1, 0xffffff00UL+i);
    CHECK(security_counter_take(1, &counter_value, 3) == SECURITY_COUNTER_EXHAUSTED);
    counter_reset();
    CHECK(security_counter_open() == SECURITY_COUNTER_OK);
    CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_EXHAUSTED);
    CHECK(security_counter_take(1, &counter_value, 3) == SECURITY_COUNTER_EXHAUSTED);
    starting(5, 9, 1); before = events;
    CHECK(security_counter_take(2, &counter_value, 3) == SECURITY_COUNTER_ARGUMENT);
    CHECK(security_counter_take(0, NULL, 3) == SECURITY_COUNTER_ARGUMENT);
    CHECK(security_counter_take(0, &counter_value, 0) == SECURITY_COUNTER_ARGUMENT);
    CHECK(security_counter_save(NULL, 1, 3) == SECURITY_COUNTER_ARGUMENT);
    CHECK(security_counter_save(counter_payload, 113, 3) == SECURITY_COUNTER_ARGUMENT);
    CHECK(security_counter_save(counter_payload, 1, 0) == SECURITY_COUNTER_ARGUMENT);
    CHECK(security_counter_read(NULL, 1, &counter_length) == SECURITY_COUNTER_ARGUMENT);
    CHECK(security_counter_read(counter_output, 112, NULL) == SECURITY_COUNTER_ARGUMENT);
    CHECK(security_counter_read(NULL, 0, &counter_length) == SECURITY_COUNTER_SPACE);
    CHECK(counter_length == 0xa5 && events == before);
    for (i = 0; i < 65536u; i++) if (i <= 0xaff || i > 0x1dfc) {
        counter_address = (uint16_t)i;
        CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_OWNERSHIP);
    }
    CHECK(events == before && counter_value == 0xa5a5a5a5UL);
    counter_reset(); memset(nv, 255, sizeof(nv));
    counter_record(0, 0xffffffffUL, 5, 9, 0);
    CHECK(security_counter_open() == SECURITY_COUNTER_OK);
    CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_NV && !commands);
    CHECK(STATUS.nv_result == NV_RECORD_GENERATION_EXHAUSTED);
}

static void interrupted(void)
{
    unsigned boundary, kind, bit, before;
    uint8_t initial[4096];
    starting(273, 265, 112);
    counter_record(1, 2, 529, 265, 112);
    memcpy(initial, nv, sizeof(initial));
    for (boundary = 1; boundary <= 38; boundary++) for (kind = 1; kind <= 3; kind++) {
        unsigned count = kind == 3 ? (boundary == 1 ? 5 : 33) : 1;
        for (bit = 0; bit < count; bit++) {
            counter_reset(); memcpy(nv, initial, sizeof(initial));
            CHECK(security_counter_open() == SECURITY_COUNTER_OK);
            cut_command = boundary; cut_kind = kind;
            torn_bits = boundary == 1 ? bit*4096 : bit;
            if (!setjmp(power_cut)) {
                (void)security_counter_take(0, &counter_value, 3);
                CHECK(0 && "Counter reservation failed to reach requested cut");
            }
            CHECK(cut_reached && counter_value == 0xa5a5a5a5UL);
            counter_reset();
            {
                security_counter_result_t r = security_counter_open();
                CHECK(r == SECURITY_COUNTER_OK || r == SECURITY_COUNTER_RECOVERY);
                if (r == SECURITY_COUNTER_OK) {
                    CHECK(STATUS.next[0] >= 529);
                    before = STATUS.next[0];
                    take(0, before);
                } else {
                    before = events;
                    CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_RECOVERY);
                    CHECK(events == before && counter_value == 0xa5a5a5a5UL);
                }
            }
        }
    }
    starting(5, 9, 112); mode = IGNORED;
    CHECK(security_counter_take(0, &counter_value, 3) == SECURITY_COUNTER_NV);
    CHECK(counter_value == 0xa5a5a5a5UL && STATUS.state == SECURITY_COUNTER_FAILED);
    before = events;
    CHECK(security_counter_take(1, &counter_value, 3) == SECURITY_COUNTER_NV && events == before);
}

static void interrupt_state(uint8_t clear)
{
    if (!setjmp(power_cut)) {
        if (clear) (void)security_counter_save(NULL, 0, 3);
        else (void)security_counter_create(529, 265, counter_payload, 112, 3);
        CHECK(0 && "State publication failed to reach requested cut");
    }
    CHECK(cut_reached);
}

static void stop_counter(void)
{
    if (!setjmp(terminal)) {
        (void)security_counter_take(0, &counter_value, 3);
        CHECK(0 && "Counter reservation escaped active RAM fail-stop");
    }
    CHECK(STATUS.state == SECURITY_COUNTER_BUSY && STATUS.result == SECURITY_COUNTER_PENDING &&
          STATUS.nv_result == NV_RECORD_PENDING && counter_value == 0xa5a5a5a5UL &&
          nv_record_diagnostic.result == NV_RECORD_PENDING && SOC_MEMCTR == 10);
}

static void destructive_failures(void)
{
    unsigned i, boundary, kind, before;
    for (i = 0; i < 2; i++) for (boundary = 1; boundary <= (i ? 10u : 38u); boundary++) {
        for (kind = 1; kind <= 2; kind++) {
            if (i) starting(529, 265, 112);
            else {
                counter_reset(); memset(nv, 255, sizeof(nv));
                CHECK(security_counter_open() == SECURITY_COUNTER_EMPTY);
            }
            cut_command = boundary; cut_kind = kind;
            interrupt_state((uint8_t)i);
            counter_reset();
            {
                security_counter_result_t r = security_counter_open();
                if (r == SECURITY_COUNTER_OK) {
                    CHECK(STATUS.next[0] == 529 && STATUS.next[1] == 265);
                    take(0, 529); take(1, 265);
                } else {
                    CHECK(r == SECURITY_COUNTER_EMPTY || r == SECURITY_COUNTER_RECOVERY || r == SECURITY_COUNTER_NV);
                    before = events;
                    CHECK(security_counter_take(0, &counter_value, 3) != SECURITY_COUNTER_OK);
                    CHECK(counter_value == 0xa5a5a5a5UL && events == before);
                }
            }
        }
    }
    for (i = 0; i < 4; i++) {
        starting(529, 265, 112);
        stop_command = i == 0 ? 1 : i == 1 ? 2 : i == 2 ? 37 : 38;
        stop_counter();
    }
}

int main(void)
{
    basic(); formats(); bounds(); interrupted(); destructive_failures();
    printf("Security counters: %u host checks PASS; real NV/flash, synthetic cuts, no physical durability.\n",
           counter_checks);
    return 0;
}
#endif
