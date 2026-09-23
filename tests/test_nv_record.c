/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nv_record.h"

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t nv_record_test_result[8];
MCU_XDATA uint8_t nv_record_test_buffer[128];
MCU_XDATA uint8_t nv_record_test_action, nv_record_test_length, nv_record_test_recovery, nv_record_test_return;
MCU_XDATA uint16_t nv_record_test_limit;
uint8_t MCU_XDATA * MCU_XDATA nv_record_test_pointer;

void nv_record_test_cycle(void)
{
    __asm
        .globl _nv_record_before
    _nv_record_before:
        nop
    __endasm;
    if (nv_record_test_action)
        nv_record_test_return = nv_record_replace(nv_record_test_pointer, nv_record_test_length,
                                                  nv_record_test_limit, nv_record_test_recovery);
    else nv_record_test_return = nv_record_load(nv_record_test_pointer, nv_record_test_length);
    __asm
        .globl _nv_record_done
    _nv_record_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    nv_record_test_result[0] = 'N'; nv_record_test_result[1] = 'V';
    nv_record_test_result[2] = 'R'; nv_record_test_result[3] = '1';
    nv_record_test_result[4] = 1; nv_record_test_result[5] = 8;
    nv_record_test_result[6] = nv_record_test_result[7] = 0;
    for (i = 0; i < 128; i++) nv_record_test_buffer[i] = i ^ 0x69;
    nv_record_test_pointer = nv_record_test_buffer;
    nv_record_test_limit = 3; nv_record_test_length = 128;
    for (;;) nv_record_test_cycle();
}
#else
#define main flash_write_reference_main
#include "test_flash_write.c"
#undef main

extern uint8_t nv_record_fault, nv_record_reserved_end;
extern uint8_t nv_record_stage[128], nv_record_chunk[32], nv_record_word[4];
extern nv_record_status_t nv_record_diagnostic;
static uint8_t input[128], output[128], saved_page[2048];
static uint16_t caller_address = 0x800;
static unsigned checks, cut_command, cut_kind, torn_bits, cut_reached, stop_command, damage_after;
static jmp_buf power_cut;

static uint16_t nv_address(const volatile void *object)
{
    if (object == &nv_record_reserved_end) return 0x700;
    if (object == nv_record_chunk) return 0x500;
    if (object == nv_record_word) return 0x524;
    if (object == input || object == output) return caller_address;
    return address(object);
}

static void nv_xstore(uint16_t a, uint8_t value)
{
    if (a == 0x6270 && stop_command == commands+1) mode = STUCK;
    if (a == 0x6270 && cut_command == commands+1) {
        unsigned offset = ((unsigned)addresses[0] | ((unsigned)addresses[1] << 8)) - 0xfa00;
        memcpy(saved_page, nv+(offset >> 9)*2048, 2048);
        if (cut_kind == 1) { cut_reached = 1; longjmp(power_cut, 1); }
    }
    xstore(a, value);
}

static uint8_t nv_xload(uint16_t a)
{
    unsigned was_active = active;
    uint8_t operation = controller & 3, value = xload(a);
    if (a >= 0xe800 && a < 0xf800 && reads_nv == damage_after) nv[12] ^= 1;
    if (a == 0x6270 && was_active && !active && commands == cut_command) {
        if (cut_kind == 3) {
            unsigned offset = ((unsigned)addresses[0] | ((unsigned)addresses[1] << 8)) - 0xfa00;
            unsigned page = offset >> 9, position = (offset & 511)*4, bit;
            if (operation == FLASH_EXEC_ERASE) {
                for (bit = torn_bits; bit < 2048u*8; bit++)
                    nv[page*2048+(bit >> 3)] =
                        (uint8_t)((nv[page*2048+(bit >> 3)] & ~(1u << (bit & 7))) |
                                  (saved_page[bit >> 3] & (1u << (bit & 7))));
            } else {
                for (bit = torn_bits; bit < 32; bit++)
                    nv[page*2048+position+(bit >> 3)] =
                        (uint8_t)((nv[page*2048+position+(bit >> 3)] & ~(1u << (bit & 7))) |
                                  (saved_page[position+(bit >> 3)] & (1u << (bit & 7))));
            }
        }
        cut_reached = 1; longjmp(power_cut, 1);
    }
    return value;
}

