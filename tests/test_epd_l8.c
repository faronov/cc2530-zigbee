/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* CC2530_EPD with CC2530_EPD_PANEL=2 (SSD1677, L8 dashboard) host test:
 * boards/lg_esl29_rev03.c (which includes src/epd.c and src/epd_l8.c)
 * against an SSD1677 model that checks the refresh sequence, the per-octet
 * CS framing and DC level, BUSY ordering, and captures the plane. Views
 * set through the cluster 0xFC10 store must render to the CRC-32 goldens
 * of tools/epd/l8_art.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epd.h"
#include "radio_autoack.h"
#include "epd_l8_golden.h"

MCU_XDATA radio_autoack_attempt_t mac_attempt_raw;

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); failures++; } } while (0)

volatile uint8_t SOC_P0, SOC_P0DIR, SOC_P0SEL, SOC_P0INP, SOC_APCFG, SOC_PERCFG, SOC_P2DIR;
volatile uint8_t SOC_P1, SOC_P1DIR, SOC_P1SEL;
volatile uint8_t SOC_U0CSR, SOC_U0DBUF, SOC_U0BAUD, SOC_U0UCR, SOC_U0GCR;

#define PLANE (EPD_ROWS * EPD_OCTETS)
#define LOG 64
#define REFRESH_MS 3200u

static uint32_t now;
static struct {
    uint8_t busy_stuck, refresh_stuck, refresh_hold, sleeping;
    uint8_t reset_low, reset_seen;
    uint8_t command, mode, misframed, misdc, unpowered, unready, asleep;
    uint32_t busy_until;
    uint8_t log[LOG], data[LOG][5], data_n[LOG], n;
    uint8_t plane[PLANE];
    uint32_t plane_n, other;
    int refreshes;
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
        if (m.n < LOG) { m.log[m.n] = value; m.data_n[m.n++] = 0; }
        if (value == 0x12u) m.busy_until = now + EPD_MS(10);
        if (value == 0x20u) {
            m.busy_until = now + EPD_MS(REFRESH_MS);
            m.refreshes++;
            if (m.refresh_stuck) m.refresh_hold = 1;
        }
        return;
    }
    if (m.command == 0x24u) {
        if (m.plane_n < PLANE) m.plane[m.plane_n] = value;
        m.plane_n++;
        return;
    }
    if (m.command == 0x26u) { m.other++; return; }
    if (m.command == 0x22u) m.mode = value;
    if (m.command == 0x10u && value == 0x01u) m.sleeping = 1;
    if (m.n && m.data_n[m.n-1u] < 5u) m.data[m.n-1u][m.data_n[m.n-1u]++] = value;
}

uint8_t host_mmio_load(const volatile uint8_t *reg, uint8_t address)
{
    if (address == SOC_P1_ADDRESS) {
        int high = (SOC_P1DIR & 0x04u) || (SOC_P1SEL & 0x04u) ? 1 : busy();
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
        SOC_U0CSR |= 0x02u;
    }
    if (address == SOC_P1_ADDRESS && (SOC_P1DIR & 0x02u)) {
        if (!(value & 0x02u)) m.reset_low = 1;
        else if (!(before & 0x02u) && m.reset_low && powered()) {
            m.reset_low = 0; m.reset_seen = 1; m.busy_until = now + EPD_MS(3);
        }
    }
    if (address == SOC_P0_ADDRESS && !powered()) { m.reset_seen = 0; m.refresh_hold = 0; m.sleeping = 0; }
}

