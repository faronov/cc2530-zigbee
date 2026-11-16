/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* E-paper driver (epd.h): the D3 layout on a UC8151D or SSD1680 panel, or
 * the L8 dashboard on an SSD1677 (epd_l8.c). The fixed LLVM link admits 45
 * translation units, so boards/lg_esl29_rev03.c includes this file under
 * CC2530_EPD. */
#include "epd.h"
#if defined(EPD_L8)
#include "epd_l8_art.h"
#else
#include "epd_d3_art.h"
#endif
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
static MCU_XDATA uint8_t stalled;

/* 7-segment masks of 0..9, minus, blank. */
#define SEG_MINUS 10u
#define SEG_BLANK 11u
static const MCU_CODE uint8_t segment[12] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x40, 0x00
};
#if !defined(EPD_L8)
/* d3_f4 glyphs after the digits. */
#define F4_DOT 10u
#define F4_MINUS 11u
#define F4_SPACE 12u
#define F4_V 13u

/* Digit slots: left column; top row, width, height and half stroke of the
 * temperature (slots 0..2) and humidity (3, 4) digits. */
static const MCU_CODE uint16_t slot_x[EPD_DIGITS] = { 26, 70, 126, 236, 258 };
static const MCU_CODE uint8_t slot_shape[2][4] = { { 32, 38, 66, 4 }, { 34, 18, 34, 2 } };
#endif

/* Command, data count, data; 0xff ends a list. BOOT follows the reset,
 * PANEL the BOOT BUSY wait and ends with the old plane's command, NEXT the
 * old plane, FULL (or PARTIAL, then PARTIAL_LUT after the LUT) the new
 * plane. */
#if CC2530_EPD_PANEL == EPD_UC8151
#define READY() (MMIO_READ(SOC_P1) & BUSY)
#define BOOT 0u
#define PANEL 8u
#define NEXT 26u
#define FULL 29u
static const MCU_CODE uint8_t commands[] = {
    0x06, 3, 0x17, 0x17, 0x17, 0x04, 0, 0xff,
    0x00, 2, 0x1f, 0x0d, 0x61, 3, 0x80, 0x01, 0x28, 0x82, 1, 0x12, 0x50, 1, 0x97, 0x10, 0, 0xff,
    0x13, 0, 0xff,
    0x12, 0, 0xff
};
#elif CC2530_EPD_PANEL == EPD_SSD1677
#define READY() (!(MMIO_READ(SOC_P1) & BUSY))
#define BOOT 0u
#define PANEL 3u
#define FULL 47u
#define SLEEP 53u
static const MCU_CODE uint8_t commands[] = {
    0x12, 0, 0xff,
    0x18, 1, 0x80, 0x0c, 5, 0xae, 0xc7, 0xc3, 0xc0, 0x80, 0x01, 3, 0xdf, 0x01, 0x02, 0x3c, 1, 0x01,
    0x11, 1, 0x03, 0x44, 4, 0x00, 0x00, 0x1f, 0x03, 0x45, 4, 0x00, 0x00, 0xdf, 0x01,
    0x4e, 2, 0x00, 0x00, 0x4f, 2, 0x00, 0x00, 0x24, 0, 0xff,
    0x22, 1, 0xf7, 0x20, 0, 0xff,
    0x10, 1, 0x01, 0xff
};
#else
#define READY() (!(MMIO_READ(SOC_P1) & BUSY))
#define BOOT 0u
#define PANEL 3u
#define NEXT 34u
#define FULL 44u
#define PARTIAL 50u
#define PARTIAL_LUT 56u
#define SLEEP 62u
static const MCU_CODE uint8_t commands[] = {
    0x12, 0, 0xff,
    0x01, 3, 0x27, 0x01, 0x00, 0x11, 1, 0x03, 0x21, 2, 0x00, 0x80, 0x44, 2, 0x00, 0x0f,
    0x45, 4, 0x00, 0x00, 0x27, 0x01, 0x3c, 1, 0x05, 0x18, 1, 0x80, 0x26, 0, 0xff,
    0x4e, 1, 0x00, 0x4f, 2, 0x00, 0x00, 0x24, 0, 0xff,
    0x22, 1, 0xf7, 0x20, 0, 0xff,
    0x22, 1, 0xb1, 0x20, 0, 0xff,
    0x22, 1, 0xc7, 0x20, 0, 0xff,
    0x10, 1, 0x01, 0xff
};
#endif

