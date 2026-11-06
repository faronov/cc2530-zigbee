/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* FlowerX application server (zcl_flower.h): the zcl_sensor.c engine with
 * two endpoints, a measured sensor cycle and grouped reports. */
#include "zcl_sensor.h"
#include "zdo_srv.h"
#include <stddef.h>
#include <string.h>

#define DEV join_smoke_device
#define CTX join_smoke_device.work.runtime.zdo
#define ST zcl_flower_state
#define RX CTX.application
#define IN CTX.application.payload
#define OUT CTX.response.payload
#define LE(at) ((uint16_t)(IN[at] | (uint16_t)IN[(uint8_t)((at)+1u)] << 8))

#define NONE 0xffu
#define BUILT 0xffu
#define SILENT 0xfeu
#define BASIC_REPORT 0x8000u
#define STRING 0x42u
#define PROFILE 0x0104u

/* Entries after the reportable 0..11. */
#define IDENTIFY 12u
#define MODEL 14u
#define INTERVAL 17u
#define CHANNEL 18u
/* Irrigation attributes: reportable read-only constants. Configure Reporting
 * succeeds without storing anything; reading it back is NOT_FOUND. */
#define SINK 19u
#define SINKS 8u

MCU_XDATA zcl_sensor_state_t zcl_flower_state;
MCU_XDATA uint8_t zdo_srv_descriptors[ZCL_FLOWER_DESCRIPTORS];

typedef struct {
    uint16_t cluster, id;
    uint8_t type, ep;
    uint16_t value;
} zcl_attribute_t;

/* Strings carry a text selector in value; the FFFD records also list the
 * simple descriptor input clusters in order. */
static const MCU_CODE zcl_attribute_t attributes[] = {
    { 0x0001u, 0x0020u, 0x20u, 1, 0 }, { 0x0001u, 0x0021u, 0x20u, 1, 0 }, { 0x0001u, 0x0200u, 0x21u, 1, 0 },
    { 0x0408u, 0x0000u, 0x21u, 1, 0 }, { 0x0408u, 0x0200u, 0x21u, 1, 0 }, { 0x0408u, 0x0201u, 0x21u, 1, 0 },
    { 0x0400u, 0x0000u, 0x21u, 1, 0 },
    { 0x0402u, 0x0000u, 0x29u, 1, 0 }, { 0x0405u, 0x0000u, 0x21u, 1, 0 },
    { 0x0403u, 0x0000u, 0x29u, 1, 0 }, { 0x0403u, 0x0010u, 0x29u, 1, 0 },
    { 0x0402u, 0x0000u, 0x29u, 2, 0 },
    { 0x0003u, 0x0000u, 0x21u, 1, 0 },
    { 0x0000u, 0x0004u, STRING, 1, 0 }, { 0x0000u, 0x0005u, STRING, 1, 1 },
    { 0x0000u, 0x0006u, STRING, 1, 2 }, { 0x0000u, 0x4000u, STRING, 1, 2 },
    { 0x0b05u, 0xff09u, 0x21u, 1, 0 }, { 0x0b05u, 0xff04u, 0x20u, 1, 0 },
    { 0x0b05u, 0xff0au, 0x30u, 1, 0 }, { 0x0b05u, 0xff0bu, 0x20u, 1, 30 }, { 0x0b05u, 0xff0cu, 0x20u, 1, 10 },
    { 0x0b05u, 0xff0du, 0x21u, 1, 300 }, { 0x0b05u, 0xff0eu, 0x21u, 1, 60 }, { 0x0b05u, 0xff0fu, 0x30u, 1, 0 },
    { 0x0b05u, 0xff10u, 0x10u, 1, 0 }, { 0x0b05u, 0xff11u, 0x21u, 1, 0 },
    { 0x0000u, 0x0000u, 0x20u, 1, 1 }, { 0x0000u, 0x0001u, 0x20u, 1, 3 }, { 0x0000u, 0x0002u, 0x20u, 1, 4 },
    { 0x0000u, 0x0003u, 0x20u, 1, 1 }, { 0x0000u, 0x0007u, 0x30u, 1, 3 },
    { 0x0001u, 0x0031u, 0x30u, 1, 4 }, { 0x0001u, 0x0033u, 0x20u, 1, 2 },
    { 0x0b05u, 0xff01u, 0x21u, 1, 0 }, { 0x0b05u, 0xff02u, 0x21u, 1, 0 }, { 0x0b05u, 0xff03u, 0x21u, 1, 0 },
    { 0x0b05u, 0xff05u, 0x28u, 1, 4 }, { 0x0b05u, 0xff06u, 0x21u, 1, 0 },
    /* TX power mode MAX (fixed +4.5 dBm) and manual setting: read-only. */
    { 0x0b05u, 0xff07u, 0x20u, 1, 2 }, { 0x0b05u, 0xff08u, 0x20u, 1, 2 },
    { 0x0403u, 0x0014u, 0x28u, 1, 0xffu },
    { 0x0000u, 0xfffdu, 0x21u, 1, 1 }, { 0x0003u, 0xfffdu, 0x21u, 1, 1 }, { 0x0001u, 0xfffdu, 0x21u, 1, 1 },
    { 0x0b05u, 0xfffdu, 0x21u, 1, 1 }, { 0x0408u, 0xfffdu, 0x21u, 1, 1 }, { 0x0400u, 0xfffdu, 0x21u, 1, 1 },
    { 0x0402u, 0xfffdu, 0x21u, 1, 1 }, { 0x0405u, 0xfffdu, 0x21u, 1, 1 }, { 0x0403u, 0xfffdu, 0x21u, 1, 1 },
    { 0x0402u, 0xfffdu, 0x21u, 2, 1 }
};
#define ATTRIBUTES (sizeof(attributes)/sizeof(attributes[0]))
typedef char zcl_flower_entries[ATTRIBUTES < NONE ? 1 : -1];
static const MCU_CODE uint16_t defaults[ZCL_SENSOR_REPORTS][3] = {
    { 3600, 21600, 1 }, { 3600, 21600, 1 }, { 3600, 21600, 1 },
    { 60, 1800, 100 }, { 60, 1800, 100 }, { 60, 1800, 100 }, { 60, 1800, 100 },
    { 30, 900, 25 }, { 30, 1200, 50 }, { 60, 1800, 1 }, { 60, 1800, 1 }, { 30, 900, 25 }
};
/* Coordinator binding slots: the reporting server clusters. */
static const MCU_CODE struct { uint8_t ep; uint16_t cluster; } slots[ZCL_FLOWER_SLOTS] = {
    { 1, 0x0001u }, { 1, 0x0408u }, { 1, 0x0400u }, { 1, 0x0402u },
    { 1, 0x0405u }, { 1, 0x0403u }, { 1, 0x0b05u }, { 2, 0x0402u }
};
static const MCU_CODE char manufacturer[] = BOARD_MANUFACTURER;
static const MCU_CODE char model[] = BOARD_MODEL;
/* DateCode and SWBuildID: fixed for reproducible images. */
static const MCU_CODE char build[] = "2026.11-llvm";

