/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Original combined controller, based on this project's flash test model.
 */
#include "security_joint_model.h"
#include "security_aes_model.h"
#include "security_counter.h"
#include "flash_write.h"
#include "flash_exec.h"
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(__SDCC)
#error Host peripheral model must not enter target firmware.
#endif

extern uint8_t flash_exec_work[9], flash_exec_reserved_end, flash_fault, flash_reserved_end;
extern volatile uint8_t flash_exec_ram[FLASH_EXEC_RAM_SIZE];
extern flash_write_diagnostic_t flash_write_status;
extern uint8_t flash_write_known, flash_write_used[128], flash_write_word[4], flash_write_check[32];
extern uint8_t flash_write_reserved_end;
extern uint8_t nv_record_fault, nv_record_reserved_end, nv_record_chunk[32], nv_record_word[4];
extern nv_record_status_t nv_record_diagnostic;
extern security_counter_status_t security_counter_diagnostic;
extern uint8_t security_counter_blob[128], security_counter_check[128], security_counter_reserved_end;
extern void security_keys_host_power_cycle(void);
extern volatile uint8_t aes_dma0[8], aes_dma1[32], aes_key[16], aes_iv[16], aes_input[16], aes_output[16];
extern uint8_t aes_reserved_end, _gptrput_PARM_2;
static uint8_t nv[4096], saved[2048], addresses[2], staged[4], controller, active, accepting;
static unsigned commands, data_writes, polls, reads_nv, fail_read, erases[2], words[1024];
static unsigned cut_command, cut_bits, stop_command;
static uint8_t cut_kind;
static uint8_t trace_enabled;
static jmp_buf *cut_env;
static host_mmio_read_hook_t aes_read;
static host_mmio_write_hook_t aes_write;
static host_mmio_cycles_hook_t aes_cycles;
static const volatile void *objects[96];
static unsigned object_count;

