/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_BOARD_H
#define CC2530_BOARD_H

#include "cc2530_mmio.h"

#define BOARD_GENERIC 0u
#define BOARD_LG_ESL29_REV03 1u
#define BOARD_FLOWER_V2 2u
#define BOARD_POLICY_DISPLAY_OFF 0x01u

#ifndef CC2530_BOARD
#error The build must select CC2530_BOARD
#endif
#if CC2530_BOARD != BOARD_GENERIC && CC2530_BOARD != BOARD_LG_ESL29_REV03 && \
    CC2530_BOARD != BOARD_FLOWER_V2
#error Unsupported CC2530 board
#endif

/* ZCL Basic identity strings; metadata only, no hardware access. */
#if CC2530_BOARD == BOARD_FLOWER_V2
/* Wire identity of faronov/flower (DIYRuZ FlowerX); the Z2M converter and the
 * ZHA quirk match on these strings. */
#define BOARD_MANUFACTURER "modkam.ru"
#define BOARD_MODEL "DIYRuZ_FlowerX"
#define BOARD_MODEL_LENGTH 14u
#else
#define BOARD_MANUFACTURER "cc2530-zigbee"
#endif
#if CC2530_BOARD == BOARD_FLOWER_V2
#elif CC2530_BOARD == BOARD_LG_ESL29_REV03
#define BOARD_MODEL "LG-ESL29"
#define BOARD_MODEL_LENGTH 8u
#else
#define BOARD_MODEL "generic"
#define BOARD_MODEL_LENGTH 7u
#endif

/* MAC CSMA energy-only CCA threshold, raw CCACTRL0 (two's complement RSSI,
 * about dBm+73, SWRS081B). F8 (about -81 dBm, clear below -83 with the
 * hysteresis of two) is the TI reference value. The FlowerX E18-MS1 board idles
 * at raw -9..-10 on channels 15-20 (debugger-measured, one board), so F8 held
 * channel 15 busy and the beacon request never left; FE is -75 dBm, the
 * IEEE 802.15.4-2006 6.9.9 maximum (sensitivity -85 dBm plus 10 dB). */
#if CC2530_BOARD == BOARD_FLOWER_V2
#define BOARD_CCA_THRESHOLD_RAW 0xfeu
#else
#define BOARD_CCA_THRESHOLD_RAW 0xf8u
#endif

/* CC2530_CHIP_SENSE reporting (zcl_sensor.h). BatteryPercentageRemaining is
 * linear in AVDD between EMPTY and FULL millivolts (FULL-EMPTY a multiple of
 * 200); ASSUMPTION, not a measured cell curve: 2.0 V is the CC2530 minimum
 * supply (SWRS081B), 3.0 V a fresh 3 V lithium cell. The die-temperature
 * offset (centi-degrees C) is added to the uncalibrated datasheet nominal. */
#define BOARD_BATTERY_EMPTY_MV 2000u
#define BOARD_BATTERY_FULL_MV 3000u
#define BOARD_CHIP_TEMPERATURE_OFFSET 0

/* CC2530_SHT3X bit-banged I2C on port 1 (open drain through P1DIR, latch 0).
 * ASSUMPTION, not a verified LG ESL29 pad: P1.6 SDA and P1.7 SCL are unused by
 * the display (P0.2-P0.5, P0.7, P1.1, P1.2); pull-ups are external or the
 * reset-default port 1 pull-ups (SWRU191F P1INP=0, P2INP.PDUP1=0).
 * Address 0x44 is the SHT3x-DIS ADDR-low address. */
#define BOARD_SHT3X_SDA 0x40u
#define BOARD_SHT3X_SCL 0x80u
#define BOARD_SHT3X_ADDRESS 0x44u

typedef struct {
    uint8_t identifier;
    uint8_t policy;
} board_description_t;

extern const MCU_CODE board_description_t board_description;

/* faronov/flower v2 (E18-MS1-PCB), HAL_BOARD_FLOWER pin map of the reference
 * firmware: LED P0.1 active high, button P2.0 active low (pull-up), sensor
 * rail P1.1 high=on, soil excitation P1.4 (Timer3 alt1 ch1), soil AIN4 P0.4,
 * light AIN7 P0.7, BME280 I2C P0.5 SCL / P0.6 SDA, DS18B20 1-Wire P1.3. */
#define BOARD_FLOWER_LED 0x02u
#define BOARD_FLOWER_BUTTON 0x01u
#define BOARD_FLOWER_RAIL 0x02u
#define BOARD_FLOWER_PWM 0x10u
#define BOARD_FLOWER_SOIL 0x10u
#define BOARD_FLOWER_LIGHT 0x80u
#define BOARD_FLOWER_SCL 0x20u
#define BOARD_FLOWER_SDA 0x40u
#define BOARD_FLOWER_ONEWIRE 0x08u

#if CC2530_BOARD == BOARD_LG_ESL29_REV03 || CC2530_BOARD == BOARD_FLOWER_V2
/* Called before C data initialization; only direct MMIO operations are allowed. */
void board_early_off(void);
#endif

#endif