static void reboot(void)
{
    unsigned i;
    reset(); nv_record_fault = 0;
    memset(&nv_record_diagnostic, 0, sizeof(nv_record_diagnostic));
    caller_address = 0x800;
    cut_command = cut_kind = torn_bits = cut_reached = stop_command = damage_after = 0;
    for (i = 0; i < 128; i++) input[i] = (uint8_t)(i ^ 0x69);
    memset(output, 0xa5, sizeof(output));
    host_mmio_xaddress_hook = nv_address;
    host_mmio_xread_hook = nv_xload; host_mmio_xwrite_hook = nv_xstore;
}

static void check_load(nv_record_result_t expected, uint8_t length)
{
    unsigned i;
    memset(output, 0xa5, sizeof(output));
    assert(nv_record_load(output, 128) == expected); checks++;
    consume();
    if (expected == NV_RECORD_OK || expected == NV_RECORD_RECOVERED) {
        assert(nv_record_status()->length == length && nv_record_status()->phase == 8);
        assert(!memcmp(output, input, length));
        for (i = length; i < 128; i++) assert(output[i] == 0xa5);
    } else for (i = 0; i < 128; i++) assert(output[i] == 0xa5);
}

static void check_replace(nv_record_result_t expected, uint8_t length, uint8_t recovery)
{
    uint8_t saved[128];
    memcpy(saved, input, 128);
    assert(nv_record_replace(input, length, 3, recovery) == expected); checks++;
    consume(); assert(!memcmp(saved, input, 128));
    assert(nv_record_status() == &nv_record_diagnostic);
}

/* Independent byte-at-a-time division, deliberately not the production
 * conditional reflected recurrence. This is a test oracle, not firmware.
 */
static uint32_t crc_reference(const uint8_t *bytes, unsigned length)
{
    uint32_t remainder = 0xffffffffUL;
    unsigned i, bit;
    for (i = 0; i < length; i++) {
        uint8_t reverse = 0;
        for (bit = 0; bit < 8; bit++) reverse |= ((bytes[i] >> bit) & 1u) << (7-bit);
        remainder ^= (uint32_t)reverse << 24;
        for (bit = 0; bit < 8; bit++)
            remainder = (remainder << 1) ^ ((remainder & 0x80000000UL) ? 0x04c11db7UL : 0);
    }
    {
        uint32_t reflected = 0;
        for (bit = 0; bit < 32; bit++) reflected |= ((remainder >> bit) & 1UL) << (31-bit);
        return reflected ^ 0xffffffffUL;
    }
}

static void set_u32(uint8_t *p, uint32_t n)
{
    unsigned i;
    for (i = 0; i < 4; i++) { p[i] = (uint8_t)n; n >>= 8; }
}

static void record(uint8_t page, uint32_t generation, uint8_t length)
{
    uint8_t *p = nv+page*2048;
    memset(p, 255, 2048); memcpy(p, "NVR1\1\0", 6);
    p[6] = length; p[7] = 0; set_u32(p+8, generation);
    memcpy(p+12, input, length); set_u32(p+2040, crc_reference(p, 2040));
    memcpy(p+2044, "CMT1", 4);
}

static void interrupt_replace(void)
{
    if (!setjmp(power_cut)) {
        (void)nv_record_replace(input, 128, 3, 0);
        assert(0 && "Power cut did not interrupt the requested command");
    }
    assert(cut_reached);
}

static void stopped_replace(void)
{
    if (!setjmp(terminal)) {
        (void)nv_record_replace(input, 128, 3, 0);
        assert(0 && "RAM fail-stop returned to the journal");
    }
    assert(nv_record_diagnostic.result == NV_RECORD_PENDING &&
           nv_record_diagnostic.writer == FLASH_WRITE_PENDING &&
           flash_write_status.result == FLASH_WRITE_PENDING && SOC_MEMCTR == 10);
    assert(nv_record_diagnostic.phase == (stop_command == 1 ? 3 : stop_command == 37 ? 5 :
                                        stop_command == 38 ? 6 : 4));
    checks++;
}