/* EP2 needs the DS18B20, the EP1 climate clusters the BME280. */
static uint8_t present(uint8_t ep, uint16_t cluster)
{
    if (ep == 2u) return ST.sensors & ZCL_FLOWER_DS18B20;
    if (cluster == 0x0402u || cluster == 0x0405u || cluster == 0x0403u) return ST.sensors & ZCL_FLOWER_BME280;
    return 1;
}

/* Looks up ST.id in the request cluster on endpoint ST.ep. */
static uint8_t find(void)
{
    const MCU_CODE zcl_attribute_t *a = attributes;
    for (ST.entry = 0; ST.entry < ATTRIBUTES; ST.entry++, a++)
        if (a->cluster == RX.aps.cluster_id && a->id == ST.id && a->ep == ST.ep && present(a->ep, a->cluster)) {
            ST.type = a->type;
            return ST.entry;
        }
    return ST.entry = NONE;
}

static uint8_t slot(uint8_t entry)
{
    uint8_t i;
    for (i = 0; i < ZCL_FLOWER_SLOTS; i++)
        if (slots[i].ep == attributes[entry].ep && slots[i].cluster == attributes[entry].cluster) return i;
    return NONE;
}

static uint16_t value(uint8_t entry)
{
    if (entry < ZCL_SENSOR_REPORTS) return ST.values[entry];
    if (entry == IDENTIFY) return ST.identify;
    if (entry == INTERVAL) return ST.interval;
    if (entry == CHANNEL) return ST.channel;
    return attributes[entry].value;
}

/* Octets of the parsed fixed-size types: data, boolean, bitmap, integer and
 * enumeration (08..31). Other types, including strings, are 0. */
static uint8_t width(uint8_t type)
{
    if (type < 0x08u) return 0;
    if (type < 0x30u) return (uint8_t)((type & 7u)+1u);
    if (type < 0x32u) return (uint8_t)(type-0x2fu);
    return 0;
}

static zcl_sensor_reporting_t MCU_XDATA *reporting(uint8_t entry)
{
    return &ST.report[entry];
}

static uint8_t left(void)
{
    return (uint8_t)(ST.length-ST.in);
}

static uint8_t take(void)
{
    return IN[ST.in++];
}

static uint16_t take16(void)
{
    ST.in += 2;
    return LE((uint8_t)(ST.in-2u));
}

static void emit(uint8_t octet)
{
    ST.out[ST.at++] = octet;
}

