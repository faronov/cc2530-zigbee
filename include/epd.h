/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef EPD_H
#define EPD_H

#include "board.h"
#include "banked_join.h"

/* CC2530_EPD (opt-in, LG ESL29 only): the 2.9" 128x296 black/white e-paper
 * panel behind a UC8151D controller shows the last temperature (large), the
 * SHT3x humidity (CC2530_SHT3X only) and AVDD. H2 evidence (LH27 debugger
 * probes, not a datasheet requirement):
 * - P0.7 high powers the panel; P1.1 is RST (low active), P1.2 BUSY (low
 *   while busy); P0.2 DC (0 command, 1 data), P0.4 CS (low active), P0.3
 *   MOSI and P0.5 SCK on USART0 alternative 1 SPI master, MSB first, mode 0,
 *   1 MHz (the stock firmware's U0GCR 0x2F), CS low per octet;
 * - the stock sequence: power, reset, 06 17 17 17, 04 (BUSY low ~50 ms),
 *   00 1F 0D (LUT from OTP, black/white), 61 80 01 28 (128x296), 82 12,
 *   50 97, 10 + 4736 old octets, 13 + 4736 new octets, 12 (BUSY low
 *   4.91 s at 22 C), 02 (BUSY low ~40 ms), then P0.7 low. The OTP waveform
 *   is temperature dependent, so the refresh bound is generous;
 * - each of the 296 gate rows carries 16 source octets, octet 0 at the top,
 *   bit 1 white, 0 black; with the ribbon on the left gate row 0 is the
 *   rightmost screen column. ASSUMPTION: the octet's MSB is its top pixel.
 * No frame buffer: every gate row is rendered on the fly into 16 octets,
 * a few rows per READY poll. The old plane is a rendering of the text shown
 * last (white after reset), the new plane of the current text. */
#define EPD_ROWS 296u
#define EPD_OCTETS 16u
#define EPD_TEXT 8u
#define EPD_FIELDS 3u
#define EPD_ROWS_PER_STEP 4u
/* 16-us MAC symbols. */
#define EPD_MS(ms) ((uint32_t)(ms) * 125u / 2u)
#define EPD_SECONDS(s) ((uint32_t)(s) * 62500UL)
#define EPD_RESET_WAIT EPD_MS(20)
#define EPD_BUSY_WAIT EPD_MS(2)
#define EPD_START_LIMIT EPD_MS(500)
#define EPD_REFRESH_EXPECTED EPD_MS(5000)
#define EPD_REFRESH_LIMIT EPD_MS(30000)
#define EPD_SLEEP_POLL EPD_MS(250)
/* A changed text starts a refresh at most once per INTERVAL; an unchanged
 * one is compared again after CHECK. A failure also waits INTERVAL. */
#define EPD_INTERVAL EPD_SECONDS(600)
#define EPD_CHECK EPD_SECONDS(10)
#define EPD_NO_HUMIDITY 0xfffeu

enum {
    EPD_IDLE = 0, EPD_RESET, EPD_BOOT, EPD_POWER, EPD_OLD, EPD_NEW, EPD_REFRESH, EPD_OFF
};

/* Glyph codes: digits 0..9, then the symbols below. */
enum { EPD_DOT = 10, EPD_MINUS, EPD_DEGREE, EPD_C, EPD_PERCENT, EPD_V, EPD_GLYPHS };

typedef struct {
    uint8_t length[EPD_FIELDS];
    uint8_t code[EPD_FIELDS][EPD_TEXT];
} epd_text_t;

/* armed: a refresh was started once (due is valid); failed: the state of
 * the last failure (0 none); duration: the last refresh BUSY time in
 * 1.024-ms units (symbols >> 6); old: the text on the panel. */
typedef struct {
    uint8_t state, armed, failed, failures;
    uint16_t row, refreshes, duration;
    uint32_t at, due;
    epd_text_t old, next;
    uint8_t column[EPD_OCTETS];
} epd_t;

#if defined(CC2530_EPD)
#if CC2530_BOARD != BOARD_LG_ESL29_REV03 || !defined(CC2530_CHIP_SENSE)
#error CC2530_EPD requires the LG ESL29 board and CC2530_CHIP_SENSE
#endif
extern MCU_XDATA epd_t epd;
/* Formats the three fields: centi-degrees C (ZCL invalid 0x8000 shows
 * --.-), humidity in 0.01 %RH (0xFFFF shows --, EPD_NO_HUMIDITY leaves the
 * field empty) and millivolts (0 shows -.--). */
void epd_format(epd_text_t MCU_XDATA *text, int16_t centi, uint16_t humidity, uint16_t millivolts) JOIN_FAR;
/* Renders gate row 0..295 of text into epd.column. */
void epd_render(const epd_text_t MCU_XDATA *text, uint16_t row) JOIN_FAR;
/* READY only, after zcl_sensor_tick: while idle and due, formats the values
 * and starts a refresh when the text differs from the one shown (always
 * before the first one). */
void epd_show(uint32_t now, int16_t centi, uint16_t humidity, uint16_t millivolts) JOIN_FAR;
/* Every READY-gate poll, also outside BDB READY: advances an active refresh
 * by at most EPD_ROWS_PER_STEP rows or one bounded wait check. Every wait
 * is bounded; a timeout or SPI stall powers the panel off and counts a
 * failure. */
void epd_step(uint32_t now) JOIN_FAR;
/* Longest PM2 sleep (symbols) the display allows: unlimited when idle,
 * none while streaming, the expected rest of a refresh (then
 * EPD_SLEEP_POLL steps) while the panel refreshes on its own. */
uint32_t epd_sleep_limit(uint32_t now) JOIN_FAR;
#endif

#endif
