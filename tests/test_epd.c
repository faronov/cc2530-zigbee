/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* CC2530_EPD host test: boards/lg_esl29_rev03.c (which includes src/epd.c)
 * against a model of the selected controller (UC8151D or SSD1680) that
 * checks the refresh sequences, the per-octet CS framing and DC level, BUSY
 * ordering, and captures both data planes. The rendered planes must match
 * the CRC-32 goldens of the reference frames (tools/epd/d3_art.py). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epd.h"
#include "epd_d3_golden.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); failures++; } } while (0)

volatile uint8_t SOC_P0, SOC_P0DIR, SOC_P0SEL, SOC_P0INP, SOC_APCFG, SOC_PERCFG, SOC_P2DIR;
volatile uint8_t SOC_P1, SOC_P1DIR, SOC_P1SEL;
volatile uint8_t SOC_U0CSR, SOC_U0DBUF, SOC_U0BAUD, SOC_U0UCR, SOC_U0GCR;

#define PLANE (EPD_ROWS * EPD_OCTETS)
#define LOG 64
#define SSD (CC2530_EPD_PANEL == EPD_SSD1680)
#if SSD
#define OLD_PLANE 0x26u
#define NEW_PLANE 0x24u
#define REFRESH_MS 2970u
#define PARTIAL_MS 1680u
#else
#define OLD_PLANE 0x10u
#define NEW_PLANE 0x13u
#define REFRESH_MS 4910u
#endif

static uint32_t now;
static struct {
    uint8_t stall, busy_stuck, refresh_stuck, refresh_hold, sleeping;
    uint8_t reset_low, reset_seen;
    uint8_t command, mode, misframed, misdc, unpowered, unready, asleep;
    uint32_t busy_until;
    uint8_t log[LOG], data[LOG][4], data_n[LOG], n;
    uint8_t plane[2][PLANE];
    uint16_t plane_n[2];
    uint8_t lut[160];
    uint16_t lut_n;
    int refreshes, partials;
} m;

static int powered(void)
{
    return (SOC_P0DIR & 0x80u) && (SOC_P0 & 0x80u) && !(SOC_P0SEL & 0x80u);
}

static int busy(void)
{
    if (!powered()) return 0;
    if (m.busy_stuck || m.refresh_hold || m.sleeping) return 1;
    return (int32_t)(m.busy_until - now) > 0;
}

static void model_command(uint8_t value)
{
    if (m.n < LOG) { m.log[m.n] = value; m.data_n[m.n++] = 0; }
#if SSD
    if (value == 0x12u) m.busy_until = now + EPD_MS(10);
    if (value == 0x32u) m.lut_n = 0;
    if (value == 0x20u) {
        if (m.mode == 0xb1u) m.busy_until = now + EPD_MS(10);
        else {
            m.busy_until = now + EPD_MS(m.mode == 0xc7u ? PARTIAL_MS : REFRESH_MS);
            m.refreshes++;
            if (m.mode == 0xc7u) m.partials++;
            if (m.refresh_stuck) m.refresh_hold = 1;
        }
    }
#else
    if (value == 0x04u) m.busy_until = now + EPD_MS(50);
    if (value == 0x12u) { m.busy_until = now + EPD_MS(REFRESH_MS); m.refreshes++; if (m.refresh_stuck) m.refresh_hold = 1; }
    if (value == 0x02u) m.busy_until = now + EPD_MS(40);
#endif
}

static void model_byte(uint8_t value)
{
    int data = (SOC_P0 & 0x04u) != 0;
    if (SOC_P0 & 0x10u) m.misframed++;
    if (!powered() || !m.reset_seen || m.reset_low) m.unpowered++;
    if (!(SOC_P0SEL & 0x08u) || !(SOC_P0SEL & 0x20u) || (SOC_P0SEL & 0x14u) || (SOC_PERCFG & 1u) ||
        SOC_U0GCR != 0x2fu || SOC_U0BAUD != 0 || (SOC_U0CSR & 0xa0u)) m.misdc++;
    if (busy()) m.unready++;
    if (m.sleeping) m.asleep++;
    if (!data) {
        m.command = value;
        model_command(value);
        return;
    }
    if (m.command == OLD_PLANE || m.command == NEW_PLANE) {
        int p = m.command == NEW_PLANE;
        if (m.plane_n[p] < PLANE) m.plane[p][m.plane_n[p]] = value;
        m.plane_n[p]++;
        return;
    }
#if SSD
    if (m.command == 0x32u && m.lut_n < sizeof(m.lut)) { m.lut[m.lut_n++] = value; return; }
    if (m.command == 0x22u) m.mode = value;
    if (m.command == 0x10u && value == 0x01u) m.sleeping = 1;
#endif
    if (m.n && m.data_n[m.n-1u] < 4u) m.data[m.n-1u][m.data_n[m.n-1u]++] = value;
}

