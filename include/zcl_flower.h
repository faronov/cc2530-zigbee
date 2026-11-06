/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_FLOWER_H
#define ZCL_FLOWER_H
/* Included by zcl_sensor.h for CC2530_FLOWER (board flower_v2): the DIYRuZ
 * FlowerX soil sensor application, wire-compatible with the faronov/flower
 * Zigbee2MQTT converter and ZHA quirk. Endpoint 1: Basic, Identify, Power
 * Configuration, Diagnostics (0x0B05), Soil Moisture (0x0408), Illuminance
 * and, with a BME280, Temperature, Relative Humidity and Pressure. Without
 * any external sensor, Temperature on endpoint 1 is the CC2530 die sensor.
 * Endpoint 2: DS18B20 Temperature. The irrigation attributes are read-only
 * constants (irrigation disabled). RAM only: a reset restores defaults. */

/* Reportable values, indexed like report[] and values[]. */
#define ZCL_FLOWER_VOLTAGE 0u
#define ZCL_FLOWER_PERCENT 1u
#define ZCL_FLOWER_BATTERY_RAW 2u
#define ZCL_FLOWER_SOIL 3u
#define ZCL_FLOWER_SOIL_RAW 4u
#define ZCL_FLOWER_SOIL_BATTERY 5u
#define ZCL_FLOWER_ILLUMINANCE 6u
#define ZCL_FLOWER_TEMPERATURE 7u
#define ZCL_FLOWER_HUMIDITY 8u
#define ZCL_FLOWER_PRESSURE 9u
#define ZCL_FLOWER_PRESSURE_SCALED 10u
#define ZCL_FLOWER_PROBE 11u
#define ZCL_SENSOR_REPORTS 12u
/* Coordinator binding slots (endpoint, cluster). */
#define ZCL_FLOWER_SLOTS 8u
#define ZCL_FLOWER_DESCRIPTORS 40u

#define ZCL_FLOWER_BME280 1u
#define ZCL_FLOWER_DS18B20 2u

#define ZCL_SENSOR_SECOND 62500UL
#define ZCL_FLOWER_INTERVAL 300u
#define ZCL_FLOWER_INTERVAL_MIN 30u
#define ZCL_FLOWER_INTERVAL_MAX 3600u
/* Sensor rail and soil excitation settle before the ADC window (symbols). */
#define ZCL_FLOWER_WARMUP 3125UL
#define ZCL_FLOWER_ADC_POLLS 4000u

#define ZCL_FLOWER_INVALID_U16 0xffffu
#define ZCL_FLOWER_INVALID_S16 0x8000u
/* CC2530 single-ended 14-bit positive half range; illuminance at or above
 * ZCL_FLOWER_LIGHT_OPEN is an unconnected input. */
#define ZCL_FLOWER_ADC_MAX 8191u
#define ZCL_FLOWER_LIGHT_OPEN 8000u
/* flower battery.c: mV = raw * 0.443 (VDD/3, internal 1.15 V reference). */
#define ZCL_FLOWER_MILLIVOLTS(raw) ((uint16_t)(((uint32_t)(raw) * 443u) / 1000u))
/* flower zcl_app.h soil calibration against the battery raw ADC:
 * air 0.179*raw + 3926, water 0.146*raw + 2020. */
#define ZCL_FLOWER_AIR(raw) ((uint16_t)(((uint32_t)(raw) * 179u + 3926000u) / 1000u))
#define ZCL_FLOWER_WATER(raw) ((uint16_t)(((uint32_t)(raw) * 146u + 2020000u) / 1000u))

/* Die sensor (SWRS081B): 1480 12-bit counts at 25 C and 3 V, 4.5 per C and
 * 1 per 100 mV; as the 14-bit averages of adc() 5920, 18 per C, 4 per 100 mV.
 * EXPERIMENTAL, undocumented by TI: the big-endian information-page word at
 * ZCL_FLOWER_FACTORY (left-aligned like ADCH:ADCL) is assumed to be this
 * chip's 25 C / 3 V reading and replaces the nominal when its 12 bits lie in
 * 1380..1580 (an erased FFFF keeps the nominal). */
#define ZCL_FLOWER_FACTORY 0x781eu
#define ZCL_FLOWER_DIE_NOMINAL 5920
#define ZCL_FLOWER_FACTORY_VALID(word) ((uint16_t)(word) >= (uint16_t)(1380u << 4) && \
    (uint16_t)(word) < (uint16_t)(1581u << 4))
#define ZCL_FLOWER_DIE_REFERENCE(word) ((int16_t)(ZCL_FLOWER_FACTORY_VALID(word) ? \
    (int16_t)((uint16_t)(word) >> 2) : ZCL_FLOWER_DIE_NOMINAL))
/* centi-C = 2500 + (die - reference - (mV-3000)/25) * 100/18, 50/9 ~ 1422/256 */
#define ZCL_FLOWER_DIE_CENTI(die, mv, word) ((int16_t)(2500 + (int16_t)(((int32_t)((int16_t)(die) - \
    ZCL_FLOWER_DIE_REFERENCE(word) - ((int16_t)(mv)-3000)/25) * 1422 + 128) >> 8)))

typedef struct {
    /* Kept by zcl_sensor_init: measurements, schedule and configuration. */
    uint16_t values[ZCL_SENSOR_REPORTS];
    uint32_t deadline;
    uint16_t interval;
    uint8_t sensors, phase, started;
    /* Last die sensor average (FFFF invalid) and information-page word:
     * Diagnostics FF12/FF13, for checking the factory reference. */
    uint16_t die, factory;
    /* zcl_sensor_init copies tc_ieee and clears everything from report on. */
    uint8_t tc_ieee[8];
    zcl_sensor_reporting_t report[ZCL_SENSOR_REPORTS];
    uint32_t second;
    uint16_t identify, value, pending;
    uint8_t MCU_XDATA *out;
    /* Coordinator endpoint per binding slot; 0 is unbound. */
    uint8_t bound[ZCL_FLOWER_SLOTS];
    uint8_t basic, sent, tsn, channel, ep;
    /* Receive scratch. */
    uint16_t id, min, max, delta;
    uint8_t length, header, at, in, entry, type, kind, dir, status, apply;
} zcl_sensor_state_t;

extern MCU_XDATA zcl_sensor_state_t zcl_flower_state;
#define ZCL_SENSOR_STATE zcl_flower_state

/* Once at boot, before the first poll: power-on defaults, sensor discovery
 * and the ZDO simple descriptors (zdo_srv.h CC2530_ZDO_DESCRIPTORS). */
void zcl_sensor_boot(void) JOIN_FAR;
/* Called by join_smoke_sleep at a quiescent READY point with the current MAC
 * symbol time: advances the measurement cycle. Returns 0 when the device must
 * stay awake (work was done, or the soil excitation timer, which PM2 stops,
 * is running), otherwise the symbols until the next sensor deadline. */
uint32_t zcl_sensor_quiet(uint32_t now) JOIN_FAR;

#endif