static void emit16(uint16_t v)
{
    uint8_t MCU_XDATA *p = ST.out+ST.at;
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    ST.at += 2;
}

/* Value octets of the found attribute: strings include their length. */
static uint8_t size(void)
{
    uint16_t text = attributes[ST.entry].value;
    if (ST.type != STRING) return width(ST.type);
    return !text ? sizeof(manufacturer) : text == 1u ? sizeof(model) : sizeof(build);
}

/* Appends the found attribute's value. */
static void put(void)
{
    uint16_t which = attributes[ST.entry].value;
    const MCU_CODE char *text = !which ? manufacturer : which == 1u ? model : build;
    uint8_t MCU_XDATA *p;
    uint8_t n = size();
    if (ST.type == STRING) {
        p = ST.out+ST.at; ST.at += n;
        *p++ = (uint8_t)(n-1u);
        while (--n) *p++ = (uint8_t)*text++;
        return;
    }
    if (n > 1u) emit16(value(ST.entry));
    else emit((uint8_t)value(ST.entry));
}

static void restore(uint8_t entry)
{
#if defined(CC2530_CLANG)
    /* LLVM: memcpy() takes default-space (XDATA) pointers; read CODE explicitly. */
    uint8_t i, *out = (uint8_t *)reporting(entry);
    const MCU_CODE uint8_t *in = (const MCU_CODE uint8_t *)defaults[entry];
    for (i = 0; i < sizeof(defaults[0]); i++) out[i] = in[i];
#else
    memcpy(reporting(entry), defaults[entry], sizeof(defaults[0]));
#endif
}

/* Server response addressed to the request source. */
static uint8_t reply(uint16_t cluster)
{
    if (CTX.response_pending) {
        CTX.response_result = NWK_APS_FULL;
        return 0;
    }
    memset(&CTX.response, 0, sizeof(CTX.response));
    CTX.response.nwk.version = 2; CTX.response.nwk.radius = 30;
    CTX.response.nwk.destination = RX.nwk.source;
    CTX.response.nwk.discover_route = NWK_DISCOVER_ROUTE_ENABLE;
    CTX.response.aps.cluster_id = cluster;
    ST.out = OUT;
    return 1;
}

static uint8_t read(void)
{
    while (left() >= 2u) {
        if ((uint8_t)(ST.at+((ST.id = take16(), find()) == NONE ? 3u : 4u+size())) > ED_PAYLOAD_MAX) break;
        emit16(ST.id);
        if (ST.entry == NONE) { emit(0x86); continue; }
        emit(0); emit(ST.type);
        put();
    }
    return BUILT;
}

/* Statuses of a failed write/configure record, then one SUCCESS if none. */
static uint8_t statuses(uint8_t command)
{
    OUT[ST.header] = command;
    if (ST.at == (uint8_t)(ST.header+1u)) emit(0);
    return BUILT;
}

/* Validates every record before the second pass writes. IdentifyTime and the
 * sampling interval (30..3600 s; a new interval samples at once) are writable. */
static uint8_t write(void)
{
    uint8_t n;
    for (ST.apply = 0; ST.apply < 2u; ST.apply++) {
        ST.in = ST.at = (uint8_t)(ST.header+1u);
        while (left()) {
            if (left() < 3u) return 0x80;
            ST.id = take16(); find();
            ST.kind = take();
            n = width(ST.kind);
            if ((ST.kind == 0x41u || ST.kind == 0x42u) && left()) n = (uint8_t)(IN[ST.in]+1u);
            if (!n || left() < n) return 0x80;
            if (ST.entry == NONE) ST.status = 0x86;
            else if (ST.kind != ST.type) ST.status = 0x8d;
            else if (ST.entry == IDENTIFY) ST.status = 0;
            else if (ST.entry != INTERVAL) ST.status = 0x88;
            else ST.status = LE(ST.in) < ZCL_FLOWER_INTERVAL_MIN || LE(ST.in) > ZCL_FLOWER_INTERVAL_MAX ? 0x87u : 0u;
            if (ST.apply) {
                if (ST.status) { emit(ST.status); emit16(ST.id); }
                else if (ST.entry == IDENTIFY) ST.identify = LE(ST.in);
                else { ST.interval = LE(ST.in); ST.started = 0; }
            }
            ST.in += n;
        }
    }
    return statuses(0x04);
}

static uint8_t sink(uint8_t entry)
{
    return entry == INTERVAL || (uint8_t)(entry-SINK) < SINKS;
}

/* Two passes like write(). max FFFF disables, max 0 with min FFFF restores
 * the default; a nonzero max below min is INVALID_VALUE. */
