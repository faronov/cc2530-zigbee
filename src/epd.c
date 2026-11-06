/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* UC8151D e-paper driver (epd.h). The fixed LLVM link admits 45 translation
 * units, so boards/lg_esl29_rev03.c includes this file under CC2530_EPD. */
#include "epd.h"
#include <string.h>

#if defined(__SDCC)
#error CC2530_EPD is an LLVM-only variant
#endif

#define SOC_U0CSR_ADDRESS 0x86u
#define SOC_U0DBUF_ADDRESS 0xc1u
#define SOC_U0BAUD_ADDRESS 0xc2u
#define SOC_U0UCR_ADDRESS 0xc4u
#define SOC_U0GCR_ADDRESS 0xc5u
#if defined(CC2530_HOST_TEST)
extern volatile uint8_t SOC_U0CSR, SOC_U0DBUF, SOC_U0BAUD, SOC_U0UCR, SOC_U0GCR;
#endif

/* Port 0: DC, MOSI, CS, SCK, power; port 1: RST, BUSY. */
#define DC 0x04u
#define MOSI 0x08u
#define CS 0x10u
#define SCK 0x20u
#define POWER 0x80u
#define RST 0x02u
#define BUSY 0x04u
#define TX_BYTE 0x02u

MCU_XDATA epd_t epd;
static MCU_XDATA epd_text_t MCU_XDATA *text_out;
static MCU_XDATA uint8_t field, stalled;

/* 5x7 column-major glyphs, bit 0 the top row. */
static const MCU_CODE uint8_t font[EPD_GLYPHS][5] = {
    { 0x3e, 0x41, 0x41, 0x41, 0x3e }, { 0x00, 0x42, 0x7f, 0x40, 0x00 },
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4b, 0x31 },
    { 0x18, 0x14, 0x12, 0x7f, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
    { 0x3c, 0x4a, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1e },
    { 0x60, 0x60 }, { 0x08, 0x08, 0x08, 0x08 }, { 0x06, 0x09, 0x09, 0x06 },
    { 0x3e, 0x41, 0x41, 0x41, 0x22 }, { 0x23, 0x13, 0x08, 0x64, 0x62 },
    { 0x1f, 0x20, 0x40, 0x20, 0x1f }
};
static const MCU_CODE uint8_t widths[EPD_GLYPHS] = { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 4, 4, 5, 5, 5 };

/* Per field: scale, top pixel row, right margin (0 centers). The longest
 * texts (-327.7 C, 100 %, 65.54 V) stay within 296 columns. */
static const MCU_CODE uint8_t layout[EPD_FIELDS][3] = { { 6, 10, 0 }, { 4, 62, 0 }, { 2, 106, 8 } };

/* Command, data count, data; 0xff ends a list. */
#define BOOST 0u
#define PANEL 8u
static const MCU_CODE uint8_t commands[] = {
    0x06, 3, 0x17, 0x17, 0x17, 0x04, 0, 0xff,
    0x00, 2, 0x1f, 0x0d, 0x61, 3, 0x80, 0x01, 0x28, 0x82, 1, 0x12, 0x50, 1, 0x97, 0x10, 0, 0xff
};

static void MCU_COMMON_CODE put(uint8_t code)
{
    text_out->code[field][text_out->length[field]++] = code;
}

/* Decimal value with at least digits digits. */
static void MCU_COMMON_CODE number(uint16_t value, uint8_t digits)
{
    uint8_t d[5], n = 0;
    do { d[n++] = (uint8_t)(value % 10u); value /= 10u; } while (value || n < digits);
    while (n) put(d[--n]);
}