#if defined(EPD_L8)
#include "epd_l8.c"
#else
void MCU_COMMON_CODE epd_format(epd_view_t MCU_XDATA *view, int16_t centi, uint16_t humidity, uint16_t millivolts) JOIN_FAR
{
    uint8_t c[EPD_DIGITS], i, neg;
    uint16_t a;
    view->dot = 1;
    if (centi == (int16_t)-0x8000) {
        c[0] = c[1] = c[2] = SEG_MINUS;
    } else {
        neg = centi < 0;
        a = neg ? (uint16_t)-(uint16_t)centi : (uint16_t)centi;
        a = (uint16_t)(a + 5u) / 10u;
        if (!a) neg = 0;
        if (a <= (neg ? 99u : 999u)) {
            c[0] = neg ? SEG_MINUS : a >= 100u ? (uint8_t)(a / 100u) : SEG_BLANK;
            c[1] = (uint8_t)(a / 10u % 10u);
            c[2] = (uint8_t)(a % 10u);
        } else if (neg && a <= 999u) {
            /* Whole degrees, -10.0 .. -99.9 tenths shifted out. */
            c[0] = SEG_MINUS;
            c[1] = (uint8_t)(a / 100u);
            c[2] = (uint8_t)(a / 10u % 10u);
            view->dot = 0;
        } else {
            c[0] = c[1] = c[2] = SEG_MINUS;
            view->dot = 0;
        }
    }
    if (humidity == 0xffffu) {
        c[3] = c[4] = SEG_MINUS;
    } else {
        a = humidity >= 9850u ? 99u : (uint16_t)(humidity + 50u) / 100u;
        c[3] = (uint8_t)(a / 10u);
        c[4] = (uint8_t)(a % 10u);
    }
    for (i = 0; i < EPD_DIGITS; i++) view->digit[i] = segment[c[i]];
    if (!millivolts) {
        view->volt[0] = view->volt[2] = view->volt[3] = F4_MINUS;
    } else {
        a = millivolts >= 9985u ? 999u : (uint16_t)(millivolts + 5u) / 10u;
        view->volt[0] = (uint8_t)(a / 100u);
        view->volt[2] = (uint8_t)(a / 10u % 10u);
        view->volt[3] = (uint8_t)(a % 10u);
    }
    view->volt[1] = F4_DOT;
    view->volt[4] = F4_SPACE;
    view->volt[5] = F4_V;
    view->fill = millivolts <= 2000u ? 0 : millivolts >= 3000u ? 18u : (uint8_t)((uint16_t)(millivolts - 2000u) * 18u / 1000u);
}

static void MCU_COMMON_CODE ink(uint8_t y)
{
    epd.column[y >> 3] &= (uint8_t)~(0x80u >> (y & 7u));
}

static void MCU_COMMON_CODE span(uint8_t y0, uint8_t y1)
{
    for (; y0 <= y1; y0++) ink(y0);
}

/* Column dx (0..width) of a digit with segment mask mask (bit 0 a .. bit 6
 * g): bars a, g, d run between the columns of f/e (at h) and b/c (at
 * width - h); every bar is 2h + 1 wide and tapers to a point at both ends. */
static void MCU_COMMON_CODE digit(uint8_t mask, uint8_t dx, const MCU_CODE uint8_t *shape)
{
    uint8_t y = shape[0], w = shape[1], ht = shape[2], h = shape[3];
    uint8_t mid = (uint8_t)(y + ht / 2u), k, m, i, xc;
    if (dx > h && dx < (uint8_t)(w - h)) {
        k = (uint8_t)(dx - h - 1u);
        m = (uint8_t)(w - h - 1u - dx);
        if (m < k) k = m;
        if (h < k) k = h;
        if (mask & 0x01u) span((uint8_t)(y + h - k), (uint8_t)(y + h + k));
        if (mask & 0x40u) span((uint8_t)(mid - k), (uint8_t)(mid + k));
        if (mask & 0x08u) span((uint8_t)(y + ht - h - k), (uint8_t)(y + ht - h + k));
    }
    for (i = 0; i < 2u; i++) {
        xc = i ? (uint8_t)(w - h) : h;
        k = dx > xc ? (uint8_t)(dx - xc) : (uint8_t)(xc - dx);
        if (k > h) continue;
        if (mask & (i ? 0x02u : 0x20u)) span((uint8_t)(y + h + 1u + k), (uint8_t)(mid - 1u - k));
        if (mask & (i ? 0x04u : 0x10u)) span((uint8_t)(mid + 1u + k), (uint8_t)(y + ht - h - 1u - k));
    }
}

