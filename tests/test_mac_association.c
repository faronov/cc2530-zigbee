/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic identities/events; no radio mock, PHY CRC or captured hardware.
 */
#include "mac_association.h"
#include "cc2530_mmio.h"
#include <stddef.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)
static const MCU_CODE uint8_t golden[] = {
    0x63, 0xcc, 0x97, 0x34, 0x12,
    1, 2, 3, 4, 5, 6, 7, 8,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    2, 0x78, 0x56, 0
};
static mac_association_t ctx, saved;
static mac_association_request_t request;
static mac_association_event_t event;
static mac_association_record_t record, before;
static uint8_t body[126], observation, expected, scenario;
static uint32_t now;

static void setup(void)
{
    memset(&request, 0, sizeof(request));
    request.epoch = 0x10203040UL;
    request.lifetime = 100;
    request.work_limit = 16;
    request.pan_id = 0x1234;
    request.channel = 15;
    request.coordinator_mode = MAC_ADDRESS_EXTENDED;
    memcpy(request.local, golden + 5, 8);
    memcpy(request.coordinator, golden + 13, 8);
    memset(body, 0xc7, sizeof(body));
    memcpy(body, golden, sizeof(golden));
    memset(&event, 0, sizeof(event));
    event.kind = MAC_ASSOCIATION_FRAME;
    event.epoch = request.epoch;
    event.generation = 1;
    event.channel = 15;
    event.crc_valid = 1;
    event.body = body;
    event.length = sizeof(golden);
    now = 100;
}