void MCU_COMMON_CODE epd_format(epd_text_t MCU_XDATA *text, int16_t centi, uint16_t humidity, uint16_t millivolts) JOIN_FAR
{
    uint16_t a;
    memset(text, 0, sizeof(*text));
    text_out = text;
    field = 0;
    if (centi == (int16_t)-0x8000) {
        put(EPD_MINUS); put(EPD_MINUS); put(EPD_DOT); put(EPD_MINUS);
    } else {
        a = (uint16_t)((uint16_t)(centi < 0 ? -centi : centi) + 5u) / 10u;
        if (centi < 0 && a) put(EPD_MINUS);
        number(a / 10u, 1); put(EPD_DOT); put((uint8_t)(a % 10u));
    }
    put(EPD_DEGREE); put(EPD_C);
    field = 1;
    if (humidity != EPD_NO_HUMIDITY) {
        if (humidity == 0xffffu) { put(EPD_MINUS); put(EPD_MINUS); }
        else number(humidity >= 10000u ? 100u : (uint16_t)(humidity + 50u) / 100u, 1);
        put(EPD_PERCENT);
    }
    field = 2;
    if (!millivolts) {
        put(EPD_MINUS); put(EPD_DOT); put(EPD_MINUS); put(EPD_MINUS);
    } else {
        a = millivolts / 10u;
        if (millivolts % 10u >= 5u) a++;
        number(a / 100u, 1); put(EPD_DOT); number(a % 100u, 2);
    }
    put(EPD_V);
}

void MCU_COMMON_CODE epd_render(const epd_text_t MCU_XDATA *text, uint16_t row) JOIN_FAR
{
    uint8_t f, i, n, s, g, b, code, y;
    uint16_t x, x0, w, col;
    memset(epd.column, 0xff, EPD_OCTETS);
    x = EPD_ROWS - 1u - row;
    for (f = 0; f < EPD_FIELDS; f++) {
        n = text->length[f];
        if (!n) continue;
        s = layout[f][0];
        w = n - 1u;
        for (i = 0; i < n; i++) w += widths[text->code[f][i]];
        w *= s;
        x0 = layout[f][2] ? EPD_ROWS - layout[f][2] - w : (EPD_ROWS - w) / 2u;
        if (x < x0 || x >= x0 + w) continue;
        col = (x - x0) / s;
        g = 0;
        for (i = 0; i < n; i++) {
            code = text->code[f][i];
            if (col < widths[code]) { g = font[code][col]; break; }
            col -= widths[code];
            if (!col) break;
            col--;
        }
        y = layout[f][1];
        for (b = 0; b < 7u; b++, g >>= 1)
            for (i = 0; i < s; i++, y++)
                if (g & 1u) epd.column[y >> 3] &= (uint8_t)~(0x80u >> (y & 7u));
    }
}

/* One octet with CS low; a TX_BYTE wait past 255 polls latches stalled. */
static void MCU_COMMON_CODE send(uint8_t data, uint8_t value)
{
    uint8_t n = 0;
    MMIO_CLEAR(SOC_P0, CS);
    if (data) MMIO_SET(SOC_P0, DC); else MMIO_CLEAR(SOC_P0, DC);
    MMIO_WRITE(SOC_U0DBUF, value);
    while (!(MMIO_READ(SOC_U0CSR) & TX_BYTE)) if (!++n) { stalled = 1; break; }
    MMIO_CLEAR(SOC_U0CSR, TX_BYTE);
    MMIO_SET(SOC_P0, CS);
}

static void MCU_COMMON_CODE sequence(uint8_t at)
{
    uint8_t n;
    while (commands[at] != 0xffu && !stalled) {
        send(0, commands[at++]);
        for (n = commands[at++]; n; n--) send(1, commands[at++]);
    }
}

/* USART0 alternative 1 SPI master (priority on port 0), 1 MHz, MSB first. */
static void MCU_COMMON_CODE open(void)
{
    MMIO_CLEAR(SOC_PERCFG, 1u);
    MMIO_CLEAR(SOC_P2DIR, 0xc0u);
    MMIO_WRITE(SOC_U0CSR, 0);
    MMIO_WRITE(SOC_U0BAUD, 0);
    MMIO_WRITE(SOC_U0GCR, 0x2fu);
    MMIO_WRITE(SOC_U0UCR, 0x80u);
    MMIO_SET(SOC_P0, CS | DC);
    MMIO_SET(SOC_P0SEL, MOSI | SCK);
}

