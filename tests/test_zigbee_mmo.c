/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Public synthetic vectors; isolated executable, NEVER flash.
 */
#include "zigbee_mmo.h"
#include <stddef.h>
#include <string.h>

static const MCU_CODE uint8_t install[18] = {
    0x83,0xfe,0xd3,0x40,0x7a,0x93,0x97,0x23,0xa5,0xc6,0x39,0xb2,0x69,0x16,0xd5,0x05,0xc3,0xb5
};
static const MCU_CODE uint8_t derived[16] = {
    0x66,0xb6,0x90,0x09,0x81,0xe1,0xee,0x3c,0xa4,0x20,0x6b,0x6b,0x86,0x1c,0x02,0xbb
};
static MCU_XDATA uint8_t input[33], output[18];
static MCU_XDATA zigbee_mmo_info_t info;
volatile MCU_XDATA uint32_t mmo_checks;
volatile MCU_XDATA uint8_t mmo_case, mmo_return;

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t mmo_result[8];
static void check(uint8_t condition, uint16_t line)
{
    mmo_checks++;
    if (!condition) {
        mmo_result[6] = (uint8_t)line; mmo_result[7] = (uint8_t)(line >> 8);
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
#define CHECK(condition) do { mmo_checks++; assert(condition); } while (0)
#endif

static uint8_t filled(const uint8_t *p, uint8_t size, uint8_t value)
{
    uint8_t i;
    for (i = 0; i < size; i++) if (p[i] != value) return 0;
    return 1;
}

static void run_case(uint8_t which)
{
    uint8_t i;
    for (i = 0; i < 33; i++) input[i] = i ^ 0x69u;
    memset(output, 0xa5, sizeof(output)); memset(&info, 0xa5, sizeof(info));
    if (which <= 32) {
        mmo_return = zigbee_mmo_hash(input, which, output+1, 1000, 128, &info);
        CHECK(mmo_return == ZIGBEE_MMO_OK);
        CHECK(info.blocks == (which+18u)/16u && info.aes_status == AES_OK);
    } else if (which <= 35) {
        memcpy(input, install, 18);
        if (which == 34) input[17] ^= 1;
        mmo_return = install_code_derive(input, which == 35 ? 8 : 18, output+1, 1000, 128, &info);
        if (which == 33) {
            CHECK(mmo_return == ZIGBEE_MMO_OK && info.blocks == 2 && info.aes_status == AES_OK);
            CHECK(!memcmp(output+1, derived, 16));
        } else {
            CHECK(mmo_return == (which == 34 ? ZIGBEE_MMO_CRC : ZIGBEE_MMO_LENGTH));
            CHECK(filled(output, 18, 0xa5) && filled((const uint8_t *)&info, sizeof(info), 0xa5));
        }
    } else if (which == 36) {
        mmo_return = zigbee_mmo_hash(input, 32, output+1, 1000, 3, &info);
        CHECK(mmo_return == ZIGBEE_MMO_AES && info.blocks == 1 &&
              info.aes_status == AES_POLL_LIMIT && info.polls == 3);
        CHECK(filled(output, 18, 0xa5));
    } else if (which == 37) {
        mmo_return = zigbee_mmo_hash(NULL, 0, output+1, 1000, 128, &info);
        CHECK(mmo_return == ZIGBEE_MMO_OK && info.blocks == 1 && info.aes_status == AES_OK);
        CHECK(info.polls > 0);
    } else {
        mmo_return = zigbee_mmo_hash(input, 65535, output+1, 1000, 128, &info);
        CHECK(mmo_return == ZIGBEE_MMO_LENGTH);
        CHECK(filled(output, 18, 0xa5) && filled((const uint8_t *)&info, sizeof(info), 0xa5));
    }
    CHECK(output[0] == 0xa5 && output[17] == 0xa5 && input[32] == (32 ^ 0x69));
}

#if defined(__SDCC)
void main(void)
{
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    mmo_result[0] = 'M'; mmo_result[1] = 'M'; mmo_result[2] = 'O'; mmo_result[3] = '1';
    mmo_result[4] = 1; mmo_result[5] = 8;
    mmo_result[6] = mmo_result[7] = 0;
    __asm
        .globl _mmo_before
    _mmo_before:
        nop
    __endasm;
    run_case(mmo_case);
    __asm
        .globl _mmo_done
    _mmo_done:
        nop
    __endasm;
    for (;;) {}
}
#else
static void reference(const uint8_t *message, unsigned length, uint8_t *hash)
{
    uint8_t padded[48] = {0}, encrypted[16];
    unsigned bits = length*8, blocks = (bits+1+16+127)/128, i, block;
    CHECK(length <= 32 && blocks <= 3);
    if (length) memcpy(padded, message, length);
    padded[length] = 0x80;
    padded[blocks*16-2] = (uint8_t)(bits >> 8); padded[blocks*16-1] = (uint8_t)bits;
    memset(hash, 0, 16);
    for (block = 0; block < blocks; block++) {
        aes_reference_encrypt(hash, padded+block*16, encrypted);
        for (i = 0; i < 16; i++) hash[i] = encrypted[i] ^ padded[block*16+i];
    }
}

static uint16_t crc_reference(const uint8_t *message)
{
    uint16_t crc = 0xffff, reflected = 0;
    unsigned i, bit;
    for (i = 0; i < 16; i++) {
        uint8_t reverse = 0;
        for (bit = 0; bit < 8; bit++) reverse |= ((message[i] >> bit) & 1u) << (7-bit);
        crc ^= (uint16_t)reverse << 8;
        for (bit = 0; bit < 8; bit++) crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0));
    }
    for (bit = 0; bit < 16; bit++) reflected |= ((crc >> bit) & 1u) << (15-bit);
    return reflected ^ 0xffffu;
}