static uint8_t configure(void)
{
    zcl_sensor_reporting_t MCU_XDATA *r;
    uint8_t n;
    for (ST.apply = 0; ST.apply < 2u; ST.apply++) {
        ST.in = ST.at = (uint8_t)(ST.header+1u);
        while (left()) {
            if (left() < 3u) return 0x80;
            ST.dir = take();
            if (ST.dir > 1u) return 0x80;
            ST.id = take16();
            ST.status = find() == NONE ? 0x86u : 0x8cu;
            n = 2;
            if (!ST.dir) {
                if (left() < 5u) return 0x80;
                ST.kind = take(); ST.min = take16(); ST.max = take16();
                /* Integers (20..2F) are the parsed analog types. */
                n = (uint8_t)(ST.kind-0x20u) < 0x10u ? width(ST.kind) : 0;
                if (ST.entry < ZCL_SENSOR_REPORTS || sink(ST.entry)) {
                    if (ST.kind != ST.type) ST.status = 0x8d;
                    else if (ST.max && ST.max != 0xffffu && ST.max < ST.min) ST.status = 0x87;
                    else ST.status = 0;
                }
            }
            if (left() < n) return 0x80;
            if (ST.apply) {
                if (ST.status) {
                    emit(ST.status); emit(ST.dir); emit16(ST.id);
                } else if (ST.entry < ZCL_SENSOR_REPORTS) {
                    if (!ST.max && ST.min == 0xffffu) restore(ST.entry);
                    else {
                        r = reporting(ST.entry);
                        r->min = ST.min; r->max = ST.max;
                        r->change = n > 1u ? LE(ST.in) : IN[ST.in];
                    }
                }
            }
            ST.in += n;
        }
    }
    return statuses(0x07);
}

static uint8_t read_configuration(void)
{
    zcl_sensor_reporting_t MCU_XDATA *r;
    uint8_t n;
    if (!left()) return 0x80;
    /* Validate even records beyond the fitting prefix (R8 2.5.10.2-3). */
    while (left()) {
        if (left() < 3u || take() > 1u) return 0x80;
        ST.in += 2;
    }
    ST.in = (uint8_t)(ST.header+1u);
    while (left()) {
        ST.dir = take(); ST.id = take16();
        ST.status = 0x86;
        if (!ST.dir && find() != NONE)
            ST.status = sink(ST.entry) ? 0x8bu : ST.entry >= ZCL_SENSOR_REPORTS ? 0x8cu :
                reporting(ST.entry)->max == 0xffffu ? 0x8bu : 0;
        n = ST.status ? 4u : (uint8_t)(9u+width(ST.type));
        if (n > (uint8_t)(ED_PAYLOAD_MAX-ST.at)) break;
        emit(ST.status); emit(ST.dir); emit16(ST.id);
        if (!ST.status) {
            r = reporting(ST.entry);
            emit(ST.type); emit16(r->min); emit16(r->max);
            if (width(ST.type) == 1u) emit((uint8_t)r->change);
            else emit16(r->change);
        }
    }
    return BUILT;
}

/* Identify and Identify Query; an idle device does not answer the query. */
static uint8_t specific(uint8_t command)
{
    if (RX.aps.cluster_id != ZDO_SRV_IDENTIFY_CLUSTER || command > 1u) return 0x81;
    if (!command) {
        if (left() < 2u) return 0x80;
        ST.identify = take16();
        return 0;
    }
    if (!ST.identify) return SILENT;
    OUT[0] = 0x19; OUT[ST.header] = 0;
    emit16(ST.identify);
    return BUILT;
}

static uint8_t zcl(void)
{
    uint8_t control, command;
    ST.ep = RX.aps.destination_endpoint;
    if (RX.aps.profile_id != DEV.config.transport.profile ||
        (ST.ep != 1u && (ST.ep != 2u || !present(2, 0)))) return ZCL_SENSOR_DISCARDED;
    ST.length = RX.length;
    if (!ST.length || ST.length > ED_PAYLOAD_MAX) return ZCL_SENSOR_DISCARDED;
    control = IN[0];
    if ((control & 3u) > 1u) return ZCL_SENSOR_DISCARDED;
    ST.header = control & 4u ? 4u : 2u;
    if (ST.length <= ST.header) return ZCL_SENSOR_DISCARDED;
    command = IN[ST.header];
    if (!(control & 3u) && (command == 0x0bu || command == 0x09u)) return ZCL_SENSOR_DISCARDED;
    if (!reply(RX.aps.cluster_id)) return ZCL_SENSOR_DISCARDED;
    CTX.response.aps.profile_id = DEV.config.transport.profile;
    CTX.response.aps.source_endpoint = ST.ep;
    CTX.response.aps.destination_endpoint = RX.aps.source_endpoint;
    memcpy(OUT, IN, ST.header);
    OUT[0] = (uint8_t)((control & 4u) | 0x18u);
    ST.in = ST.at = (uint8_t)(ST.header+1u);
    OUT[ST.header] = (uint8_t)(command+1u);
    ST.id = 0xfffdu;
    if ((control & 8u) || find() == NONE) ST.status = 0xc3;
    else if (control & 4u) ST.status = 0x81;
    else if (control & 1u) ST.status = specific(command);
    else if (command == 0x00u) ST.status = read();
    else if (command == 0x02u) ST.status = write();
    else if (command == 0x06u) ST.status = configure();
    else if (command == 0x08u) ST.status = read_configuration();
    else ST.status = 0x81;
    if (ST.status == SILENT || (!ST.status && (control & 0x10u))) return ZCL_SENSOR_HANDLED;
    if (ST.status != BUILT) {
        ST.at = ST.header;
        emit(0x0b); emit(command); emit(ST.status);
    }
    CTX.response.length = ST.at;
    CTX.response_pending = 1;
    return ZCL_SENSOR_HANDLED;
}

