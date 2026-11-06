/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* The FlowerX application server (board flower_v2) against its real device
 * overlay, a modeled ADC and Timer 3, and the faronov/flower converter
 * contract (attributes, descriptors, grouped reports). The source is
 * included to reach its static conversions. Values are SYNTHETIC. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "zcl_flower.c"

MCU_XDATA bdb_join_t join_smoke_device;

#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "zcl_flower: line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static const uint8_t own[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t tc[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
static unsigned long checks;

/* ADC model: ADCCON3 starts one conversion of the selected channel; the
 * 14-bit code is returned left-aligned (SWRU191F 12.2.10) and ADCH clears
 * EOC. stuck never completes; negative returns a negative result. */
volatile uint8_t SOC_ADCCON1, SOC_ADCCON3, SOC_ADCL, SOC_ADCH, SOC_APCFG;
volatile uint8_t SOC_P1, SOC_P1SEL, SOC_P2SEL, SOC_PERCFG, SOC_T3CTL, SOC_T3CCTL1, SOC_T3CC0;
static struct {
    uint16_t battery, light, soil;
    uint8_t stuck, negative, commands[16], n;
    unsigned conversions, unpowered;
} adc_model;

uint8_t host_mmio_load(const volatile uint8_t *reg, uint8_t address)
{
    uint8_t v = *reg;
    if (address == SOC_ADCH_ADDRESS) SOC_ADCCON1 &= 0x7fu;
    return v;
}

void host_mmio_store(volatile uint8_t *reg, uint8_t address, uint8_t value)
{
    uint16_t code;
    *reg = value;
    if (address != SOC_ADCCON3_ADDRESS) return;
    adc_model.conversions++;
    if (adc_model.n < sizeof(adc_model.commands)) adc_model.commands[adc_model.n++] = value;
    if (value != 0x3fu && (!(SOC_P1 & BOARD_FLOWER_RAIL) || !(SOC_T3CTL & 0x10u))) adc_model.unpowered++;
    if (adc_model.stuck) return;
    code = value == 0x3fu ? adc_model.battery : value == 0xb7u ? adc_model.light :
        value == 0xb4u ? adc_model.soil : 0;
    code = adc_model.negative ? 0xfff0u : (uint16_t)(code << 2);
    SOC_ADCL = (uint8_t)code; SOC_ADCH = (uint8_t)(code >> 8);
    SOC_ADCCON1 |= 0x80u;
}

uint8_t host_mmio_xload(uint16_t address)
{
    (void)address;
    CHECK(0);
    return 0;
}

void host_mmio_xstore(uint16_t address, uint8_t value)
{
    (void)address; (void)value;
    CHECK(0);
}

static void adc_set(uint16_t battery, uint16_t light, uint16_t soil)
{
    memset(&adc_model, 0, sizeof(adc_model));
    adc_model.battery = battery; adc_model.light = light; adc_model.soil = soil;
}

static void boot(uint8_t sensors)
{
    memset(&join_smoke_device, 0, sizeof(join_smoke_device));
    memset(zdo_srv_descriptors, 0xa5, sizeof(zdo_srv_descriptors));
    SOC_PERCFG = 0xffu; SOC_P2SEL = 0; SOC_P1 = 0; SOC_P1SEL = 0; SOC_T3CTL = 0;
    zcl_sensor_boot();
    if (sensors) {
        uint8_t MCU_XDATA *d;
        ST.sensors = sensors;
        d = describe(zdo_srv_descriptors, 1);
        if (present(2, 0)) d = describe(d, 2);
        *d = 0;
    }
}

static void reset(uint32_t now)
{
    security_keys_config_t identity;
    join_smoke_device.config.transport.profile = 0x0104;
    join_smoke_device.config.transport.endpoint = 1;
    memcpy(join_smoke_device.config.association.extraction.local, own, 8);
    CTX.last = now;
    memset(&identity, 0, sizeof(identity));
    memcpy(identity.own_ieee, own, 8); memcpy(identity.tc_ieee, tc, 8);
    identity.channel = 15;
    CHECK(zcl_sensor_init(&identity));
}