static uint16_t corpus(void)
{
    memset(&ctx, 0xc7, sizeof(ctx));
    saved = ctx;
    CHECK(mac_association_init(NULL, 0) == MAC_ASSOCIATION_INVALID);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(mac_association_step(&ctx, 0, NULL, &observation) == MAC_ASSOCIATION_INVALID);
    CHECK(mac_association_init(&ctx, 7) == MAC_ASSOCIATION_OK);
    CHECK(ctx.last == 7 && ctx.version == 1 && ctx.phase == MAC_ASSOCIATION_IDLE);
    for (scenario = 0; scenario < 34; scenario++) {
        setup();
        if (scenario == 0 || scenario == 2) {
            request.coordinator_mode = MAC_ADDRESS_SHORT;
            memset(request.coordinator, 0, 8);
            request.coordinator[0] = 0x44;
            request.coordinator[1] = 0x33;
        }
        if (scenario == 27 || scenario == 31)
            request.work_limit = 1;
        if (scenario == 29 || scenario == 30)
            now = 0xfffffff0UL;
        CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
        memset(&record, 0xc7, sizeof(record));
        before = record;
        CHECK(mac_association_take(&ctx, &record) == MAC_ASSOCIATION_STATE);
        CHECK(memcmp(&record, &before, sizeof(record)) == 0);
        CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
        CHECK(ctx.generation == 1 && ctx.phase == MAC_ASSOCIATION_WAIT);
        CHECK(memcmp(&request, &ctx.request, sizeof(request)) == 0);
        saved = ctx;
        CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_STATE);
        CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
        expected = MAC_ASSOCIATION_RESPONSE;
        event.stamp = now;
        switch (scenario) {
        case 0: event.body = golden; break; /* Genuine generic CODE input. */
        case 1: body[0] |= MAC_FLAG_PENDING; break; /* RX ignored, not a TX rule. */
        case 2: body[13] ^= 0x80; break; /* Unknown IEEE is observed, NOT bound. */
        case 3: body[13] ^= 1; expected = MAC_ASSOCIATION_MISMATCH; break;
        case 4: body[5] ^= 1; expected = MAC_ASSOCIATION_MISMATCH; break;
        case 5: body[3] ^= 1; expected = MAC_ASSOCIATION_MISMATCH; break;
        case 6: body[3] = body[4] = 0xff; expected = MAC_ASSOCIATION_MISMATCH; break;
        case 7: event.channel = 16; expected = MAC_ASSOCIATION_MISMATCH; break;
        case 8: event.crc_valid = 0; expected = MAC_ASSOCIATION_BAD_CRC; break;
        case 9: body[0] &= (uint8_t)~MAC_FLAG_ACK_REQUEST; expected = MAC_ASSOCIATION_MALFORMED; break;
        case 10: event.length--; expected = MAC_ASSOCIATION_MALFORMED; break;
        case 11: event.length++; expected = MAC_ASSOCIATION_MALFORMED; break;
        case 12: body[0] |= MAC_FLAG_SECURITY; expected = MAC_ASSOCIATION_MALFORMED; break;
        case 13: body[21] = 3; expected = MAC_ASSOCIATION_MALFORMED; break;
        case 14:
            body[21] = 3; body[22] = 1; event.length = 23;
            expected = MAC_ASSOCIATION_MISMATCH; break;
        case 15: case 16:
            body[22] = body[23] = 0xff; body[24] = scenario - 14; break;
        case 17: body[22] = 0xfe; body[23] = 0xff; break;
        case 18:
            body[22] = body[23] = 0xff; expected = MAC_ASSOCIATION_MALFORMED; break;
        case 19: body[24] = 3; expected = MAC_ASSOCIATION_MALFORMED; break;
        case 20: event.epoch--; expected = MAC_ASSOCIATION_STALE; break;
        case 21: event.generation--; expected = MAC_ASSOCIATION_STALE; break;
        case 22: event.stamp--; expected = MAC_ASSOCIATION_STALE; break;
        case 23: event.stamp++; expected = MAC_ASSOCIATION_STALE; break;
        case 24: now = 200; event.stamp = 199; expected = MAC_ASSOCIATION_EXPIRED; break;
        case 25:
            CHECK(mac_association_step(&ctx, 197, NULL, &observation) == MAC_ASSOCIATION_OK);
            now = 198; event.stamp = 196; expected = MAC_ASSOCIATION_STALE; break;
        case 26: event.kind = MAC_ASSOCIATION_CANCEL; expected = MAC_ASSOCIATION_CANCELLED; break;
        case 27: event.crc_valid = 0; expected = MAC_ASSOCIATION_EXHAUSTED; break;
        case 28: now--; break;
        case 29: now = event.stamp = 0; break;
        case 30: now = 84; event.stamp = 83; expected = MAC_ASSOCIATION_EXPIRED; break;
        case 32: body[2] = 0; break; /* No Request DSN equality rule. */
        case 33:
            event.kind = MAC_ASSOCIATION_CANCEL;
            CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_OK);
            CHECK(mac_association_take(&ctx, &record) == MAC_ASSOCIATION_OK);
            CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
            CHECK(ctx.generation == 2);
            event.kind = MAC_ASSOCIATION_FRAME;
            expected = MAC_ASSOCIATION_STALE; break;
        default: break;
        }
        observation = 0xa5;
        saved = ctx;
        if (scenario == 28) {
            CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_INVALID);
            CHECK(observation == 0xa5 && memcmp(&ctx, &saved, sizeof(ctx)) == 0);
            continue;
        }
        CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_OK);
        CHECK(observation == expected && ctx.last == now);
        if (expected == MAC_ASSOCIATION_RESPONSE || expected >= MAC_ASSOCIATION_EXPIRED) {
            CHECK(ctx.phase == MAC_ASSOCIATION_DONE && ctx.record.outcome == expected);
            saved = ctx;
            CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_STATE);
            CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0 && observation == expected);
            CHECK(mac_association_take(&ctx, &record) == MAC_ASSOCIATION_OK);
            CHECK(ctx.phase == MAC_ASSOCIATION_IDLE && record.outcome == expected
                  && record.generation == 1 && record.epoch == request.epoch);
            if (expected == MAC_ASSOCIATION_RESPONSE) {
                CHECK(record.sequence == (scenario == 32 ? 0 : 0x97));
                CHECK(record.stamp == event.stamp);
                /* Both conditional arms must be generic pointers on SDCC;
                 * otherwise the CODE-qualified arm can select CODE for RAM.
                 */
                CHECK(memcmp(record.source_ieee, scenario == 0
                      ? (const uint8_t *)(golden + 13) : (const uint8_t *)(body + 13), 8) == 0);
                CHECK(record.source_relation == (scenario == 0 || scenario == 2
                      ? MAC_ASSOCIATION_SOURCE_UNBOUND : MAC_ASSOCIATION_SOURCE_MATCHED));
                CHECK(record.status == (scenario == 15 || scenario == 16 ? scenario - 14 : 0));
                CHECK(record.address_kind == (scenario == 15 || scenario == 16
                      ? MAC_ASSOCIATION_REFUSED : scenario == 17
                      ? MAC_ASSOCIATION_EXTENDED_ONLY : MAC_ASSOCIATION_ALLOCATED));
                CHECK(record.short_address == (scenario == 15 || scenario == 16 ? 0xffff
                      : scenario == 17 ? 0xfffe : 0x5678));
                memset(body, 0, sizeof(body));
                CHECK(memcmp(&record, &saved.record, sizeof(record)) == 0);
            }
            before = record;
            CHECK(mac_association_take(&ctx, &record) == MAC_ASSOCIATION_STATE);
            CHECK(memcmp(&record, &before, sizeof(record)) == 0);
        } else {
            CHECK(ctx.phase == MAC_ASSOCIATION_WAIT && ctx.record.outcome == 0);
            CHECK(ctx.remaining + 1u == saved.remaining);
        }
    }
    setup();
    CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
    ctx.generation = UINT32_MAX;
    saved = ctx;
    CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_GENERATION_LIMIT);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    return 0;
}