/* RAM-only, one coordinator endpoint per binding slot. */
static uint8_t bind(void)
{
    uint8_t i, mode = IN[12];
    ST.length = RX.length;
    if (ST.length < 13u || (mode == 3u && ST.length < 22u) || (mode == 1u && ST.length < 15u))
        return ZCL_SENSOR_DISCARDED;
    if (!reply(RX.aps.cluster_id | 0x8000u)) return ZCL_SENSOR_DISCARDED;
    ST.id = LE(10);
    ST.entry = NONE;
    for (i = 0; i < ZCL_FLOWER_SLOTS; i++)
        if (slots[i].ep == IN[9] && slots[i].cluster == ST.id && present(IN[9], ST.id)) ST.entry = i;
    ST.status = 0x84;
    if (!memcmp(IN+1, DEV.config.association.extraction.local, 8) && mode == 3u) {
        ST.status = 0x82;
        if ((IN[9] == 1u || (IN[9] == 2u && present(2, 0))) && IN[21] && IN[21] != 0xffu) {
            ST.dir = RX.aps.cluster_id == ZDO_SRV_BIND_REQUEST;
            ST.status = ST.dir ? 0x8cu : 0x88u;
            if (ST.entry != NONE && !memcmp(IN+13, ST.tc_ieee, 8) &&
                ST.bound[ST.entry] == (ST.dir && !ST.bound[ST.entry] ? 0 : IN[21])) {
                ST.bound[ST.entry] = ST.dir ? IN[21] : 0;
                ST.status = 0;
            }
        }
    }
    OUT[0] = IN[0]; OUT[1] = ST.status;
    CTX.response.length = 2;
    CTX.response_pending = 1;
    return ZCL_SENSOR_HANDLED;
}

static uint8_t known_ieee(const uint8_t MCU_XDATA *ieee)
{
    uint8_t i, any = 0, different = 0;
    for (i = 0; i < 8u; i++) {
        any |= ieee[i];
        different |= (uint8_t)~ieee[i];
    }
    return any && different;
}

uint8_t zcl_sensor_binding_page(uint8_t sequence, uint8_t start,
    uint8_t MCU_XDATA *output, uint8_t capacity) JOIN_FAR
{
    uint8_t i, total = 0, index = 0, at = 5;
    uint16_t cluster;
    if (!output || capacity < 5u || capacity > ED_PAYLOAD_MAX ||
        !known_ieee(DEV.config.association.extraction.local) || !known_ieee(ST.tc_ieee))
        return 0;
    for (i = 0; i < ZCL_FLOWER_SLOTS; i++) {
        if (ST.bound[i] == 0xffu) return 0;
        if (ST.bound[i]) total++;
    }
    output[0] = sequence; output[1] = 0; output[2] = total;
    output[3] = total ? start : 0; output[4] = 0;
    for (i = 0; i < ZCL_FLOWER_SLOTS; i++) {
        if (!ST.bound[i]) continue;
        if (index++ < start) continue;
        if ((uint8_t)(capacity-at) < 21u) break;
        memcpy(output+at, DEV.config.association.extraction.local, 8);
        output[at+8u] = slots[i].ep;
        cluster = slots[i].cluster;
        output[at+9u] = (uint8_t)cluster; output[at+10u] = (uint8_t)(cluster >> 8);
        output[at+11u] = 3;
        memcpy(output+at+12u, ST.tc_ieee, 8);
        output[at+20u] = ST.bound[i];
        at += 21; output[4]++;
    }
    return at;
}