static uint8_t sensor_poll(void)
{
    zcl_sensor_tick();
    return zcl_sensor_serve();
}

static uint8_t receive(uint16_t cluster, uint8_t ep, const void *payload, uint8_t length)
{
    uint8_t result;
    memset(&CTX.application, 0, sizeof(CTX.application));
    CTX.application.nwk.source = 0x0000; CTX.application.nwk.destination = 0x4321;
    CTX.application.aps.profile_id = ep ? 0x0104 : 0; CTX.application.aps.cluster_id = cluster;
    CTX.application.aps.source_endpoint = ep ? 9 : 0; CTX.application.aps.destination_endpoint = ep;
    memcpy(CTX.application.payload, payload, length); CTX.application.length = length;
    CTX.application_ready = 1;
    result = sensor_poll();
    CTX.application_ready = 0;
    memset(&CTX.application, 0, sizeof(CTX.application));
    return result;
}

static void expect(uint16_t cluster, uint8_t ep, const void *expected, uint8_t length)
{
    if (CTX.response_pending && (CTX.response.length != length || memcmp(CTX.response.payload, expected, length))) {
        uint8_t i;
        fprintf(stderr, "response:");
        for (i = 0; i < CTX.response.length; i++) fprintf(stderr, " %02x", CTX.response.payload[i]);
        fprintf(stderr, "\n");
    }
    CHECK(CTX.response_pending && CTX.response.length == length);
    CHECK(!memcmp(CTX.response.payload, expected, length));
    CHECK(CTX.response.aps.cluster_id == cluster && !CTX.response.nwk.destination);
    if (ep) CHECK(CTX.response.aps.profile_id == 0x0104 && CTX.response.aps.source_endpoint == ep &&
                  CTX.response.aps.destination_endpoint == 9);
    CTX.response_pending = 0;
}

static void zcl_ep(uint16_t cluster, uint8_t ep, const void *request, uint8_t length,
                   const void *expected, uint8_t reply_length)
{
    CHECK(receive(cluster, ep, request, length) == ZCL_SENSOR_HANDLED);
    if (reply_length) expect(cluster, ep, expected, reply_length);
    else CHECK(!CTX.response_pending);
}

static void zcl_req(uint16_t cluster, const void *request, uint8_t length, const void *expected, uint8_t reply_length)
{
    zcl_ep(cluster, 1, request, length, expected, reply_length);
}

/* One grouped report: cluster/endpoint and the (id, type, value) records. */
static void reported(uint16_t cluster, uint8_t ep, uint8_t dest, const uint8_t *records, uint8_t length)
{
    CHECK(sensor_poll() == ZCL_SENSOR_REPORT);
    CHECK(CTX.application.aps.cluster_id == cluster && CTX.application.aps.profile_id == 0x0104);
    CHECK(CTX.application.aps.flags == APS_FLAG_ACK_REQUEST && !CTX.application.nwk.destination);
    CHECK(CTX.application.aps.source_endpoint == ep && CTX.application.aps.destination_endpoint == dest);
    CHECK(CTX.application.payload[0] == 0x18 && CTX.application.payload[1] == ST.tsn &&
          CTX.application.payload[2] == 0x0a);
    if (CTX.application.length != 3u+length || memcmp(CTX.application.payload+3, records, length)) {
        uint8_t i;
        fprintf(stderr, "report %04x:", cluster);
        for (i = 3; i < CTX.application.length; i++) fprintf(stderr, " %02x", CTX.application.payload[i]);
        fprintf(stderr, "\n");
    }
    CHECK(CTX.application.length == 3u+length && !memcmp(CTX.application.payload+3, records, length));
    memset(&CTX.application, 0, sizeof(CTX.application));
    ST.sent = 1;
}

static void idle(void)
{
    CHECK(sensor_poll() == ZCL_SENSOR_IDLE && !CTX.application.length);
}

