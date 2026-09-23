/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Isolated synthetic executable. NEVER flash this image.
 */
#include "zigbee_security.h"
#include "aps_frame.h"
#include "nwk_frame.h"
#include <stddef.h>
#include <string.h>

static const MCU_CODE uint8_t kat_key[16] = {
    0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf
};
static const MCU_CODE uint8_t kat_nonce[13] = {
    0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,3,2,1,0,6
};
static const MCU_CODE uint8_t kat_aad[8] = {0,1,2,3,4,5,6,7};
static const MCU_CODE uint8_t kat_plain[23] = {
    8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
};
static const MCU_CODE uint8_t kat_cipher[31] = {
    0x1a,0x55,0xa3,0x6a,0xbb,0x6c,0x61,0x0d,0x06,0x6b,0x33,0x75,0x64,0x9c,0xef,0x10,
    0xd4,0x66,0x4e,0xca,0xd8,0x54,0xa8,0x0a,0x89,0x5c,0xc1,0xd8,0xff,0x94,0x69
};
static const MCU_CODE uint8_t nwk_packet[19] = {
    0x08,0x20,0x34,0x12,0x78,0x56,0x1e,0x21,
    0x40,0,2,0,0,0,7,0xa9,0x5a,0x34,0x12
};
static const MCU_CODE uint8_t aps_packet[11] = {
    0x40,0,2,0,0,0,7,0xa9,0x5a,0x34,0x12
};
static const MCU_CODE uint8_t command_packet[7] = {0x41,0xa9,5,1,2,3,4};

