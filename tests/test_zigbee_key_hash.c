/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Public synthetic vectors; isolated executable, NEVER flash.
 */
#include "zigbee_key_hash.h"
#include <stddef.h>
#include <string.h>

static const MCU_CODE uint8_t keys[3][16] = {
    {0},
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    {0x66,0xb6,0x90,0x09,0x81,0xe1,0xee,0x3c,0xa4,0x20,0x6b,0x6b,0x86,0x1c,0x02,0xbb}
};
static const MCU_CODE uint8_t purposes[3] = {0,2,3};
static const MCU_CODE uint8_t answers[9][16] = {
    {0x29,0x82,0xb9,0x74,0x19,0xde,0x76,0x57,0x1b,0x97,0x57,0x47,0x99,0x59,0x82,0x3d},
    {0x7a,0x1b,0x49,0xc2,0xb7,0x76,0xee,0xb5,0xce,0x82,0xa3,0xe7,0x79,0x32,0xed,0xbd},
    {0x9f,0xe6,0x39,0xb0,0xa9,0x93,0xaf,0xb1,0x6d,0x31,0x23,0x6e,0xdc,0x4c,0xfc,0xef},
    {0xd2,0x28,0x9c,0x6f,0xeb,0xfe,0xdc,0xb8,0x91,0xda,0x27,0xdc,0xd0,0xb6,0x88,0x5d},
    {0xec,0xe7,0xed,0x53,0xe6,0x6e,0xf7,0x69,0x76,0x2b,0xd6,0x27,0x79,0xdc,0x36,0x6a},
    {0x7a,0x2f,0x50,0xcc,0x4c,0xa0,0x9d,0x35,0x02,0x0a,0x86,0x39,0x32,0x37,0x50,0x7f},
    {0x3c,0x6c,0xca,0x89,0x77,0xeb,0x18,0x9e,0xfd,0x16,0x14,0xc3,0xe7,0xf7,0x59,0x89},
    {0x11,0x77,0xa0,0xee,0x26,0x00,0xd0,0x02,0x0d,0xba,0x82,0xd7,0x04,0x9f,0x53,0xbc},
    {0x62,0x16,0x1e,0x9b,0xe4,0xc0,0x97,0x28,0x95,0x86,0x0a,0xd5,0x68,0xfa,0x8f,0xdd}
};
static MCU_XDATA uint8_t key[16], output[18];
static MCU_XDATA zigbee_mmo_info_t info;
volatile MCU_XDATA uint32_t kh_checks;
volatile MCU_XDATA uint8_t kh_case, kh_return;

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t kh_result[8];
static void check(uint8_t condition, uint16_t line)
{
    kh_checks++;
    if (!condition) {
        kh_result[6] = (uint8_t)line; kh_result[7] = (uint8_t)(line >> 8);
        for (;;) {}
    }
}
#define CHECK(condition) check(!!(condition), __LINE__)
#else
#include "aes_reference.h"
#include "security_aes_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(condition) do { kh_checks++; assert(condition); } while (0)
#endif

static uint8_t filled(const uint8_t *p, uint8_t size, uint8_t value)
{
    uint8_t i;
    for (i = 0; i < size; i++) if (p[i] != value) return 0;
    return 1;
}

static void run_case(uint8_t which)
{
    uint8_t selected = which < 9 ? which/3 : 2;
    memcpy(key, keys[selected], 16);
    memset(output, 0xa5, sizeof(output)); memset(&info, 0xa5, sizeof(info));
    kh_return = zigbee_key_hash(which == 13 ? NULL : key,
        which == 9 ? 1 : which < 9 ? purposes[which%3] : ZIGBEE_HASH_VERIFY,
        output+1, which == 14 ? 0 : 1000, which == 10 ? 3 : 128, &info);
    if (which < 9) {
        CHECK(kh_return == ZIGBEE_MMO_OK && info.blocks == 5 && info.aes_status == AES_OK);
        CHECK(!memcmp(output+1, answers[which], 16));
    } else if (which >= 10 && which <= 12) {
        CHECK(kh_return == ZIGBEE_MMO_AES && info.blocks == (which == 10 ? 1 : which == 11 ? 3 : 5) &&
              info.aes_status == AES_POLL_LIMIT);
        CHECK(filled(output, 18, 0xa5));
    } else {
        CHECK(kh_return == ZIGBEE_MMO_ARGUMENT);
        CHECK(filled(output, 18, 0xa5) && filled((const uint8_t *)&info, sizeof(info), 0xa5));
    }
    CHECK(output[0] == 0xa5 && output[17] == 0xa5 && !memcmp(key, keys[selected], 16));
}