static uint8_t management_bind(void)
{
    if (RX.nwk.type || RX.aps.type || RX.aps.source_endpoint ||
        (RX.aps.flags & (uint8_t)~APS_FLAG_ACK_REQUEST) ||
        RX.length < 2u || RX.length > ED_PAYLOAD_MAX)
        return ZCL_SENSOR_DISCARDED;
    if (!reply(ZDO_SRV_MGMT_BIND_REQUEST | 0x8000u)) return ZCL_SENSOR_DISCARDED;
    CTX.response.length = zcl_sensor_binding_page(IN[0], IN[1], OUT, ED_PAYLOAD_MAX);
    if (!CTX.response.length) return ZCL_SENSOR_DISCARDED;
    CTX.response.aps.destination_endpoint = RX.aps.source_endpoint;
    CTX.response_pending = 1;
    return ZCL_SENSOR_HANDLED;
}

/* Enabled, past min and past max or the reportable change (0: any change). */
static uint8_t due(uint8_t entry)
{
    zcl_sensor_reporting_t MCU_XDATA *r = reporting(entry);
    ST.value = value(entry);
    if (r->max == 0xffffu || r->age < r->min) return 0;
    if (r->max && r->age >= r->max) return 1;
    ST.delta = ST.value-r->last;
    if ((int16_t)ST.delta < 0) ST.delta = (uint16_t)-ST.delta;
    if (!ST.delta || ST.delta < r->change) return 0;
    return 1;
}

/* One frame: every due attribute of the first due bound cluster. */
static uint8_t report(void)
{
    const MCU_CODE zcl_attribute_t *a;
    uint8_t entry, first = MODEL, endpoint = 1, i;
    ST.pending = BASIC_REPORT;
    if (!ST.basic) {
        ST.pending = 0;
        first = NONE;
        for (entry = 0; entry < ZCL_SENSOR_REPORTS; entry++) {
            a = &attributes[entry];
            if (first != NONE && (a->cluster != attributes[first].cluster || a->ep != attributes[first].ep)) continue;
            if (!present(a->ep, a->cluster) || (i = slot(entry)) == NONE || !ST.bound[i] || !due(entry)) continue;
            if (first == NONE) { first = entry; endpoint = ST.bound[i]; }
            ST.pending |= (uint16_t)1u << entry;
        }
        if (first == NONE) return ZCL_SENSOR_IDLE;
    }
    memset(&RX, 0, sizeof(RX));
    RX.nwk.version = 2; RX.nwk.radius = 30;
    RX.aps.flags = APS_FLAG_ACK_REQUEST;
    RX.aps.cluster_id = attributes[first].cluster;
    RX.aps.profile_id = DEV.config.transport.profile;
    RX.aps.source_endpoint = attributes[first].ep;
    RX.aps.destination_endpoint = endpoint;
    ST.out = IN; ST.at = 0;
    emit(0x18); emit(ST.tsn); emit(0x0a);
    for (entry = first; entry == first || entry < ZCL_SENSOR_REPORTS; entry++) {
        if (entry != first && !(ST.pending & (uint16_t)1u << entry)) continue;
        ST.entry = entry; ST.type = attributes[entry].type;
        emit16(attributes[entry].id); emit(ST.type);
        put();
    }
    RX.length = ST.at;
    return ZCL_SENSOR_REPORT;
}

uint8_t zcl_sensor_init(const security_keys_config_t MCU_XDATA *identity) JOIN_FAR
{
    uint8_t i;
    if (!identity || !known_ieee(identity->own_ieee) || !known_ieee(identity->tc_ieee) ||
        memcmp(identity->own_ieee, DEV.config.association.extraction.local, 8) ||
        DEV.config.transport.endpoint != 1u || DEV.config.transport.profile != PROFILE)
        return 0;
    memcpy(ST.tc_ieee, identity->tc_ieee, 8);
    memset((uint8_t MCU_XDATA *)&ST.report, 0, sizeof(ST)-offsetof(zcl_sensor_state_t, report));
    ST.channel = identity->channel;
    for (i = 0; i < ZCL_SENSOR_REPORTS; i++) {
        restore(i);
        reporting(i)->last = value(i);
        /* The first measurement reports at once. */
        reporting(i)->age = reporting(i)->min;
    }
    for (i = 0; i < ZCL_FLOWER_SLOTS; i++) ST.bound[i] = present(slots[i].ep, slots[i].cluster) ? 1u : 0u;
    ST.second = CTX.last;
    ST.basic = 1;
    return 1;
}

void zcl_sensor_tick(void) JOIN_FAR
{
    zcl_sensor_reporting_t MCU_XDATA *r;
    uint8_t entry;
    if (ST.sent) {
        ST.sent = 0;
        ST.tsn++;
        if (ST.pending == BASIC_REPORT) ST.basic = 0;
        else
            for (entry = 0; entry < ZCL_SENSOR_REPORTS; entry++)
                if (ST.pending & (uint16_t)1u << entry) {
                    r = reporting(entry);
                    r->last = value(entry); r->age = 0;
                }
    }
    while ((uint32_t)(CTX.last-ST.second) >= ZCL_SENSOR_SECOND) {
        ST.second += ZCL_SENSOR_SECOND;
        for (r = ST.report; r != ST.report+ZCL_SENSOR_REPORTS; r++)
            if (r->age != 0xffffu) r->age++;
        if (ST.identify) ST.identify--;
    }
}