static void logs(void)
{
    read_count = write_count = xread_count = xwrite_count = 0;
}
static uint16_t address(const volatile void *p)
{
    unsigned i;
    if (p == &aes_reserved_end) return 0x1ff;
    if (p == &_gptrput_PARM_2) return 0x1d00;
    if (p == aes_dma0) return 0x20;
    if (p == aes_dma1) return 0x28;
    if (p == aes_key) return 0x48;
    if (p == aes_iv) return 0x58;
    if (p == aes_input) return 0x68;
    if (p == aes_output) return 0x78;
    if (p == flash_exec_ram) return 0x200;
    if (p == &flash_exec_reserved_end) return 0x2ff;
    if (p == &flash_reserved_end) return 0x3ff;
    if (p == flash_write_word) return 0x420;
    if (p == flash_write_check) return 0x424;
    if (p == &flash_write_reserved_end) return 0x500;
    if (p == nv_record_chunk) return 0x600;
    if (p == nv_record_word) return 0x624;
    if (p == &nv_record_reserved_end) return 0x700;
    if (p == security_counter_blob) return 0x800;
    if (p == security_counter_check) return 0x880;
    if (p == &security_counter_reserved_end) return 0xc00;
    /* Borrowed generic objects are resolved by C pointers by the real driver;
     * no controller DMA descriptor uses this synthetic admission-only address.
     * It establishes lower ownership checks, not target allocation/alias proof. */
    for (i = 0; i < object_count; i++)
        if (objects[i] == p) return (uint16_t)(0xc40u+32u*i);
    assert(object_count < 96);
    objects[object_count++] = p;
    return (uint16_t)(0xc40u+32u*i);
}
static void cut(void)
{
    assert(cut_env);
    longjmp(*cut_env, 1);
}
static void complete(void)
{
    unsigned a = ((unsigned)addresses[0] | ((unsigned)addresses[1] << 8)) - 0xfa00;
    unsigned page = a >> 9, i, position = (a & 511u)*4u;
    uint8_t erase = (controller & 3) == FLASH_EXEC_ERASE;
    assert(a < 1024 && page < 2);
    if (erase) {
        assert(!(a & 511));
        memset(nv+page*2048, 255, 2048);
        memset(words+page*512, 0, sizeof(words[0])*512);
        erases[page]++;
    } else {
        assert(data_writes == 4 && !words[a]);
        words[a]++;
        for (i = 0; i < 4; i++) nv[a*4+i] &= staged[i];
    }
    if (trace_enabled)
        printf("FLASH %u %u %u %02x%02x%02x%02x\n", erase ? 1u : 2u, page, position,
               erase ? 0 : staged[0], erase ? 0 : staged[1],
               erase ? 0 : staged[2], erase ? 0 : staged[3]);
    controller &= 12; active = 0;
    if (cut_env && commands == cut_command) {
        if (cut_kind == 3) {
            unsigned bits = erase ? 16384 : 32;
            if (erase) position = 0;
            for (i = cut_bits; i < bits; i++) {
                unsigned offset = position+(i >> 3);
                uint8_t mask = (uint8_t)(1u << (i & 7));
                nv[page*2048+offset] = (nv[page*2048+offset] & (uint8_t)~mask) |
                    (saved[offset] & mask);
            }
        }
        cut();
    }
}
static uint8_t xload(uint16_t a)
{
    logs();
    if (a == 0x6270) {
        if (accepting) accepting = 0;
        else if (active && ++polls == 2 && commands != stop_command) complete();
        return controller;
    }
    if (a == 0x624a) return 0xa5;
    if (a == 0x6276) return 0x44;
    if (a == 0x6277) return 0xff;
    if (a == 0x6271 || a == 0x6272) return addresses[a-0x6271];
    assert(a >= 0xe800 && a < 0xf800 && SOC_MEMCTR == 7 && !(controller & 0xf3));
    if (++reads_nv == fail_read) controller ^= 4;
    return nv[a-0xe800];
}
static void xstore(uint16_t a, uint8_t value)
{
    logs();
    if (a == 0x6271 || a == 0x6272) addresses[a-0x6271] = value;
    else if (a == 0x6270) {
        unsigned offset = ((unsigned)addresses[0] | ((unsigned)addresses[1] << 8)) - 0xfa00;
        assert(!(controller & 0xf3) && (SOC_MEMCTR & 8) && value == flash_exec_work[0]);
        assert(offset < 1024 && flash_write_status.result == FLASH_WRITE_PENDING);
        assert(!SOC_DMAARM && !SOC_DMAREQ);
        if (cut_env && cut_command == commands+1) {
            memcpy(saved, nv+(offset >> 9)*2048, 2048);
            if (cut_kind == 1) cut();
        }
        commands++; data_writes = polls = 0;
        controller = value | 0x80; active = accepting = 1;
    } else {
        assert(a == 0x6273 && (controller & 0x83) == 0x82 && data_writes < 4);
        staged[data_writes++] = value;
    }
}
static uint8_t load(uint8_t reg, uint8_t value)
{
    logs();
    /* AES completion runs only for an actually armed AES transaction. */
    return aes_read(reg, value);
}
static void store(uint8_t reg, uint8_t before, uint8_t value)
{
    if (reg == 0xc7) {
        logs();
        assert(value == 7 || value == 2 || value == 10);
    } else {
        assert(!active && SOC_MEMCTR == 2);
        aes_write(reg, before, value);
    }
}
static void cycles(uint8_t n) { aes_cycles(n); }
void host_flash_engine_stop(void)
{
    assert(flash_exec_work[7] == FLASH_EXEC_RAM_STOP && cut_env);
    longjmp(*cut_env, 2);
}
void security_joint_reset(uint8_t erase)
{
    /* The ONLY AES reset in the combined model: real modeled cold boot. */
    security_aes_reset();
    security_aes_trace(0);
    trace_enabled = 0;
    aes_read = host_mmio_read_hook; aes_write = host_mmio_write_hook;
    aes_cycles = host_mmio_cycles_hook;
    memset(flash_exec_work, 0, sizeof(flash_exec_work)); flash_fault = 0;
    memset(&flash_write_status, 0, sizeof(flash_write_status)); flash_write_known = 0;
    memset(flash_write_used, 0, sizeof(flash_write_used));
    nv_record_fault = 0; memset(&nv_record_diagnostic, 0, sizeof(nv_record_diagnostic));
    memset(&security_counter_diagnostic, 0, sizeof(security_counter_diagnostic));
    memset(security_counter_blob, 0, 128); memset(security_counter_check, 0, 128);
    security_keys_host_power_cycle();
    SOC_MEMCTR = 2; controller = 4; active = accepting = 0;
    commands = data_writes = polls = reads_nv = fail_read = stop_command = 0;
    object_count = 0;
    cut_env = NULL; cut_command = cut_bits = cut_kind = 0;
    memset(erases, 0, sizeof(erases)); memset(words, 0, sizeof(words));
    if (erase) memset(nv, 255, sizeof(nv));
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xread_hook = xload; host_mmio_xwrite_hook = xstore;
    host_mmio_xaddress_hook = address; host_mmio_cycles_hook = cycles;
}
uint8_t *security_joint_nv(void) { return nv; }
unsigned security_joint_flash_commands(void) { return commands; }
unsigned security_joint_flash_erases(uint8_t page) { assert(page < 2); return erases[page]; }
unsigned security_joint_aes_blocks(void) { return security_aes_blocks(); }
void security_joint_trace(uint8_t enabled)
{
    trace_enabled = enabled;
    security_aes_trace(enabled);
}
void security_joint_cut(jmp_buf *env, unsigned command, uint8_t kind, unsigned bits)
{
    assert(!env || (kind >= 1 && kind <= 3));
    cut_env = env; cut_command = command; cut_kind = kind; cut_bits = bits;
}
void security_joint_stall_flash(unsigned command) { stop_command = command; }
void security_joint_stall_aes(unsigned block) { security_aes_stall(block); }
void security_joint_fail_read(unsigned relative_read) { fail_read = reads_nv+relative_read; }
