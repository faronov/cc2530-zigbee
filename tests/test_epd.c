/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* CC2530_EPD host test: boards/lg_esl29_rev03.c (which includes src/epd.c)
 * against a UC8151D model that checks the H2 refresh sequence, the per-octet
 * CS framing and DC level, and captures both data planes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epd.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); failures++; } } while (0)

volatile uint8_t SOC_P0, SOC_P0DIR, SOC_P0SEL, SOC_P0INP, SOC_APCFG, SOC_PERCFG, SOC_P2DIR;
volatile uint8_t SOC_P1, SOC_P1DIR, SOC_P1SEL;
volatile uint8_t SOC_U0CSR, SOC_U0DBUF, SOC_U0BAUD, SOC_U0UCR, SOC_U0GCR;

#define ROWS ((int)EPD_ROWS)
#define FIELDS ((int)EPD_FIELDS)
#define PLANE (EPD_ROWS * EPD_OCTETS)
#define LOG 64

static uint32_t now;
static struct {
    uint8_t stall, busy_stuck, refresh_stuck, refresh_hold;
    uint8_t reset_low, reset_seen;
    uint8_t command, misframed, misdc, unpowered, unready;
    uint32_t busy_until;
    uint16_t count;
    uint8_t log[LOG], data[LOG][4], data_n[LOG], n;
    uint8_t plane[2][PLANE];
    uint16_t plane_n[2];
    int refreshes;
} m;

static int powered(void)
{
    return (SOC_P0DIR & 0x80u) && (SOC_P0 & 0x80u) && !(SOC_P0SEL & 0x80u);
}

static int busy(void)
{
    if (!powered()) return 0;
    if (m.busy_stuck || m.refresh_hold) return 1;
    return (int32_t)(m.busy_until - now) > 0;
}

static void model_byte(uint8_t value)
{
    int data = (SOC_P0 & 0x04u) != 0;
    if (SOC_P0 & 0x10u) m.misframed++;
    if (!powered() || !m.reset_seen || m.reset_low) m.unpowered++;
    if (!(SOC_P0SEL & 0x08u) || !(SOC_P0SEL & 0x20u) || (SOC_P0SEL & 0x14u) || (SOC_PERCFG & 1u) ||
        SOC_U0GCR != 0x2fu || SOC_U0BAUD != 0 || (SOC_U0CSR & 0xa0u)) m.misdc++;
    if (busy()) m.unready++;
    if (!data) {
        m.command = value;
        m.count = 0;
        if (m.n < LOG) { m.log[m.n] = value; m.data_n[m.n++] = 0; }
        if (value == 0x04u) m.busy_until = now + EPD_MS(50);
        if (value == 0x12u) { m.busy_until = now + EPD_MS(4910); m.refreshes++; if (m.refresh_stuck) m.refresh_hold = 1; }
        if (value == 0x02u) m.busy_until = now + EPD_MS(40);
        return;
    }
    if (m.command == 0x10u || m.command == 0x13u) {
        int p = m.command == 0x13u;
        if (m.plane_n[p] < PLANE) m.plane[p][m.plane_n[p]] = value;
        m.plane_n[p]++;
    } else if (m.n && m.data_n[m.n-1u] < 4u) {
        m.data[m.n-1u][m.data_n[m.n-1u]++] = value;
    }
    m.count++;
}

uint8_t host_mmio_load(const volatile uint8_t *reg, uint8_t address)
{
    if (address == SOC_P1_ADDRESS)
        return (uint8_t)((*reg & (uint8_t)~0x04u) | ((SOC_P1DIR & 0x04u) || (SOC_P1SEL & 0x04u) || !busy() ? 0x04u : 0));
    return *reg;
}

void host_mmio_store(volatile uint8_t *reg, uint8_t address, uint8_t value)
{
    uint8_t before = *reg;
    *reg = value;
    if (address == 0xc1u) {
        model_byte(value);
        if (!m.stall) SOC_U0CSR |= 0x02u;
    }
    if (address == SOC_P1_ADDRESS && (SOC_P1DIR & 0x02u)) {
        if (!(value & 0x02u)) m.reset_low = 1;
        else if (!(before & 0x02u) && m.reset_low && powered()) {
            m.reset_low = 0; m.reset_seen = 1; m.busy_until = now + EPD_MS(3);
        }
    }
    if (address == SOC_P0_ADDRESS && !powered()) { m.reset_seen = 0; m.refresh_hold = 0; }
}

