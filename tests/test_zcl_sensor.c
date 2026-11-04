/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zcl_sensor.h"
#include "zcl_temperature.h"

/* The application ZCL/Bind server against its real device overlay. The
 * runtime publication and the caller's queueing are covered by
 * test_bdb_join.c and test_join_smoke.c; values are SYNTHETIC. */

MCU_XDATA bdb_join_t join_smoke_device;

#define CTX join_smoke_device.work.runtime.zdo
#define ST ZCL_SENSOR_STATE
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "zcl_sensor: line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static const uint8_t own[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t tc[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
static const char model[] = BOARD_MODEL, manufacturer[] = BOARD_MANUFACTURER;
static unsigned long checks;

#if defined(CC2530_CHIP_SENSE)
/* ADC model (SWRS081B nominals): AVDD5/3 against 1.15 V and the die sensor,
 * 1480 at 25 C and 3 V, 4.5 /C, 1 /0.1 V, live only with TR0=1, ATEST=1. */
volatile uint8_t SOC_ADCCON1, SOC_ADCCON3, SOC_ADCL, SOC_ADCH;
static struct {
    long millivolts, centi;
    uint8_t tr0, atest, stuck, fresh;
    unsigned conversions, vdd, temperature, unsensed, overlapped, foreign;
} adc;

static void convert_model(uint8_t command)
{
    long code;
    if ((SOC_ADCCON1 & 0x80u) || adc.fresh) adc.overlapped++;
    adc.conversions++;
    if (command == 0x3fu) {
        adc.vdd++;
        code = (adc.millivolts*2048 + 1725) / 3450;
    } else if (command == 0x3eu) {
        adc.temperature++;
        if (adc.tr0 != 1u || adc.atest != 1u) { adc.unsensed++; code = 0; }
        else code = 1480 + (adc.centi-2500)*45/1000 + (adc.millivolts-3000)/100;
        /* Tests use whole-count temperatures: 25 C plus multiples of 2 C. */
        CHECK((adc.centi-2500) % 200 == 0 && (adc.millivolts-3000) % 100 == 0);
    } else {
        adc.foreign++;
        code = 0;
    }
    if (code > 2047) code = 2047;
    code = (long)((uint16_t)(code << 4));
    SOC_ADCL = (uint8_t)code; SOC_ADCH = (uint8_t)(code >> 8);
    adc.fresh = 1;
    if (adc.stuck != 1u && (adc.stuck != 2u || command != 0x3eu)) SOC_ADCCON1 |= 0x80u;
}

#if defined(CC2530_SHT3X)
/* Bit-level SHT3x-DIS slave on the board.h port 1 lines (open drain: a line
 * is low if the master drives it, P1DIR set with latch 0, or the slave pulls
 * SDA). Single shot 0x2400 measures at once; the next busy read headers are
 * NACKed, then the result is read once. */
volatile uint8_t SOC_P1, SOC_P1DIR, SOC_P1SEL;
#define SDA BOARD_SHT3X_SDA
#define SCL BOARD_SHT3X_SCL
enum { BUS_IDLE, BUS_RX, BUS_ACK, BUS_TX, BUS_MASTER_ACK };
static struct {
    uint8_t present, stuck, corrupt, ready;
    uint16_t busy, wait, t, h, command;
    uint8_t out[6], index, mode, bits, byte, first, reading, ncommand, pull, sda, scl, acked;
    unsigned starts, stops, measures, results, nacked, unknown, reads, latch;
} sht;

static uint8_t sht_crc(uint16_t word)
{
    uint8_t c = 0xffu, i, b;
    for (b = 0; b < 2; b++) {
        c ^= (uint8_t)(b ? word : word >> 8);
        for (i = 0; i < 8; i++) c = c & 0x80u ? (uint8_t)(c << 1 ^ 0x31u) : (uint8_t)(c << 1);
    }
    return c;
}

static uint8_t level(uint8_t mask)
{
    if ((SOC_P1DIR & mask) && !(SOC_P1 & mask)) return 0;
    if (mask == SCL) return !sht.stuck;
    return !sht.pull;
}

static void sht_received(void)
{
    uint8_t ack = 0, b = sht.byte;
    if (sht.first) {
        sht.first = 0;
        if (sht.present && (b >> 1) == BOARD_SHT3X_ADDRESS) {
            if (!(b & 1u)) { ack = 1; sht.reading = 0; sht.ncommand = 0; sht.command = 0; }
            else if (sht.ready && !sht.wait) { ack = 1; sht.reading = 1; sht.index = 0; sht.ready = 0; sht.reads++; }
            else { if (sht.wait) sht.wait--; sht.nacked++; }
        }
    } else if (sht.ncommand < 2u) {
        ack = 1;
        sht.command = (uint16_t)(sht.command << 8 | b);
        if (++sht.ncommand == 2u) {
            if (sht.command == 0x2400u) {
                sht.out[0] = (uint8_t)(sht.t >> 8); sht.out[1] = (uint8_t)sht.t; sht.out[2] = sht_crc(sht.t);
                sht.out[3] = (uint8_t)(sht.h >> 8); sht.out[4] = (uint8_t)sht.h; sht.out[5] = sht_crc(sht.h);
                if (sht.corrupt) { sht.out[5] ^= 1u; sht.corrupt--; }
                sht.ready = 1; sht.wait = sht.busy; sht.measures++;
            } else sht.unknown++;
        }
    } else sht.unknown++;
    sht.pull = ack;
    sht.mode = ack ? BUS_ACK : BUS_IDLE;
}

static void sht_transmit(void)
{
    sht.byte = sht.out[sht.index++]; sht.bits = 0;
    sht.pull = !(sht.byte & 0x80u);
    sht.mode = BUS_TX;
}

static void sht_bus(void)
{
    uint8_t sda = level(SDA), scl = level(SCL);
    if (scl && sht.scl && sda != sht.sda) {
        if (!sda) { sht.starts++; sht.mode = BUS_RX; sht.bits = 0; sht.byte = 0; sht.first = 1; }
        else { sht.stops++; sht.mode = BUS_IDLE; }
        sht.pull = 0;
    } else if (scl && !sht.scl) {
        if (sht.mode == BUS_RX && sht.bits < 8u) { sht.byte = (uint8_t)(sht.byte << 1 | sda); sht.bits++; }
        else if (sht.mode == BUS_MASTER_ACK) sht.acked = !sda;
    } else if (!scl && sht.scl) {
        if (sht.mode == BUS_RX && sht.bits == 8u) sht_received();
        else if (sht.mode == BUS_ACK) {
            sht.pull = 0;
            if (sht.reading) sht_transmit();
            else { sht.mode = BUS_RX; sht.bits = 0; sht.byte = 0; }
        } else if (sht.mode == BUS_TX) {
            if (++sht.bits == 8u) { sht.pull = 0; sht.mode = BUS_MASTER_ACK; }
            else sht.pull = !((uint8_t)(sht.byte << sht.bits) & 0x80u);
        } else if (sht.mode == BUS_MASTER_ACK) {
            if (sht.index == 6u) sht.results++;
            if (sht.acked && sht.index < 6u) sht_transmit();
            else { sht.mode = BUS_IDLE; sht.pull = 0; }
        }
    }
    sht.sda = level(SDA); sht.scl = level(SCL);
}

static void sht_set(double centi, double humidity, uint8_t present)
{
    memset(&sht, 0, sizeof(sht));
    sht.present = present;
    sht.t = (uint16_t)((centi/100.0 + 45.0) / 175.0 * 65535.0 + 0.5);
    sht.h = (uint16_t)(humidity/100.0 / 100.0 * 65535.0 + 0.5);
    sht.sda = sht.scl = 1;
}
#endif

uint8_t host_mmio_load(const volatile uint8_t *reg, uint8_t address)
{
    uint8_t v = *reg;
    if (address == SOC_ADCH_ADDRESS) { SOC_ADCCON1 &= 0x7fu; adc.fresh = 0; }
#if defined(CC2530_SHT3X)
    if (address == SOC_P1_ADDRESS) v = (uint8_t)((v & (uint8_t)~(SDA | SCL)) | (level(SDA) ? SDA : 0) | (level(SCL) ? SCL : 0));
#endif
    return v;
}

void host_mmio_store(volatile uint8_t *reg, uint8_t address, uint8_t value)
{
    *reg = value;
    if (address == SOC_ADCCON3_ADDRESS) convert_model(value);
#if defined(CC2530_SHT3X)
    if (address == SOC_P1_ADDRESS && (value & (SDA | SCL))) sht.latch++;
    if (address == SOC_P1DIR_ADDRESS || address == SOC_P1_ADDRESS) sht_bus();
#endif
}

uint8_t host_mmio_xload(uint16_t address)
{
    CHECK(address == 0x61bdu);
    return adc.atest;
}

void host_mmio_xstore(uint16_t address, uint8_t value)
{
    CHECK(address == 0x61bdu || address == 0x624bu);
    if (address == 0x61bdu) adc.atest = value; else adc.tr0 = value;
}

static void adc_set(long millivolts, long centi, uint8_t stuck)
{
    uint8_t atest = adc.atest;
    memset(&adc, 0, sizeof(adc));
    adc.millivolts = millivolts; adc.centi = centi; adc.stuck = stuck; adc.atest = atest;
    SOC_ADCCON1 = 0x30u;
}
#endif

