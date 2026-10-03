/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_SENSOR_H
#define ZCL_SENSOR_H
#include "bdb_join.h"
#include "board.h"

/* SYNTHETIC demonstration values, NOT measurements: one sawtooth phase
 * 0..40 over the 8-bit sample index, bits 22..29 of the runtime's MAC-symbol
 * time (zdo.last), so one step per 2^22 symbols (~67 s). Temperature
 * 21.00..25.00 C in 0.10 steps, humidity 45.00..55.00 %RH in 0.25 steps,
 * BatteryPercentageRemaining 100..80 % in 0.5 % steps and BatteryVoltage
 * 3.0..2.8 V. The 256-sample index wrap (~4.8 h) restarts the phase. 8-bit
 * operands keep SDCC on inline DIV/MUL AB. */
#define ZCL_SENSOR_SAMPLE(now) ((uint8_t)((uint16_t)((uint32_t)(now) >> 16) >> 6))
#define ZCL_SENSOR_PHASE(sample) ((uint8_t)((uint8_t)(sample) % (uint8_t)41))
#define ZCL_SENSOR_TEMPERATURE(phase) ((uint16_t)(2100u + (uint16_t)((uint8_t)(phase) * (uint8_t)10)))
#define ZCL_SENSOR_HUMIDITY(phase) ((uint16_t)(4500u + (uint16_t)((uint8_t)(phase) * (uint8_t)25)))
#define ZCL_SENSOR_PERCENTAGE(phase) ((uint8_t)(200u - (uint8_t)(phase)))
#define ZCL_SENSOR_VOLTAGE(phase) ((uint8_t)(30u - (uint8_t)((uint8_t)(phase) / (uint8_t)14)))

/* Reportable attributes: temperature, humidity, BatteryVoltage and
 * BatteryPercentageRemaining; Basic ModelIdentifier is BASIC. */
#define ZCL_SENSOR_REPORTS 4u
#define ZCL_SENSOR_BASIC 4u
#define ZCL_SENSOR_SECOND 62500UL

enum { ZCL_SENSOR_IDLE = 0, ZCL_SENSOR_REPORT, ZCL_SENSOR_HANDLED, ZCL_SENSOR_DISCARDED };

typedef struct {
    uint16_t min, max, change, last, age;
} zcl_sensor_reporting_t;

/* RAM-only application state. tc_ieee must stay first: init copies it
 * upward from the association record that this state then overlays. */
typedef struct {
    uint8_t tc_ieee[8];
    zcl_sensor_reporting_t report[ZCL_SENSOR_REPORTS];
    uint32_t second;
    uint16_t identify, value;
    uint8_t MCU_XDATA *out;
    /* Bound coordinator endpoint for temperature, humidity and power; 0 is unbound. */
    uint8_t bound[3];
    uint8_t basic, pending, sent, tsn;
    /* Receive scratch: kept here, not in SDCC XSEG locals. */
    uint16_t id, min, max, delta;
    uint8_t length, header, at, in, entry, type, kind, dir, status, apply;
} zcl_sensor_state_t;

/* The single application device, owned by join_smoke. In READY its
 * association record is dead (see bdb_join.h) and holds this state. */
extern MCU_XDATA bdb_join_t join_smoke_device;
#define ZCL_SENSOR_STATE (*(zcl_sensor_state_t MCU_XDATA *)&join_smoke_device.record)

/* Call once on the first READY observation: claims the record overlay, binds
 * the three reportable clusters locally to coordinator endpoint 1, loads the
 * default reporting configuration and queues one Basic ModelIdentifier
 * report (an uninterviewed device's first frame invites the interview). */
void zcl_sensor_init(void) JOIN_FAR;
/* Each READY poll. A frame the runtime published (application_ready) is
 * served first: Read Attributes, Write Attributes (IdentifyTime) and Configure
 * Reporting for Basic, Power Configuration, Identify, Temperature and
 * Relative Humidity Measurement on the transport endpoint, Identify/Identify
 * Query, Default Responses (other general commands, including Read Reporting
 * Configuration, are UNSUP_GENERAL_COMMAND), and ZDO Bind/Unbind for the
 * coordinator. Replies use the runtime server response slot; the
 * published frame is always cleared. Otherwise, with no application TX
 * outstanding, it builds at most one due unsolicited ACK-requested Report
 * Attributes frame in the EMPTY application slot and returns REPORT; the
 * caller queues it and, once accepted, sets ZCL_SENSOR_STATE.sent. The next
 * call commits that report. RAM only: a reset restores the defaults. All
 * reported values are SYNTHETIC demonstration data, never measurements. */
uint8_t zcl_sensor_serve(void) JOIN_FAR;

#endif
