/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef EPD_H
#define EPD_H

#include "board.h"
#include "banked_join.h"

/* CC2530_EPD (opt-in, LG ESL29 only): a 2.9" 128x296 black/white e-paper
 * panel on the board's FPC shows the D3 layout (docs/EPAPER.md): title,
 * 7-segment temperature and humidity, battery gauge and voltage.
 * CC2530_EPD_PANEL selects the controller (EPD_UC8151, the default, or
 * EPD_SSD1680). Pins, H2 evidence (LH27 debugger probes):
 * - P0.7 high powers the panel; P1.1 is RST (low active), P1.2 BUSY; P0.2 DC
 *   (0 command, 1 data), P0.4 CS (low active), P0.3 MOSI and P0.5 SCK on
 *   USART0 alternative 1 SPI master, MSB first, mode 0, 1 MHz (the stock
 *   firmware's U0GCR 0x2F), CS low per octet;
 * - each of the 296 gate rows carries 16 source octets, octet 0 at the top
 *   and its MSB the top pixel, bit 1 white, 0 black; with the ribbon on the
 *   left gate row 0 is the rightmost screen column (all three panels).
 * UC8151D (stock H2 panel, BUSY low while busy): power, reset, 06 17 17 17,
 * 04 (BUSY ~50 ms), 00 1F 0D (OTP LUT, black/white), 61 80 01 28 (128x296),
 * 82 12, 50 97, 10 + 4736 old octets, 13 + 4736 new octets, 12 (BUSY
 * 4.91 s at 22 C), 02 (BUSY ~40 ms), then P0.7 low. The OTP waveform is
 * temperature dependent, so the refresh bound is generous.
 * SSD1680 (Waveshare 2.9" V2, BUSY high while busy): power, reset, 12 (BUSY
 * ~10 ms), 01 27 01 00, 11 03, 21 00 80 (sources S8..S167), 44 00 0F,
 * 45 00 00 27 01, 3C 05, 18 80, 4E 00, 4F 00 00, 26 + old plane, 4E 00,
 * 4F 00 00, 24 + new plane; a full refresh 22 F7, 20 (BUSY 2.97 s), a
 * partial one 22 B1, 20 (BUSY ~10 ms: OTP voltages), 32 + own 153-octet LUT
 * (old black to white VSL, old white to black VSH1, others VSS; 80 frames),
 * 22 C7, 20 (BUSY ~1.7 s); then 10 01 and P0.7 low.
 * No frame buffer: every gate row is rendered on the fly into 16 octets, a
 * few rows per READY poll. The old plane renders the view shown last (white
 * before the first refresh), the new plane the current view. */
#define EPD_UC8151 0
#define EPD_SSD1680 1
#if !defined(CC2530_EPD_PANEL)
#define CC2530_EPD_PANEL EPD_UC8151
#endif
#define EPD_ROWS 296u
#define EPD_OCTETS 16u
#define EPD_DIGITS 5u
#define EPD_VOLT 6u
#define EPD_ROWS_PER_STEP 4u
/* 16-us MAC symbols. */
#define EPD_MS(ms) ((uint32_t)(ms) * 125u / 2u)
#define EPD_SECONDS(s) ((uint32_t)(s) * 62500UL)
#define EPD_RESET_WAIT EPD_MS(20)
#define EPD_BUSY_WAIT EPD_MS(2)
#define EPD_START_LIMIT EPD_MS(500)
#define EPD_REFRESH_LIMIT EPD_MS(30000)
#define EPD_SLEEP_POLL EPD_MS(250)
#if CC2530_EPD_PANEL == EPD_UC8151
#define EPD_REFRESH_EXPECTED EPD_MS(5000)
#define EPD_PARTIALS 0u
#elif CC2530_EPD_PANEL == EPD_SSD1680
#define EPD_REFRESH_EXPECTED EPD_MS(3000)
#define EPD_PARTIAL_EXPECTED EPD_MS(1700)
#define EPD_PARTIAL_FRAMES 80u
/* Partial refreshes between full ones. */
#define EPD_PARTIALS 10u
#else
#error unknown CC2530_EPD_PANEL
#endif
/* A changed view starts a refresh at most once per INTERVAL; an unchanged
 * one is compared again after CHECK. A failure also waits INTERVAL. */
#define EPD_INTERVAL EPD_SECONDS(600)
#define EPD_CHECK EPD_SECONDS(10)
/* Shown without a humidity sensor (the D3 layout always has the field). */
#define EPD_PLACEHOLDER_HUMIDITY 4500u

enum {
    EPD_IDLE = 0, EPD_RESET, EPD_BOOT, EPD_POWER, EPD_OLD, EPD_NEW, EPD_LOAD, EPD_REFRESH, EPD_OFF
};

/* What the panel shows: 7-segment masks (bit 0 segment a .. bit 6 g) of
 * the three temperature and two humidity digits, the temperature decimal
 * point, the battery gauge fill (0..18 columns) and the small-font glyphs
 * of the voltage text (d3_f4 indices). */
typedef struct {
    uint8_t digit[EPD_DIGITS];
    uint8_t dot, fill;
    uint8_t volt[EPD_VOLT];
} epd_view_t;

/* armed: a refresh was started once (due is valid); shown: old is on the
 * panel (cleared by a failure, so the next refresh is a full one); partial:
 * the running refresh is partial; partials: partial refreshes since the
 * last full one; failed: the state of the last failure (0 none); duration:
 * the last refresh BUSY time in 1.024-ms units (symbols >> 6). */
typedef struct {
    uint8_t state, armed, shown, partial, partials, failed, failures;
    uint16_t row, refreshes, duration;
    uint32_t at, due;
    epd_view_t old, next;
    uint8_t column[EPD_OCTETS];
} epd_t;

#if defined(CC2530_EPD)
#if CC2530_BOARD != BOARD_LG_ESL29_REV03 || !defined(CC2530_CHIP_SENSE)
#error CC2530_EPD requires the LG ESL29 board and CC2530_CHIP_SENSE
#endif
extern MCU_XDATA epd_t epd;
/* Formats the view: centi-degrees C rounded to tenths (-9.9 .. 99.9 with
 * the point, -10 .. -99 whole degrees without it, otherwise ---; the ZCL
 * invalid 0x8000 shows --.-), humidity in 0.01 %RH (0xFFFF shows --, at
 * most 99) and millivolts rounded to 10 mV (0 shows -.-- V). */
void epd_format(epd_view_t MCU_XDATA *view, int16_t centi, uint16_t humidity, uint16_t millivolts) JOIN_FAR;
/* Renders gate row 0..295 of view into epd.column. */
void epd_render(const epd_view_t MCU_XDATA *view, uint16_t row) JOIN_FAR;
/* READY only, after zcl_sensor_tick: while idle and due, formats the values
 * and starts a refresh when the view differs from the one shown (always
 * before the first one). The refresh is partial when the panel supports it,
 * the view shown is intact and fewer than EPD_PARTIALS partial refreshes
 * followed the last full one. */
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