static void seconds(unsigned n)
{
    CTX.last += n*ZCL_SENSOR_SECOND;
    zcl_sensor_tick();
}

static void conversions(void)
{
    unsigned raw, mv;
    double reference;
    /* 10000*log10(lux)+1 within one unit of the double reference. */
    CHECK(illuminance(0) == 0 && illuminance(1) == 1);
    CHECK(illuminance(ZCL_FLOWER_LIGHT_OPEN) == 0xffffu && illuminance(0xffffu) == 0xffffu);
    for (raw = 1; raw < ZCL_FLOWER_LIGHT_OPEN; raw++) {
        reference = 10000.0*log10((double)raw)+1.0;
        CHECK(fabs(illuminance((uint16_t)raw)-reference) <= 1.0);
    }
    /* flower battery.c: mV = raw*0.443, voltage rounds to 100 mV, percent
     * is the Alkaline 2S curve in 0.5 % units. */
    for (raw = 0; raw <= ZCL_FLOWER_ADC_MAX; raw++)
        CHECK(ZCL_FLOWER_MILLIVOLTS(raw) == (unsigned)(raw*443u/1000u));
    for (mv = 0; mv < 4000u; mv++) {
        unsigned p = mv >= 3000u ? 100u : mv > 2800u ? 80u+(mv-2800u)*20u/200u :
            mv > 2500u ? 40u+(mv-2500u)*40u/300u : mv > 2200u ? 10u+(mv-2200u)*30u/300u :
            mv > 2000u ? (mv-2000u)*10u/200u : 0u;
        CHECK(percent((uint16_t)mv) == p*2u);
    }
    CHECK(percent(3000) == 200 && percent(2800) == 160 && percent(2000) == 0);
    /* Soil: battery 6772 (3.0 V) gives air 5138, water 3008. */
    CHECK(ZCL_FLOWER_AIR(6772u) == 5138u && ZCL_FLOWER_WATER(6772u) == 3008u);
    CHECK(moisture(5138, 6772) == 0 && moisture(3008, 6772) == 10000);
    CHECK(moisture(4073, 6772) == 5000);
    CHECK(moisture(6000, 6772) == 0 && moisture(2000, 6772) == 10000);
    /* Beyond one calibrated span: an open or shorted probe is invalid. */
    CHECK(moisture(5138+2130+1, 6772) == 0xffffu && moisture(3008-2130-1, 6772) == 0xffffu);
    CHECK(moisture(0xffffu, 6772) == 0xffffu && moisture(4000, 0xffffu) == 0xffffu);
}

static void descriptors(void)
{
    static const uint8_t ep1[] = {
        20, 1, 0x04, 0x01, 0x0c, 0x00, 2, 6,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x0b, 0x08, 0x04, 0x00, 0x04, 0 };
    static const uint8_t full[] = {
        26, 1, 0x04, 0x01, 0x0c, 0x00, 2, 9,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0x0b, 0x08, 0x04, 0x00, 0x04,
        0x02, 0x04, 0x05, 0x04, 0x03, 0x04, 0,
        10, 2, 0x04, 0x01, 0x0c, 0x00, 2, 1, 0x02, 0x04, 0, 0 };
    uint8_t i;
    boot(0);
    /* Timer 3 alternative 1 with priority over USART 1: up/down, toggle on
     * compare, idle; the rail and excitation pin stay off. */
    CHECK(!(SOC_PERCFG & 0x20u) && (SOC_P2SEL & 0x20u));
    CHECK(SOC_T3CTL == 0x07u && SOC_T3CCTL1 == 0x14u && SOC_T3CC0 == 4u);
    CHECK(!(SOC_P1 & (BOARD_FLOWER_RAIL | BOARD_FLOWER_PWM)) && !(SOC_P1SEL & BOARD_FLOWER_PWM));
    CHECK(!memcmp(zdo_srv_descriptors, ep1, sizeof(ep1)) && !zdo_srv_descriptors[sizeof(ep1)]);
    CHECK(ST.interval == ZCL_FLOWER_INTERVAL && !ST.started && !ST.phase);
    for (i = 0; i < ZCL_SENSOR_REPORTS; i++)
        CHECK(ST.values[i] == (attributes[i].type == 0x29u ? 0x8000u : 0xffffu));
    boot(ZCL_FLOWER_BME280 | ZCL_FLOWER_DS18B20);
    CHECK(sizeof(full) <= ZCL_FLOWER_DESCRIPTORS);
    CHECK(!memcmp(zdo_srv_descriptors, full, sizeof(full)));
}