static void model_reset(void)
{
    memset(&m, 0, sizeof(m));
}

/* The startup board state: board_early_off has run. */
static void boot(void)
{
    SOC_P0 = SOC_P0DIR = SOC_P0SEL = SOC_P0INP = SOC_APCFG = 0;
    SOC_P1 = SOC_P1DIR = SOC_P1SEL = 0;
    SOC_PERCFG = 0x01u; SOC_P2DIR = 0xc0u;
    SOC_U0CSR = 0x80u; SOC_U0BAUD = 0x11u; SOC_U0UCR = 0; SOC_U0GCR = 0;
    model_reset();
    memset(&epd, 0, sizeof(epd));
    board_early_off();
}

static int run(uint32_t limit)
{
    uint32_t end = now + limit;
    while (epd.state != EPD_IDLE && (int32_t)(end - now) > 0) {
        epd_step(now);
        now += EPD_MS(1);
    }
    return epd.state == EPD_IDLE;
}

static const char *const texts[] = { "0123456789.-oC%V" };

static void text(const epd_text_t *t, char out[EPD_FIELDS][EPD_TEXT+1])
{
    int f, i;
    for (f = 0; f < FIELDS; f++) {
        for (i = 0; i < t->length[f]; i++) out[f][i] = texts[0][t->code[f][i]];
        out[f][i] = 0;
    }
}

static void format_case(int16_t centi, uint16_t humidity, uint16_t mv, const char *a, const char *b, const char *c)
{
    epd_text_t t;
    char s[EPD_FIELDS][EPD_TEXT+1];
    memset(&t, 0xa5, sizeof(t));
    epd_format(&t, centi, humidity, mv);
    text(&t, s);
    CHECK(!strcmp(s[0], a) && !strcmp(s[1], b) && !strcmp(s[2], c));
    if (strcmp(s[0], a) || strcmp(s[1], b) || strcmp(s[2], c))
        fprintf(stderr, "  %d %u %u -> '%s' '%s' '%s'\n", centi, humidity, mv, s[0], s[1], s[2]);
    CHECK(t.length[0] <= EPD_TEXT && t.length[1] <= EPD_TEXT && t.length[2] <= EPD_TEXT);
}

static void test_format(void)
{
    format_case(2149, EPD_NO_HUMIDITY, 3005, "21.5oC", "", "3.01V");
    format_case(2144, 4749, 3004, "21.4oC", "47%", "3.00V");
    format_case(-4, 4750, 2995, "0.0oC", "48%", "3.00V");
    format_case(-5, 0xffffu, 0, "-0.1oC", "--%", "-.--V");
    format_case((int16_t)-0x8000, 10000, 1, "--.-oC", "100%", "0.00V");
    format_case(-32767, 9999, 65535, "-327.7oC", "100%", "65.54V");
    format_case(32767, 0, 999, "327.7oC", "0%", "1.00V");
    format_case(5, 49, 1234, "0.1oC", "0%", "1.23V");
}

static const uint8_t layout[EPD_FIELDS][3] = { { 6, 10, 0 }, { 4, 62, 0 }, { 2, 106, 8 } };
static const uint8_t glyph_width[EPD_GLYPHS] = { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 4, 4, 5, 5, 5 };

static int pixel(const uint8_t *plane, int x, int y)
{
    /* Gate row 0 is the rightmost column; octet 0 bit 7 is the top pixel. */
    return !(plane[(EPD_ROWS - 1 - x) * EPD_OCTETS + y / 8] & (0x80u >> (y & 7)));
}

/* Independent of the renderer: each field occupies exactly its expected
 * box, every other pixel is white, and each glyph column matches its
 * font popcount times the scale squared. */