uint8_t zcl_sensor_serve(void) JOIN_FAR
{
    if (CTX.application_ready) {
        if (RX.nwk.destination >= 0xfffbu || RX.aps.delivery_mode) return ZCL_SENSOR_DISCARDED;
        if (!RX.aps.profile_id && !RX.aps.destination_endpoint) {
            if (RX.aps.cluster_id == ZDO_SRV_MGMT_BIND_REQUEST) return management_bind();
            return RX.aps.cluster_id == ZDO_SRV_BIND_REQUEST || RX.aps.cluster_id == ZDO_SRV_UNBIND_REQUEST ?
                bind() : ZCL_SENSOR_DISCARDED;
        }
        return zcl();
    }
    if (DEV.application_pending || DEV.application_done) return ZCL_SENSOR_IDLE;
    return report();
}

/* One simple descriptor: the endpoint's present FFFD clusters as inputs. */
static uint8_t MCU_XDATA *describe(uint8_t MCU_XDATA *d, uint8_t ep)
{
    const MCU_CODE zcl_attribute_t *a = attributes;
    uint8_t i, n = 8;
    d[1] = ep; d[2] = (uint8_t)PROFILE; d[3] = (uint8_t)(PROFILE >> 8);
    d[4] = 0x0c; d[5] = 0; d[6] = 2;   /* Simple Sensor, version 2 */
    for (i = 0; i < ATTRIBUTES; i++, a++)
        if (a->ep == ep && a->id == 0xfffdu && present(ep, a->cluster)) {
            d[n++] = (uint8_t)a->cluster; d[n++] = (uint8_t)(a->cluster >> 8);
        }
    d[7] = (uint8_t)((n-8u) >> 1);
    d[n] = 0;
    d[0] = n;
    return d+n+1u;
}

/* Timer 3 channel 1 (alternative 1, P1.4) toggles at T3CC0 in up/down mode:
 * the soil probe excitation. P1.4 stays a low GPIO until power(1). */
static void power(uint8_t on)
{
    if (on) {
        MMIO_SET(SOC_P1, BOARD_FLOWER_RAIL);
        MMIO_SET(SOC_P1SEL, BOARD_FLOWER_PWM);
        MMIO_SET(SOC_T3CTL, 0x10u);
        return;
    }
    MMIO_CLEAR(SOC_T3CTL, 0x10u);
    MMIO_SET(SOC_T3CTL, 0x04u);
    MMIO_CLEAR(SOC_P1, BOARD_FLOWER_PWM | BOARD_FLOWER_RAIL);
    MMIO_CLEAR(SOC_P1SEL, BOARD_FLOWER_PWM);
}

void zcl_sensor_boot(void) JOIN_FAR
{
    uint8_t MCU_XDATA *d;
    uint8_t i;
    memset(&ST, 0, sizeof(ST));
    for (i = 0; i < ZCL_SENSOR_REPORTS; i++)
        ST.values[i] = attributes[i].type == 0x29u ? ZCL_FLOWER_INVALID_S16 : ZCL_FLOWER_INVALID_U16;
    ST.interval = ZCL_FLOWER_INTERVAL;
    MMIO_CLEAR(SOC_PERCFG, 0x20u);
    MMIO_SET(SOC_P2SEL, 0x20u);
    MMIO_WRITE(SOC_T3CTL, 0x07u);
    MMIO_WRITE(SOC_T3CCTL1, 0x14u);
    MMIO_WRITE(SOC_T3CC0, 4u);
    d = describe(zdo_srv_descriptors, 1);
    if (present(2, 0)) d = describe(d, 2);
    *d = 0;
}

/* n single conversions (ADCCON3, SWRU191F 12.2.10) averaged as 14-bit
 * positive codes; FFFF when a conversion misses its poll bound. */
static uint16_t adc(uint8_t command, uint8_t n)
{
    uint32_t sum = 0;
    uint16_t polls, code;
    uint8_t i, low;
    for (i = n; i; i--) {
        (void)MMIO_READ(SOC_ADCH);
        MMIO_WRITE(SOC_ADCCON3, command);
        for (polls = ZCL_FLOWER_ADC_POLLS; !(MMIO_READ(SOC_ADCCON1) & 0x80u); )
            if (!--polls) return ZCL_FLOWER_INVALID_U16;
        low = MMIO_READ(SOC_ADCL);
        code = (uint16_t)MMIO_READ(SOC_ADCH) << 8 | low;
        if (!(code & 0x8000u)) sum += code >> 2;
    }
    return (uint16_t)(sum / n);
}

