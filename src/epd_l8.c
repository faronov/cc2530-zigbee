/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* L8 dashboard (epd.h, EPD_SSD1677): the row renderer mirrors render_row()
 * of tools/epd/l8_art.py bit for bit (tests/test_epd_l8.c checks the CRC-32
 * goldens), plus the cluster 0xFC10 store it shows. Included by src/epd.c;
 * the tables and the per-row drawing primitives stay in common CODE, the
 * rest is left to the auto-banker. */
#if defined(CC2530_MAC_ATTEMPT)
#include "radio_autoack.h"

/* src/mac_attempt.c: the last attempt's received frame. */
extern MCU_XDATA radio_autoack_attempt_t mac_attempt_raw;
#endif

MCU_XDATA epd_l8_t epd_l8;

#define L8_WIDTH 800u
#define G(c) ((uint8_t)((c) - 0x20))
#define L8_DIGIT G('0')
#define L8_MINUS G('-')
#define L8_DOT G('.')
#define L8_TEXT_MAX 72u
#define CYRILLIC 95u
#define YO 159u
#define YO_SMALL 160u

/* 7-segment number sizes: digit width w, height ht (w + 1 and ht + 1
 * pixels), half stroke h, gap between characters. */
static const MCU_CODE uint8_t l8_size[2][4] = { { 52, 100, 7, 10 }, { 30, 56, 4, 6 } };
static const MCU_CODE uint8_t l8_double[16] = {
    0x00, 0x03, 0x0c, 0x0f, 0x30, 0x33, 0x3c, 0x3f, 0xc0, 0xc3, 0xcc, 0xcf, 0xf0, 0xf3, 0xfc, 0xff
};
static const MCU_CODE uint8_t l8_month[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static void l8_prepare(void)
{
    if (epd_l8.ready) return;
    memset(&epd_l8, EPD_L8_END, sizeof(epd_l8));
    epd_l8.time = epd_l8.tick = epd_l8.written = epd_l8.started = 0;
    epd_l8.interval = 300;
    epd_l8.outdoor = (int16_t)-0x8000;
    epd_l8.weather = epd_l8.dirty = epd_l8.heard = 0;
    epd_l8.ready = 1;
}

/* Counts the clock on by the whole seconds since tick. */
static void l8_tick(uint32_t now)
{
    uint32_t d;
    l8_prepare();
    d = (now - epd_l8.tick) / 62500UL;
    if (!d) return;
    epd_l8.tick += d * 62500UL;
    if (epd_l8.time) epd_l8.time += d;
}

/* --- rendering into epd.column (ink clears a bit, MSB left) --- */
static void MCU_COMMON_CODE l8_span(uint16_t x0, uint16_t x1)
{
    uint8_t a, b;
    if (x1 > L8_WIDTH - 1u) x1 = L8_WIDTH - 1u;
    if (x0 > x1) return;
    a = (uint8_t)(x0 >> 3);
    b = (uint8_t)(x1 >> 3);
    if (a == b) {
        epd.column[a] &= (uint8_t)~((uint8_t)(0xffu >> (x0 & 7u)) & (uint8_t)(0xffu << (7u - (x1 & 7u))));
        return;
    }
    epd.column[a] &= (uint8_t)~(uint8_t)(0xffu >> (x0 & 7u));
    while (++a < b) epd.column[a] = 0;
    epd.column[b] &= (uint8_t)(0xffu >> ((x1 & 7u) + 1u));
}

/* Inks the set bits of v (MSB left) at columns x .. x + 7. */
static void MCU_COMMON_CODE l8_put(uint16_t x, uint8_t v)
{
    uint8_t i = (uint8_t)(x >> 3), s = (uint8_t)(x & 7u);
    if (!v) return;
    epd.column[i] &= (uint8_t)~(uint8_t)(v >> s);
    if (s && i + 1u < EPD_OCTETS) epd.column[i + 1u] &= (uint8_t)~(uint8_t)(v << (8u - s));
}

/* v with every bit doubled, at columns x .. x + 15. */
static void MCU_COMMON_CODE l8_put2(uint16_t x, uint8_t v)
{
    l8_put(x, l8_double[v >> 4]);
    l8_put((uint16_t)(x + 8u), l8_double[v & 15u]);
}

/* Row r (0..15) of glyph c. */
static uint8_t MCU_COMMON_CODE l8_glyph(uint8_t c, uint8_t r)
{
    const MCU_CODE epd_l8_glyph_t *g = &l8_glyph_index[c];
    uint8_t top = g->span >> 4;
    if (r < top || r > (g->span & 15u)) return 0;
    return l8_glyph_bits[g->at + (uint8_t)(r - top)];
}

/* Glyph row r of n codes at scale 1 or 2 from x, stopping before clip. */
static void MCU_COMMON_CODE l8_text(uint16_t x, uint16_t y, uint16_t clip, const uint8_t MCU_XDATA *codes, uint8_t n,
    uint8_t scale, uint16_t fy)
{
    uint16_t dy = (uint16_t)(fy - y);
    uint8_t r, c;
    if (fy < y || dy >= 16u * scale) return;
    r = (uint8_t)(dy >> (scale - 1u));
    for (; n && (c = *codes) != EPD_L8_END; n--, codes++) {
        if (x + 8u * scale > clip) return;
        if (scale == 2u) l8_put2(x, l8_glyph(c, r));
        else l8_put(x, l8_glyph(c, r));
        x += 8u * scale;
    }
}

/* Columns x + a .. x + b. */
static __attribute__((noinline)) void MCU_COMMON_CODE l8_bar(uint16_t x, uint8_t a, uint8_t b)
{
    l8_span((uint16_t)(x + a), (uint16_t)(x + b));
}

static uint8_t l8_min(uint8_t a, uint8_t b, uint8_t c)
{
    if (b < a) a = b;
    return c < a ? c : a;
}

/* Row dy (0..ht) of a digit with segment mask (bit 0 a .. bit 6 g) at
 * column x; the b/c verticals are centred on x + right. The bars a, g, d
 * run between the verticals and taper; without the a (d) bar the upper
 * (lower) verticals reach the digit edge. */
static __attribute__((noinline)) void MCU_COMMON_CODE l8_digit(uint16_t x, uint8_t mask, uint8_t dy, const MCU_CODE uint8_t *z, uint8_t right)
{
    uint8_t w = z[0], ht = z[1], h = z[2], mid = (uint8_t)(ht >> 1), c, d, k, i, bit;
    for (i = 0; i < 3u; i++) {
        bit = i == 0 ? 0x01u : i == 1u ? 0x40u : 0x08u;
        c = i == 0 ? h : i == 1u ? mid : (uint8_t)(ht - h);
        if (!(mask & bit)) continue;
        d = dy > c ? (uint8_t)(dy - c) : (uint8_t)(c - dy);
        if (d <= h && (uint8_t)(2u * (h + d + 1u)) <= w)
            l8_bar(x, (uint8_t)(h + 1u + d), (uint8_t)(w - h - 1u - d));
    }
    c = mask & 0x01u ? (uint8_t)(h + 1u) : 0;
    if (dy >= c && dy < mid) {
        k = l8_min(h, (uint8_t)(dy - c), (uint8_t)(mid - 1u - dy));
        if (mask & 0x20u) l8_bar(x, (uint8_t)(h - k), (uint8_t)(h + k));
        if (mask & 0x02u) l8_bar(x, (uint8_t)(right - k), (uint8_t)(right + k));
    }
    c = mask & 0x08u ? (uint8_t)(ht - h - 1u) : ht;
    if (dy > mid && dy <= c) {
        k = l8_min(h, (uint8_t)(dy - mid - 1u), (uint8_t)(c - dy));
        if (mask & 0x10u) l8_bar(x, (uint8_t)(h - k), (uint8_t)(h + k));
        if (mask & 0x04u) l8_bar(x, (uint8_t)(right - k), (uint8_t)(right + k));
    }
}

/* A number at size: digits, '-', '.', ',' and ':' are 7-segment shapes
 * (1, dots and colons narrow), other glyphs doubled. */
static void l8_number(uint16_t x, uint16_t y, uint16_t clip, const uint8_t MCU_XDATA *codes, uint8_t n,
    uint8_t size, uint16_t fy)
{
    const MCU_CODE uint8_t *z = l8_size[size];
    uint8_t w = z[0], ht = z[1], h = z[2], c, adv, d, dy;
    if (fy < y || fy - y > ht) return;
    dy = (uint8_t)(fy - y);
    for (; n && (c = *codes) != EPD_L8_END; n--, codes++) {
        d = (uint8_t)(c - L8_DIGIT);
        if (d >= 10u && c != L8_MINUS && c != L8_DOT && c != G(',') && c != G(':')) {
            if (x + 16u > clip) return;
            if (dy < 32u) l8_put2(x, l8_glyph(c, dy >> 1));
            x += 16u;
            continue;
        }
        adv = d == 1u || (d >= 10u && c != L8_MINUS) ? (uint8_t)(2u * h + 1u) : (uint8_t)(w + 1u);
        if (x + adv > clip) return;
        if (c == L8_DOT || c == G(',')) {
            if (dy >= ht - 2u * h) l8_bar(x, 0, (uint8_t)(2u * h));
        } else if (c == G(':')) {
            d = (uint8_t)(ht / 4u);
            if ((dy >= d - h && dy <= d + h) || (dy >= 3u * d - h && dy <= 3u * d + h))
                l8_bar(x, 0, (uint8_t)(2u * h));
        } else {
            l8_digit(x, c == L8_MINUS ? 0x40u : segment[d], dy, z, (uint8_t)(d == 1u ? h : w - h));
        }
        x += (uint8_t)(adv + z[3]);
    }
}

void epd_render(const epd_view_t MCU_XDATA *view, uint16_t row) JOIN_FAR
{
    const MCU_CODE epd_l8_sprite_t *s;
    const MCU_CODE uint8_t *bits;
    uint16_t fy = (uint16_t)(EPD_ROWS - 1u - row), x;
    uint8_t i, j;
    memset(epd.column, 0xff, EPD_OCTETS);
    for (i = 0; i < EPD_L8_RULES; i++)
        if (fy >= l8_rule[i][1] && fy <= l8_rule[i][3]) l8_span(l8_rule[i][0], l8_rule[i][2]);
    for (i = 0; i < EPD_L8_SPRITES; i++) {
        s = &l8_sprite[i];
        if (fy < s->y || fy >= s->y + s->height) continue;
        bits = &l8_sprite_bits[s->at + (fy - s->y) * s->octets];
        for (j = 0, x = s->x; j < s->octets; j++, x += 8u) l8_put(x, bits[j]);
    }
    if (fy >= 19u && fy <= 32u && view->fill) l8_span(20, (uint16_t)(19u + view->fill));
    if (view->weather && fy >= 86u && fy < 86u + 96u) {
        bits = l8_icon_row[l8_icon[view->weather - 1u][(fy - 86u) >> 1]];
        for (j = 0, x = 420; j < 6u; j++, x += 16u) l8_put2(x, bits[j]);
    }
    l8_text(72, 10, 184, view->volt, sizeof(view->volt), 2, fy);
    l8_text(248, 10, 536, view->rssi, sizeof(view->rssi), 2, fy);
    l8_text(560, 10, 784, view->clock, sizeof(view->clock), 2, fy);
    l8_number(72, 86, 396, view->indoor, sizeof(view->indoor), 0, fy);
    l8_number(72, 206, 396, view->humidity, sizeof(view->humidity), 1, fy);
    l8_number(528, 86, 796, view->outdoor, sizeof(view->outdoor), 0, fy);
    l8_number(528, 206, 796, view->outdoor_humidity, sizeof(view->outdoor_humidity), 1, fy);
    for (i = 0, x = 16; i < EPD_L8_SLOTS; i++, x += 200u) {
        l8_text(x, 296, (uint16_t)(x + 176u), epd_l8.label[i], EPD_L8_LABEL, 1, fy);
        l8_number(x, 324, (uint16_t)(x + 176u), epd_l8.value[i], EPD_L8_VALUE, 1, fy);
    }
    l8_text(16, 424, 784, epd_l8.message, EPD_L8_MESSAGE, 2, fy);
}

/* --- formatting into glyph codes --- */
static uint8_t MCU_XDATA *l8_two(uint8_t MCU_XDATA *p, uint8_t v)
{
    *p++ = (uint8_t)(L8_DIGIT + v / 10u);
    *p++ = (uint8_t)(L8_DIGIT + v % 10u);
    return p;
}

/* v (< 1000) without leading zeros. */
static uint8_t MCU_XDATA *l8_decimal(uint8_t MCU_XDATA *p, uint16_t v)
{
    if (v >= 100u) { *p++ = (uint8_t)(L8_DIGIT + v / 100u); v %= 100u; return l8_two(p, (uint8_t)v); }
    if (v >= 10u) return l8_two(p, (uint8_t)v);
    *p++ = (uint8_t)(L8_DIGIT + v);
    return p;
}

/* Tenths for -9.9 .. 99.9, whole degrees for -10 .. -99, otherwise ---;
 * 0x8000 shows --.-. */
static void l8_temperature(uint8_t MCU_XDATA *p, int16_t centi)
{
    uint16_t a;
    uint8_t neg;
    if (centi == (int16_t)-0x8000) {
        *p++ = L8_MINUS; *p++ = L8_MINUS; *p++ = L8_DOT; *p++ = L8_MINUS;
    } else {
        neg = centi < 0;
        a = neg ? (uint16_t)-(uint16_t)centi : (uint16_t)centi;
        a = (uint16_t)(a + 5u) / 10u;
        if (!a) neg = 0;
        if (a <= (neg ? 99u : 999u)) {
            if (neg) *p++ = L8_MINUS;
            p = l8_decimal(p, a / 10u);
            *p++ = L8_DOT;
            *p++ = (uint8_t)(L8_DIGIT + a % 10u);
        } else if (neg && a <= 999u) {
            *p++ = L8_MINUS;
            p = l8_decimal(p, a / 10u);
        } else {
            *p++ = L8_MINUS; *p++ = L8_MINUS; *p++ = L8_MINUS;
        }
    }
    *p++ = EPD_L8_DEGREE; *p++ = G('C'); *p = EPD_L8_END;
}

static void l8_humidity(uint8_t MCU_XDATA *p, uint16_t humidity)
{
    if (humidity == 0xffffu) { *p++ = L8_MINUS; *p++ = L8_MINUS; }
    else p = l8_decimal(p, humidity >= 9850u ? 99u : (uint16_t)(humidity + 50u) / 100u);
    *p++ = G('%'); *p = EPD_L8_END;
}

/* "-- --.-- --:--" */
static const MCU_CODE uint8_t l8_no_clock[14] = {
    G('-'), G('-'), G(' '), G('-'), G('-'), G('.'), G('-'), G('-'), G(' '), G('-'), G('-'), G(':'), G('-'), G('-')
};

/* Local seconds since 2000-01-01 as weekday dd.mm hh:mm (every fourth year
 * a leap year). */
static void l8_clock(uint8_t MCU_XDATA *p, uint32_t t)
{
    uint16_t days, n;
    uint32_t rest;
    uint8_t y = 0, m = 0;
    if (!t) {
        for (m = 0; m < sizeof(l8_no_clock); m++) p[m] = l8_no_clock[m];
        p[m] = EPD_L8_END;
        return;
    }
    days = (uint16_t)(t / 86400UL);
    rest = t % 86400UL;
    *p++ = l8_weekday[days % 7u][0];
    *p++ = l8_weekday[days % 7u][1];
    *p++ = G(' ');
    for (;; y++) {
        n = (y & 3u) ? 365u : 366u;
        if (days < n) break;
        days -= n;
    }
    for (;; m++) {
        n = (uint16_t)(l8_month[m] + (m == 1u && !(y & 3u)));
        if (days < n) break;
        days -= n;
    }
    p = l8_two(p, (uint8_t)(days + 1u));
    *p++ = L8_DOT;
    p = l8_two(p, (uint8_t)(m + 1u));
    *p++ = G(' ');
    p = l8_two(p, (uint8_t)(rest / 3600UL));
    *p++ = G(':');
    p = l8_two(p, (uint8_t)((uint16_t)(rest % 3600UL) / 60u));
    *p = EPD_L8_END;
}

void epd_format(epd_view_t MCU_XDATA *view, int16_t centi, uint16_t humidity, uint16_t millivolts) JOIN_FAR
{
    uint8_t MCU_XDATA *p = view->volt;
    uint16_t a;
    int16_t dbm;
    l8_prepare();
#if defined(CC2530_MAC_ATTEMPT)
    if (mac_attempt_raw.frame.length) {
        epd_l8.rssi = mac_attempt_raw.frame.rssi_raw;
        epd_l8.heard = 1;
    }
#endif
    view->fill = millivolts <= 2000u ? 0 : millivolts >= 3000u ? 36u :
        (uint8_t)((uint16_t)(millivolts - 2000u) * 36u / 1000u);
    view->weather = epd_l8.weather;
    if (!millivolts) {
        p[0] = p[2] = p[3] = L8_MINUS;
    } else {
        a = millivolts >= 9985u ? 999u : (uint16_t)(millivolts + 5u) / 10u;
        p[0] = (uint8_t)(L8_DIGIT + a / 100u);
        l8_two(p + 2, (uint8_t)(a % 100u));
    }
    p[1] = L8_DOT; p[4] = G(' '); p[5] = G('V'); p[6] = EPD_L8_END;
    p = view->rssi;
    *p++ = G('R'); *p++ = G('S'); *p++ = G('S'); *p++ = G('I'); *p++ = G(' ');
    if (!epd_l8.heard) {
        *p++ = L8_MINUS; *p++ = L8_MINUS;
    } else {
        dbm = (int16_t)(int8_t)epd_l8.rssi - 73;
        if (dbm < 0) { *p++ = L8_MINUS; dbm = (int16_t)-dbm; }
        p = l8_decimal(p, (uint16_t)dbm);
    }
    *p = EPD_L8_END;
    l8_clock(view->clock, epd_l8.time);
    l8_temperature(view->indoor, centi);
    l8_humidity(view->humidity, humidity);
    l8_temperature(view->outdoor, epd_l8.outdoor);
    l8_humidity(view->outdoor_humidity, epd_l8.outdoor_humidity);
}

/* --- cluster 0xFC10 --- */
static uint8_t MCU_XDATA *l8_field(uint16_t id, uint8_t *capacity)
{
    uint8_t k = (uint8_t)(id - 0x20u);
    if (id == 0x30u) { *capacity = EPD_L8_MESSAGE; return epd_l8.message; }
    *capacity = k & 1u ? EPD_L8_VALUE : EPD_L8_LABEL;
    return k & 1u ? epd_l8.value[k >> 1] : epd_l8.label[k >> 1];
}

uint8_t epd_l8_type(uint16_t id) JOIN_FAR
{
    if (!id) return 0x23u;
    if (id == 0x01u || id == 0x11u || id == 0xfffdu) return 0x21u;
    if (id == 0x10u) return 0x29u;
    if (id == 0x12u) return 0x30u;
    if ((uint16_t)(id - 0x20u) < 2u * EPD_L8_SLOTS || id == 0x30u) return 0x42u;
    return 0;
}

/* Code point of a glyph code. */
static uint16_t l8_point(uint8_t c)
{
    if (c < CYRILLIC) return (uint16_t)(c + 0x20u);
    if (c < YO) return (uint16_t)(c - CYRILLIC + 0x410u);
    return c == YO ? 0x401u : c == YO_SMALL ? 0x451u : 0xb0u;
}

uint8_t epd_l8_get(uint16_t id, uint8_t MCU_XDATA *out, uint8_t apply) JOIN_FAR
{
    uint8_t MCU_XDATA *text;
    uint8_t capacity, i, n = 0;
    uint16_t v;
    uint32_t t;
    l8_prepare();
    if (epd_l8_type(id) == 0x42u) {
        text = l8_field(id, &capacity);
        for (i = 0; i < capacity && text[i] != EPD_L8_END; i++) {
            v = l8_point(text[i]);
            if (v < 0x80u) {
                if (n + 1u > L8_TEXT_MAX) break;
                if (apply) out[1u + n] = (uint8_t)v;
                n++;
            } else {
                if (n + 2u > L8_TEXT_MAX) break;
                if (apply) { out[1u + n] = (uint8_t)(0xc0u | v >> 6); out[2u + n] = (uint8_t)(0x80u | (v & 0x3fu)); }
                n += 2u;
            }
        }
        if (apply) out[0] = n;
        return (uint8_t)(n + 1u);
    }
    if (!id) {
        t = epd_l8.time;
        if (apply) { out[0] = (uint8_t)t; out[1] = (uint8_t)(t >> 8); out[2] = (uint8_t)(t >> 16); out[3] = (uint8_t)(t >> 24); }
        return 4;
    }
    if (id == 0x12u) {
        if (apply) out[0] = epd_l8.weather;
        return 1;
    }
    v = id == 0x01u ? epd_l8.interval : id == 0x10u ? (uint16_t)epd_l8.outdoor :
        id == 0x11u ? epd_l8.outdoor_humidity : 1u;
    if (apply) { out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8); }
    return 2;
}

