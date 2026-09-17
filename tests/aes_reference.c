/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Original host-only FIPS 197 (2001) section 5.1 mathematical oracle.
 */
#include "aes_reference.h"
#include "aes_vectors.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t sbox[256], initialized;

static uint8_t multiply(uint8_t a, uint8_t b)
{
    uint8_t result = 0;
    while (b) {
        if (b & 1) result ^= a;
        a = (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1b : 0));
        b >>= 1;
    }
    return result;
}

static void substitute_table(void)
{
    unsigned x, n;
    for (x = 0; x < 256; x++) {
        uint8_t base = (uint8_t)x, inverse = 1, power = 254, value;
        while (power) {
            if (power & 1) inverse = multiply(inverse, base);
            base = multiply(base, base); power >>= 1;
        }
        value = inverse ^ 0x63;
        for (n = 1; n <= 4; n++)
            value ^= (uint8_t)((inverse << n) | (inverse >> (8 - n)));
        sbox[x] = value;
    }
    initialized = 1;
}

void aes_reference_encrypt(const uint8_t key[16], const uint8_t input[16], uint8_t output[16])
{
    uint8_t state[16], next[16], round_key[16], temp[4], rcon = 1;
    unsigned i, round, c;
    if (!initialized) substitute_table();
    memcpy(round_key, key, 16);
    for (i = 0; i < 16; i++) state[i] = input[i] ^ key[i];
    for (round = 1; round <= 10; round++) {
        for (i = 0; i < 16; i++) next[i] = sbox[state[4 * ((i / 4 + i % 4) % 4) + i % 4]];
        if (round < 10)
            for (c = 0; c < 16; c += 4) {
                uint8_t a = next[c], b = next[c+1], d = next[c+2], e = next[c+3];
                next[c]   = multiply(a, 2) ^ multiply(b, 3) ^ d ^ e;
                next[c+1] = a ^ multiply(b, 2) ^ multiply(d, 3) ^ e;
                next[c+2] = a ^ b ^ multiply(d, 2) ^ multiply(e, 3);
                next[c+3] = multiply(a, 3) ^ b ^ d ^ multiply(e, 2);
            }
        for (i = 0; i < 4; i++) temp[i] = sbox[round_key[12 + ((i + 1) % 4)]];
        temp[0] ^= rcon; rcon = multiply(rcon, 2);
        for (i = 0; i < 16; i++) round_key[i] ^= i < 4 ? temp[i] : round_key[i-4];
        for (i = 0; i < 16; i++) state[i] = next[i] ^ round_key[i];
    }
    memcpy(output, state, 16);
}

void aes_reference_check(void)
{
    uint8_t out[16];
    unsigned i;
    for (i = 0; i < 5; i++) {
        aes_reference_encrypt(aes_test_vectors[i][0], aes_test_vectors[i][1], out);
        assert(!memcmp(out, aes_test_vectors[i][2], 16));
    }
    assert(sbox[0] == 0x63 && sbox[0x53] == 0xed);
}

#if defined(AES_REFERENCE_MAIN)
static int parse(const char *text, uint8_t out[16])
{
    unsigned i, value;
    if (strlen(text) != 32 || strspn(text, "0123456789abcdefABCDEF") != 32) return 0;
    for (i = 0; i < 16; i++) {
        if (sscanf(text + 2*i, "%2x", &value) != 1) return 0;
        out[i] = (uint8_t)value;
    }
    return 1;
}

int main(int argc, char **argv)
{
    uint8_t key[16], input[16], output[16];
    unsigned i;
    aes_reference_check();
    if (argc == 1) { puts("AES host-only independent reference: five primary KATs PASS"); return 0; }
    if (argc != 3 || !parse(argv[1], key) || !parse(argv[2], input)) {
        fputs("aes-reference: expected exactly two 16-byte public hexadecimal test inputs\n", stderr);
        return 1;
    }
    aes_reference_encrypt(key, input, output);
    for (i = 0; i < 16; i++) printf("%02x", output[i]);
    putchar('\n');
    return 0;
}
#endif