static void reset(uint32_t now)
{
    security_keys_config_t identity;
    memset(&join_smoke_device, 0, sizeof(join_smoke_device));
    join_smoke_device.config.transport.profile = 0x0104;
    join_smoke_device.config.transport.endpoint = 1;
    memcpy(join_smoke_device.config.association.extraction.local, own, 8);
    memcpy(join_smoke_device.record.association.source_ieee, tc, 8);
    CTX.last = now;
    memset(&identity, 0, sizeof(identity));
    memcpy(identity.own_ieee, own, 8); memcpy(identity.tc_ieee, tc, 8);
    CHECK(zcl_sensor_init(&identity));
    CHECK(!memcmp(ST.tc_ieee, tc, 8) && ST.basic && ST.bound[0] == 1 && ST.bound[1] == 1 && ST.bound[2] == 1);
}

static uint8_t sensor_poll(void)
{
    zcl_sensor_tick();
    return zcl_sensor_serve();
}

static uint8_t receive(uint16_t cluster, const void *payload, uint8_t length)
{
    uint8_t result;
    memset(&CTX.application, 0, sizeof(CTX.application));
    CTX.application.nwk.source = 0x0000; CTX.application.nwk.destination = 0x4321;
    CTX.application.aps.profile_id = 0x0104; CTX.application.aps.cluster_id = cluster;
    CTX.application.aps.source_endpoint = 9; CTX.application.aps.destination_endpoint = 1;
    memcpy(CTX.application.payload, payload, length); CTX.application.length = length;
    CTX.application_ready = 1;
    result = sensor_poll();
    CTX.application_ready = 0;
    memset(&CTX.application, 0, sizeof(CTX.application));
    return result;
}

static uint8_t zdo(uint16_t cluster, const void *payload, uint8_t length)
{
    uint8_t result;
    memset(&CTX.application, 0, sizeof(CTX.application));
    CTX.application.nwk.source = 0x0000; CTX.application.nwk.destination = 0x4321;
    CTX.application.aps.cluster_id = cluster;
    memcpy(CTX.application.payload, payload, length); CTX.application.length = length;
    CTX.application_ready = 1;
    result = zcl_sensor_serve();
    CTX.application_ready = 0;
    return result;
}

static void reply(uint16_t cluster, const void *expected, uint8_t length)
{
    if (CTX.response_pending && CTX.response.length != length)
        fprintf(stderr, "response length %u instead of %u, command %02x, status %02x\n",
                CTX.response.length, length, CTX.response.payload[2], CTX.response.payload[4]);
    CHECK(CTX.response_pending && CTX.response.length == length);
    CHECK(!memcmp(CTX.response.payload, expected, length));
    CHECK(CTX.response.aps.cluster_id == cluster && !CTX.response.nwk.destination);
    CHECK(CTX.response.nwk.version == 2 && CTX.response.nwk.radius == 30 && !CTX.response.aps.flags);
    CHECK(CTX.response.nwk.discover_route == NWK_DISCOVER_ROUTE_ENABLE);
    if (cluster & 0x8000u) CHECK(!CTX.response.aps.profile_id && !CTX.response.aps.destination_endpoint);
    else CHECK(CTX.response.aps.profile_id == 0x0104 && CTX.response.aps.source_endpoint == 1 &&
               CTX.response.aps.destination_endpoint == 9);
    CTX.response_pending = 0;
}

static void zcl(uint16_t cluster, const void *request, uint8_t length, const void *expected, uint8_t reply_length)
{
    CHECK(receive(cluster, request, length) == ZCL_SENSOR_HANDLED);
    if (reply_length) reply(cluster, expected, reply_length);
    else CHECK(!CTX.response_pending);
}

static void discarded(uint16_t cluster, const void *request, uint8_t length);

static void observational(uint16_t cluster, const void *request, uint8_t length,
                          const void *expected, uint8_t reply_length)
{
    zcl_sensor_state_t before = ST;
    uint8_t i;
    zcl(cluster, request, length, expected, reply_length);
    CHECK(!memcmp(ST.report, before.report, sizeof(ST.report)));
    CHECK(!memcmp(ST.bound, before.bound, sizeof(ST.bound)));
    CHECK(!memcmp(ST.tc_ieee, before.tc_ieee, sizeof(ST.tc_ieee)));
    CHECK(ST.second == before.second && ST.identify == before.identify && ST.value == before.value);
    CHECK(ST.basic == before.basic && ST.pending == before.pending && ST.sent == before.sent && ST.tsn == before.tsn);
    CHECK(!join_smoke_device.application_pending && !join_smoke_device.application_done);
    for (i = reply_length; i < ED_PAYLOAD_MAX; i++) CHECK(!CTX.response.payload[i]);
}

static void standalone_read(const uint8_t *request, uint8_t length)
{
    zcl_temp_t temperature, before;
    zcl_temp_cfg_t defaults = {30, 300, 10};
    zcl_dispatch_info_t info;
    uint8_t output[ED_PAYLOAD_MAX];
    CHECK(zcl_temp_init(&temperature, 0, 2100, 2500, &defaults) == ZCL_CODEC_OK);
    temperature.reporting.minimum = ST.report[0].min;
    temperature.reporting.maximum = ST.report[0].max;
    temperature.reporting.change = (int16_t)ST.report[0].change;
    temperature.configured = ST.report[0].max != 0xffffu;
    before = temperature;
    CHECK(zcl_temp_rx(&temperature, 0, request, length, output, sizeof(output), &info) == ZCL_CODEC_OK);
    CHECK(!memcmp(&temperature, &before, sizeof(before)));
    observational(0x0402, request, length, output, info.length);
}