static void cuts(void)
{
    unsigned boundary, kind, bit, before, total = 38;
    for (boundary = 1; boundary <= total; boundary++) {
        for (kind = 1; kind <= 3; kind++) {
            unsigned count = kind == 3 ? (boundary == 1 ? 5 : 33) : 1;
            for (bit = 0; bit < count; bit++) {
                reboot(); memset(nv, 255, sizeof(nv));
                record(0, 3, 17); record(1, 2, 19);
                cut_command = boundary; cut_kind = kind;
                torn_bits = boundary == 1 ? bit*4096 : bit;
                interrupt_replace();
                assert(!memcmp(nv, "NVR1", 4));
                before = nv[2048+2044] == 'C' && nv[2048+2045] == 'M' &&
                         nv[2048+2046] == 'T' && nv[2048+2047] == '1' &&
                         nv[2048+8] == 4;
                reboot();
                {
                    nv_record_result_t result = nv_record_load(output, 128);
                    assert(result == NV_RECORD_OK || result == NV_RECORD_RECOVERED);
                    assert(nv_record_diagnostic.generation == (before ? 4u : 3u));
                    assert(nv_record_diagnostic.length == (before ? 128 : 17));
                    assert(!memcmp(output, input, nv_record_diagnostic.length));
                    assert(flash_write_known == 0 && commands == 0);
                }
                checks++;
            }
        }
    }
}

