/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_SENSOR_H
#define ZCL_SENSOR_H
#include "bdb_join.h"

/* Builds one unsolicited, ACK-requested ZCL Report Attributes frame in the
 * READY device's EMPTY runtime application slot, from its configured profile
 * and endpoint to the coordinator's endpoint 1. TSN is the 2^21-symbol report
 * slot of the runtime time (zdo.last). With basic set
 * it reports Basic ModelIdentifier; otherwise it reports the SYNTHETIC
 * MeasuredValue (see ZDO_RUNTIME_SAMPLE): temperature in even slots, humidity
 * in odd ones. The caller queues it. Values are demonstration data, not
 * measurements. */
void zcl_sensor_report(bdb_join_t MCU_XDATA * volatile device, volatile uint8_t basic) JOIN_FAR;

#endif