uint8_t host_mmio_load(const volatile uint8_t *reg, uint8_t address)
{
    if (address == SOC_P1_ADDRESS) {
        int high = (SOC_P1DIR & 0x04u) || (SOC_P1SEL & 0x04u) ? 1 : SSD ? busy() : !busy();
        return (uint8_t)((*reg & (uint8_t)~0x04u) | (high ? 0x04u : 0));
    }
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
    if (address == SOC_P0_ADDRESS && !powered()) { m.reset_seen = 0; m.refresh_hold = 0; m.sleeping = 0; }
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

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffUL;
    int k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++) c = c & 1u ? (c >> 1) ^ 0xedb88320UL : c >> 1;
    }
    return ~c;
}

static uint32_t plane_crc(const uint8_t *plane)
{
    return crc32(plane, PLANE);
}

static uint32_t golden(int16_t centi, uint16_t humidity, uint16_t mv)
{
    size_t i;
    for (i = 0; i < sizeof(d3_golden) / sizeof(d3_golden[0]); i++)
        if (d3_golden[i].centi == centi && d3_golden[i].humidity == humidity && d3_golden[i].millivolts == mv)
            return d3_golden[i].crc;
    abort();
}

static void dump(const char *path, const uint8_t *plane)
{
    FILE *f = fopen(path, "w");
    int x, y;
    if (!f) return;
    fprintf(f, "P1\n%u 128\n", EPD_ROWS);
    for (y = 0; y < 128; y++) {
        for (x = 0; x < (int)EPD_ROWS; x++)
            fputc(plane[(EPD_ROWS - 1 - x) * EPD_OCTETS + y / 8] & (0x80u >> (y & 7)) ? '0' : '1', f);
        fputc('\n', f);
    }
    fclose(f);
}

/* Every reference frame, rendered row by row. */
static void test_render(void)
{
    static uint8_t plane[PLANE];
    epd_view_t view;
    size_t i;
    uint16_t row;
    for (i = 0; i < sizeof(d3_golden) / sizeof(d3_golden[0]); i++) {
        memset(&view, 0xa5, sizeof(view));
        epd_format(&view, d3_golden[i].centi, d3_golden[i].humidity, d3_golden[i].millivolts);
        for (row = 0; row < EPD_ROWS; row++) {
            epd_render(&view, row);
            memcpy(plane + row * EPD_OCTETS, epd.column, EPD_OCTETS);
        }
        CHECK(plane_crc(plane) == d3_golden[i].crc);
        if (plane_crc(plane) != d3_golden[i].crc)
            fprintf(stderr, "  case %zu (%d %u %u): %08lx\n", i, d3_golden[i].centi, d3_golden[i].humidity,
                    d3_golden[i].millivolts, (unsigned long)plane_crc(plane));
    }
}

/* Views equal exactly when the rendering is: rounding inside a digit. */
static void test_format(void)
{
    epd_view_t a, b;
    epd_format(&a, 2345, 4549, 2974);
    epd_format(&b, 2354, 4450, 2965);
    CHECK(!memcmp(&a, &b, sizeof(a)));
    epd_format(&b, 2355, 4450, 2965);
    CHECK(memcmp(&a, &b, sizeof(a)));
    epd_format(&b, 2345, 4549, 2975);
    CHECK(memcmp(&a, &b, sizeof(a)));
}

#if SSD
static const uint8_t full_sequence[] = {
    0x12, 0x01, 0x11, 0x21, 0x44, 0x45, 0x3c, 0x18, 0x26, 0x4e, 0x4f, 0x24, 0x22, 0x20, 0x10
};
static const uint8_t partial_sequence[] = {
    0x12, 0x01, 0x11, 0x21, 0x44, 0x45, 0x3c, 0x18, 0x26, 0x4e, 0x4f, 0x24, 0x22, 0x20, 0x32, 0x22, 0x20, 0x10
};
#else
static const uint8_t full_sequence[] = { 0x06, 0x04, 0x00, 0x61, 0x82, 0x50, 0x10, 0x13, 0x12, 0x02 };
#endif