void MCU_COMMON_CODE epd_render(const epd_view_t MCU_XDATA *view, uint16_t row) JOIN_FAR
{
    const MCU_CODE epd_d3_sprite_t *s;
    const MCU_CODE uint8_t *bits;
    uint16_t x, x0;
    uint8_t i, b, n, g;
    memset(epd.column, 0xff, EPD_OCTETS);
    x = EPD_ROWS - 1u - row;
    ink(23);
    ink(107);
    if (x == 210u) span(30, 100);
    for (i = 0; i < EPD_D3_SPRITES; i++) {
        s = &d3_sprite[i];
        if (x < s->x0 || x >= s->x0 + s->width) continue;
        n = (uint8_t)((s->height + 7u) >> 3);
        bits = &d3_sprite_bits[s->at + (x - s->x0) * n];
        for (b = 0; b < s->height; b++)
            if (bits[b >> 3] & (1u << (b & 7u))) ink((uint8_t)(s->top + b));
    }
    for (i = 0; i < EPD_DIGITS; i++)
        if (x >= slot_x[i] && x - slot_x[i] <= slot_shape[i >= 3u][1])
            digit(view->digit[i], (uint8_t)(x - slot_x[i]), slot_shape[i >= 3u]);
    if (view->dot && x >= 113u && x <= 120u) span(90, 97);
    if (x >= 264u && x < 264u + view->fill) span(7, 13);
    x0 = EPD_D3_VOLT_X;
    for (i = 0; i < EPD_VOLT; i++) {
        g = view->volt[i];
        if (x < x0 + d3_f4[g][1]) {
            if (x >= x0) {
                b = d3_f4_bits[d3_f4[g][2] + (uint8_t)(x - x0)];
                for (n = 0; n < 8u; n++, b >>= 1)
                    if (b & 1u) ink((uint8_t)(EPD_D3_VOLT_TOP + n));
            }
            break;
        }
        x0 += d3_f4[g][0];
    }
}
#endif

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

#if EPD_PARTIALS
/* SSD1680 partial LUT (153 octets): VS L1 (old black, new white) VSL and L2
 * (old white, new black) VSH1 in group 0 phase A, everything else VSS;
 * group 0 phase A lasts EPD_PARTIAL_FRAMES frames; FR 0x22; XON 0. */
static void MCU_COMMON_CODE lut(void)
{
    uint8_t i = 0, v;
    send(0, 0x32u);
    do {
        v = i == 12u ? 0x80u : i == 24u ? 0x40u : i == 60u ? EPD_PARTIAL_FRAMES : (uint8_t)(i - 144u) < 6u ? 0x22u : 0;
        send(1, v);
    } while (++i != 153u && !stalled);
}
#endif

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
    epd.shown = 0;
    board_early_off();
    epd.state = EPD_IDLE;
}

static void MCU_COMMON_CODE done(void)
{
    board_early_off();
    memcpy(&epd.old, &epd.next, sizeof(epd.old));
    epd.shown = 1;
    epd.partials = epd.partial ? (uint8_t)(epd.partials + 1u) : 0;
    epd.refreshes++;
    epd.state = EPD_IDLE;
}

/* Enters state at now unless an octet stalled. */
static void MCU_COMMON_CODE enter(uint8_t state, uint32_t now)
{
    if (stalled) { fail(); return; }
    epd.state = state;
    epd.at = now;
}