/* flower battery.c Alkaline 2S curve, 0.5 % units. */
static uint8_t percent(uint16_t mv)
{
    uint16_t p;
    if (mv >= 3000u) p = 100;
    else if (mv > 2800u) p = 80u + (mv-2800u)*20u/200u;
    else if (mv > 2500u) p = 40u + (mv-2500u)*40u/300u;
    else if (mv > 2200u) p = 10u + (mv-2200u)*30u/300u;
    else if (mv > 2000u) p = (mv-2000u)*10u/200u;
    else p = 0;
    return (uint8_t)(p*2u);
}

/* flower zclApp_ReadSoilHumidity: air..water maps to 0..100.00 %, within
 * one calibrated span beyond either end; otherwise invalid. */
static uint16_t moisture(uint16_t raw, uint16_t battery)
{
    uint16_t air = ZCL_FLOWER_AIR(battery), water = ZCL_FLOWER_WATER(battery), span;
    if (raw > ZCL_FLOWER_ADC_MAX || air > ZCL_FLOWER_ADC_MAX || battery > ZCL_FLOWER_ADC_MAX || air <= water)
        return ZCL_FLOWER_INVALID_U16;
    span = air-water;
    if (raw < (water > span ? (uint16_t)(water-span) : 0u) ||
        (uint32_t)raw > ((uint32_t)air+span < ZCL_FLOWER_ADC_MAX ? (uint32_t)air+span : ZCL_FLOWER_ADC_MAX))
        return ZCL_FLOWER_INVALID_U16;
    if (raw >= air) return 0;
    if (raw <= water) return 10000u;
    return (uint16_t)((uint32_t)(air-raw)*10000u/span);
}

/* ZCL MeasuredValue 10000*log10(raw)+1 with the raw ADC as uncalibrated lux:
 * log2 by repeated squaring of the Q15 mantissa (12 fraction bits). */
static uint16_t illuminance(uint16_t raw)
{
    uint32_t m;
    uint16_t l, bit;
    uint8_t k = 0;
    if (raw >= ZCL_FLOWER_LIGHT_OPEN) return ZCL_FLOWER_INVALID_U16;
    if (!raw) return 0;
    for (l = raw; l > 1u; l >>= 1) k++;
    m = (uint32_t)raw << (15u-k);
    l = (uint16_t)k << 12;
    for (bit = 0x800u; bit; bit >>= 1) {
        m = (m*m) >> 15;
        if (m >= 0x10000UL) { m >>= 1; l |= bit; }
    }
    return (uint16_t)(((uint32_t)l*30103u+40959u)/40960u+1u);
}

/* Battery (VDD/3, internal reference), then illuminance and soil (AVDD
 * reference) like flower's APP_PHASE_ADC; the soil curve uses the battery
 * raw ADC. */
static void measure(void)
{
    uint16_t battery, light, soil, mv;
    MMIO_SET(SOC_APCFG, BOARD_FLOWER_SOIL | BOARD_FLOWER_LIGHT);
    battery = adc(0x3fu, 5);
    light = adc(0xb7u, 5);
    soil = adc(0xb4u, 3);
    MMIO_CLEAR(SOC_APCFG, BOARD_FLOWER_SOIL | BOARD_FLOWER_LIGHT);
    ST.values[ZCL_FLOWER_BATTERY_RAW] = ST.values[ZCL_FLOWER_SOIL_BATTERY] = battery;
    if (battery > ZCL_FLOWER_ADC_MAX) {
        ST.values[ZCL_FLOWER_VOLTAGE] = ST.values[ZCL_FLOWER_PERCENT] = 0xffu;
    } else {
        mv = ZCL_FLOWER_MILLIVOLTS(battery);
        ST.values[ZCL_FLOWER_VOLTAGE] = (uint16_t)((mv+50u)/100u);
        ST.values[ZCL_FLOWER_PERCENT] = percent(mv);
    }
    ST.values[ZCL_FLOWER_SOIL_RAW] = soil;
    ST.values[ZCL_FLOWER_SOIL] = moisture(soil, battery);
    ST.values[ZCL_FLOWER_ILLUMINANCE] = illuminance(light);
}

uint32_t zcl_sensor_quiet(uint32_t now) JOIN_FAR
{
    uint32_t left;
    if (ST.phase) {
        if ((int32_t)(now-ST.deadline) < 0) return 0;
        measure();
        power(0);
        ST.phase = 0;
        ST.started = 1;
        ST.deadline = now+(uint32_t)ST.interval*ZCL_SENSOR_SECOND;
        return 0;
    }
    left = ST.deadline-now;
    if (ST.started && left && left < 0x80000000UL) return left;
    power(1);
    ST.phase = 1;
    ST.deadline = now+ZCL_FLOWER_WARMUP;
    return 0;
}