static void check_plane(const uint8_t *plane, const epd_text_t *t)
{
    int f, i, x, y, black = 0, expected = 0;
    static int owned[EPD_ROWS][128];
    memset(owned, 0, sizeof(owned));
    for (f = 0; f < FIELDS; f++) {
        int s = layout[f][0], w = 0, x0, n = t->length[f];
        if (!n) continue;
        for (i = 0; i < n; i++) w += glyph_width[t->code[f][i]];
        w = (w + n - 1) * s;
        x0 = layout[f][2] ? EPD_ROWS - layout[f][2] - w : (EPD_ROWS - w) / 2;
        CHECK(x0 >= 0 && x0 + w <= ROWS && layout[f][1] + 7 * s <= 128);
        for (x = x0; x < x0 + w; x++)
            for (y = layout[f][1]; y < layout[f][1] + 7 * s; y++) owned[x][y] = 1;
    }
    for (x = 0; x < ROWS; x++)
        for (y = 0; y < 128; y++)
            if (pixel(plane, x, y)) { black++; CHECK(owned[x][y]); if (!owned[x][y]) return; }
    for (f = 0; f < FIELDS; f++)
        for (i = 0; i < t->length[f]; i++) {
            static const uint8_t font[EPD_GLYPHS][5] = {
                { 0x3e, 0x41, 0x41, 0x41, 0x3e }, { 0x00, 0x42, 0x7f, 0x40, 0x00 },
                { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4b, 0x31 },
                { 0x18, 0x14, 0x12, 0x7f, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
                { 0x3c, 0x4a, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
                { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1e },
                { 0x60, 0x60 }, { 0x08, 0x08, 0x08, 0x08 }, { 0x06, 0x09, 0x09, 0x06 },
                { 0x3e, 0x41, 0x41, 0x41, 0x22 }, { 0x23, 0x13, 0x08, 0x64, 0x62 },
                { 0x1f, 0x20, 0x40, 0x20, 0x1f } };
            int c, b;
            for (c = 0; c < glyph_width[t->code[f][i]]; c++)
                for (b = 0; b < 7; b++)
                    if (font[t->code[f][i]][c] & (1u << b)) expected += layout[f][0] * layout[f][0];
        }
    CHECK(black == expected);
}

/* Checks a digit's left column lands where expected: '1' starts blank,
 * so its glyph's first lit column is the second one. */
static void check_orientation(const uint8_t *plane)
{
    /* Field 0 "21.5oC" (scale 6): '2' column 0 (0x42) has rows 1 and 6 lit. */
    int w = (5 + 5 + 2 + 5 + 4 + 5 + 5) * 6, x0 = (EPD_ROWS - w) / 2;
    CHECK(!pixel(plane, x0, 10) && pixel(plane, x0, 10 + 6) && pixel(plane, x0 + 5, 10 + 36));
    CHECK(!pixel(plane, x0 - 1, 10 + 6));
}

static void dump(const char *path, const uint8_t *plane)
{
    FILE *f = fopen(path, "w");
    int x, y;
    if (!f) return;
    fprintf(f, "P1\n%d 128\n", EPD_ROWS);
    for (y = 0; y < 128; y++) {
        for (x = 0; x < ROWS; x++) fputc(pixel(plane, x, y) ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}

static const uint8_t sequence[] = { 0x06, 0x04, 0x00, 0x61, 0x82, 0x50, 0x10, 0x13, 0x12, 0x02 };

static void test_refresh(const char *pbm)
{
    epd_text_t expect;
    unsigned i;
    boot();
    now = 0x7fff0000UL;
    CHECK(epd_sleep_limit(now) == 0xffffffffUL);
    epd_show(now, 2149, EPD_NO_HUMIDITY, 3005);
    CHECK(epd.state == EPD_RESET && epd.armed && powered() && epd_sleep_limit(now) == 0);
    epd_show(now, 0, EPD_NO_HUMIDITY, 0);
    CHECK(epd.state == EPD_RESET);
    while (epd.state != EPD_REFRESH && epd.state != EPD_IDLE) { epd_step(now); now += EPD_MS(1); }
    CHECK(epd.state == EPD_REFRESH && epd_sleep_limit(now) > EPD_MS(4800) && epd_sleep_limit(now) <= EPD_REFRESH_EXPECTED);
    now += EPD_MS(4000);
    CHECK(epd_sleep_limit(now) <= EPD_MS(1000));
    now += EPD_MS(1000);
    CHECK(epd_sleep_limit(now) == EPD_SLEEP_POLL);
    CHECK(run(EPD_SECONDS(1)));
    CHECK(!epd.failures && epd.refreshes == 1 && m.refreshes == 1);
    CHECK(epd.duration >= (EPD_MS(4910) >> 6) && epd.duration <= (EPD_MS(5100) >> 6));
    CHECK(m.n == sizeof(sequence));
    for (i = 0; i < sizeof(sequence) && i < m.n; i++) CHECK(m.log[i] == sequence[i]);
    CHECK(m.data_n[0] == 3 && m.data[0][0] == 0x17 && m.data[0][2] == 0x17 && m.data_n[1] == 0);
    CHECK(m.data_n[2] == 2 && m.data[2][0] == 0x1f && m.data[2][1] == 0x0d);
    CHECK(m.data_n[3] == 3 && m.data[3][0] == 0x80 && m.data[3][1] == 0x01 && m.data[3][2] == 0x28);
    CHECK(m.data_n[4] == 1 && m.data[4][0] == 0x12 && m.data_n[5] == 1 && m.data[5][0] == 0x97);
    CHECK(m.plane_n[0] == PLANE && m.plane_n[1] == PLANE);
    CHECK(!m.misframed && !m.misdc && !m.unpowered && !m.unready);
    for (i = 0; i < PLANE; i++) CHECK(m.plane[0][i] == 0xffu);
    epd_format(&expect, 2149, EPD_NO_HUMIDITY, 3005);
    check_plane(m.plane[1], &expect);
    check_orientation(m.plane[1]);
    if (pbm) dump(pbm, m.plane[1]);
    /* Panel off: power latch low, controls driven low, SPI pins released. */
    CHECK(!(SOC_P0 & 0xbcu) && !(SOC_P0SEL & 0xbcu) && (SOC_P0DIR & 0xbcu) == 0xbcu && !(SOC_P1 & 0x02u));
    CHECK(!memcmp(&epd.old, &expect, sizeof(expect)));

    /* Not due: nothing. Due and unchanged: recheck in EPD_CHECK. */
    epd_show(now, 2101, 0, 3000);
    CHECK(epd.state == EPD_IDLE);
    now = epd.due;
    epd_show(now, 2149, EPD_NO_HUMIDITY, 3005);
    CHECK(epd.state == EPD_IDLE && epd.due == now + EPD_CHECK);
    now = epd.due;
    model_reset();
    m.reset_low = 0;
    epd_show(now, -1234, 5555, 2500);
    CHECK(epd.state == EPD_RESET && epd.due == now + EPD_INTERVAL);
    CHECK(run(EPD_SECONDS(10)) && epd.refreshes == 2 && !epd.failures);
    CHECK(m.plane_n[0] == PLANE && m.plane_n[1] == PLANE);
    check_plane(m.plane[0], &expect);
    epd_format(&expect, -1234, 5555, 2500);
    check_plane(m.plane[1], &expect);
    if (pbm) {
        char path[512];
        snprintf(path, sizeof(path), "%s.2.pbm", pbm);
        dump(path, m.plane[1]);
    }
}

static void test_failures(void)
{
    uint8_t latched;
    /* BUSY never releases after reset. */
    boot();
    now = 100;
    m.busy_stuck = 1;
    epd_show(now, 2000, EPD_NO_HUMIDITY, 3000);
    CHECK(run(EPD_SECONDS(2)));
    CHECK(epd.failures == 1 && epd.failed == EPD_BOOT && !powered() && !epd.refreshes);
    CHECK(epd.due == 100 + EPD_INTERVAL);
    /* The refresh never ends: bounded by EPD_REFRESH_LIMIT. */
    boot();
    now = 100;
    m.refresh_stuck = 1;
    epd_show(now, 2000, EPD_NO_HUMIDITY, 3000);
    CHECK(run(EPD_SECONDS(40)));
    CHECK(epd.failures == 1 && epd.failed == EPD_REFRESH && !powered() && !epd.refreshes);
    CHECK(epd_sleep_limit(now) == 0xffffffffUL);
    /* TX_BYTE never sets: the first octet stalls the transfer. */
    boot();
    now = 100;
    m.stall = 1;
    epd_show(now, 2000, EPD_NO_HUMIDITY, 3000);
    CHECK(run(EPD_SECONDS(2)));
    CHECK(epd.failures == 1 && epd.failed == EPD_BOOT && !powered() && m.n == 1);
    /* Failures saturate. */
    epd.failures = 0xff;
    latched = epd.failures;
    m.stall = 1;
    now = epd.due;
    epd_show(now, 2000, EPD_NO_HUMIDITY, 3000);
    CHECK(run(EPD_SECONDS(2)) && epd.failures == latched);
}

int main(int argc, char **argv)
{
    test_format();
    test_refresh(argc > 1 ? argv[1] : NULL);
    test_failures();
    if (failures) { fprintf(stderr, "test_epd: %d failure(s)\n", failures); return 1; }
    puts("test_epd: PASS");
    return 0;
}
