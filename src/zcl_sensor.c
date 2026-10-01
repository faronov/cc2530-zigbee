/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_sensor.h"
#include <string.h>

void zcl_sensor_report(bdb_join_t MCU_XDATA * volatile device, volatile uint8_t basic)
{
    zdo_runtime_t MCU_XDATA *ctx = &device->work.runtime.zdo;
    ed_packet_t MCU_XDATA *report = &ctx->application;
    uint8_t i;
    uint16_t value;
    memset(report, 0, sizeof(*report));
    report->nwk.version = 2; report->nwk.radius = 30;
    report->aps.flags = APS_FLAG_ACK_REQUEST;
    report->aps.profile_id = device->config.transport.profile;
    report->aps.source_endpoint = device->config.transport.endpoint;
    report->aps.destination_endpoint = 1;
    report->payload[0] = 0x18; report->payload[1] = (uint8_t)(ctx->last >> 21);
    report->payload[2] = 0x0a;
    if (basic) {
        report->payload[3] = 5; report->payload[5] = 0x42;
        report->payload[6] = sizeof(zdo_runtime_model)-1u;
        for (i = 0; i < sizeof(zdo_runtime_model)-1u; i++)
            report->payload[7+i] = (uint8_t)zdo_runtime_model[i];
        report->length = (uint8_t)(6u+sizeof(zdo_runtime_model));
        return;
    }
    i = ZDO_RUNTIME_SAMPLE(ctx->last);
    report->length = 8;
    if (report->payload[1] & 1u) {
        report->aps.cluster_id = ZDO_SRV_HUMIDITY_CLUSTER;
        report->payload[5] = 0x21; value = ZDO_RUNTIME_HUMIDITY(i);
        report->payload[6] = (uint8_t)value; report->payload[7] = (uint8_t)(value >> 8);
        return;
    }
    report->aps.cluster_id = ZDO_SRV_TEMPERATURE_CLUSTER;
    report->payload[5] = 0x29; value = ZDO_RUNTIME_TEMPERATURE(i);
    report->payload[6] = (uint8_t)value; report->payload[7] = (uint8_t)(value >> 8);
}