static void cycle(void)
{
    uint32_t now = 0xfffff000ul;
    boot(0);
    adc_set(6772, 1000, 4073);
    /* Not started: the rail and excitation come up and the device stays
     * awake through the warm-up (PM2 would stop Timer 3). */
    CHECK(zcl_sensor_quiet(now) == 0 && ST.phase == 1);
    CHECK((SOC_P1 & BOARD_FLOWER_RAIL) && (SOC_P1SEL & BOARD_FLOWER_PWM) && (SOC_T3CTL & 0x10u));
    CHECK(zcl_sensor_quiet(now+ZCL_FLOWER_WARMUP-1u) == 0 && !adc_model.conversions);
    /* Past the warm-up (across the 32-bit wrap): battery x5, light x5, soil x3. */
    CHECK(zcl_sensor_quiet(now+ZCL_FLOWER_WARMUP) == 0);
    CHECK(adc_model.conversions == 13 && !adc_model.unpowered);
    CHECK(!memcmp(adc_model.commands, "\x3f\x3f\x3f\x3f\x3f\xb7\xb7\xb7\xb7\xb7\xb4\xb4\xb4", 13));
    CHECK(!SOC_APCFG && !(SOC_P1 & (BOARD_FLOWER_RAIL | BOARD_FLOWER_PWM)) && !(SOC_P1SEL & BOARD_FLOWER_PWM));
    CHECK(!(SOC_T3CTL & 0x10u) && ST.phase == 0 && ST.started);
    CHECK(ST.values[ZCL_FLOWER_BATTERY_RAW] == 6772 && ST.values[ZCL_FLOWER_SOIL_BATTERY] == 6772);
    CHECK(ST.values[ZCL_FLOWER_VOLTAGE] == 30 && ST.values[ZCL_FLOWER_PERCENT] == 198);
    CHECK(ST.values[ZCL_FLOWER_SOIL_RAW] == 4073 && ST.values[ZCL_FLOWER_SOIL] == 5000);
    CHECK(ST.values[ZCL_FLOWER_ILLUMINANCE] == 30001);
    CHECK(ST.values[ZCL_FLOWER_TEMPERATURE] == 0x8000u && ST.values[ZCL_FLOWER_PROBE] == 0x8000u);
    /* Asleep until the interval, then the next cycle. */
    now += ZCL_FLOWER_WARMUP;
    CHECK(zcl_sensor_quiet(now) == 300ul*ZCL_SENSOR_SECOND);
    CHECK(zcl_sensor_quiet(now+1000u) == 300ul*ZCL_SENSOR_SECOND-1000u && !ST.phase);
    CHECK(zcl_sensor_quiet(now+300ul*ZCL_SENSOR_SECOND) == 0 && ST.phase == 1);
    /* A missed conversion and a negative result: invalid, never stale. */
    adc_set(6772, 1000, 4073); adc_model.stuck = 1;
    CHECK(zcl_sensor_quiet(now+300ul*ZCL_SENSOR_SECOND+ZCL_FLOWER_WARMUP) == 0);
    CHECK(adc_model.conversions == 3 && !SOC_APCFG);
    CHECK(ST.values[ZCL_FLOWER_VOLTAGE] == 0xffu && ST.values[ZCL_FLOWER_PERCENT] == 0xffu);
    CHECK(ST.values[ZCL_FLOWER_BATTERY_RAW] == 0xffffu && ST.values[ZCL_FLOWER_SOIL] == 0xffffu);
    CHECK(ST.values[ZCL_FLOWER_ILLUMINANCE] == 0xffffu && ST.values[ZCL_FLOWER_SOIL_RAW] == 0xffffu);
    ST.started = 0; adc_set(6772, 1000, 4073); adc_model.negative = 1;
    CHECK(zcl_sensor_quiet(now) == 0 && zcl_sensor_quiet(now+ZCL_FLOWER_WARMUP) == 0);
    CHECK(ST.values[ZCL_FLOWER_BATTERY_RAW] == 0 && ST.values[ZCL_FLOWER_ILLUMINANCE] == 0);
    CHECK(ST.values[ZCL_FLOWER_SOIL] == 0xffffu);
}