static void read_reporting(void)
{
    static const uint16_t clusters[] = {0x0402, 0x0405, 0x0001, 0x0001};
    static const uint16_t ids[] = {0, 0, 0x20, 0x21};
    static const uint8_t types[] = {0x29, 0x21, 0x20, 0x20};
    static const uint8_t gold[][14] = {
        {0x18,0x80,0x09,0,0,0,0,0x29,30,0,0x2c,1,10,0},
        {0x18,0x80,0x09,0,0,0,0,0x21,30,0,0x2c,1,25,0},
        {0x18,0x80,0x09,0,0,0x20,0,0x20,60,0,0x10,0x0e,1},
        {0x18,0x80,0x09,0,0,0x21,0,0x20,60,0,0x10,0x0e,2}
    };
    uint8_t request[ED_PAYLOAD_MAX], expected[ED_PAYLOAD_MAX], i, n, at, count, length;
    ed_packet_t occupied;
    reset(0);
    for (i = 0; i < 4; i++) {
        memcpy(request, "\x00\x80\x08\x00\x00\x00", 6);
        request[4] = (uint8_t)ids[i];
        observational(clusters[i], request, 6, gold[i], i < 2 ? 14 : 13);
        request[0] = 0x10;
        observational(clusters[i], request, 6, gold[i], i < 2 ? 14 : 13);
        if (!i) standalone_read(request, 6);
        request[0] = 0; request[1] = 0x81; request[2] = 6;
        request[6] = types[i]; request[7] = 7; request[8] = 0;
        request[9] = 90; request[10] = 0; request[11] = 0x34; request[12] = 0x12;
        zcl(clusters[i], request, i < 2 ? 13 : 12, "\x18\x81\x07\x00", 4);
        CHECK(ST.report[i].min == 7 && ST.report[i].max == 90 &&
              ST.report[i].change == (i < 2 ? 0x1234 : 0x34));
        memcpy(expected, gold[i], sizeof(gold[i]));
        expected[1] = 0x82; expected[8] = 7; expected[10] = 90; expected[11] = 0;
        expected[12] = 0x34; expected[13] = 0x12;
        request[1] = 0x82; request[2] = 8;
        observational(clusters[i], request, 6, expected, i < 2 ? 14 : 13);
        if (!i) standalone_read(request, 6);
        request[1] = 0x83; request[2] = 6;
        request[9] = request[10] = 0xff;
        zcl(clusters[i], request, i < 2 ? 13 : 12, "\x18\x83\x07\x00", 4);
        request[1] = 0x84; request[2] = 8;
        memcpy(expected, "\x18\x84\x09\x8b\x00\x00\x00", 7); expected[5] = (uint8_t)ids[i];
        observational(clusters[i], request, 6, expected, 7);
        if (!i) standalone_read(request, 6);
        request[1] = 0x85; request[2] = 6;
        request[7] = request[8] = 0xff; request[9] = request[10] = 0;
        zcl(clusters[i], request, i < 2 ? 13 : 12, "\x18\x85\x07\x00", 4);
        request[1] = 0x80; request[2] = 8;
        observational(clusters[i], request, 6, gold[i], i < 2 ? 14 : 13);
    }
    reset(0);
    memcpy(request, "\x00\x86\x08\x00\x00\x00\x00\x01\x00\x00\x09\x00\x01\x00\x00", 15);
    memcpy(expected, gold[0], 14); expected[1] = 0x86;
    memcpy(expected+14, "\x8c\x00\x01\x00\x86\x00\x09\x00\x86\x01\x00\x00", 12);
    observational(0x0402, request, 15, expected, 26);
    standalone_read(request, 15);
    observational(0x0000, "\x00\x87\x08\x00\x05\x00", 6, "\x18\x87\x09\x8c\x00\x05\x00", 7);
    observational(0x0001, "\x00\x88\x08\x00\x20\x00\x00\x21\x00", 9,
        "\x18\x88\x09\x00\x00\x20\x00\x20\x3c\x00\x10\x0e\x01"
        "\x00\x00\x21\x00\x20\x3c\x00\x10\x0e\x02", 23);
    observational(0x0402, "\x00\x89\x08\x01\x00\x00\x01\x01\x00\x01\xff\xff", 12,
        "\x18\x89\x09\x86\x01\x00\x00\x86\x01\x01\x00\x86\x01\xff\xff", 15);
    /* Full request validation precedes even a fitting response prefix. */
    for (length = 3; length <= ED_PAYLOAD_MAX; length++) {
        memset(request, 0, sizeof(request)); request[1] = 0x8a; request[2] = 8;
        if (length > 3 && !((length-3u)%3u)) {
            at = 3;
            while ((uint8_t)(at+11u) <= ED_PAYLOAD_MAX) {
                memcpy(expected+at, gold[0]+3, 11); at += 11;
                if ((uint8_t)((at-3u)/11u) == (length-3u)/3u) break;
            }
            memcpy(expected, "\x18\x8a\x09", 3);
            observational(0x0402, request, length, expected, at);
            standalone_read(request, length);
        } else {
            observational(0x0402, request, length, "\x18\x8a\x0b\x08\x80", 5);
        }
    }
    /* Exactly82 output bytes: a complete success, then17 error records. */
    memcpy(request, "\x00\x8b\x08", 3); memcpy(expected, "\x18\x8b\x09", 3);
    n = 1; while ((ED_PAYLOAD_MAX-3u-11u*n)%4u) n++;
    count = (uint8_t)(n+(ED_PAYLOAD_MAX-3u-11u*n)/4u); at = 3;
    for (i = 0; i < count; i++) {
        request[3u+3u*i] = 0; request[4u+3u*i] = i < n ? 0 : 9; request[5u+3u*i] = 0;
        if (i < n) { memcpy(expected+at, gold[0]+3, 11); at += 11; }
        else { memcpy(expected+at, "\x86\x00\x09\x00", 4); at += 4; }
    }
    CHECK(at == ED_PAYLOAD_MAX);
    length = (uint8_t)(3u+3u*count);
    observational(0x0402, request, length, expected, at);
    standalone_read(request, length);
    memset(request+length, 0, 3);
    observational(0x0402, request, (uint8_t)(length+3u), expected, at);
    standalone_read(request, (uint8_t)(length+3u));
    request[length] = 2;
    observational(0x0402, request, (uint8_t)(length+3u), "\x18\x8b\x0b\x08\x80", 5);
    request[length] = 0;
    observational(0x0402, request, (uint8_t)(length+1u), "\x18\x8b\x0b\x08\x80", 5);
    for (i = 2; i != 0; i++) {
        request[0] = 0; request[1] = 0x8c; request[2] = 8; request[3] = i;
        request[4] = request[5] = 0;
        observational(0x0402, request, 6, "\x18\x8c\x0b\x08\x80", 5);
    }
    /* Manufacturer namespace remains unsupported; header/code/TSN echo. */
    observational(0x0402, "\x14\x34\x12\x8d\x08\x00\x00\x00", 8,
        "\x1c\x34\x12\x8d\x0b\x08\x81", 7);
    observational(0x0402, "\x08\x8e\x08\x00\x00\x00", 6, "\x18\x8e\x0b\x08\xc3", 5);
    for (i = 0; i < 4; i++) {
        memcpy(request, "\x00\x8f\x08\x00\x00\x00", 6);
        request[0] = (uint8_t)(i*0x10u | 0x60u);
        memcpy(expected, gold[0], 14); expected[1] = 0x8f;
        observational(0x0402, request, 6, expected, 14);
    }
    discarded(0x0402, "\x18\x90\x09\x00\x00\x00\x00", 7);
    discarded(0x0402, "\x00\x90\x09\x00", 4);
    for (i = 0; i < 3; i++) {
        memset(&CTX.application, 0, sizeof(CTX.application));
        CTX.application.aps.profile_id = i == 0 ? 0x0105 : 0x0104;
        CTX.application.aps.cluster_id = 0x0402;
        CTX.application.aps.source_endpoint = 9;
        CTX.application.aps.destination_endpoint = i == 1 ? 2 : 1;
        memcpy(CTX.application.payload, "\x00\x91\x08\x00\x00\x00", 6);
        CTX.application.length = i == 2 ? ED_PAYLOAD_MAX+1u : 6;
        CTX.application_ready = 1;
        CHECK(zcl_sensor_serve() == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
        CTX.application_ready = 0;
    }
    memset(&CTX.response, 0xa5, sizeof(CTX.response)); occupied = CTX.response;
    CTX.response_pending = 1;
    CHECK(receive(0x0402, "\x00\x92\x08\x00\x00\x00", 6) == ZCL_SENSOR_DISCARDED);
    CHECK(CTX.response_result == NWK_APS_FULL && !memcmp(&CTX.response, &occupied, sizeof(occupied)));
    CTX.response_pending = 0;
    reset(0);
    for (i = 0; i < 20; i++)
        observational(0x0402, "\x00\x80\x08\x00\x00\x00", 6, gold[0], 14);
}

static void discarded(uint16_t cluster, const void *request, uint8_t length)
{
    CHECK(receive(cluster, request, length) == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
}

/* Returns the next report's attribute and value, committing it as accepted. */
static uint8_t report(uint16_t cluster, uint16_t attribute, uint8_t type, uint16_t value)
{
    uint8_t width = type == 0x20u ? 1u : 2u;
    if (sensor_poll() != ZCL_SENSOR_REPORT) return 0;
    CHECK(CTX.application.aps.cluster_id == cluster && CTX.application.aps.profile_id == 0x0104);
    CHECK(CTX.application.aps.flags == APS_FLAG_ACK_REQUEST && !CTX.application.nwk.destination);
    CHECK(CTX.application.aps.source_endpoint == 1 && CTX.application.aps.destination_endpoint ==
          (cluster ? ST.bound[cluster == 0x0402u ? 0 : cluster == 0x0405u ? 1 : 2] : 1));
    CHECK(CTX.application.length == 6u+width && CTX.application.payload[0] == 0x18);
    CHECK(CTX.application.payload[1] == ST.tsn && CTX.application.payload[2] == 0x0a);
    CHECK(CTX.application.payload[3] == (uint8_t)attribute && CTX.application.payload[4] == attribute >> 8);
    CHECK(CTX.application.payload[5] == type && CTX.application.payload[6] == (uint8_t)value);
    if (width == 2u) CHECK(CTX.application.payload[7] == value >> 8);
    memset(&CTX.application, 0, sizeof(CTX.application));
    ST.sent = 1;
    return 1;
}

static void idle(void)
{
    CHECK(sensor_poll() == ZCL_SENSOR_IDLE && !CTX.application.length);
}

static void values(void)
{
    uint8_t s;
    CHECK(ZCL_SENSOR_SAMPLE(0) == 0 && ZCL_SENSOR_SAMPLE((1ul << 22)-1) == 0);
    CHECK(ZCL_SENSOR_SAMPLE(1ul << 22) == 1 && ZCL_SENSOR_SAMPLE(0x3ffffffful) == 255);
    CHECK(ZCL_SENSOR_SAMPLE(0x40000000ul) == 0 && ZCL_SENSOR_SAMPLE(0xfffffffful) == 255);
    for (s = 0; s < 255; s++) {
        uint8_t p = ZCL_SENSOR_PHASE(s);
        CHECK(p == s % 41);
        CHECK(ZCL_SENSOR_TEMPERATURE(p) == 2100 + 10*p && ZCL_SENSOR_HUMIDITY(p) == 4500 + 25*p);
        CHECK(ZCL_SENSOR_PERCENTAGE(p) == 200 - p && ZCL_SENSOR_VOLTAGE(p) == 30 - p/14);
    }
    CHECK(ZCL_SENSOR_TEMPERATURE(40) == 2500 && ZCL_SENSOR_HUMIDITY(40) == 5500);
    CHECK(ZCL_SENSOR_PERCENTAGE(40) == 160 && ZCL_SENSOR_VOLTAGE(40) == 28);
    /* Chip sense conversions: 1781 per sample is 3.0 V on AVDD5/3. */
    CHECK(ZCL_SENSOR_MILLIVOLTS(0) == 0 && ZCL_SENSOR_MILLIVOLTS(4*1781) == 3000);
    CHECK(ZCL_SENSOR_MILLIVOLTS(4*2047) == 3448 && ZCL_SENSOR_MILLIVOLTS(4*1187) == 2000);
    CHECK(ZCL_SENSOR_COUNTS(11840, 3000) == 0 && ZCL_SENSOR_COUNTS(11848, 3100) == 0);
    CHECK(ZCL_SENSOR_COUNTS(11840, 2000) == 80);
    CHECK(ZCL_SENSOR_CENTI(11840, 3000) == 2500 + BOARD_CHIP_TEMPERATURE_OFFSET);
    CHECK(ZCL_SENSOR_CENTI(11840+36*10, 3000) == 3500 + BOARD_CHIP_TEMPERATURE_OFFSET);
    /* 711/256 approximates 25/9 within 0.02 %. */
    CHECK(abs(ZCL_SENSOR_CENTI(11840-36*35, 3000) - (-1000 + BOARD_CHIP_TEMPERATURE_OFFSET)) <= 1);
    CHECK(abs(ZCL_SENSOR_CENTI(11840+36*60, 3000) - (8500 + BOARD_CHIP_TEMPERATURE_OFFSET)) <= 2);
    CHECK(ZCL_SENSOR_DEGREES(2549) == 25 && ZCL_SENSOR_DEGREES(2550) == 26 && ZCL_SENSOR_DEGREES(0) == 0);
    CHECK(ZCL_SENSOR_DEGREES(-2549) == -25 && ZCL_SENSOR_DEGREES(-2550) == -26 && ZCL_SENSOR_DEGREES(-49) == 0);
    CHECK(ZCL_SENSOR_DEGREES(ZCL_SENSOR_INVALID_TEMPERATURE) == ZCL_SENSOR_INVALID_TEMPERATURE);
    CHECK(ZCL_SENSOR_DECIVOLTS(0) == 0xff && ZCL_SENSOR_DECIVOLTS(2949) == 29 && ZCL_SENSOR_DECIVOLTS(2950) == 30);
    CHECK(ZCL_SENSOR_HALF_PERCENT(0) == 0xff && ZCL_SENSOR_HALF_PERCENT(1) == 0);
    CHECK(ZCL_SENSOR_HALF_PERCENT(BOARD_BATTERY_EMPTY_MV) == 0);
    CHECK(ZCL_SENSOR_HALF_PERCENT(BOARD_BATTERY_FULL_MV) == 200 && ZCL_SENSOR_HALF_PERCENT(3600) == 200);
    CHECK(ZCL_SENSOR_HALF_PERCENT((BOARD_BATTERY_EMPTY_MV+BOARD_BATTERY_FULL_MV)/2) == 100);
}

#if !defined(CC2530_CHIP_SENSE)
static void reads(void)
{
    static const uint8_t basic[] = {0x00, 0x31, 0x00, 0x04, 0x00, 0x05, 0x00, 0x00, 0x00, 0x07, 0x00,
        0xfd, 0xff, 0x34, 0x12};
    static const uint8_t sensor[] = {0x00, 0x32, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0xfd, 0xff, 0x03, 0x00};
    static const uint8_t power[] = {0x00, 0x33, 0x00, 0x20, 0x00, 0x21, 0x00, 0xfd, 0xff, 0x00, 0x00};
    static const uint8_t odd[] = {0x10, 0x34, 0x00, 0x05, 0x00, 0x04};
    uint8_t expected[ED_PAYLOAD_MAX], request[ED_PAYLOAD_MAX], at = 3, i;
    reset(5ul << 22);
    expected[0] = 0x18; expected[1] = 0x31; expected[2] = 0x01;
    expected[at++] = 4; expected[at++] = 0; expected[at++] = 0; expected[at++] = 0x42;
    expected[at++] = sizeof(manufacturer)-1; memcpy(expected+at, manufacturer, sizeof(manufacturer)-1);
    at += sizeof(manufacturer)-1;
    expected[at++] = 5; expected[at++] = 0; expected[at++] = 0; expected[at++] = 0x42;
    expected[at++] = sizeof(model)-1; memcpy(expected+at, model, sizeof(model)-1); at += sizeof(model)-1;
    memcpy(expected+at, "\x00\x00\x00\x20\x08\x07\x00\x00\x30\x03\xfd\xff\x00\x21\x03\x00\x34\x12\x86", 19);
    zcl(0x0000, basic, sizeof(basic), expected, (uint8_t)(at+19));
    zcl(0x0402, sensor, sizeof(sensor),
        "\x18\x32\x01\x00\x00\x00\x29\x66\x08\x01\x00\x00\x29\x34\x08\x02\x00\x00\x29\xc4\x09"
        "\xfd\xff\x00\x21\x03\x00\x03\x00\x86", 30);
    memcpy(expected, "\x18\x32\x01\x00\x00\x00\x21\x00\x00\x01\x00\x00\x21\x94\x11\x02\x00\x00\x21\x7c\x15"
        "\xfd\xff\x00\x21\x02\x00\x03\x00\x86", 30);
    expected[7] = (uint8_t)(4500+25*5); expected[8] = (uint8_t)((4500+25*5) >> 8);
    zcl(0x0405, sensor, sizeof(sensor), expected, 30);
    zcl(0x0001, power, sizeof(power),
        "\x18\x33\x01\x20\x00\x00\x20\x1e\x21\x00\x00\x20\xc3\xfd\xff\x00\x21\x02\x00\x00\x00\x86", 22);
    zcl(0x0003, "\x00\x35\x00\x00\x00\xfd\xff", 7, "\x18\x35\x01\x00\x00\x00\x21\x00\x00\xfd\xff\x00\x21\x02\x00", 15);
    /* Device Temperature: whole degrees of the temperature value (21.50 -> 22). */
    zcl(0x0002, "\x00\x37\x00\x00\x00\xfd\xff\x01\x00", 9,
        "\x18\x37\x01\x00\x00\x00\x29\x16\x00\xfd\xff\x00\x21\x01\x00\x01\x00\x86", 18);
    zcl(0x0002, "\x00\x38\x06\x00\x00\x00\x29\x01\x00\x02\x00\x01\x00", 13, "\x18\x38\x07\x8c\x00\x00\x00", 7);
    zcl(0x0002, "\x00\x39\x08\x00\x00\x00", 6, "\x18\x39\x09\x8c\x00\x00\x00", 7);
    /* Disable-default-response does not suppress a Read Attributes Response;
     * a trailing odd octet is ignored. */
    memcpy(expected, "\x18\x34\x01\x05\x00\x00\x42", 7); expected[7] = sizeof(model)-1;
    memcpy(expected+8, model, sizeof(model)-1);
    zcl(0x0000, odd, sizeof(odd), expected, (uint8_t)(8+sizeof(model)-1));
    /* Manufacturer-specific frame header: echoed code and TSN. */
    zcl(0x0000, "\x04\x34\x12\x36\x00\x04\x00", 7, "\x1c\x34\x12\x36\x0b\x00\x81", 7);
    /* Records that would overflow one APS payload are omitted, not split. */
    request[0] = 0; request[1] = 0x3c; request[2] = 0;
    for (i = 3; i+1u < ED_PAYLOAD_MAX; i += 2) { request[i] = 7; request[i+1] = 0x10; }
    memcpy(expected, "\x18\x3c\x01", 3); at = 3;
    while (at+3u <= ED_PAYLOAD_MAX) { expected[at++] = 7; expected[at++] = 0x10; expected[at++] = 0x86; }
    zcl(0x0000, request, ED_PAYLOAD_MAX-1, expected, at);
    for (i = 3; i+1u < ED_PAYLOAD_MAX; i += 2) { request[i] = 5; request[i+1] = 0; }
    at = 3;
    while (at+5u+sizeof(model)-1u <= ED_PAYLOAD_MAX) {
        expected[at++] = 5; expected[at++] = 0; expected[at++] = 0; expected[at++] = 0x42;
        expected[at++] = sizeof(model)-1; memcpy(expected+at, model, sizeof(model)-1); at += sizeof(model)-1;
    }
    zcl(0x0000, request, ED_PAYLOAD_MAX-1, expected, at);
}
#endif

static void defaults_and_discards(void)
{
    uint8_t length;
    reset(0);
    /* Unsupported cluster, server-to-client direction, general commands. */
    zcl(0x0006, "\x00\x40\x00\x00\x00", 5, "\x18\x40\x0b\x00\xc3", 5);
    zcl(0x0402, "\x08\x41\x00\x00\x00", 5, "\x18\x41\x0b\x00\xc3", 5);
    zcl(0x0402, "\x00\x43\x0c\x00\x00\x05", 6, "\x18\x43\x0b\x0c\x81", 5);
    zcl(0x0402, "\x01\x44\x00", 3, "\x18\x44\x0b\x00\x81", 5);
    zcl(0x0003, "\x01\x45\x40\x00\x00", 5, "\x18\x45\x0b\x40\x81", 5);
    zcl(0x0003, "\x01\x46\x00\x05", 4, "\x18\x46\x0b\x00\x80", 5);
    /* Errors are sent even with the disable-default-response bit. */
    zcl(0x0402, "\x10\x47\x0c\x00\x00\x00", 6, "\x18\x47\x0b\x0c\x81", 5);
    /* Silent: wrong profile/endpoint, empty, reserved type, short header, a
     * received Default Response, broadcast and group delivery. */
    CHECK(receive(0x0402, "\x02\x48\x00", 3) == ZCL_SENSOR_DISCARDED);
    discarded(0x0402, "", 0);
    discarded(0x0402, "\x00\x49", 2);
    discarded(0x0402, "\x04\x49\x12\x34", 4);
    discarded(0x0402, "\x00\x4a\x0b\x00\x00", 5);
    discarded(0x0402, "\x18\x4a\x0b\x00\x00", 5);
    for (length = 0; length < 4; length++) {
        memset(&CTX.application, 0, sizeof(CTX.application));
        CTX.application.aps.profile_id = length == 0 ? 0x0105 : 0x0104;
        CTX.application.aps.cluster_id = 0x0402;
        CTX.application.aps.source_endpoint = 9;
        CTX.application.aps.destination_endpoint = length == 1 ? 2 : 1;
        CTX.application.nwk.destination = length == 2 ? 0xfffd : 0x4321;
        CTX.application.aps.delivery_mode = length == 3 ? APS_DELIVERY_GROUP : 0;
        memcpy(CTX.application.payload, "\x00\x4b\x00\x00\x00", 5); CTX.application.length = 5;
        CTX.application_ready = 1;
        CHECK(zcl_sensor_serve() == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
        CTX.application_ready = 0;
    }
    /* A busy response slot drops the request; the slot is untouched. */
    CTX.response_pending = 1; CTX.response.length = 77;
    CHECK(receive(0x0402, "\x00\x4c\x00\x00\x00", 5) == ZCL_SENSOR_DISCARDED);
    CHECK(CTX.response_result == NWK_APS_FULL && CTX.response.length == 77);
    CTX.response_pending = 0;
    /* ZDO frames other than Bind/Unbind are not the application's. */
    memset(&CTX.application, 0, sizeof(CTX.application));
    CTX.application.aps.cluster_id = 0x0005; CTX.application.length = 3; CTX.application_ready = 1;
    CHECK(zcl_sensor_serve() == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
    CTX.application_ready = 0;
}

static void identify(void)
{
    reset(0);
    zcl(0x0003, "\x01\x50\x01", 3, NULL, 0);
    zcl(0x0003, "\x00\x51\x02\x00\x00\x21\x0a\x00", 8, "\x18\x51\x04\x00", 4);
    CHECK(ST.identify == 10);
    zcl(0x0003, "\x01\x52\x01", 3, "\x19\x52\x00\x0a\x00", 5);
    CTX.last = 3*ZCL_SENSOR_SECOND+5;
    zcl(0x0003, "\x01\x53\x01", 3, "\x19\x53\x00\x07\x00", 5);
    zcl(0x0003, "\x01\x54\x00\x02\x00", 5, "\x18\x54\x0b\x00\x00", 5);
    CHECK(ST.identify == 2);
    zcl(0x0003, "\x11\x55\x00\x00\x00", 5, NULL, 0);
    CHECK(!ST.identify);
    zcl(0x0003, "\x01\x56\x01", 3, NULL, 0);
    CTX.last += 100ul*ZCL_SENSOR_SECOND;
    zcl(0x0003, "\x01\x57\x01", 3, NULL, 0);
    CHECK(!ST.identify && ST.report[0].age == 103);
    /* Write: unsupported, wrong type, read-only; malformed and validated
     * before any record is applied. */
    zcl(0x0003, "\x00\x58\x02\x00\x00\x21\x05\x00\x09\x00\x21\x01\x00\x00\x00\x20\x01", 17,
        "\x18\x58\x04\x86\x09\x00\x8d\x00\x00", 9);
    CHECK(ST.identify == 5);
    zcl(0x0402, "\x00\x59\x02\x00\x00\x29\x01\x00", 8, "\x18\x59\x04\x88\x00\x00", 6);
    zcl(0x0000, "\x00\x5a\x02\x05\x00\x42\x01\x41", 8, "\x18\x5a\x04\x88\x05\x00", 6);
    zcl(0x0003, "\x00\x5b\x02\x00\x00\x21\x09\x00\x00\x00\x21\x01", 12, "\x18\x5b\x0b\x02\x80", 5);
    zcl(0x0003, "\x00\x5c\x02\x00\x00\x48\x00", 7, "\x18\x5c\x0b\x02\x80", 5);
    CHECK(ST.identify == 5);
    zcl(0x0003, "\x10\x5d\x02\x00\x00\x21\x00\x00", 8, "\x18\x5d\x04\x00", 4);
    CHECK(!ST.identify);
}

static void configure(void)
{
    static const uint16_t defaults[4][3] = {{30, 300, 10}, {30, 300, 25}, {60, 3600, 1}, {60, 3600, 2}};
    uint8_t i;
    reset(0);
    for (i = 0; i < 4; i++)
        CHECK(ST.report[i].min == defaults[i][0] && ST.report[i].max == defaults[i][1] &&
              ST.report[i].change == defaults[i][2] && !ST.report[i].age);
    /* The ZHA temperature, humidity and battery configurations. */
    zcl(0x0402, "\x00\x60\x06\x00\x00\x00\x29\x1e\x00\x84\x03\x32\x00", 13, "\x18\x60\x07\x00", 4);
    zcl(0x0405, "\x00\x61\x06\x00\x00\x00\x21\x1e\x00\x84\x03\x64\x00", 13, "\x18\x61\x07\x00", 4);
    zcl(0x0001, "\x00\x62\x06\x00\x21\x00\x20\x10\x0e\x30\x2a\x01", 12, "\x18\x62\x07\x00", 4);
    CHECK(ST.report[0].min == 30 && ST.report[0].max == 900 && ST.report[0].change == 50);
    CHECK(ST.report[1].min == 30 && ST.report[1].max == 900 && ST.report[1].change == 100);
    CHECK(ST.report[3].min == 3600 && ST.report[3].max == 10800 && ST.report[3].change == 1);
    /* Per-record failures: wrong type, max below min, not reportable,
     * unsupported attribute, received direction; the valid record applies. */
    zcl(0x0402, "\x00\x63\x06\x00\x00\x00\x21\x01\x00\x02\x00\x01\x00"
        "\x00\x00\x00\x29\x05\x00\x04\x00\x01\x00"
        "\x00\x01\x00\x29\x01\x00\x02\x00\x01\x00"
        "\x00\x09\x00\x29\x01\x00\x02\x00\x01\x00"
        "\x01\x00\x00\x0a\x00"
        "\x00\x00\x00\x29\x00\x00\x00\x00\x07\x00", 58,
        "\x18\x63\x07\x8d\x00\x00\x00\x87\x00\x00\x00\x8c\x00\x01\x00\x86\x00\x09\x00\x8c\x01\x00\x00", 23);
    CHECK(!ST.report[0].min && !ST.report[0].max && ST.report[0].change == 7);
    /* Malformed: nothing applies. */
    zcl(0x0402, "\x00\x64\x06\x00\x00\x00\x29\x01\x00\x02\x00\x03\x00\x02\x00\x00", 16,
        "\x18\x64\x0b\x06\x80", 5);
    zcl(0x0402, "\x00\x65\x06\x00\x00\x00\x29\x01\x00\x02\x00\x03", 12, "\x18\x65\x0b\x06\x80", 5);
    zcl(0x0402, "\x00\x66\x06\x00\x00\x00", 6, "\x18\x66\x0b\x06\x80", 5);
    CHECK(!ST.report[0].min && !ST.report[0].max && ST.report[0].change == 7);
    /* A non-integer reportable type has no parsed change field. */
    zcl(0x0001, "\x00\x67\x06\x00\x20\x00\x30\x01\x00\x02\x00", 11, "\x18\x67\x07\x8d\x00\x20\x00", 7);
    /* Restore defaults and disable. */
    zcl(0x0402, "\x10\x68\x06\x00\x00\x00\x29\xff\xff\x00\x00\x00\x00", 13, "\x18\x68\x07\x00", 4);
    CHECK(ST.report[0].min == 30 && ST.report[0].max == 300 && ST.report[0].change == 10);
    zcl(0x0405, "\x00\x69\x06\x00\x00\x00\x21\x00\x00\xff\xff\x00\x00", 13, "\x18\x69\x07\x00", 4);
    CHECK(ST.report[1].max == 0xffff);
}

static void bind(void)
{
    uint8_t request[22];
    reset(0);
    request[0] = 0x70; memcpy(request+1, own, 8); request[9] = 1; request[10] = 0x02; request[11] = 0x04;
    request[12] = 3; memcpy(request+13, tc, 8); request[21] = 5;
    /* Bound to endpoint 1 by init: another endpoint is TABLE_FULL. */
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x8c", 2);
    CHECK(zdo(0x0022, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8022, "\x70\x88", 2);
    request[21] = 1;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x00", 2);
    CHECK(zdo(0x0022, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8022, "\x70\x00", 2);
    CHECK(!ST.bound[0] && ST.bound[1] == 1);
    CHECK(zdo(0x0022, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8022, "\x70\x88", 2);
    request[21] = 5;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x00", 2);
    CHECK(ST.bound[0] == 5);
    /* Not the coordinator, unknown cluster, source endpoint, invalid
     * destination endpoint, foreign source and group destination. */
    request[13] ^= 1;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x8c", 2);
    request[13] ^= 1; request[10] = 0x06;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x8c", 2);
    request[10] = 0x01; request[9] = 2;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x82", 2);
    request[9] = 1; request[21] = 0xff;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x82", 2);
    request[21] = 1; request[1] ^= 1;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x84", 2);
    request[1] ^= 1; request[12] = 1;
    CHECK(zdo(0x0021, request, 15) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x84", 2);
    CHECK(ST.bound[0] == 5 && ST.bound[1] == 1 && ST.bound[2] == 1);
    /* Truncated requests are dropped. */
    CHECK(zdo(0x0021, request, 14) == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
    request[12] = 3;
    CHECK(zdo(0x0021, request, 21) == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
    CHECK(zdo(0x0021, request, 12) == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
}

/* Synthetic sawtooth schedule; chip() covers measured reports. */
#if !defined(CC2530_CHIP_SENSE)
static void reports(void)
{
    uint32_t t;
    reset(0);
    /* Basic ModelIdentifier first, rebuilt until accepted. */
    CHECK(sensor_poll() == ZCL_SENSOR_REPORT);
    CHECK(CTX.application.aps.cluster_id == 0 && CTX.application.aps.destination_endpoint == 1);
    CHECK(CTX.application.length == 7u+sizeof(model)-1u && CTX.application.payload[1] == 0);
    CHECK(!memcmp(CTX.application.payload+2, "\x0a\x05\x00\x42", 4) &&
          CTX.application.payload[6] == sizeof(model)-1 &&
          !memcmp(CTX.application.payload+7, model, sizeof(model)-1));
    /* A rebuild keeps its sequence number; acceptance advances it. */
    CHECK(sensor_poll() == ZCL_SENSOR_REPORT && CTX.application.payload[1] == 0 && ST.basic);
    ST.sent = 1; memset(&CTX.application, 0, sizeof(CTX.application));
    idle(); CHECK(!ST.basic && !ST.sent && ST.tsn == 1);
    /* An outstanding application TX or a pending confirmation blocks. */
    CTX.last = 1ul << 22;
    join_smoke_device.application_pending = 1; idle();
    join_smoke_device.application_pending = 0; join_smoke_device.application_done = 1; idle();
    join_smoke_device.application_done = 0;
    /* Phase 1 at 67 s: temperature +10, humidity +25; battery 0.5 % below
     * its change of 2 and voltage unchanged. */
    CHECK(report(0x0402, 0, 0x29, 2110));
    CHECK(report(0x0405, 0, 0x21, 4525));
    idle();
    CHECK(ST.report[0].last == 2110 && !ST.report[0].age && ST.report[1].last == 4525);
    /* Not committed until accepted: unaccepted builds repeat. */
    CTX.last = 2ul << 22;
    CHECK(sensor_poll() == ZCL_SENSOR_REPORT && CTX.application.aps.cluster_id == 0x0402);
    CHECK(CTX.application.payload[1] == 3 && ST.tsn == 3);
    memset(&CTX.application, 0, sizeof(CTX.application));
    CHECK(report(0x0402, 0, 0x29, 2120) && ST.tsn == 3);
    CHECK(report(0x0405, 0, 0x21, 4550) && ST.tsn == 4);
    CHECK(report(0x0001, 0x21, 0x20, 198));
    idle();
    /* Unbound clusters are silent; max interval reports without change. */
    ST.bound[0] = 0;
    CTX.last = 3ul << 22;
    CHECK(report(0x0405, 0, 0x21, 4575));
    idle();
    ST.bound[1] = 0;
    for (t = CTX.last; ST.report[2].age < 3600; ) {
        t += ZCL_SENSOR_SECOND*60; if (ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(t)) > 13) break;
        CTX.last = t;
        while (sensor_poll() == ZCL_SENSOR_REPORT) {
            CHECK(CTX.application.aps.cluster_id == 0x0001 && CTX.application.payload[3] == 0x21);
            memset(&CTX.application, 0, sizeof(CTX.application)); ST.sent = 1;
        }
    }
    CHECK(ZCL_SENSOR_VOLTAGE(ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(CTX.last))) == 30);
    ST.report[2].age = 3599; ST.report[3].max = 0xffff;
    CTX.last += ZCL_SENSOR_SECOND;
    CHECK(report(0x0001, 0x20, 0x20, 30));
    idle();
    /* Change-only (max 0) and min gating. */
    ST.bound[0] = 1; ST.bound[2] = 0;
    zcl(0x0402, "\x00\x70\x06\x00\x00\x00\x29\x05\x00\x00\x00\x01\x00", 13, "\x18\x70\x07\x00", 4);
    ST.report[0].age = 0; ST.report[0].last = 0;
    CTX.last += 4*ZCL_SENSOR_SECOND; idle();
    CTX.last += ZCL_SENSOR_SECOND;
    CHECK(report(0x0402, 0, 0x29, ZCL_SENSOR_TEMPERATURE(ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(CTX.last)))));
    t = CTX.last | ((1ul << 22)-1u);
    CTX.last = t; idle();
    CTX.last = t+1u;
    CHECK(report(0x0402, 0, 0x29, ZCL_SENSOR_TEMPERATURE(ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(CTX.last)))));
    idle();
    /* A served request takes the poll; the report waits. */
    ST.report[0].last = 0; CTX.last += 5*ZCL_SENSOR_SECOND;
    zcl(0x0003, "\x01\x71\x01", 3, NULL, 0);
    CHECK(report(0x0402, 0, 0x29, ZCL_SENSOR_TEMPERATURE(ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(CTX.last)))));
    /* Ages saturate. */
    idle(); CHECK(!ST.report[0].age);
    ST.report[0].age = 0xfffe; ST.report[0].max = 0xffff;
    CTX.last += 5*ZCL_SENSOR_SECOND; sensor_poll();
    CHECK(ST.report[0].age == 0xffff);
}

#endif

static uint8_t binding_golden(uint8_t *output, uint8_t sequence, uint8_t start, uint8_t capacity)
{
    static const uint16_t clusters[] = {0x0402, 0x0405, 0x0001};
    uint8_t i, total = 0, index = 0, at = 5;
    for (i = 0; i < 3; i++) if (ST.bound[i]) total++;
    memcpy(output, "\x91\x00\x00\x00\x00", 5);
    output[0] = sequence; output[2] = total; output[3] = total ? start : 0;
    for (i = 0; i < 3; i++) {
        if (!ST.bound[i]) continue;
        if (index++ < start) continue;
        if (capacity-at < 21) break;
        memcpy(output+at, own, 8); output[at+8] = 1;
        output[at+9] = (uint8_t)clusters[i]; output[at+10] = (uint8_t)(clusters[i] >> 8);
        output[at+11] = 3; memcpy(output+at+12, tc, 8); output[at+20] = ST.bound[i];
        output[4]++; at += 21;
    }
    return at;
}

static void management_query(uint8_t start, uint8_t sequence, uint8_t length)
{
    uint8_t request[ED_PAYLOAD_MAX], expected[ED_PAYLOAD_MAX], size;
    zcl_sensor_state_t before = ST;
    bdb_join_config_t config = join_smoke_device.config;
    nwk_parent_choice_t parent = join_smoke_device.parent;
    memset(request, 0x5a, sizeof(request)); request[0] = sequence; request[1] = start;
    size = binding_golden(expected, sequence, start, ED_PAYLOAD_MAX);
    memset(&CTX.application, 0, sizeof(CTX.application));
    CTX.application.nwk.source = 0x1234; CTX.application.nwk.destination = 0x4321;
    CTX.application.aps.cluster_id = 0x0033;
    CTX.application.aps.flags = APS_FLAG_ACK_REQUEST;
    memcpy(CTX.application.payload, request, length); CTX.application.length = length;
    CTX.application_ready = 1;
    CHECK(zcl_sensor_serve() == ZCL_SENSOR_HANDLED);
    CHECK(CTX.response_pending && CTX.response.length == size);
    CHECK(!memcmp(CTX.response.payload, expected, size));
    CHECK(CTX.response.aps.cluster_id == 0x8033 && !CTX.response.aps.profile_id &&
          !CTX.response.aps.source_endpoint && !CTX.response.aps.destination_endpoint);
    CHECK(CTX.response.nwk.destination == 0x1234 && CTX.response.nwk.version == 2 &&
          CTX.response.nwk.radius == 30 && CTX.response.nwk.discover_route == NWK_DISCOVER_ROUTE_ENABLE);
    CHECK(!memcmp(ST.bound, before.bound, sizeof(ST.bound)));
    CHECK(!memcmp(ST.report, before.report, sizeof(ST.report)));
    CHECK(!memcmp(ST.tc_ieee, before.tc_ieee, 8));
    CHECK(ST.second == before.second && ST.identify == before.identify && ST.value == before.value &&
          ST.basic == before.basic && ST.pending == before.pending && ST.sent == before.sent && ST.tsn == before.tsn);
    CHECK(!memcmp(&join_smoke_device.config, &config, sizeof(config)) &&
          !memcmp(&join_smoke_device.parent, &parent, sizeof(parent)));
    CTX.application_ready = CTX.response_pending = 0;
}

static void management_bind(void)
{
    static const uint16_t clusters[] = {0x0402, 0x0405, 0x0001};
    uint8_t request[22], output[ED_PAYLOAD_MAX+2], expected[ED_PAYLOAD_MAX];
    uint8_t mask, i, start, capacity, size, length;
    zcl_sensor_state_t before;
    ed_packet_t occupied;
    security_keys_config_t identity;
    uint16_t initial;
    reset(0);
    initial = ST.report[0].last;
#if !defined(CC2530_CHIP_SENSE)
    CHECK(initial == 2100);
#endif
    CHECK(zdo(0x0033, "\x91\x00", 2) == ZCL_SENSOR_HANDLED);
    CHECK(CTX.response_pending && CTX.response.length == 68);
    CHECK(!memcmp(CTX.response.payload, "\x91\x00\x03\x00\x03", 5));
    CHECK(!memcmp(CTX.response.payload+5, own, 8));
    CHECK(!memcmp(CTX.response.payload+17, tc, 8));
    CTX.response_pending = 0;
    /* A read is not a report completion or an application timer poll. */
    ST.sent = 1; ST.pending = 0; ST.value = 2340; ST.report[0].age = 17;
    CTX.last += 3*ZCL_SENSOR_SECOND;
    management_query(0, 0x92, 2);
    CHECK(ST.sent == 1 && ST.report[0].age == 17 && ST.report[0].last == initial);
    reset(0);
    for (mask = 0; mask < 8; mask++) {
        reset(0);
        for (i = 0; i < 3; i++) if (!(mask & (1u << i))) {
            request[0] = 0x92; memcpy(request+1, own, 8); request[9] = 1;
            request[10] = (uint8_t)clusters[i]; request[11] = (uint8_t)(clusters[i] >> 8);
            request[12] = 3; memcpy(request+13, tc, 8); request[21] = 1;
            CHECK(zdo(0x0022, request, 22) == ZCL_SENSOR_HANDLED);
            reply(0x8022, "\x92\x00", 2);
        }
        for (start = 0; start < 5; start++) management_query(start, (uint8_t)(0x93+start), 2);
        management_query(255, 0x99, ED_PAYLOAD_MAX);
        for (capacity = 0; capacity <= ED_PAYLOAD_MAX; capacity++) {
            memset(output, 0xa5, sizeof(output)); before = ST;
            size = zcl_sensor_binding_page(0x9a, 0, output+1, capacity);
            CHECK(!memcmp(&ST, &before, sizeof(ST)));
            CHECK(output[0] == 0xa5 && output[ED_PAYLOAD_MAX+1] == 0xa5);
            if (capacity < 5) {
                CHECK(!size);
                for (i = 1; i <= ED_PAYLOAD_MAX; i++) CHECK(output[i] == 0xa5);
            } else {
                length = binding_golden(expected, 0x9a, 0, capacity);
                CHECK(size == length && !memcmp(output+1, expected, size));
                for (i = (uint8_t)(size+1); i <= ED_PAYLOAD_MAX; i++) CHECK(output[i] == 0xa5);
            }
        }
    }
    /* Actual Bind/Unbind owners, including repeated transactions. */
    reset(0);
    request[0] = 0xa0; memcpy(request+1, own, 8); request[9] = 1;
    request[10] = 2; request[11] = 4; request[12] = 3; memcpy(request+13, tc, 8); request[21] = 1;
    CHECK(zdo(0x0022, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8022, "\xa0\x00", 2);
    CHECK(zdo(0x0022, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8022, "\xa0\x88", 2);
    management_query(0, 0xa1, 2);
    request[21] = 9;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\xa0\x00", 2);
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\xa0\x00", 2);
    management_query(0, 0xa2, 2);
    before = ST;
    for (i = 0; i < 5; i++) {
        uint8_t bad[22];
        memcpy(bad, request, sizeof(bad));
        if (!i) bad[10] = 6;
        if (i == 1) bad[1] ^= 1;
        if (i == 2) bad[9] = 2;
        if (i == 3) bad[13] ^= 1;
        if (i == 4) bad[12] = 2;
        CHECK(zdo(0x0021, bad, 22) == ZCL_SENSOR_HANDLED);
        CHECK(CTX.response_pending && CTX.response.payload[1]);
        CHECK(!memcmp(ST.bound, before.bound, sizeof(ST.bound)));
        CTX.response_pending = 0;
        management_query(0, 0xa3, 2);
    }
    for (i = 0; i < 20; i++) management_query(1, (uint8_t)(0xb0+i), 2);
    /* Busy queue and inadmissible contexts never alter live state. */
    for (i = 0; i < 11; i++) {
        memset(&CTX.application, 0, sizeof(CTX.application));
        CTX.application.nwk.destination = 0x4321; CTX.application.aps.cluster_id = 0x0033;
        memcpy(CTX.application.payload, "\xe0\x00", 2); CTX.application.length = 2;
        if (!i) CTX.application.length = 0;
        if (i == 1) CTX.application.length = 1;
        if (i == 2) CTX.application.nwk.destination = 0xfffd;
        if (i == 3) CTX.application.aps.delivery_mode = APS_DELIVERY_BROADCAST;
        if (i == 4) CTX.application.aps.profile_id = 0x0104;
        if (i == 5) CTX.application.aps.destination_endpoint = 1;
        if (i == 6) CTX.application.aps.source_endpoint = 7;
        if (i == 7) CTX.application.aps.type = ED_APS_ACK;
        if (i == 8) CTX.application.aps.cluster_id = 0x8033;
        if (i == 9) CTX.application.length = ED_PAYLOAD_MAX+1u;
        if (i == 10) CTX.application.aps.source_endpoint = 0xff;
        before = ST; CTX.application_ready = 1;
        CHECK(zcl_sensor_serve() == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
        CHECK(!memcmp(ST.bound, before.bound, sizeof(ST.bound)) &&
              !memcmp(ST.report, before.report, sizeof(ST.report)));
        CTX.application_ready = 0;
    }
    CTX.response_pending = 1; memset(&CTX.response, 0xa5, sizeof(CTX.response)); occupied = CTX.response;
    CHECK(zdo(0x0033, "\xe1\x00", 2) == ZCL_SENSOR_DISCARDED);
    CHECK(CTX.response_result == NWK_APS_FULL && !memcmp(&CTX.response, &occupied, sizeof(occupied)));
    CTX.response_pending = 0;
    memset(&identity, 0, sizeof(identity)); before = ST;
    CHECK(!zcl_sensor_init(&identity) && !memcmp(&ST, &before, sizeof(ST)));
    memset(join_smoke_device.config.association.extraction.local, 0, 8);
    memset(output, 0xa5, sizeof(output));
    CHECK(!zcl_sensor_binding_page(0xe2, 0, output+1, ED_PAYLOAD_MAX));
    for (i = 0; i < sizeof(output); i++) CHECK(output[i] == 0xa5);
    reset(0);
    memset(ST.tc_ieee, 0, 8);
    CHECK(!zcl_sensor_binding_page(0xe3, 0, output+1, ED_PAYLOAD_MAX));
    for (i = 0; i < sizeof(output); i++) CHECK(output[i] == 0xa5);
}

#if defined(CC2530_CHIP_SENSE)
static void sensed(long millivolts, long centi, uint8_t stuck)
{
    adc_set(millivolts, centi, stuck);
    adc.atest = 0x5a;
}

static void chip(void)
{
    static const uint8_t power[] = {0x00, 0x40, 0x00, 0x20, 0x00, 0x21, 0x00};
    static const uint8_t device[] = {0x00, 0x41, 0x00, 0x00, 0x00};
    uint8_t expected[40];
    uint32_t t;
    unsigned i;
    /* Init measures once: four VDD then one discarded and eight sensor
     * conversions, ATEST restored, TR0 off, no overlapping conversion. */
    sensed(3000, 2500, 0); reset(5ul << 22);
    CHECK(adc.vdd == 4 && adc.temperature == 9 && adc.conversions == 13 && !adc.foreign);
    CHECK(!adc.unsensed && !adc.overlapped && adc.atest == 0x5a && !adc.tr0 && !(SOC_ADCCON1 & 0x80u));
    CHECK(abs(ST.millivolts - 3000) <= 2 && abs(ST.temperature - 2500) <= 3);
#if !defined(CC2530_SHT3X)
    CHECK(ST.report[0].last == (uint16_t)ST.temperature);
#endif
    CHECK(ST.report[2].last == 30 && ST.report[3].last == 200);
    zcl(0x0001, power, sizeof(power), "\x18\x40\x01\x20\x00\x00\x20\x1e\x21\x00\x00\x20\xc8", 13);
    zcl(0x0002, device, sizeof(device), "\x18\x41\x01\x00\x00\x00\x29\x19\x00", 9);
    memcpy(expected, "\x18\x42\x01\x00\x00\x00\x29", 7);
#if !defined(CC2530_SHT3X)
    expected[7] = (uint8_t)ST.temperature; expected[8] = (uint8_t)((uint16_t)ST.temperature >> 8);
    zcl(0x0402, "\x00\x42\x00\x00\x00", 5, expected, 9);
#endif
    memcpy(expected, "\x18\x43\x01\x05\x00\x00\x42", 7); expected[7] = sizeof(model)-1;
    CHECK(sizeof(model)-1 <= sizeof(expected)-8); memcpy(expected+8, model, sizeof(model)-1);
    zcl(0x0000, "\x00\x43\x00\x05\x00", 5, expected, (uint8_t)(8+sizeof(model)-1));
    memcpy(expected, "\x18\x44\x01\x04\x00\x00\x42", 7); expected[7] = sizeof(manufacturer)-1;
    CHECK(sizeof(manufacturer)-1 <= sizeof(expected)-8); memcpy(expected+8, manufacturer, sizeof(manufacturer)-1);
    zcl(0x0000, "\x00\x44\x00\x04\x00", 5, expected, (uint8_t)(8+sizeof(manufacturer)-1));
    /* Cadence: nine whole seconds do not measure, the tenth does. */
    adc_set(2500, -1100, 0);
    for (i = 1, t = CTX.last; i < ZCL_SENSOR_MEASURE; i++) {
        CTX.last = t + i*ZCL_SENSOR_SECOND; zcl_sensor_tick();
        CHECK(!adc.conversions && ST.measured == i);
    }
    CTX.last = t + ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND - 1u; zcl_sensor_tick();
    CHECK(!adc.conversions);
    CTX.last += 1u; zcl_sensor_tick();
    CHECK(adc.conversions == 13 && !ST.measured && !adc.unsensed && !adc.overlapped);
    CHECK(abs(ST.millivolts - 2500) <= 2 && abs(ST.temperature + 1100) <= 3);
    zcl(0x0001, power, sizeof(power), "\x18\x40\x01\x20\x00\x00\x20\x19\x21\x00\x00\x20\x64", 13);
    zcl(0x0002, device, sizeof(device), "\x18\x41\x01\x00\x00\x00\x29\xf5\xff", 9);
    /* A long gap measures once, saturating the second count meanwhile. */
    adc_set(3300, 4100, 0);
    CTX.last += 1000ul*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(adc.conversions == 13 && !ST.measured && abs(ST.temperature - 4100) <= 3);
    zcl(0x0001, power, sizeof(power), "\x18\x40\x01\x20\x00\x00\x20\x21\x21\x00\x00\x20\xc8", 13);
    /* A conversion that never ends publishes invalid values and stops
     * after the first VDD poll bound; the sensor is never enabled. */
    adc_set(3000, 2500, 1);
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(adc.conversions == 1 && !adc.temperature && !adc.tr0 && adc.atest == 0x5a);
    CHECK(ST.temperature == ZCL_SENSOR_INVALID_TEMPERATURE && !ST.millivolts);
    zcl(0x0001, power, sizeof(power), "\x18\x40\x01\x20\x00\x00\x20\xff\x21\x00\x00\x20\xff", 13);
    zcl(0x0002, device, sizeof(device), "\x18\x41\x01\x00\x00\x00\x29\x00\x80", 9);
#if !defined(CC2530_SHT3X)
    zcl(0x0402, "\x00\x42\x00\x00\x00", 5, "\x18\x42\x01\x00\x00\x00\x29\x00\x80", 9);
#endif
    /* Recovery on the next measurement. */
    adc_set(3000, 2500, 0);
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(abs(ST.millivolts - 3000) <= 2 && abs(ST.temperature - 2500) <= 3 && adc.conversions == 13);
    /* A stuck sensor conversion invalidates both and restores ATEST and TR0. */
    sensed(3000, 2500, 2);
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(adc.vdd == 4 && adc.temperature == 1 && adc.atest == 0x5a && !adc.tr0);
    CHECK(ST.temperature == ZCL_SENSOR_INVALID_TEMPERATURE && !ST.millivolts);
#if !defined(CC2530_SHT3X)
    /* Changed values report through the existing scheduler. */
    adc_set(3000, 3100, 0);
    ST.report[0].age = 0xff; ST.report[0].last = 0; ST.basic = 0; ST.bound[0] = 1; ST.bound[1] = ST.bound[2] = 0;
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND;
    zcl_sensor_tick();
    CHECK(abs(ST.temperature - 3100) <= 3 && report(0x0402, 0, 0x29, (uint16_t)ST.temperature));
    idle();
#endif
}
#endif

#if defined(CC2530_SHT3X)
/* extra: STOPs without a START (the init bus clear). */
static void bus_idle(unsigned extra)
{
    CHECK(sht.mode == BUS_IDLE && !sht.pull && !(SOC_P1DIR & (SDA | SCL)) && !sht.latch && !ST.bus);
    CHECK(level(SDA) && level(SCL) && !sht.unknown && sht.starts + extra == sht.stops);
}

static void climate_read(int16_t centi, uint16_t humidity)
{
    uint8_t expected[9];
    memcpy(expected, "\x18\x50\x01\x00\x00\x00\x29", 7);
    expected[7] = (uint8_t)centi; expected[8] = (uint8_t)((uint16_t)centi >> 8);
    zcl(0x0402, "\x00\x50\x00\x00\x00", 5, expected, 9);
    memcpy(expected, "\x18\x51\x01\x00\x00\x00\x21", 7);
    expected[7] = (uint8_t)humidity; expected[8] = (uint8_t)(humidity >> 8);
    zcl(0x0405, "\x00\x51\x00\x00\x00", 5, expected, 9);
}

static void sht3x(void)
{
    static const uint8_t device[] = {0x00, 0x41, 0x00, 0x00, 0x00};
    long long u;
    unsigned measures;
    /* Model and macros against the datasheet CRC example and exact formulas. */
    CHECK(sht_crc(0xbeefu) == 0x92u);
    for (u = 0; u <= 0xffff; u++) {
        CHECK(llabs(ZCL_SENSOR_SHT3X_CENTI(u)*65535ll - (17500*u - 4500ll*65535)) < 65535);
        CHECK(llabs(ZCL_SENSOR_SHT3X_HUMIDITY(u)*65535ll - 10000*u) < 65535);
    }
    CHECK(ZCL_SENSOR_SHT3X_CENTI(0) == -4500 && ZCL_SENSOR_SHT3X_CENTI(0xffffu) == 13000);
    CHECK(ZCL_SENSOR_SHT3X_HUMIDITY(0) == 0 && ZCL_SENSOR_SHT3X_HUMIDITY(0xffffu) == 10000);
    /* Init claims only the two pins, clocks the bus clear (a STOP without
     * START), starts, polls through three busy NACKs, reads once and starts
     * the next measurement. The die temperature stays in 0x0002. */
    sensed(3000, 2500, 0); sht_set(2134, 4567, 1); sht.busy = 3;
    SOC_P1SEL = 0xffu; SOC_P1 = 0xffu; SOC_P1DIR = 0x02u;
    reset(5ul << 22);
    CHECK(SOC_P1SEL == (uint8_t)~(SDA | SCL) && SOC_P1 == (uint8_t)~(SDA | SCL) && SOC_P1DIR == 0x02u);
    SOC_P1DIR = 0;
    CHECK(sht.measures == 2 && sht.nacked == 3 && sht.reads == 1 && sht.results == 1 && sht.starts == 6);
    bus_idle(1);
    CHECK(abs(ST.climate - 2134) <= 1 && abs((int)ST.humidity - 4567) <= 1);
    CHECK(ST.report[0].last == (uint16_t)ST.climate && ST.report[1].last == ST.humidity);
    climate_read(ST.climate, ST.humidity);
    zcl(0x0002, device, sizeof(device), "\x18\x41\x01\x00\x00\x00\x29\x19\x00", 9);
    /* Each measurement period returns the result started one period earlier. */
    sht.t = (uint16_t)((30.0 + 45.0) / 175.0 * 65535.0 + 0.5); sht.starts = sht.stops = 0; sht.busy = sht.wait = 0;
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(abs(ST.climate - 2134) <= 1 && sht.measures == 3 && sht.reads == 2 && sht.starts == 2);
    bus_idle(0);
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(abs(ST.climate - 3000) <= 1 && sht.measures == 4 && sht.reads == 3);
    climate_read(ST.climate, ST.humidity);
    /* A corrupted humidity CRC invalidates both; the next period recovers. */
    sht.corrupt = 1;
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(ST.climate == ZCL_SENSOR_INVALID_TEMPERATURE && ST.humidity == ZCL_SENSOR_INVALID_HUMIDITY);
    climate_read(ZCL_SENSOR_INVALID_TEMPERATURE, ZCL_SENSOR_INVALID_HUMIDITY);
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(abs(ST.climate - 3000) <= 1 && abs((int)ST.humidity - 4567) <= 1 && sht.results == 6);
    /* A held SCL (bounded wait) invalidates and flags the period. */
    sht.stuck = 1; sht.starts = sht.stops = 0;
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(ST.climate == ZCL_SENSOR_INVALID_TEMPERATURE && ST.bus && !sht.starts);
    sht.stuck = 0;
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(abs(ST.climate - 3000) <= 1 && !ST.bus);
    /* A removed sensor NACKs: invalid at the next period, no data clocked. */
    sht.present = 0; measures = sht.measures;
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CHECK(ST.climate == ZCL_SENSOR_INVALID_TEMPERATURE && ST.humidity == ZCL_SENSOR_INVALID_HUMIDITY);
    CHECK(sht.measures == measures);
    /* Absent at init: the start command is NACKed, so no read header polls. */
    sensed(3000, 2500, 0); sht_set(2500, 5000, 0);
    reset(5ul << 22);
    CHECK(sht.starts == 2 && !sht.nacked && !sht.measures && ST.climate == ZCL_SENSOR_INVALID_TEMPERATURE);
    bus_idle(1);
    /* A sensor that never finishes bounds the init poll. */
    sensed(3000, 2500, 0); sht_set(2500, 5000, 1); sht.busy = 60000u;
    reset(5ul << 22);
    CHECK(sht.nacked == ZCL_SENSOR_SHT3X_POLLS && ST.humidity == ZCL_SENSOR_INVALID_HUMIDITY);
    /* Changed values report through the existing scheduler. */
    sht_set(2500, 5000, 1); reset(5ul << 22);
    sht.t = (uint16_t)((31.0 + 45.0) / 175.0 * 65535.0 + 0.5);
    ST.basic = 0; ST.bound[0] = ST.bound[1] = 1; ST.bound[2] = 0;
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND; zcl_sensor_tick();
    CTX.last += ZCL_SENSOR_MEASURE*ZCL_SENSOR_SECOND;
    ST.report[0].age = 0xff;
    CHECK(abs(ST.climate - 2500) <= 1);
    CHECK(report(0x0402, 0, 0x29, (uint16_t)ZCL_SENSOR_SHT3X_CENTI(sht.t)));
    CHECK(abs(ST.climate - 3100) <= 1);
    idle();
}
#endif

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "mgmt-bind")) {
        management_bind();
        printf("Mgmt Bind initial live table: %lu checks PASS.\n", checks);
        return 0;
    }
#if defined(CC2530_CHIP_SENSE)
    sensed(3000, 2500, 0);
#if defined(CC2530_SHT3X)
    sht_set(2500, 5000, 1);
#endif
    management_bind(); read_reporting(); values(); defaults_and_discards(); identify(); configure(); bind();
    chip();
#if defined(CC2530_SHT3X)
    sht3x();
    printf("ZCL sensor chip sense + SHT3x: %lu checks PASS; modeled ADC and bit-level SHT3x I2C, "
           "CRC, lag, NACK/stuck/CRC failures (host only).\n", checks);
#else
    printf("ZCL sensor chip sense: %lu checks PASS; modeled ADC VDD and die temperature, "
           "cadence, invalid values (host only).\n", checks);
#endif
#else
    management_bind(); read_reporting(); values(); reads(); defaults_and_discards(); identify(); configure(); bind(); reports();
    printf("ZCL sensor: %lu checks PASS; SYNTHETIC values, Read/Write/Configure/Read Reporting, "
           "Identify, Bind/Unbind and report scheduling (host only).\n", checks);
#endif
    return 0;
}