static MCU_XDATA uint8_t input[132], output[134], wire[132], decoded[132], nonce[13], aad[132];
static MCU_XDATA uint8_t written;
static MCU_XDATA ccm_star_limits_t limits;
static MCU_XDATA ccm_star_info_t crypto_info;
static MCU_XDATA zigbee_security_key_t context;
static MCU_XDATA zigbee_security_info_t info;
static MCU_XDATA zigbee_security_meta_t meta;
volatile MCU_XDATA uint32_t security_checks;
volatile MCU_XDATA uint8_t security_case;

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t security_result[8];
static void check(uint8_t condition, uint16_t line)
{
    security_checks++;
    if (!condition) {
        security_result[6] = (uint8_t)line;
        security_result[7] = (uint8_t)(line >> 8);
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
#define CHECK(condition) do { security_checks++; assert(condition); } while (0)
#endif

static uint8_t filled(const uint8_t *bytes, uint8_t size, uint8_t value)
{
    uint8_t i;
    for (i = 0; i < size; i++)
        if (bytes[i] != value)
            return 0;
    return 1;
}

static void setup(void)
{
    memset(&context, 0, sizeof(context));
    memcpy(context.key, kat_key, 16);
    memcpy(context.source, kat_nonce, 8);
    context.counter = 0x00010203UL;
    context.level = 5; context.key_identifier = 1; context.key_sequence = 0x7f;
    context.extended_nonce = 1;
    limits.block_timeout = context.limits.block_timeout = 1000;
    limits.block_polls = context.limits.block_polls = 128;
    memset(output, 0xa5, sizeof(output));
    memset(wire, 0xa5, sizeof(wire));
    memset(decoded, 0xa5, sizeof(decoded));
    written = 0xa5;
}

static void primitive_case(uint8_t which)
{
    ccm_star_result_t result;
    setup();
    if (which == 0) {
        result = ccm_star_crypt(0, kat_key, kat_nonce, kat_aad, 8, kat_plain, 23, 8,
                               output + 1, 132, &written, &limits, &crypto_info);
        CHECK(result == CCM_STAR_OK && written == 31);
        CHECK(!memcmp(output + 1, kat_cipher, 31));
        CHECK(crypto_info.blocks == 7 && !crypto_info.aes_status);
        CHECK(output[0] == 0xa5 && filled(output + 32, 102, 0xa5));
    } else {
        memcpy(input, kat_cipher, 31);
        if (which == 2) input[30] ^= 1;
        if (which == 3) limits.block_polls = 1;
        result = ccm_star_crypt(1, kat_key, kat_nonce, kat_aad, 8, input, 31, 8,
                               output + 1, 132, &written, &limits, &crypto_info);
        if (which == 1) {
            CHECK(result == CCM_STAR_OK && written == 23);
            CHECK(!memcmp(output + 1, kat_plain, 23));
            CHECK(crypto_info.blocks == 7 && !crypto_info.aes_status);
            CHECK(output[0] == 0xa5 && filled(output + 24, 110, 0xa5));
        } else {
            CHECK(result == (which == 2 ? CCM_STAR_AUTH : CCM_STAR_AES));
            CHECK(written == 0xa5 && filled(output, sizeof(output), 0xa5));
            CHECK(crypto_info.blocks == (which == 2 ? 7 : 1));
            CHECK(crypto_info.aes_status == (which == 2 ? AES_OK : AES_POLL_LIMIT));
        }
    }
}

static void frame_case(uint8_t which)
{
    volatile uint8_t layer, n, size, level;
    const uint8_t * volatile packet;
    setup();
    level = which % 6u;
    context.level = level < 3 ? level + 1 : level + 2;
    layer = which / 6u;
    if (!layer) {
        packet = nwk_packet; n = sizeof(nwk_packet);
    } else {
        context.key_identifier = layer == 1 ? 0 : 2;
        context.extended_nonce = layer == 1 ? 0 : 1;
        packet = layer == 1 ? aps_packet : command_packet;
        n = layer == 1 ? sizeof(aps_packet) : sizeof(command_packet);
        layer = ZIGBEE_SECURITY_APS;
    }
    CHECK(zigbee_security_crypt(0, layer, &context, packet, n, wire, sizeof(wire), &info) ==
          ZIGBEE_SECURITY_OK);
    size = info.length;
    CHECK(info.meta.counter == context.counter && info.meta.level == context.level);
    CHECK(!memcmp(info.meta.source, context.source, 8));
    CHECK(!(wire[info.meta.header_length] & 7u));
    CHECK(zigbee_security_inspect(layer, context.level, wire, size, &meta) == ZIGBEE_SECURITY_OK);
    CHECK(meta.counter == context.counter && meta.payload_length == n - meta.header_length);
    CHECK(zigbee_security_crypt(1, layer, &context, wire, size, decoded, sizeof(decoded), &info) ==
          ZIGBEE_SECURITY_OK);
    CHECK(info.length == n && !memcmp(decoded, packet, n));
    CHECK(filled(decoded + n, sizeof(decoded) - n, 0xa5));
    wire[size - 1u] ^= 0x80;
    memset(&info, 0xa5, sizeof(info));
    CHECK(zigbee_security_crypt(1, layer, &context, wire, size, output, sizeof(output), &info) ==
          ZIGBEE_SECURITY_AUTH);
    CHECK(filled(output, sizeof(output), 0xa5) && filled((const uint8_t *)&info, sizeof(info), 0xa5));
}

static void run_case(uint8_t which)
{
    uint8_t i;
    if (which < 4) {
        primitive_case(which);
    } else if (which < 22) {
        frame_case(which - 4u);
    } else {
        setup();
        for (i = 0; i < 132; i++) aad[i] = (uint8_t)(3u * i + 132u);
        for (i = 0; i < 116; i++) input[i] = (uint8_t)(7u * i + 116u);
        CHECK(ccm_star_crypt(0, kat_key, kat_nonce, aad, 132, input, 116, 16,
                            output + 1, 132, &written, &limits, &crypto_info) == CCM_STAR_OK);
        CHECK(written == 132 && crypto_info.blocks == 27);
        CHECK(output[0] == 0xa5 && output[133] == 0xa5);
        CHECK(ccm_star_crypt(1, kat_key, kat_nonce, aad, 132, output + 1, 132, 16,
                            decoded, 116, &written, &limits, &crypto_info) == CCM_STAR_OK);
        CHECK(written == 116 && !memcmp(decoded, input, 116));
        CHECK(filled(decoded + 116, 16, 0xa5));
    }
}

#if defined(__SDCC)
void main(void)
{
    security_result[0] = 'S'; security_result[1] = 'E';
    security_result[2] = 'C'; security_result[3] = '1';
    security_result[4] = 1; security_result[5] = 8;
    security_result[6] = security_result[7] = 0;
    __asm
        .globl _security_before
    _security_before:
        nop
    __endasm;
    run_case(security_case);
    __asm
        .globl _security_done
    _security_done:
        nop
    __endasm;
    for (;;) {}
}
#else
static void reference(const uint8_t *key, const uint8_t *iv, const uint8_t *auth,
                      unsigned auth_size, const uint8_t *plain, unsigned size,
                      unsigned tag, uint8_t *out)
{
    uint8_t blocks[288] = {0}, chain[16] = {0}, stream[16], ctr[16] = {1};
    unsigned i, n, padded, end;
    blocks[0] = (uint8_t)(1u | ((tag - 2u) << 2) | (auth_size ? 0x40u : 0u));
    memcpy(blocks + 1, iv, 13);
    blocks[14] = (uint8_t)(size >> 8); blocks[15] = (uint8_t)size;
    end = 16;
    if (auth_size) {
        blocks[16] = (uint8_t)(auth_size >> 8); blocks[17] = (uint8_t)auth_size;
        memcpy(blocks + 18, auth, auth_size);
        end += (auth_size + 17u) & ~15u;
    }
    if (size) memcpy(blocks + end, plain, size);
    padded = (size + 15u) & ~15u;
    end += padded;
    CHECK(end <= sizeof(blocks));
    for (n = 0; n < end; n += 16) {
        for (i = 0; i < 16; i++) chain[i] ^= blocks[n + i];
        aes_reference_encrypt(key, chain, stream);
        memcpy(chain, stream, 16);
    }
    memcpy(ctr + 1, iv, 13);
    for (n = 0; n <= padded; n += 16) {
        ctr[14] = (uint8_t)((n / 16u) >> 8); ctr[15] = (uint8_t)(n / 16u);
        aes_reference_encrypt(key, ctr, stream);
        if (!n) {
            for (i = 0; i < tag; i++) out[size + i] = chain[i] ^ stream[i];
        } else {
            for (i = 0; i < 16 && n - 16u + i < size; i++)
                out[n - 16u + i] = plain[n - 16u + i] ^ stream[i];
        }
    }
}

static uint8_t reference_frame(uint8_t layer, const uint8_t *packet, uint8_t size, uint8_t *out)
{
    uint8_t iv[13], encrypted[132];
    unsigned h = layer == 0 ? 8 : (packet[0] & 3u) == 1 ? 2 : 8;
    unsigned at = h, i, payload = size - h, mic = 2u << (context.level & 3u);
    memcpy(out, packet, h);
    out[layer == 0 ? 1 : 0] |= layer == 0 ? 2 : 0x20;
    out[at++] = context.level | (uint8_t)(context.key_identifier << 3) |
                (uint8_t)(context.extended_nonce << 5);
    for (i = 0; i < 4; i++) out[at++] = (uint8_t)(context.counter >> (8u * i));
    if (context.extended_nonce) {
        memcpy(out + at, context.source, 8); at += 8;
    }
    if (context.key_identifier == 1) out[at++] = context.key_sequence;
    memcpy(out + at, packet + h, payload);
    memcpy(iv, context.source, 8); memcpy(iv + 8, out + h + 1, 4); iv[12] = out[h];
    if (context.level & 4u) {
        reference(context.key, iv, out, at, packet + h, payload, mic, encrypted);
        memcpy(out + at, encrypted, payload + mic);
    } else {
        reference(context.key, iv, out, at + payload, NULL, 0, mic, encrypted);
        memcpy(out + at + payload, encrypted, mic);
    }
    out[h] &= 0xf8;
    return (uint8_t)(at + payload + mic);
}

static void length_case(unsigned alen, unsigned size, unsigned tag)
{
    uint8_t *a = malloc(alen ? alen : 1), *p = malloc(size ? size : 1);
    uint8_t *c = malloc(size + tag), *out = malloc(size ? size : 1);
    uint8_t expected[132], n = 0xa5;
    unsigned i, count;
    CHECK(a && p && c && out);
    a[0] = p[0] = 0;
    for (i = 0; i < alen; i++) a[i] = (uint8_t)(3u * i + alen);
    for (i = 0; i < size; i++) p[i] = (uint8_t)(7u * i + size);
    reference(kat_key, kat_nonce, a, alen, p, size, tag, expected);
    security_aes_reset();
    count = 2u + (alen ? (alen + 17u) / 16u : 0u) + 2u * ((size + 15u) / 16u);
    CHECK(ccm_star_crypt(0, kat_key, kat_nonce, a, (uint16_t)alen, p, (uint16_t)size, (uint8_t)tag,
                        c, (uint16_t)(size + tag), &n, &limits, &crypto_info) == CCM_STAR_OK);
    CHECK(n == size + tag && !memcmp(c, expected, n));
    CHECK(crypto_info.blocks == count && security_aes_blocks() == count);
    CHECK(ccm_star_crypt(1, kat_key, kat_nonce, a, (uint16_t)alen, c, n, (uint8_t)tag,
                        out, (uint16_t)size, &n, &limits, &crypto_info) == CCM_STAR_OK);
    CHECK(n == size && !memcmp(p, out, size));
    CHECK(crypto_info.blocks == count && security_aes_blocks() == 2u * count);
    free(out); free(c); free(p); free(a);
}

static void native_cases(void)
{
    unsigned i, j, tag, size, blocks;
    ccm_star_info_t saved;
    zigbee_security_info_t old;
    uint8_t expected[132], h, n;
    setup();
    reference(kat_key, kat_nonce, kat_aad, 8, kat_plain, 23, 8, expected);
    CHECK(!memcmp(expected, kat_cipher, 31));
    for (tag = 4; tag <= 16; tag *= 2) {
        for (i = 0; i <= CCM_STAR_MAX_AAD; i++) length_case(i, 23, tag);
        for (i = 0; i <= CCM_STAR_MAX_MESSAGE; i++) length_case(8, i, tag);
        length_case(0, 0, tag);
        length_case(132, 116, tag);
    }
    setup();
    for (i = 0; i < sizeof(kat_cipher) * 8u; i++) {
        memcpy(input, kat_cipher, 31); input[i / 8] ^= 1u << (i % 8);
        security_aes_reset();
        CHECK(ccm_star_crypt(1, kat_key, kat_nonce, kat_aad, 8, input, 31, 8,
                            output, sizeof(output), &written, &limits, &crypto_info) == CCM_STAR_AUTH);
        CHECK(written == 0xa5 && filled(output, sizeof(output), 0xa5));
        CHECK(crypto_info.blocks == 7);
    }
    for (i = 0; i < 13; i++) {
        memcpy(nonce, kat_nonce, 13); nonce[i] ^= 1;
        security_aes_reset();
        CHECK(ccm_star_crypt(1, kat_key, nonce, kat_aad, 8, kat_cipher, 31, 8,
                            output, sizeof(output), &written, &limits, &crypto_info) == CCM_STAR_AUTH);
        CHECK(filled(output, sizeof(output), 0xa5));
    }
    for (i = 0; i < 24; i++) {
        memcpy(context.key, kat_key, 16); memcpy(aad, kat_aad, 8);
        if (i < 16) context.key[i] ^= 1;
        else aad[i - 16u] ^= 1;
        security_aes_reset();
        CHECK(ccm_star_crypt(1, context.key, kat_nonce, aad, 8, kat_cipher, 31, 8,
                            output, sizeof(output), &written, &limits, &crypto_info) == CCM_STAR_AUTH);
        CHECK(filled(output, sizeof(output), 0xa5));
    }
    for (i = 1; i <= 7; i++) {
        security_aes_reset(); security_aes_stall(i);
        limits.block_polls = 32;
        CHECK(ccm_star_crypt(1, kat_key, kat_nonce, kat_aad, 8, kat_cipher, 31, 8,
                            output, sizeof(output), &written, &limits, &crypto_info) == CCM_STAR_AES);
        CHECK(crypto_info.blocks == i && crypto_info.aes_status == AES_POLL_LIMIT);
        CHECK(filled(output, sizeof(output), 0xa5) && written == 0xa5);
        blocks = security_aes_blocks();
        CHECK(ccm_star_crypt(0, kat_key, kat_nonce, kat_aad, 8, kat_plain, 23, 8,
                            output, sizeof(output), &written, &limits, &crypto_info) == CCM_STAR_AES);
        CHECK(security_aes_blocks() == blocks);
    }
    setup(); security_aes_reset();
    memset(&crypto_info, 0xa5, sizeof(crypto_info)); saved = crypto_info;
    for (i = 0; i < 256; i++) {
        if (i == 4 || i == 8 || i == 16) continue;
        CHECK(ccm_star_crypt(0, kat_key, kat_nonce, NULL, 0, NULL, 0, (uint8_t)i,
                            output, 134, &written, &limits, &crypto_info) == CCM_STAR_TAG);
        CHECK(!memcmp(&saved, &crypto_info, sizeof(saved)) && !security_aes_blocks());
    }
    for (i = 0; i < 31; i++) {
        CHECK(ccm_star_crypt(0, kat_key, kat_nonce, kat_aad, 8, kat_plain, 23, 8,
                            output, (uint16_t)i, &written, &limits, &crypto_info) == CCM_STAR_SPACE);
        CHECK(filled(output, sizeof(output), 0xa5) && !security_aes_blocks());
    }
    CHECK(ccm_star_crypt(0, kat_key, kat_nonce, kat_aad, 65535, kat_plain, 23, 8,
                        output, 65535, &written, &limits, &crypto_info) == CCM_STAR_LENGTH);
    CHECK(ccm_star_crypt(0, kat_key, kat_nonce, kat_aad, 8, kat_plain, 65535, 8,
                        output, 65535, &written, &limits, &crypto_info) == CCM_STAR_LENGTH);
    CHECK(ccm_star_crypt(1, kat_key, kat_nonce, NULL, 0, NULL, 0, 4,
                        output, 0, &written, &limits, &crypto_info) == CCM_STAR_LENGTH);
    CHECK(!security_aes_blocks() && !memcmp(&saved, &crypto_info, sizeof(saved)));
    for (i = 0; i < 9; i++) {
        CHECK(ccm_star_crypt(0, i == 0 ? NULL : kat_key, i == 1 ? NULL : kat_nonce,
                            i == 2 ? NULL : kat_aad, 8, i == 3 ? NULL : kat_plain, 23, 8,
                            i == 4 ? NULL : output, 134, i == 5 ? NULL : &written,
                            i == 6 ? NULL : &limits, i == 7 ? NULL : &crypto_info) ==
              (i == 8 ? CCM_STAR_OK : CCM_STAR_ARGUMENT));
        if (i < 8)
            CHECK(filled(output, sizeof(output), 0xa5) && !security_aes_blocks());
    }
    setup();
    CHECK(ccm_star_crypt(0, kat_key, kat_nonce, NULL, 0, NULL, 0, 4,
                        output, 4, &written, &limits, &crypto_info) == CCM_STAR_OK);
    CHECK(written == 4);
    CHECK(ccm_star_crypt(1, kat_key, kat_nonce, NULL, 0, output, 4, 4,
                        decoded, 0, &written, &limits, &crypto_info) == CCM_STAR_OK);
    CHECK(!written && filled(decoded, sizeof(decoded), 0xa5));

    setup(); security_aes_reset();
    CHECK(zigbee_security_crypt(0, 0, &context, nwk_packet, sizeof(nwk_packet),
                                wire, sizeof(wire), &info) == ZIGBEE_SECURITY_OK);
    size = info.length; h = info.meta.header_length;
    memcpy(aad, wire, h + 14u); aad[h] |= context.level;
    memcpy(nonce, context.source, 8);
    memcpy(nonce + 8, aad + h + 1, 4); nonce[12] = aad[h];
    reference(context.key, nonce, aad, h + 14u, nwk_packet + h,
              sizeof(nwk_packet) - h, 4, expected);
    CHECK(!memcmp(wire + h + 14u, expected, sizeof(nwk_packet) - h + 4u));
    CHECK(wire[h] == 0x28 && !memcmp(wire + h + 1, "\3\2\1\0", 4) &&
          !memcmp(wire + h + 5, context.source, 8) && wire[h + 13] == 0x7f);
    memset(&info, 0xa5, sizeof(info)); old = info;
    for (i = 0; i < size; i++)
        for (j = 0; j < 8; j++) {
            zigbee_security_result_t result;
            memcpy(input, wire, size); input[i] ^= 1u << j;
            if (i == h && j < 3) {
                CHECK(zigbee_security_crypt(1, 0, &context, input, (uint16_t)size,
                                            decoded, sizeof(decoded), &info) == ZIGBEE_SECURITY_OK);
                CHECK(info.length == sizeof(nwk_packet) && !memcmp(decoded, nwk_packet, sizeof(nwk_packet)));
            } else {
                info = old;
                result = zigbee_security_crypt(1, 0, &context, input, (uint16_t)size,
                                              output, sizeof(output), &info);
                CHECK(result != ZIGBEE_SECURITY_OK);
                CHECK(filled(output, sizeof(output), 0xa5) && !memcmp(&info, &old, sizeof(old)));
            }
        }
    for (i = 0; i < size; i++) {
        info = old;
        CHECK(zigbee_security_crypt(1, 0, &context, wire, (uint16_t)i, output, sizeof(output), &info) !=
              ZIGBEE_SECURITY_OK);
        CHECK(filled(output, sizeof(output), 0xa5) && !memcmp(&info, &old, sizeof(old)));
    }
    for (i = 0; i < 256; i++) {
        context.level = (uint8_t)i;
        if (i >= 1 && i <= 7 && i != 4) continue;
        CHECK(zigbee_security_crypt(1, 0, &context, wire, (uint16_t)size, output, sizeof(output), &info) ==
              ZIGBEE_SECURITY_LEVEL);
    }
    context.level = 5; context.counter = 0xffffffffUL;
    CHECK(zigbee_security_crypt(0, 0, &context, nwk_packet, sizeof(nwk_packet), output, sizeof(output), &info) ==
          ZIGBEE_SECURITY_COUNTER);
    context.counter = 0xfffffffeUL;
    CHECK(zigbee_security_crypt(0, 0, &context, nwk_packet, sizeof(nwk_packet), decoded, sizeof(decoded), &info) ==
          ZIGBEE_SECURITY_OK);
    CHECK(!memcmp(decoded + h + 1, "\376\377\377\377", 4));
    for (i = 0; i < 18; i++) {
        security_aes_reset(); frame_case((uint8_t)i);
        n = reference_frame(i < 6 ? 0 : 1,
                            i < 6 ? nwk_packet : i < 12 ? aps_packet : command_packet,
                            i < 6 ? sizeof(nwk_packet) : i < 12 ? sizeof(aps_packet) : sizeof(command_packet),
                            expected);
        wire[n - 1u] ^= 0x80;
        CHECK(!memcmp(wire, expected, n));
    }
    setup();
    context.key_identifier = 0;
    for (i = 0; i <= 1; i++) {
        context.extended_nonce = (uint8_t)i;
        for (j = 0; j < 256; j++) {
            context.key_sequence = (uint8_t)j;
            security_aes_reset();
            CHECK(zigbee_security_crypt(0, 1, &context, aps_packet, sizeof(aps_packet),
                                       wire, sizeof(wire), &info) == ZIGBEE_SECURITY_OK);
            n = info.length;
            CHECK(info.meta.auxiliary_length == 5 + 8 * i && !info.meta.key_sequence);
            CHECK(zigbee_security_crypt(1, 1, &context, wire, n, decoded, sizeof(decoded), &info) ==
                  ZIGBEE_SECURITY_OK);
            CHECK(!memcmp(decoded, aps_packet, sizeof(aps_packet)));
        }
    }
    for (i = 0; i < 4; i++) {
        setup(); context.key_identifier = (uint8_t)i;
        security_aes_reset();
        CHECK(zigbee_security_crypt(0, 1, &context, command_packet, sizeof(command_packet),
                                   wire, sizeof(wire), &info) ==
              (i == 1 ? ZIGBEE_SECURITY_AUX : ZIGBEE_SECURITY_OK));
        if (i != 1) {
            n = info.length;
            CHECK(reference_frame(1, command_packet, sizeof(command_packet), expected) == n);
            CHECK(!memcmp(wire, expected, n));
            CHECK(zigbee_security_crypt(1, 1, &context, wire, n, decoded, sizeof(decoded), &info) ==
                  ZIGBEE_SECURITY_OK);
            CHECK(info.length == sizeof(command_packet) && !memcmp(decoded, command_packet, sizeof(command_packet)));
        }
    }
    setup();
    for (i = 0; i < 256; i++) {
        context.key_sequence = (uint8_t)i;
        security_aes_reset();
        CHECK(zigbee_security_crypt(0, 0, &context, nwk_packet, sizeof(nwk_packet),
                                   wire, sizeof(wire), &info) == ZIGBEE_SECURITY_OK);
        CHECK(wire[21] == i);
        n = info.length;
        CHECK(zigbee_security_crypt(1, 0, &context, wire, n, decoded, sizeof(decoded), &info) ==
              ZIGBEE_SECURITY_OK);
        CHECK(!memcmp(decoded, nwk_packet, sizeof(nwk_packet)));
    }
    setup();
    context.level = 7;
    memcpy(input, nwk_packet, 8);
    memset(input + 8, 0x31, 108);
    for (i = 8; i <= 116; i++) {
        memset(&info, 0xa5, sizeof(info)); old = info;
        security_aes_reset();
        CHECK(zigbee_security_crypt(0, 0, &context, input, (uint16_t)i,
                                   output, sizeof(output), &info) ==
              (i <= 86 ? ZIGBEE_SECURITY_OK : ZIGBEE_SECURITY_LENGTH));
        if (i > 86) CHECK(!memcmp(&info, &old, sizeof(old)) && !security_aes_blocks());
        else CHECK(info.length == i + 30u);
    }
    setup(); security_aes_reset();
    CHECK(zigbee_security_crypt(0, 0, &context, input, 65535, output, 65535, &info) ==
          ZIGBEE_SECURITY_LENGTH);
    CHECK(zigbee_security_crypt(0, 0, &context, nwk_packet, sizeof(nwk_packet), output, 0, &info) ==
          ZIGBEE_SECURITY_SPACE);
    CHECK(!security_aes_blocks() && filled(output, sizeof(output), 0xa5));
}

int main(int argc, char **argv)
{
    unsigned i;
    aes_reference_check();
    if (argc == 3 && !strcmp(argv[1], "--trace")) {
        char *end;
        unsigned long which = strtoul(argv[2], &end, 10);
        if (*end || which >= 23) return 2;
        security_aes_reset(); security_aes_trace(1); run_case((uint8_t)which);
        printf("CHECKS %lu\n", (unsigned long)security_checks);
        fputs("OUTPUT ", stdout);
        for (i = 0; i < sizeof(output); i++) printf("%02x", output[i]);
        putchar('\n');
        fputs("WIRE ", stdout);
        for (i = 0; i < sizeof(wire); i++) printf("%02x", wire[i]);
        putchar('\n');
        fputs("DECODED ", stdout);
        for (i = 0; i < sizeof(decoded); i++) printf("%02x", decoded[i]);
        putchar('\n');
        return 0;
    }
    if (argc != 1) return 2;
    for (i = 0; i < 23; i++) {
        security_aes_reset(); run_case((uint8_t)i);
    }
    native_cases();
    printf("Zigbee security: %lu host checks PASS; real AES driver, no membership.\n",
           (unsigned long)security_checks);
    return 0;
}
#endif