static void MCU_COMMON_CODE fail(void)
{
    epd.failed = epd.state;
    if (epd.failures != 0xffu) epd.failures++;
    board_early_off();
    epd.state = EPD_IDLE;
}

void MCU_COMMON_CODE epd_show(uint32_t now, int16_t centi, uint16_t humidity, uint16_t millivolts) JOIN_FAR
{
    if (epd.state != EPD_IDLE || (epd.armed && (uint32_t)(now - epd.due) >= 0x80000000UL)) return;
    epd_format(&epd.next, centi, humidity, millivolts);
    if (epd.armed && !memcmp(&epd.next, &epd.old, sizeof(epd.old))) {
        epd.due = now + EPD_CHECK;
        return;
    }
    epd.armed = 1;
    epd.due = now + EPD_INTERVAL;
    stalled = 0;
    MMIO_CLEAR(SOC_P1SEL, BUSY);
    MMIO_CLEAR(SOC_P1DIR, BUSY);
    open();
    MMIO_CLEAR(SOC_P1, RST);
    MMIO_SET(SOC_P0, POWER);
    epd.state = EPD_RESET;
    epd.at = now;
}

void MCU_COMMON_CODE epd_step(uint32_t now) JOIN_FAR
{
    uint32_t d = now - epd.at;
    uint8_t n, i;
    switch (epd.state) {
    case EPD_RESET:
        if (d < EPD_RESET_WAIT) return;
        MMIO_SET(SOC_P1, RST);
        break;
    case EPD_BOOT:
    case EPD_POWER:
    case EPD_REFRESH:
    case EPD_OFF:
        if (d < (epd.state == EPD_BOOT ? EPD_RESET_WAIT : EPD_BUSY_WAIT)) return;
        if (!(MMIO_READ(SOC_P1) & BUSY)) {
            if (d >= (epd.state == EPD_REFRESH ? EPD_REFRESH_LIMIT : EPD_START_LIMIT)) fail();
            return;
        }
        if (epd.state == EPD_BOOT) sequence(BOOST);
        else if (epd.state == EPD_POWER) { sequence(PANEL); epd.row = 0; }
        else if (epd.state == EPD_REFRESH) {
            epd.duration = (uint16_t)(d >> 6);
            open();
            send(0, 0x02u);
        } else {
            board_early_off();
            memcpy(&epd.old, &epd.next, sizeof(epd.old));
            epd.refreshes++;
            epd.state = EPD_IDLE;
            return;
        }
        break;
    case EPD_OLD:
    case EPD_NEW:
        for (n = 0; n < EPD_ROWS_PER_STEP && epd.row != EPD_ROWS && !stalled; n++, epd.row++) {
            epd_render(epd.state == EPD_OLD ? &epd.old : &epd.next, epd.row);
            for (i = 0; i < EPD_OCTETS; i++) send(1, epd.column[i]);
        }
        if (epd.row != EPD_ROWS || stalled) break;
        epd.row = 0;
        send(0, epd.state == EPD_OLD ? 0x13u : 0x12u);
        if (stalled) break;
        epd.state++;
        epd.at = now;
        return;
    default:
        return;
    }
    if (stalled) { fail(); return; }
    if (epd.state == EPD_OLD || epd.state == EPD_NEW) return;
    epd.state++;
    epd.at = now;
}

uint32_t MCU_COMMON_CODE epd_sleep_limit(uint32_t now) JOIN_FAR
{
    uint32_t d;
    if (epd.state == EPD_IDLE) return 0xffffffffUL;
    if (epd.state != EPD_REFRESH) return 0;
    d = now - epd.at;
    return d < EPD_REFRESH_EXPECTED ? EPD_REFRESH_EXPECTED - d : EPD_SLEEP_POLL;
}