#ifdef CC2530_HOST_TEST
#include <stdio.h>
#include <stdlib.h>

static uint16_t exhaustive(void)
{
    unsigned n, j;
    uint32_t fcf, address;
    uint8_t *exact;
    for (fcf = 3; fcf <= 0xffffu; fcf += 8) {
        setup();
        body[0] = (uint8_t)fcf; body[1] = (uint8_t)(fcf >> 8);
        CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
        CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
        event.stamp = now;
        CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_OK);
        CHECK((observation == MAC_ASSOCIATION_RESPONSE) == (fcf == 0xcc63 || fcf == 0xcc73));
    }
    for (n = 0; n <= 126; n++) {
        setup();
        exact = malloc(n ? n : 1); CHECK(exact != NULL);
        memset(exact, 0xc7, n);
        memcpy(exact, golden, n < 25 ? n : 25);
        event.body = exact; event.length = (uint16_t)n; event.stamp = now;
        CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
        CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
        CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_OK);
        CHECK((observation == MAC_ASSOCIATION_RESPONSE) == (n == 25));
        free(exact);
    }
    for (j = 0; j < 25; j++) {
        for (n = 0; n < 256; n++) {
            setup(); body[j] = (uint8_t)n; event.stamp = now;
            CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
            CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
            CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_OK);
            CHECK((observation == MAC_ASSOCIATION_RESPONSE)
                  == (j == 2 || j == 22 || j == 23 || n == golden[j] || (j == 0 && n == 0x73)));
        }
    }
    for (address = 0; address <= 0xffff; address++) {
        setup(); body[22] = (uint8_t)address; body[23] = (uint8_t)(address >> 8);
        event.stamp = now;
        CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
        CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
        CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_OK);
        CHECK((observation == MAC_ASSOCIATION_RESPONSE) == (address != 0xffff));
    }
    for (n = 0; n < 256; n++) {
        setup(); body[22] = body[23] = 0xff; body[24] = (uint8_t)n; event.stamp = now;
        CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
        CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
        CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_OK);
        CHECK((observation == MAC_ASSOCIATION_RESPONSE) == (n == 1 || n == 2));
    }
    /* Invalid contexts, requests, arguments and half-range time are atomic. */
    for (n = 0; n < 17; n++) {
        setup();
        CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
        switch (n) {
        case 0: request.epoch = 0; break;
        case 1: request.lifetime = 0; break;
        case 2: request.lifetime = MAC_ASSOCIATION_MAX_LIFETIME + 1; break;
        case 3: request.work_limit = 0; break;
        case 4: request.work_limit = MAC_ASSOCIATION_MAX_WORK + 1; break;
        case 5: request.pan_id = 0xffff; break;
        case 6: request.channel = 10; break;
        case 7: request.channel = 27; break;
        case 8: request.coordinator_mode = 1; break;
        case 9: case 10:
            request.coordinator_mode = MAC_ADDRESS_SHORT;
            memset(request.coordinator, 0, 8);
            request.coordinator[0] = n == 9 ? 0xfe : 0xff;
            request.coordinator[1] = 0xff; break;
        case 11: request.coordinator_mode = MAC_ADDRESS_SHORT; break; /* nonzero tail */
        case 12: now--; break;
        case 13: now += MAC_ASSOCIATION_HALF; break;
        case 14: ctx.version++; break;
        case 15: ctx.phase = 0xff; break;
        default: break;
        }
        saved = ctx;
        CHECK(mac_association_start(&ctx, n == 16 ? NULL : &request, now) == MAC_ASSOCIATION_INVALID);
        CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    }
    for (n = 0; n < 8; n++) {
        setup();
        CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
        CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
        switch (n) {
        case 0: event.kind = 0; break;
        case 1: event.crc_valid = 2; break;
        case 2: event.channel = 10; break;
        case 3: event.channel = 27; break;
        case 4: event.body = NULL; break;
        case 5: now += MAC_ASSOCIATION_HALF; break;
        case 6: now--; break;
        default: break;
        }
        saved = ctx; observation = 0xa5;
        CHECK(mac_association_step(&ctx, now, &event, n == 7 ? NULL : &observation)
              == MAC_ASSOCIATION_INVALID);
        CHECK(observation == 0xa5 && memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    }
    setup(); request.lifetime = MAC_ASSOCIATION_MAX_LIFETIME;
    request.work_limit = MAC_ASSOCIATION_MAX_WORK;
    CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
    CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
    memset(&request, 0, sizeof(request)); /* Start owns its complete copy. */
    for (n = 0; n < MAC_ASSOCIATION_MAX_WORK; n++) {
        CHECK(mac_association_step(&ctx, now, NULL, &observation) == MAC_ASSOCIATION_OK);
        CHECK(observation == (n + 1 == MAC_ASSOCIATION_MAX_WORK
              ? MAC_ASSOCIATION_EXHAUSTED : MAC_ASSOCIATION_WAITING));
    }
    CHECK(mac_association_take(&ctx, &record) == MAC_ASSOCIATION_OK);
    CHECK(record.outcome == MAC_ASSOCIATION_EXHAUSTED && record.epoch == 0x10203040UL);
    setup();
    CHECK(mac_association_init(&ctx, now) == MAC_ASSOCIATION_OK);
    ctx.generation = UINT32_MAX - 1;
    CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_OK);
    CHECK(ctx.generation == UINT32_MAX);
    event.generation = UINT32_MAX;
    event.kind = MAC_ASSOCIATION_CANCEL;
    CHECK(mac_association_step(&ctx, now, &event, &observation) == MAC_ASSOCIATION_OK);
    CHECK(mac_association_take(&ctx, &record) == MAC_ASSOCIATION_OK);
    CHECK(record.generation == UINT32_MAX && record.outcome == MAC_ASSOCIATION_CANCELLED);
    saved = ctx;
    CHECK(mac_association_start(&ctx, &request, now) == MAC_ASSOCIATION_GENERATION_LIMIT);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(mac_association_start(NULL, &request, now) == MAC_ASSOCIATION_INVALID);
    CHECK(mac_association_step(NULL, now, NULL, &observation) == MAC_ASSOCIATION_INVALID);
    CHECK(mac_association_take(NULL, &record) == MAC_ASSOCIATION_INVALID);
    CHECK(mac_association_take(&ctx, NULL) == MAC_ASSOCIATION_INVALID);
    return 0;
}

int main(void)
{
    uint16_t line = corpus();
    if (!line) line = exhaustive();
    if (line) { fprintf(stderr, "Association context failed C line %u\n", (unsigned)line); return 1; }
    puts("Association context: 34 shared scenarios, 8192 FCFs, 6400 byte variants, "
         "65536 short addresses, 256 statuses, exact spans and finite/atomic cases PASS");
    return 0;
}
#else
volatile __xdata __at(0x1e00) uint8_t mac_association_result[8];
void main(void)
{
    uint16_t line = corpus();
    mac_association_result[0] = 'A'; mac_association_result[1] = 'S';
    mac_association_result[2] = 'R'; mac_association_result[3] = '1';
    mac_association_result[4] = 1; mac_association_result[5] = 8;
    mac_association_result[6] = (uint8_t)line;
    mac_association_result[7] = (uint8_t)(line >> 8);
    __asm
        .globl _mac_association_done
    _mac_association_done:
        nop
    __endasm;
    for (;;) {}
}
#endif