static void check_sequence(const uint8_t *s, unsigned n)
{
    unsigned i;
    CHECK(m.n == n);
    for (i = 0; i < n && i < m.n; i++) CHECK(m.log[i] == s[i]);
    CHECK(m.plane_n[0] == PLANE && m.plane_n[1] == PLANE);
    CHECK(!m.misframed && !m.misdc && !m.unpowered && !m.unready && !m.asleep);
    /* Panel off: power latch low, controls driven low, SPI pins released. */
    CHECK(!(SOC_P0 & 0xbcu) && !(SOC_P0SEL & 0xbcu) && (SOC_P0DIR & 0xbcu) == 0xbcu && !(SOC_P1 & 0x02u));
#if SSD
    CHECK(m.data_n[1] == 3 && m.data[1][0] == 0x27 && m.data[1][1] == 0x01 && m.data[1][2] == 0x00);
    CHECK(m.data_n[3] == 2 && m.data[3][0] == 0x00 && m.data[3][1] == 0x80);
    CHECK(m.data_n[5] == 4 && m.data[5][2] == 0x27 && m.data[5][3] == 0x01);
    CHECK(m.data[12][0] == (n == sizeof(partial_sequence) ? 0xb1u : 0xf7u));
    CHECK(m.data[n - 1u][0] == 0x01u);
#else
    CHECK(m.data_n[0] == 3 && m.data[0][0] == 0x17 && m.data[0][2] == 0x17 && m.data_n[1] == 0);
    CHECK(m.data_n[2] == 2 && m.data[2][0] == 0x1f && m.data[2][1] == 0x0d);
    CHECK(m.data_n[3] == 3 && m.data[3][0] == 0x80 && m.data[3][1] == 0x01 && m.data[3][2] == 0x28);
    CHECK(m.data_n[4] == 1 && m.data[4][0] == 0x12 && m.data_n[5] == 1 && m.data[5][0] == 0x97);
#endif
}

static void test_refresh(const char *pbm)
{
    unsigned i;
    boot();
    now = 0x7fff0000UL;
    CHECK(epd_sleep_limit(now) == 0xffffffffUL);
    epd_show(now, 2347, 4500, 2970);
    CHECK(epd.state == EPD_RESET && epd.armed && !epd.partial && powered() && epd_sleep_limit(now) == 0);
    epd_show(now, 0, 0, 0);
    CHECK(epd.state == EPD_RESET);
    while (epd.state != EPD_REFRESH && epd.state != EPD_IDLE) { epd_step(now); now += EPD_MS(1); }
    CHECK(epd.state == EPD_REFRESH && epd_sleep_limit(now) > EPD_REFRESH_EXPECTED - EPD_MS(10) &&
          epd_sleep_limit(now) <= EPD_REFRESH_EXPECTED);
    now += EPD_REFRESH_EXPECTED - EPD_MS(1000);
    CHECK(epd_sleep_limit(now) <= EPD_MS(1000));
    now += EPD_MS(1000);
    CHECK(epd_sleep_limit(now) == EPD_SLEEP_POLL);
    CHECK(run(EPD_SECONDS(1)));
    CHECK(!epd.failures && epd.refreshes == 1 && m.refreshes == 1 && epd.shown && !epd.partials);
    CHECK(epd.duration >= (EPD_MS(REFRESH_MS) >> 6) && epd.duration <= (EPD_MS(REFRESH_MS + 200u) >> 6));
    check_sequence(full_sequence, sizeof(full_sequence));
    for (i = 0; i < PLANE; i++) CHECK(m.plane[0][i] == 0xffu);
    CHECK(plane_crc(m.plane[1]) == golden(2347, 4500, 2970));
    if (pbm) dump(pbm, m.plane[1]);

    /* Not due: nothing. Due and unchanged: recheck in EPD_CHECK. */
    epd_show(now, 2101, 0, 3000);
    CHECK(epd.state == EPD_IDLE);
    now = epd.due;
    epd_show(now, 2351, 4549, 2974);
    CHECK(epd.state == EPD_IDLE && epd.due == now + EPD_CHECK);
    now = epd.due;
    model_reset();
    epd_show(now, -512, 9999, 2005);
    CHECK(epd.state == EPD_RESET && epd.due == now + EPD_INTERVAL && epd.partial == SSD);
    CHECK(run(EPD_SECONDS(10)) && epd.refreshes == 2 && !epd.failures);
    CHECK(plane_crc(m.plane[0]) == golden(2347, 4500, 2970));
    CHECK(plane_crc(m.plane[1]) == golden(-512, 9999, 2005));
#if SSD
    check_sequence(partial_sequence, sizeof(partial_sequence));
    CHECK(m.partials == 1 && epd.partials == 1);
    CHECK(epd.duration >= (EPD_MS(PARTIAL_MS) >> 6) && epd.duration <= (EPD_MS(PARTIAL_MS + 200u) >> 6));
    CHECK(m.lut_n == 153);
    for (i = 0; i < 153u; i++)
        CHECK(m.lut[i] == (i == 12u ? 0x80u : i == 24u ? 0x40u : i == 60u ? EPD_PARTIAL_FRAMES :
                           i >= 144u && i < 150u ? 0x22u : 0));
#else
    check_sequence(full_sequence, sizeof(full_sequence));
#endif
    if (pbm) {
        char path[512];
        snprintf(path, sizeof(path), "%s.2.pbm", pbm);
        dump(path, m.plane[1]);
    }
}