static void native(void)
{
    uint8_t oracle[16];
    unsigned n, i, j;
    aes_reference_check();
    CHECK(crc_reference(install) == 0xb5c3);
    reference(install, 18, oracle);
    CHECK(!memcmp(oracle, derived, 16));
    for (n = 0; n <= 32; n++) for (i = 0; i < 16; i++) {
        uint8_t *message = n ? malloc(n) : NULL, *out = malloc(16);
        CHECK((!n || message) && out);
        for (j = 0; j < n; j++) message[j] = (uint8_t)(j*29u+i*17u);
        reference(message, n, oracle); security_aes_reset();
        CHECK(zigbee_mmo_hash(message, (uint16_t)n, out, 1000, 128, &info) == ZIGBEE_MMO_OK);
        CHECK(!memcmp(out, oracle, 16) && info.blocks == (n+18)/16 && security_aes_blocks() == info.blocks);
        free(out); free(message);
    }
    for (n = 0; n < 256; n++) {
        uint16_t crc;
        for (i = 0; i < 16; i++) input[i] = (uint8_t)(n*13u+i*29u);
        crc = crc_reference(input); input[16] = (uint8_t)crc; input[17] = (uint8_t)(crc >> 8);
        reference(input, 18, oracle); security_aes_reset();
        CHECK(install_code_derive(input, 18, output+1, 1000, 128, &info) == ZIGBEE_MMO_OK);
        CHECK(!memcmp(output+1, oracle, 16) && info.blocks == 2);
    }
    for (n = 0; n < 144; n++) {
        memcpy(input, install, 18); input[n/8] ^= 1u << (n%8);
        memset(output, 0xa5, 18); memset(&info, 0xa5, sizeof(info)); security_aes_reset();
        CHECK(install_code_derive(input, 18, output+1, 1000, 128, &info) == ZIGBEE_MMO_CRC);
        CHECK(!security_aes_blocks() && filled(output, 18, 0xa5) &&
              filled((const uint8_t *)&info, sizeof(info), 0xa5));
    }
    memcpy(input, install, 18);
    for (n = 0; n <= 65535u; n++) {
        memset(output, 0xa5, 18); memset(&info, 0xa5, sizeof(info)); security_aes_reset();
        if (n != 18) {
            CHECK(install_code_derive(input, (uint16_t)n, output+1, 1000, 128, &info) == ZIGBEE_MMO_LENGTH);
            CHECK(!security_aes_blocks() && filled(output, 18, 0xa5) &&
                  filled((const uint8_t *)&info, sizeof(info), 0xa5));
        }
        if (n > 32) {
            CHECK(zigbee_mmo_hash(input, (uint16_t)n, output+1, 1000, 128, &info) == ZIGBEE_MMO_LENGTH);
            CHECK(!security_aes_blocks() && filled(output, 18, 0xa5) &&
                  filled((const uint8_t *)&info, sizeof(info), 0xa5));
        }
    }
    for (n = 1; n <= 3; n++) {
        memset(output, 0xa5, 18); security_aes_reset(); security_aes_stall(n);
        CHECK(zigbee_mmo_hash(input, 32, output+1, 1000, 128, &info) == ZIGBEE_MMO_AES);
        CHECK(info.blocks == n && info.aes_status == AES_POLL_LIMIT && security_aes_blocks() == n &&
              filled(output, 18, 0xa5));
        CHECK(zigbee_mmo_hash(input, 32, output+1, 1000, 128, &info) == ZIGBEE_MMO_AES);
        CHECK(info.blocks == 1 && info.polls == 0 && security_aes_blocks() == n &&
              filled(output, 18, 0xa5));
    }
    memset(output, 0xa5, 18); memset(&info, 0xa5, sizeof(info)); security_aes_reset();
    CHECK(zigbee_mmo_hash(NULL, 1, output+1, 1000, 128, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_mmo_hash(input, 1, NULL, 1000, 128, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_mmo_hash(input, 1, output+1, 1000, 128, NULL) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_mmo_hash(input, 1, output+1, 0, 128, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_mmo_hash(input, 1, output+1, 0x800000, 128, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(zigbee_mmo_hash(input, 1, output+1, 1000, 0, &info) == ZIGBEE_MMO_ARGUMENT);
    CHECK(install_code_derive(NULL, 18, output+1, 1000, 128, &info) == ZIGBEE_MMO_ARGUMENT);
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
        if (!*argv[2] || *end || n > 38) return 2;
        security_aes_reset(); security_aes_trace(1);
        if (n == 36) security_aes_stall(1);
        run_case((uint8_t)n);
        printf("RESULT %lu %u ", (unsigned long)mmo_checks, mmo_return);
        hex(input, 33); putchar(' '); hex(output, 18);
        printf(" %lu %u %u\n", (unsigned long)info.polls, info.blocks, info.aes_status);
        return 0;
    }
    if (argc != 1) return 2;
    for (which = 0; which <= 38; which++) {
        security_aes_reset();
        if (which == 36) security_aes_stall(1);
        run_case((uint8_t)which);
    }
    native();
    printf("Zigbee MMO/install code: %lu host checks PASS; actual AES driver, no provisioning or membership.\n",
           (unsigned long)mmo_checks);
    return 0;
}
#endif