#ifndef NV_RECORD_TEST_ENTRY
#define NV_RECORD_TEST_ENTRY main
#endif
int NV_RECORD_TEST_ENTRY(void)
{
    unsigned i, page, before;
    uint8_t baseline[4096], saved[128];
    assert(crc_reference((const uint8_t *)"123456789", 9) == 0xcbf43926UL);
    reboot(); memset(nv, 255, sizeof(nv)); check_load(NV_RECORD_EMPTY, 0);
    check_replace(NV_RECORD_OK, 1, 0); check_load(NV_RECORD_OK, 1);
    assert(erases[0] && nv_record_diagnostic.erase_attempts[0] == 1);
    record(1, 2, 128); check_load(NV_RECORD_OK, 128);
    reboot(); assert(!flash_write_known);
    check_replace(NV_RECORD_OK, 125, 0); check_load(NV_RECORD_OK, 125);
    assert(nv_record_diagnostic.generation == 3 && nv_record_diagnostic.selected == 0 &&
           flash_write_known == 1);
    for (i = 1; i <= 128; i++) {
        reboot(); memset(nv, 255, sizeof(nv));
        check_replace(NV_RECORD_OK, (uint8_t)i, 0);
        memcpy(baseline, nv, sizeof(nv));
        record(0, 1, (uint8_t)i);
        assert(!memcmp(baseline, nv, 2048));
        check_load(NV_RECORD_OK, (uint8_t)i);
    }
    reboot(); record(0, 7, 128); record(1, 6, 19);
    memcpy(baseline, nv, sizeof(nv));
    for (page = 0; page < 2; page++) for (i = 0; i < 2048; i++) {
        reboot(); memcpy(nv, baseline, sizeof(nv)); nv[page*2048+i] ^= 1;
        {
            nv_record_result_t result = nv_record_load(output, 128);
            assert(result == (i == 4 ? NV_RECORD_UNSUPPORTED : NV_RECORD_RECOVERED));
            if (result == NV_RECORD_RECOVERED) {
                assert(nv_record_diagnostic.selected == (page ^ 1u));
                assert(!memcmp(output, input, nv_record_diagnostic.length));
            } else assert(output[0] == 0xa5);
            checks++;
        }
        before = commands;
        check_replace(i == 4 ? NV_RECORD_UNSUPPORTED : NV_RECORD_RECOVERY_REQUIRED, 5, 0);
        assert(commands == before);
    }
    reboot(); memcpy(nv, baseline, sizeof(nv)); nv[12] ^= 1;
    check_replace(NV_RECORD_RECOVERED, 5, 1);
    assert(nv_record_diagnostic.generation == 7); check_load(NV_RECORD_OK, 5);
    reboot(); memset(nv, 0, sizeof(nv)); check_load(NV_RECORD_CORRUPT, 0);
    check_replace(NV_RECORD_CORRUPT, 5, 1); assert(!commands);
    for (i = 0; i < 2; i++) {
        reboot(); record(0, 7, 5); record(1, i ? 3 : 7, 5);
        check_load(NV_RECORD_CONFLICT, 0); check_replace(NV_RECORD_CONFLICT, 5, 1);
        assert(!commands);
    }
    reboot(); record(0, 0xffffffffUL, 5); record(1, 0xfffffffeUL, 5);
    check_load(NV_RECORD_OK, 5); check_replace(NV_RECORD_GENERATION_EXHAUSTED, 5, 0);
    assert(!commands);
    reboot(); memset(nv, 255, sizeof(nv));
    for (i = 0; i < 2*NV_RECORD_ERASE_LIMIT; i++) check_replace(NV_RECORD_OK, 5, 0);
    before = commands; check_replace(NV_RECORD_ERASE_LIMIT_REACHED, 5, 0);
    assert(commands == before && nv_record_diagnostic.erase_attempts[0] == NV_RECORD_ERASE_LIMIT &&
           nv_record_diagnostic.erase_attempts[1] == NV_RECORD_ERASE_LIMIT);
    reboot(); record(0, 1, 128); memset(nv+2048, 255, 2048);
    assert(nv_record_load(output, 127) == NV_RECORD_NO_SPACE && output[0] == 0xa5);
    before = events; memcpy(saved, output, sizeof(saved));
    assert(nv_record_load(NULL, 128) == NV_RECORD_INVALID_ARGUMENT);
    assert(nv_record_replace(NULL, 1, 3, 0) == NV_RECORD_INVALID_ARGUMENT);
    assert(nv_record_replace(input, 0, 3, 0) == NV_RECORD_INVALID_ARGUMENT);
    assert(nv_record_replace(input, 129, 3, 0) == NV_RECORD_INVALID_ARGUMENT);
    assert(nv_record_replace(input, 1, 0, 0) == NV_RECORD_INVALID_ARGUMENT);
    assert(nv_record_replace(input, 1, 3, 2) == NV_RECORD_INVALID_ARGUMENT);
    for (i = 0; i < 65536u; i++) if (i <= 0x700 || i > 0x1d80) {
        caller_address = (uint16_t)i;
        assert(nv_record_load(output, 128) == NV_RECORD_BUFFER_OWNERSHIP); checks++;
    }
    assert(events == before && !memcmp(saved, output, sizeof(saved)));
    reboot(); memset(nv, 255, sizeof(nv)); fail_read = 65;
    check_load(NV_RECORD_READ_FAILED, 0); before = events;
    check_replace(NV_RECORD_READ_FAILED, 5, 0); check_load(NV_RECORD_READ_FAILED, 0);
    assert(events == before);
    reboot(); memset(nv, 255, sizeof(nv)); mode = IGNORED;
    check_replace(NV_RECORD_WRITE_FAILED, 5, 0); before = events;
    check_load(NV_RECORD_WRITE_FAILED, 0); check_replace(NV_RECORD_WRITE_FAILED, 5, 0);
    assert(events == before && nv_record_diagnostic.erase_attempts[0] == 1);
    for (i = 0; i < 3; i++) {
        reboot(); record(0, 1, 128); memset(nv+2048, 255, 2048);
        fail_read = 4096+(i == 0 ? 1 : i == 1 ? 64 : 2048);
        check_load(NV_RECORD_READ_FAILED, 0);
    }
    reboot(); record(0, 1, 128); memset(nv+2048, 255, 2048); damage_after = 4096;
    check_load(NV_RECORD_CORRUPT, 0);
    reboot(); memset(nv, 255, sizeof(nv)); damage_after = 4096+2049+7*8;
    check_replace(NV_RECORD_CORRUPT, 5, 0); before = events;
    check_load(NV_RECORD_CORRUPT, 0); assert(events == before && nv_record_fault == NV_RECORD_CORRUPT);
    for (i = 0; i < 4; i++) {
        reboot(); record(0, 1, 128); memset(nv+2048, 255, 2048);
        stop_command = i == 0 ? 1 : i == 1 ? 2 : i == 2 ? 37 : 38;
        stopped_replace();
    }
    cuts();
    printf("NV record host: %u checks; real flash composition, all lengths/page bytes, command cuts/torn bits, "
           "recovery, wrap rejection, runtime erase budget and retained faults PASS (synthetic only)\n", checks);
    return 0;
}
#endif