static void server(void)
{
    static const uint8_t model[] = "\x18\x01\x01\x05\x00\x00\x42\x0e" "DIYRuZ_FlowerX";
    static const uint8_t maker[] = "\x18\x02\x01\x04\x00\x00\x42\x09" "modkam.ru";
    boot(0);
    reset(1000);
    CHECK(!memcmp(ST.tc_ieee, tc, 8) && ST.basic && ST.channel == 15 && ST.interval == 300);
    CHECK(ST.bound[0] == 1 && ST.bound[1] == 1 && ST.bound[2] == 1 && !ST.bound[3] && !ST.bound[4]);
    CHECK(!ST.bound[5] && ST.bound[6] == 1 && !ST.bound[7]);
    zcl_req(0x0000, "\x00\x01\x00\x05\x00", 5, model, sizeof(model)-1u);
    zcl_req(0x0000, "\x00\x02\x00\x04\x00", 5, maker, sizeof(maker)-1u);
    zcl_req(0x0000, "\x00\x03\x00\x00\x00\x07\x00\x00\x40", 9,
            "\x18\x03\x01\x00\x00\x00\x20\x01\x07\x00\x00\x30\x03\x00\x40\x00\x42\x0c" "2026.11-llvm", 30);
    /* Absent climate clusters and EP2 without the DS18B20. */
    zcl_req(0x0402, "\x00\x04\x00\x00\x00", 5, "\x18\x04\x0b\x00\xc3", 5);
    CHECK(receive(0x0402, 2, "\x00\x05\x00\x00\x00", 5) == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
    /* Power configuration constants and invalid values before a measurement. */
    zcl_req(0x0001, "\x00\x06\x00\x20\x00\x21\x00\x31\x00\x33\x00\x00\x02", 13,
            "\x18\x06\x01\x20\x00\x00\x20\xff\x21\x00\x00\x20\xff\x31\x00\x00\x30\x04"
            "\x33\x00\x00\x20\x02\x00\x02\x00\x21\xff\xff", 29);
    /* Diagnostics: interval and channel, irrigation constants. */
    zcl_req(0x0b05, "\x00\x07\x00\x09\xff\x04\xff\x0b\xff\x10\xff", 11,
            "\x18\x07\x01\x09\xff\x00\x21\x2c\x01\x04\xff\x00\x20\x0f"
            "\x0b\xff\x00\x20\x1e\x10\xff\x00\x10\x00", 24);
    /* Interval 30..3600 is writable and samples at once; others read-only. */
    ST.started = 1;
    zcl_req(0x0b05, "\x00\x08\x02\x09\xff\x21\x1d\x00", 8, "\x18\x08\x04\x87\x09\xff", 6);
    CHECK(ST.interval == 300 && ST.started);
    zcl_req(0x0b05, "\x00\x09\x02\x09\xff\x21\x11\x0e", 8, "\x18\x09\x04\x87\x09\xff", 6);
    zcl_req(0x0b05, "\x00\x0a\x02\x0b\xff\x20\x05", 7, "\x18\x0a\x04\x88\x0b\xff", 6);
    zcl_req(0x0b05, "\x00\x0b\x02\x09\xff\x21\x3c\x00", 8, "\x18\x0b\x04\x00", 4);
    CHECK(ST.interval == 60 && !ST.started);
    zcl_req(0x0b05, "\x00\x0c\x02\x09\xff\x20\x3c", 7, "\x18\x0c\x04\x8d\x09\xff", 6);
    /* The converter's configure step: irrigation sinks accept and store
     * nothing; soil raw reporting is stored and read back. */
    zcl_req(0x0b05, "\x00\x0d\x06\x00\x0a\xff\x30\x00\x00\x10\x0e\x00\x10\xff\x10\x00\x00\x10\x0e", 19,
            "\x18\x0d\x07\x00", 4);
    zcl_req(0x0b05, "\x00\x0e\x08\x00\x0a\xff", 6, "\x18\x0e\x09\x8b\x00\x0a\xff", 7);
    zcl_req(0x0408, "\x00\x0f\x06\x00\x00\x02\x21\x3c\x00\x08\x07\x00\x00", 13, "\x18\x0f\x07\x00", 4);
    zcl_req(0x0408, "\x00\x10\x08\x00\x00\x02", 6,
            "\x18\x10\x09\x00\x00\x00\x02\x21\x3c\x00\x08\x07\x00\x00", 14);
    zcl_req(0x0001, "\x00\x11\x08\x00\x20\x00", 6,
            "\x18\x11\x09\x00\x00\x20\x00\x20\x10\x0e\x60\x54\x01", 13);
    zcl_req(0x0403, "\x00\x12\x06\x00\x00\x00\x29\x3c\x00\x08\x07\x01\x00", 13, "\x18\x12\x0b\x06\xc3", 5);
    /* Identify. */
    zcl_req(0x0003, "\x01\x13\x00\x05\x00", 5, "\x18\x13\x0b\x00\x00", 5);
    CHECK(ST.identify == 5);
    zcl_req(0x0003, "\x01\x14\x01", 3, "\x19\x14\x00\x05\x00", 5);
    seconds(5);
    CHECK(!ST.identify);
    zcl_req(0x0003, "\x11\x15\x00\x03\x00", 5, NULL, 0);
    CHECK(ST.identify == 3);
    seconds(3);
    zcl_req(0x0003, "\x01\x16\x01", 3, NULL, 0);
}

static void reports(void)
{
    static const uint8_t power[] = { 0x20, 0x00, 0x20, 30, 0x21, 0x00, 0x20, 198, 0x00, 0x02, 0x21, 0x74, 0x1a };
    static const uint8_t soil[] = { 0x00, 0x00, 0x21, 0x88, 0x13, 0x00, 0x02, 0x21, 0xe9, 0x0f,
                                    0x01, 0x02, 0x21, 0x74, 0x1a };
    static const uint8_t light[] = { 0x00, 0x00, 0x21, 0x31, 0x75 };
    static const uint8_t model[] = "\x05\x00\x42\x0e" "DIYRuZ_FlowerX";
    uint32_t now = 5000;
    boot(0);
    reset(now);
    /* The Basic model first, to coordinator endpoint 1. */
    reported(0x0000, 1, 1, model, sizeof(model)-1u);
    /* Invalid values before the first measurement are not reported. */
    idle();
    CHECK(!ST.basic);
    adc_set(6772, 1000, 4073);
    CHECK(zcl_sensor_quiet(now) == 0);
    CHECK(zcl_sensor_quiet(now+ZCL_FLOWER_WARMUP) == 0);
    /* One frame per cluster, in table order. */
    reported(0x0001, 1, 1, power, sizeof(power));
    reported(0x0408, 1, 1, soil, sizeof(soil));
    reported(0x0400, 1, 1, light, sizeof(light));
    idle();
    CHECK(ST.report[ZCL_FLOWER_SOIL].last == 5000 && !ST.report[ZCL_FLOWER_SOIL].age);
    /* Below the reportable change and min: silent; a change past min reports
     * only the changed attribute. */
    ST.values[ZCL_FLOWER_SOIL] = 5050;
    seconds(60);
    idle();
    ST.values[ZCL_FLOWER_SOIL] = 5100;
    reported(0x0408, 1, 1, (const uint8_t *)"\x00\x00\x21\xec\x13", 5);
    /* A coordinator bound at another endpoint receives the group there;
     * an unbound cluster stays silent even at max. */
    CHECK(receive(ZDO_SRV_UNBIND_REQUEST, 0,
        "\x21\x01\x02\x03\x04\x05\x06\x07\x08\x01\x00\x04\x03\x11\x22\x33\x44\x55\x66\x77\x88\x01", 22)
        == ZCL_SENSOR_HANDLED);
    expect(0x8022, 0, "\x21\x00", 2);
    CHECK(!ST.bound[2]);
    CHECK(receive(ZDO_SRV_BIND_REQUEST, 0,
        "\x22\x01\x02\x03\x04\x05\x06\x07\x08\x01\x08\x04\x03\x11\x22\x33\x44\x55\x66\x77\x88\x07", 22)
        == ZCL_SENSOR_HANDLED);
    expect(0x8021, 0, "\x22\x8c", 2);
    CHECK(receive(ZDO_SRV_UNBIND_REQUEST, 0,
        "\x23\x01\x02\x03\x04\x05\x06\x07\x08\x01\x08\x04\x03\x11\x22\x33\x44\x55\x66\x77\x88\x01", 22)
        == ZCL_SENSOR_HANDLED);
    expect(0x8022, 0, "\x23\x00", 2);
    CHECK(receive(ZDO_SRV_BIND_REQUEST, 0,
        "\x24\x01\x02\x03\x04\x05\x06\x07\x08\x01\x08\x04\x03\x11\x22\x33\x44\x55\x66\x77\x88\x07", 22)
        == ZCL_SENSOR_HANDLED);
    expect(0x8021, 0, "\x24\x00", 2);
    /* Bind to an absent cluster: not supported. */
    CHECK(receive(ZDO_SRV_BIND_REQUEST, 0,
        "\x25\x01\x02\x03\x04\x05\x06\x07\x08\x02\x02\x04\x03\x11\x22\x33\x44\x55\x66\x77\x88\x01", 22)
        == ZCL_SENSOR_HANDLED);
    expect(0x8021, 0, "\x25\x82", 2);
    seconds(1800);
    reported(0x0408, 1, 7, (const uint8_t *)"\x00\x00\x21\xec\x13\x00\x02\x21\xe9\x0f\x01\x02\x21\x74\x1a", 15);
    idle();
    CHECK(ST.report[ZCL_FLOWER_SOIL].age == 0 && ST.report[ZCL_FLOWER_SOIL_RAW].age == 0);
}

static void climate_and_probe(void)
{
    static const uint8_t probe[] = { 0x00, 0x00, 0x29, 0x6a, 0x09 };
    boot(ZCL_FLOWER_BME280 | ZCL_FLOWER_DS18B20);
    reset(0);
    CHECK(ST.bound[3] == 1 && ST.bound[4] == 1 && ST.bound[5] == 1 && ST.bound[7] == 1);
    zcl_req(0x0403, "\x00\x01\x00\x00\x00\x10\x00\x14\x00", 9,
            "\x18\x01\x01\x00\x00\x00\x29\x00\x80\x10\x00\x00\x29\x00\x80\x14\x00\x00\x28\xff", 20);
    zcl_ep(0x0402, 2, "\x00\x02\x00\x00\x00", 5, "\x18\x02\x01\x00\x00\x00\x29\x00\x80", 9);
    CHECK(sensor_poll() == ZCL_SENSOR_REPORT);
    ST.sent = 1;
    ST.values[ZCL_FLOWER_PROBE] = 2410;
    reported(0x0402, 2, 1, probe, sizeof(probe));
    idle();
}

int main(void)
{
    conversions(); descriptors(); cycle(); server(); reports(); climate_and_probe();
    printf("ZCL flower: %lu checks PASS; modeled ADC/Timer 3 cycle, conversions, descriptors, "
           "converter attributes and grouped reports (host only).\n", checks);
    return 0;
}