/* Glyph code of the UTF-8 sequence at in[*at] (< n), advancing *at; 0xFF
 * for a stray continuation octet. */
static uint8_t l8_decode(const uint8_t MCU_XDATA *in, uint8_t *at, uint8_t n)
{
    uint8_t b = in[(*at)++], extra;
    uint16_t cp;
    if (b < 0x80u) return b >= 0x20u && b < 0x7fu ? G(b) : EPD_L8_QMARK;
    if (b < 0xc0u) return EPD_L8_END;
    extra = b < 0xe0u ? 1u : b < 0xf0u ? 2u : 3u;
    cp = b & 0x1fu;
    for (b = extra; b; b--) {
        if (*at >= n || (in[*at] & 0xc0u) != 0x80u) return EPD_L8_QMARK;
        cp = (uint16_t)(cp << 6 | (in[(*at)++] & 0x3fu));
    }
    if (extra != 1u) return EPD_L8_QMARK;
    if (cp >= 0x410u && cp < 0x450u) return (uint8_t)(cp - 0x410u + CYRILLIC);
    return cp == 0x401u ? YO : cp == 0x451u ? YO_SMALL : cp == 0xb0u ? EPD_L8_DEGREE : EPD_L8_QMARK;
}

uint8_t epd_l8_set(uint16_t id, const uint8_t MCU_XDATA *in, uint8_t apply, uint32_t now) JOIN_FAR
{
    uint8_t MCU_XDATA *text;
    uint8_t capacity, n, at, i, c, changed = 0;
    uint16_t v = (uint16_t)(in[0] | (uint16_t)in[1] << 8);
    uint32_t t;
    l8_prepare();
    if (id == 0xfffdu) return 0x88u;
    if ((id == 0x01u && (v < 60u || v > 28800u)) || (id == 0x11u && v > 10000u && v != 0xffffu) ||
        (id == 0x12u && in[0] > 9u)) return 0x87u;
    if (!apply) return 0;
    epd_l8.written = now;
    if (epd_l8_type(id) == 0x42u) {
        text = l8_field(id, &capacity);
        n = in[0] == 0xffu ? 0 : in[0];
        in++;
        for (i = 0, at = 0; i < capacity; i++) {
            c = EPD_L8_END;
            while (at < n && (c = l8_decode(in, &at, n)) == EPD_L8_END) {}
            if (text[i] != c) { text[i] = c; changed = 1; }
        }
    } else if (!id) {
        t = (uint32_t)v | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
        changed = !epd_l8.time;
        epd_l8.time = t;
        epd_l8.tick = now;
    } else if (id == 0x01u) {
        epd_l8.interval = v;
    } else if (id == 0x10u) {
        changed = epd_l8.outdoor != (int16_t)v;
        epd_l8.outdoor = (int16_t)v;
    } else if (id == 0x11u) {
        changed = epd_l8.outdoor_humidity != v;
        epd_l8.outdoor_humidity = v;
    } else {
        changed = epd_l8.weather != in[0];
        epd_l8.weather = in[0];
    }
    if (changed) epd_l8.dirty = 1;
    return 0;
}
