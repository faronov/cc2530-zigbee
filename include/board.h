/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_BOARD_H
#define CC2530_BOARD_H

#include "cc2530_mmio.h"

#define BOARD_GENERIC 0u
#define BOARD_LG_ESL29_REV03 1u
#define BOARD_EXTERNAL 2u
#define BOARD_POLICY_DISPLAY_OFF 0x01u

#ifndef CC2530_BOARD
#error The build must select CC2530_BOARD
#endif
#if CC2530_BOARD != BOARD_GENERIC && CC2530_BOARD != BOARD_LG_ESL29_REV03 && \
    CC2530_BOARD != BOARD_EXTERNAL
#error Unsupported CC2530 board
#endif
/* An external board belongs to an external application (docs/EXTERNAL_APP.md):
 * its app_config.h, included by cc2530_mmio.h, supplies BOARD_MANUFACTURER,
 * BOARD_MODEL, BOARD_MODEL_LENGTH and optionally BOARD_CCA_THRESHOLD_RAW. */
#if CC2530_BOARD == BOARD_EXTERNAL && !defined(CC2530_APP)
#error CC2530_BOARD=BOARD_EXTERNAL requires an external application (CC2530_APP)
#endif

/* ZCL Basic identity strings; metadata only, no hardware access. */
#if CC2530_BOARD != BOARD_EXTERNAL
#define BOARD_MANUFACTURER "cc2530-zigbee"
#if CC2530_BOARD == BOARD_LG_ESL29_REV03
#define BOARD_MODEL "LG-ESL29"
#define BOARD_MODEL_LENGTH 8u
#else
#define BOARD_MODEL "generic"
#define BOARD_MODEL_LENGTH 7u
#endif
#endif
#if !defined(BOARD_MANUFACTURER) || !defined(BOARD_MODEL) || !defined(BOARD_MODEL_LENGTH)
#error The board must define BOARD_MANUFACTURER, BOARD_MODEL and BOARD_MODEL_LENGTH
#endif

/* MAC CSMA energy-only CCA threshold, raw CCACTRL0 (two's complement RSSI,
 * about dBm+73, SWRS081B). F8 (about -81 dBm, clear below -83 with the
 * hysteresis of two) is the TI reference value. A board whose idle noise floor
 * keeps F8 busy may raise it up to FE (-75 dBm, the IEEE 802.15.4-2006 6.9.9
 * maximum: sensitivity -85 dBm plus 10 dB). */
#ifndef BOARD_CCA_THRESHOLD_RAW
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

#if CC2530_BOARD == BOARD_LG_ESL29_REV03 || CC2530_BOARD == BOARD_EXTERNAL
/* Called before C data initialization; only direct MMIO operations are allowed. */
void board_early_off(void);
#endif

#endif