#if SSD
/* EPD_PARTIALS partial refreshes, then a full one; a failure forces one. */
static void test_partials(void)
{
    int i;
    boot();
    now = 1000;
    for (i = 0; i <= (int)EPD_PARTIALS + 1; i++) {
        model_reset();
        m.reset_low = 0;
        now = epd.armed ? epd.due : now;
        epd_show(now, (int16_t)(1000 + i * 10), 4500, 2970);
        CHECK(epd.partial == (i >= 1 && i <= (int)EPD_PARTIALS));
        CHECK(run(EPD_SECONDS(10)) && !epd.failures);
        CHECK(m.partials == (i >= 1 && i <= (int)EPD_PARTIALS));
    }
    CHECK(epd.partials == 0 && epd.shown);
    /* A stuck refresh fails; the next one is full and starts from white. */
    model_reset();
    m.refresh_stuck = 1;
    now = epd.due;
    epd_show(now, 1500, 4500, 2970);
    CHECK(epd.partial && run(EPD_SECONDS(40)) && epd.failures == 1 && epd.failed == EPD_REFRESH && !epd.shown);
    model_reset();
    now = epd.due;
    epd_show(now, 1500, 4500, 2970);
    CHECK(!epd.partial && run(EPD_SECONDS(10)) && epd.failures == 1 && !m.partials && epd.shown);
    for (i = 0; i < (int)PLANE; i++) CHECK(m.plane[0][i] == 0xffu);
}
#endif

static void test_failures(void)
{
    uint8_t latched;
    /* BUSY never releases after reset. */
    boot();
    now = 100;
    m.busy_stuck = 1;
    epd_show(now, 2000, 4500, 3000);
    CHECK(run(EPD_SECONDS(2)));
    CHECK(epd.failures == 1 && epd.failed == EPD_BOOT && !powered() && !epd.refreshes);
    CHECK(epd.due == 100 + EPD_INTERVAL);
    /* The refresh never ends: bounded by EPD_REFRESH_LIMIT. */
    boot();
    now = 100;
    m.refresh_stuck = 1;
    epd_show(now, 2000, 4500, 3000);
    CHECK(run(EPD_SECONDS(40)));
    CHECK(epd.failures == 1 && epd.failed == EPD_REFRESH && !powered() && !epd.refreshes);
    CHECK(epd_sleep_limit(now) == 0xffffffffUL);
    /* TX_BYTE never sets: the first octet stalls the transfer. */
    boot();
    now = 100;
    m.stall = 1;
    epd_show(now, 2000, 4500, 3000);
    CHECK(run(EPD_SECONDS(2)));
    CHECK(epd.failures == 1 && epd.failed == EPD_BOOT && !powered() && m.n == 1);
    /* Failures saturate; an unchanged view is retried after a failure. */
    epd.failures = 0xff;
    latched = epd.failures;
    m.stall = 1;
    now = epd.due;
    epd_show(now, 2000, 4500, 3000);
    CHECK(epd.state == EPD_RESET);
    CHECK(run(EPD_SECONDS(2)) && epd.failures == latched);
}

int main(int argc, char **argv)
{
    test_render();
    test_format();
    test_refresh(argc > 1 ? argv[1] : NULL);
#if SSD
    test_partials();
#endif
    test_failures();
    if (failures) { fprintf(stderr, "test_epd: %d failure(s)\n", failures); return 1; }
    puts("test_epd: PASS");
    return 0;
}