void MCU_COMMON_CODE epd_show(uint32_t now, int16_t centi, uint16_t humidity, uint16_t millivolts) JOIN_FAR
{
#if defined(EPD_L8)
    l8_tick(now);
    if (epd.state != EPD_IDLE) return;
    if (epd.armed && (uint32_t)(now - epd.due) >= 0x80000000UL &&
        !(epd_l8.dirty && epd.shown && now - epd_l8.written >= EPD_L8_SETTLE &&
          now - epd_l8.started >= EPD_L8_SPACING)) return;
    epd_format(&epd.next, centi, humidity, millivolts);
    if (epd.armed && epd.shown && !epd_l8.dirty && !memcmp(&epd.next, &epd.old, sizeof(epd.old))) {
        epd.due = now + EPD_CHECK;
        return;
    }
    epd_l8.dirty = 0;
    epd_l8.started = now;
    epd.armed = 1;
    epd.due = now + EPD_SECONDS(epd_l8.interval);
#else
    if (epd.state != EPD_IDLE || (epd.armed && (uint32_t)(now - epd.due) >= 0x80000000UL)) return;
    epd_format(&epd.next, centi, humidity, millivolts);
    if (epd.armed && epd.shown && !memcmp(&epd.next, &epd.old, sizeof(epd.old))) {
        epd.due = now + EPD_CHECK;
        return;
    }
    epd.armed = 1;
    epd.due = now + EPD_INTERVAL;
#endif
#if EPD_PARTIALS
    epd.partial = epd.shown && epd.partials < EPD_PARTIALS;
#endif
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
        enter(EPD_BOOT, now);
        return;
    case EPD_BOOT:
    case EPD_POWER:
    case EPD_LOAD:
    case EPD_REFRESH:
    case EPD_OFF:
        if (d < (epd.state == EPD_BOOT ? EPD_RESET_WAIT : EPD_BUSY_WAIT)) return;
        if (!READY()) {
            if (d >= (epd.state == EPD_REFRESH ? EPD_REFRESH_LIMIT : EPD_START_LIMIT)) fail();
            return;
        }
        switch (epd.state) {
        case EPD_BOOT:
            sequence(BOOT);
            enter(EPD_POWER, now);
            return;
        case EPD_POWER:
            sequence(PANEL);
            epd.row = 0;
#if defined(EPD_L8)
            enter(EPD_NEW, now);
#else
            enter(EPD_OLD, now);
#endif
            return;
#if EPD_PARTIALS
        case EPD_LOAD:
            lut();
            sequence(PARTIAL_LUT);
            enter(EPD_REFRESH, now);
            return;
#endif
        case EPD_REFRESH:
            epd.duration = (uint16_t)(d >> 6);
            /* PM2 may have run during the refresh. */
            open();
#if CC2530_EPD_PANEL == EPD_UC8151
            send(0, 0x02u);
            enter(EPD_OFF, now);
#else
            /* Deep sleep holds BUSY high; the power cut follows at once. */
            sequence(SLEEP);
            if (stalled) fail(); else done();
#endif
            return;
        default:
            done();
            return;
        }
    case EPD_OLD:
    case EPD_NEW:
        for (n = 0; n < EPD_ROWS_PER_STEP && epd.row != EPD_ROWS && !stalled; n++, epd.row++) {
            if (epd.state == EPD_OLD && !epd.shown) memset(epd.column, 0xff, EPD_OCTETS);
            else epd_render(epd.state == EPD_OLD ? &epd.old : &epd.next, epd.row);
            for (i = 0; i < EPD_OCTETS; i++) send(1, epd.column[i]);
        }
        if (stalled) { fail(); return; }
        if (epd.row != EPD_ROWS) return;
        epd.row = 0;
        if (epd.state == EPD_OLD) {
#if defined(NEXT)
            sequence(NEXT);
#endif
            enter(EPD_NEW, now);
#if EPD_PARTIALS
        } else if (epd.partial) {
            sequence(PARTIAL);
            enter(EPD_LOAD, now);
#endif
        } else {
            sequence(FULL);
            enter(EPD_REFRESH, now);
        }
        return;
    default:
        return;
    }
}

uint32_t MCU_COMMON_CODE epd_sleep_limit(uint32_t now) JOIN_FAR
{
    uint32_t d, expected = EPD_REFRESH_EXPECTED;
    if (epd.state == EPD_IDLE) return 0xffffffffUL;
    if (epd.state != EPD_REFRESH) return 0;
#if EPD_PARTIALS
    if (epd.partial) expected = EPD_PARTIAL_EXPECTED;
#endif
    d = now - epd.at;
    return d < expected ? expected - d : EPD_SLEEP_POLL;
}