static void boot(void)
{
    SOC_P0 = SOC_P0DIR = SOC_P0SEL = SOC_P0INP = SOC_APCFG = 0;
    SOC_P1 = SOC_P1DIR = SOC_P1SEL = 0;
    SOC_PERCFG = 0x01u; SOC_P2DIR = 0xc0u;
    SOC_U0CSR = 0x80u; SOC_U0BAUD = 0x11u; SOC_U0UCR = 0; SOC_U0GCR = 0;
    memset(&m, 0, sizeof(m));
    memset(&epd, 0, sizeof(epd));
    memset(&epd_l8, 0, sizeof(epd_l8));
    memset(&mac_attempt_raw, 0, sizeof(mac_attempt_raw));
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

static uint8_t set16(uint16_t id, uint16_t v)
{
    uint8_t in[4] = { (uint8_t)v, (uint8_t)(v >> 8), 0, 0 };
    return epd_l8_set(id, in, 1, now);
}

static uint8_t set32(uint16_t id, uint32_t v)
{
    uint8_t in[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return epd_l8_set(id, in, 1, now);
}

static uint8_t set_text(uint16_t id, const char *s)
{
    uint8_t in[256];
    size_t n = strlen(s);
    if (n > 250) abort();
    in[0] = (uint8_t)n;
    memcpy(in + 1, s, n);
    return epd_l8_set(id, in, 1, now);
}

/* The store holds golden case i; the view is formatted from it. */
static void load(size_t i, epd_view_t *view)
{
    unsigned k;
    memset(&epd_l8, 0, sizeof(epd_l8));
    CHECK(!set32(0x0000, l8_golden[i].time));
    CHECK(!set16(0x0010, (uint16_t)l8_golden[i].t_out));
    if (l8_golden[i].h_out <= 10000u || l8_golden[i].h_out == 0xffffu) CHECK(!set16(0x0011, l8_golden[i].h_out));
    else epd_l8.outdoor_humidity = l8_golden[i].h_out;
    CHECK(!set16(0x0012, l8_golden[i].weather));
    for (k = 0; k < 4; k++) {
        CHECK(!set_text((uint16_t)(0x20u + 2u * k), l8_golden[i].label[k]));
        CHECK(!set_text((uint16_t)(0x21u + 2u * k), l8_golden[i].value[k]));
    }
    CHECK(!set_text(0x0030, l8_golden[i].message));
    memset(&mac_attempt_raw, 0, sizeof(mac_attempt_raw));
    epd_l8.heard = 0;
    if (l8_golden[i].rssi != 0x7fff) {
        mac_attempt_raw.frame.length = 10;
        mac_attempt_raw.frame.rssi_raw = (uint8_t)(l8_golden[i].rssi + 73);
    }
    memset(view, 0xa5, sizeof(*view));
    epd_format(view, l8_golden[i].t_in, l8_golden[i].h_in, l8_golden[i].mv);
}

static void dump(const char *path, const uint8_t *plane)
{
    FILE *f = fopen(path, "w");
    int x, y;
    if (!f) return;
    fprintf(f, "P1\n800 %u\n", EPD_ROWS);
    for (y = 0; y < (int)EPD_ROWS; y++) {
        for (x = 0; x < 800; x++)
            fputc(plane[(EPD_ROWS - 1 - y) * EPD_OCTETS + x / 8] & (0x80u >> (x & 7)) ? '0' : '1', f);
        fputc('\n', f);
    }
    fclose(f);
}

static void test_render(const char *pbm)
{
    static uint8_t plane[PLANE];
    epd_view_t view;
    size_t i;
    uint16_t row;
    for (i = 0; i < sizeof(l8_golden) / sizeof(l8_golden[0]); i++) {
        load(i, &view);
        for (row = 0; row < EPD_ROWS; row++) {
            epd_render(&view, row);
            memcpy(plane + row * EPD_OCTETS, epd.column, EPD_OCTETS);
        }
        CHECK(crc32(plane, PLANE) == l8_golden[i].crc);
        if (crc32(plane, PLANE) != l8_golden[i].crc)
            fprintf(stderr, "  case %zu: %08lx\n", i, (unsigned long)crc32(plane, PLANE));
        if (pbm && !i) dump(pbm, plane);
    }
}

static const uint8_t sequence[] = {
    0x12, 0x18, 0x0c, 0x01, 0x3c, 0x11, 0x44, 0x45, 0x4e, 0x4f, 0x24, 0x22, 0x20, 0x10
};

static void check_sequence(void)
{
    unsigned i;
    CHECK(m.n == sizeof(sequence));
    for (i = 0; i < sizeof(sequence) && i < m.n; i++) CHECK(m.log[i] == sequence[i]);
    CHECK(m.plane_n == PLANE && !m.other);
    CHECK(!m.misframed && !m.misdc && !m.unpowered && !m.unready && !m.asleep);
    CHECK(!(SOC_P0 & 0xbcu) && !(SOC_P0SEL & 0xbcu) && (SOC_P0DIR & 0xbcu) == 0xbcu && !(SOC_P1 & 0x02u));
    CHECK(m.data_n[1] == 1 && m.data[1][0] == 0x80);
    CHECK(m.data_n[2] == 5 && m.data[2][0] == 0xae && m.data[2][4] == 0x80);
    CHECK(m.data_n[3] == 3 && m.data[3][0] == 0xdf && m.data[3][1] == 0x01 && m.data[3][2] == 0x02);
    CHECK(m.data_n[5] == 1 && m.data[5][0] == 0x03);
    CHECK(m.data_n[6] == 4 && m.data[6][2] == 0x1f && m.data[6][3] == 0x03);
    CHECK(m.data_n[7] == 4 && m.data[7][2] == 0xdf && m.data[7][3] == 0x01);
    CHECK(m.data[11][0] == 0xf7 && m.data[13][0] == 0x01);
}

/* The refresh, the clock and the write-driven policy. */
static void test_refresh(void)
{
    uint32_t written, t0;
    uint8_t in[4];
    boot();
    now = 0x7fff0000UL;
    epd_show(now, -0x8000, 0xffff, 0);
    CHECK(epd.state == EPD_RESET && epd.armed && powered());
    CHECK(epd.due == now + EPD_SECONDS(300) && epd_l8.interval == 300 && !epd_l8.dirty);
    CHECK(run(EPD_SECONDS(5)));
    CHECK(!epd.failures && epd.refreshes == 1 && m.refreshes == 1 && epd.shown);
    CHECK(epd.duration >= (EPD_MS(REFRESH_MS) >> 6) && epd.duration <= (EPD_MS(REFRESH_MS + 200u) >> 6));
    check_sequence();
    CHECK(crc32(m.plane, PLANE) == l8_golden[1].crc);

    /* A write waits EPD_L8_SETTLE after the last one and EPD_L8_SPACING
     * after the last start. */
    t0 = now;
    CHECK(!set_text(0x0030, "Hello"));
    CHECK(epd_l8.dirty && epd_l8.written == now);
    now += EPD_SECONDS(5);
    CHECK(!set_text(0x0030, "Hello"));
    written = now;
    epd_show(now, -0x8000, 0xffff, 0);
    CHECK(epd.state == EPD_IDLE);
    now = written + EPD_L8_SETTLE;
    epd_show(now, -0x8000, 0xffff, 0);
    CHECK(epd.state == EPD_IDLE);
    now = epd_l8.started + EPD_L8_SPACING;
    CHECK(now - written >= EPD_L8_SETTLE);
    memset(&m, 0, sizeof(m));
    epd_show(now, -0x8000, 0xffff, 0);
    CHECK(epd.state == EPD_RESET && !epd_l8.dirty && epd_l8.started == now && epd.due == now + EPD_SECONDS(300));
    CHECK(run(EPD_SECONDS(5)) && epd.refreshes == 2);
    (void)t0;

    /* Writing the same value again is no change; due and unchanged rechecks. */
    CHECK(!set_text(0x0030, "Hello") && !epd_l8.dirty);
    in[0] = 0x2c; in[1] = 0x01;
    CHECK(!epd_l8_set(0x0001, in, 1, now) && !epd_l8.dirty);
    now = epd.due;
    epd_show(now, -0x8000, 0xffff, 0);
    CHECK(epd.state == EPD_IDLE && epd.due == now + EPD_CHECK);

    /* The clock counts whole seconds on from the time write. */
    CHECK(!set32(0x0000, 1000) && epd_l8.dirty && epd_l8.tick == now);
    now += EPD_SECONDS(125) + EPD_MS(500);
    epd_l8.dirty = 0;
    epd_show(now, -0x8000, 0xffff, 0);
    CHECK(epd_l8.time == 1125 && epd_l8.tick == now - EPD_MS(500));
    CHECK(epd.state == EPD_RESET && run(EPD_SECONDS(5)));
    /* A later time write is not a change by itself. */
    CHECK(!set32(0x0000, 5000) && !epd_l8.dirty && epd_l8.time == 5000);

    /* The interval spaces scheduled refreshes. */
    CHECK(!set16(0x0001, 3600));
    now = epd.due;
    memset(&m, 0, sizeof(m));
    epd_show(now, 2000, 5000, 3000);
    CHECK(epd.state == EPD_RESET && epd.due == now + EPD_SECONDS(3600));
    CHECK(run(EPD_SECONDS(5)));
}

static void test_store(void)
{
    uint8_t out[80], in[200];
    unsigned i;
    boot();
    now = 1000;
    CHECK(epd_l8_type(0x0000) == 0x23 && epd_l8_type(0x0001) == 0x21 && epd_l8_type(0x0010) == 0x29);
    CHECK(epd_l8_type(0x0011) == 0x21 && epd_l8_type(0x0012) == 0x30 && epd_l8_type(0xfffd) == 0x21);
    CHECK(epd_l8_type(0x0020) == 0x42 && epd_l8_type(0x0027) == 0x42 && epd_l8_type(0x0030) == 0x42);
    CHECK(!epd_l8_type(0x0002) && !epd_l8_type(0x0028) && !epd_l8_type(0x0031) && !epd_l8_type(0xfffc));
    /* Defaults. */
    CHECK(epd_l8_get(0x0001, out, 1) == 2 && out[0] == 0x2c && out[1] == 0x01);
    CHECK(epd_l8_get(0x0010, out, 1) == 2 && out[0] == 0x00 && out[1] == 0x80);
    CHECK(epd_l8_get(0x0011, out, 1) == 2 && out[0] == 0xff && out[1] == 0xff);
    CHECK(epd_l8_get(0x0012, out, 1) == 1 && out[0] == 0);
    CHECK(epd_l8_get(0xfffd, out, 1) == 2 && out[0] == 1 && out[1] == 0);
    CHECK(epd_l8_get(0x0030, out, 1) == 1 && out[0] == 0);
    /* Ranges; nothing is applied without apply. */
    CHECK(set16(0x0001, 59) == 0x87 && set16(0x0001, 28801) == 0x87 && !set16(0x0001, 60) && !set16(0x0001, 28800));
    CHECK(set16(0x0011, 10001) == 0x87 && set16(0x0011, 0xfffe) == 0x87 && !set16(0x0011, 10000) && !set16(0x0011, 0xffff));
    CHECK(set16(0x0012, 10) == 0x87 && !set16(0x0012, 9));
    CHECK(set16(0xfffd, 1) == 0x88);
    in[0] = 0x10; in[1] = 0x27;
    epd_l8.dirty = 0;
    CHECK(!epd_l8_set(0x0010, in, 0, now) && epd_l8.outdoor == (int16_t)0x8000 && !epd_l8.dirty);
    CHECK(!set32(0x0000, 0x12345678UL));
    CHECK(epd_l8_get(0x0000, out, 1) == 4 && out[0] == 0x78 && out[3] == 0x12);
    /* UTF-8 round trip: Cyrillic, Ё, ё, °. */
    CHECK(!set_text(0x0021, "Ёлка ё 5°"));
    CHECK(epd_l8_get(0x0021, out, 1) == 1 + 15 && !memcmp(out + 1, "Ёлка ё 5°", 15) && out[0] == 15);
    CHECK(epd_l8_get(0x0021, NULL, 0) == 16);
    /* Unsupported: '?', stray continuations dropped, truncation at the
     * field, and an empty (0xFF length) string. */
    CHECK(!set_text(0x0020, "a\x80" "b\tc€d\xc3"));
    CHECK(epd_l8_get(0x0020, out, 1) == 1 + 7 && !memcmp(out + 1, "ab?c?d?", 7));
    CHECK(!set_text(0x0020, "abcdefghijklmnopqrstuvwxyz"));
    CHECK(epd_l8_get(0x0020, out, 1) == 1 + 22 && !memcmp(out + 1, "abcdefghijklmnopqrstuv", 22));
    in[0] = 0xff;
    CHECK(!epd_l8_set(0x0020, in, 1, now) && epd_l8_get(0x0020, out, 1) == 1 && !out[0]);
    /* A long Cyrillic message reads back cut at 72 octets. */
    in[0] = 96;
    for (i = 0; i < 48; i++) { in[1 + 2 * i] = 0xd0; in[2 + 2 * i] = 0x96; }
    CHECK(!epd_l8_set(0x0030, in, 1, now) && epd_l8.message[47] != EPD_L8_END);
    CHECK(epd_l8_get(0x0030, out, 1) == 73 && out[0] == 72 && out[71] == 0xd0 && out[72] == 0x96);
}

static void test_failures(void)
{
    boot();
    now = 100;
    m.busy_stuck = 1;
    epd_show(now, 2000, 4500, 3000);
    CHECK(run(EPD_SECONDS(2)));
    CHECK(epd.failures == 1 && epd.failed == EPD_BOOT && !powered() && !epd.refreshes);
    boot();
    now = 100;
    m.refresh_stuck = 1;
    epd_show(now, 2000, 4500, 3000);
    CHECK(run(EPD_SECONDS(40)));
    CHECK(epd.failures == 1 && epd.failed == EPD_REFRESH && !powered() && !epd.shown);
    /* After a failure the next due refresh runs even if unchanged. */
    memset(&m, 0, sizeof(m));
    now = epd.due;
    epd_show(now, 2000, 4500, 3000);
    CHECK(epd.state == EPD_RESET && run(EPD_SECONDS(5)) && epd.shown && epd.failures == 1);
}

int main(int argc, char **argv)
{
    test_render(argc > 1 ? argv[1] : NULL);
    test_refresh();
    test_store();
    test_failures();
    if (failures) { fprintf(stderr, "test_epd_l8: %d failure(s)\n", failures); return 1; }
    puts("test_epd_l8: PASS");
    return 0;
}