#if defined(__SDCC)
void main(void)
{
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    kh_result[0] = 'K'; kh_result[1] = 'H'; kh_result[2] = 'S'; kh_result[3] = '1';
    kh_result[4] = 1; kh_result[5] = 8;
    kh_result[6] = kh_result[7] = 0;
    __asm
        .globl _kh_before
    _kh_before:
        nop
    __endasm;
    run_case(kh_case);
    __asm
        .globl _kh_done
    _kh_done:
        nop
    __endasm;
    for (;;) {}
}
#else
static void reference(const uint8_t *k, uint8_t purpose, uint8_t result[16])
{
    uint8_t inner[17], outer[32];
    unsigned i;
    for (i = 0; i < 16; i++) { inner[i] = k[i] ^ 0x36u; outer[i] = k[i] ^ 0x5cu; }
    inner[16] = purpose;
    aes_mmo_reference(inner, sizeof(inner), outer+16);
    aes_mmo_reference(outer, sizeof(outer), result);
}

static void native(void)
{
    uint8_t oracle[16], first[16];
    unsigned n, i, j;
    aes_reference_check();
    for (n = 0; n < 9; n++) {
        reference(keys[n/3], purposes[n%3], oracle);
        CHECK(!memcmp(oracle, answers[n], 16));
    }
    for (n = 0; n < 256; n++) {
        uint8_t *k = malloc(16), *out = malloc(16);
        CHECK(k && out);
        for (i = 0; i < 16; i++) k[i] = (uint8_t)(n*13u+i*29u);
        for (j = 0; j < 3; j++) {
            reference(k, purposes[j], oracle); security_aes_reset();
            CHECK(zigbee_key_hash(k, purposes[j], out, 1000, 128, &info) == ZIGBEE_MMO_OK);
            CHECK(info.blocks == 5 && security_aes_blocks() == 5 && !memcmp(out, oracle, 16));
            if (!j) memcpy(first, out, 16); else CHECK(memcmp(first, out, 16));
        }
        free(out); free(k);
    }
    for (n = 0; n <= 255; n++) if (n != 0 && n != 2 && n != 3) {
        security_aes_reset(); memset(output, 0xa5, 18); memset(&info, 0xa5, sizeof(info));
        CHECK(zigbee_key_hash(key, (uint8_t)n, output+1, 1000, 128, &info) == ZIGBEE_MMO_ARGUMENT);
        CHECK(!security_aes_blocks() && filled(output, 18, 0xa5) &&
              filled((const uint8_t *)&info, sizeof(info), 0xa5));
    }
    for (n = 1; n <= 5; n++) {
        security_aes_reset(); security_aes_stall(n); memset(output, 0xa5, 18);
        CHECK(zigbee_key_hash(key, 3, output+1, 1000, 128, &info) == ZIGBEE_MMO_AES);
        CHECK(info.blocks == n && info.aes_status == AES_POLL_LIMIT &&
              security_aes_blocks() == n && filled(output, 18, 0xa5));
        CHECK(zigbee_key_hash(key, 0, output+1, 1000, 128, &info) == ZIGBEE_MMO_AES);
        CHECK(info.blocks == 1 && info.polls == 0 && security_aes_blocks() == n && filled(output, 18, 0xa5));
    }
    security_aes_reset(); memset(output, 0xa5, 18); memset(&info, 0xa5, sizeof(info));
    CHECK(zigbee_key_hash(NULL, 0, output+1, 1000, 128, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_key_hash(key, 0, NULL, 1000, 128, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_key_hash(key, 0, output+1, 1000, 128, NULL) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_key_hash(key, 0, output+1, 0, 128, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_key_hash(key, 0, output+1, 0x800000, 128, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_key_hash(key, 0, output+1, 1000, 0, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(!security_aes_blocks() && filled(output, 18, 0xa5) && filled((const uint8_t *)&info, sizeof(info), 0xa5));
}

static void hex(const uint8_t *bytes, unsigned size)
{
    unsigned i;
    for (i = 0; i < size; i++) printf("%02x", bytes[i]);
}

int main(int argc, char **argv)
{
    unsigned which;
    if (argc == 3 && !strcmp(argv[1], "--trace")) {
        char *end;
        unsigned long n = strtoul(argv[2], &end, 10);
        if (!*argv[2] || *end || n > 14) return 2;
        security_aes_reset(); security_aes_trace(1);
        if (n == 11 || n == 12) security_aes_stall(n == 11 ? 3 : 5);
        run_case((uint8_t)n);
        printf("RESULT %lu %u ", (unsigned long)kh_checks, kh_return);
        hex(key, 16); putchar(' '); hex(output, 18);
        printf(" %lu %u %u\n", (unsigned long)info.polls, info.blocks, info.aes_status);
        return 0;
    }
    if (argc != 1) return 2;
    for (which = 0; which <= 14; which++) {
        security_aes_reset();
        if (which == 11 || which == 12) security_aes_stall(which == 11 ? 3 : 5);
        run_case((uint8_t)which);
    }
    native();
    printf("Zigbee keyed hash: %lu host checks PASS; genuine AES/MMO, not TC verification.\n",
           (unsigned long)kh_checks);
    return 0;
}
#endif
