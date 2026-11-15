#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Generate the D3 e-paper layout tables and host-test goldens.

  d3_art.py --fonts U8G2/tools/font/bdf [--check] [--pbm OUT.pbm]

The reference frame below is the D3 design (296x128 landscape, X right, Y down).
It needs Pillow and three BDF fonts of the u8g2 font collection
(github.com/olikraus/u8g2, tools/font/bdf):
  spleen-8x16.bdf          Spleen, (c) 2018-2022 Frederic Cambus, BSD-2-Clause
  t0-13b-uni.bdf           t0, (c) 2012-2025 Uwe Waldmann, MIT-style licence
  u8g2_squeezed_bold_7.bdf public domain
Their notices are reproduced in the generated header. The firmware draws the
static layer from sprites extracted here plus three rules by code, and every value
(7-segment digits, battery fill, voltage text) at run time. The goldens are CRC-32
values of whole gate-row planes of this reference, so the C renderer must match it
bit for bit. --check compares the generated files with the tracked ones.
"""
import argparse
import sys
import zlib
from pathlib import Path

from PIL import Image, ImageDraw

W, H = 296, 128
REPO = Path(__file__).resolve().parents[2]
ART = REPO / "include" / "epd_d3_art.h"
GOLDEN = REPO / "tests" / "epd_d3_golden.h"
TITLE = "LIVING ROOM"

SEG = {'0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc', '5': 'afgcd',
       '6': 'afgedc', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg', '-': 'g', ' ': ''}
TEMP = dict(y=32, w=38, ht=66, t=8, slots=(26, 70, 126), dot=114)
HUM = dict(y=34, w=18, ht=34, t=4, slots=(236, 258))
BAT = (262, 5)
VOLT_PREFIX, VOLT_X, VOLT_BASE = "BATTERY ", 6, 121
RULES = ((23,), (107,))          # full-width horizontal rules: y
VRULE = (210, 30, 100)           # vertical rule: x, y0, y1
F4_CHARS = "0123456789.- V"


def parse_bdf(path):
    g, cur = {}, None
    with open(path, errors='replace') as f:
        it = iter(f)
        for line in it:
            if line.startswith('ENCODING'):
                cur = {'enc': int(line.split()[1])}
            elif line.startswith('DWIDTH') and cur is not None:
                cur['dw'] = int(line.split()[1])
            elif line.startswith('BBX') and cur is not None:
                cur['bbx'] = list(map(int, line.split()[1:]))
            elif line.startswith('BITMAP') and cur is not None:
                rows = []
                for l2 in it:
                    if l2.startswith('ENDCHAR'):
                        break
                    rows.append(int(l2.strip() or '0', 16))
                cur['rows'] = rows
                g[cur['enc']] = cur
                cur = None
    return g


def render_text(g, text, px, x, y_base):
    for ch in text:
        c = g[ord(ch)]
        w, h, ox, oy = c['bbx']
        nb = (w + 7) // 8 * 8
        for r, v in enumerate(c['rows']):
            Y = y_base - oy - h + r
            for i in range(w):
                if v >> (nb - 1 - i) & 1:
                    X = x + ox + i
                    if 0 <= X < W and 0 <= Y < H:
                        px[X, Y] = 0
        x += c['dw']
    return x


def segments(x, y, w, ht, t, ch, g=1):
    h = t // 2
    L, R, T, M, B = x + h, x + w - h, y + h, y + ht // 2, y + ht - h
    s, out = SEG[ch], []
    if 'a' in s: out.append(('h', L + g, R - g, T, h))
    if 'g' in s: out.append(('h', L + g, R - g, M, h))
    if 'd' in s: out.append(('h', L + g, R - g, B, h))
    if 'f' in s: out.append(('v', L, T + g, M - g, h))
    if 'b' in s: out.append(('v', R, T + g, M - g, h))
    if 'e' in s: out.append(('v', L, M + g, B - g, h))
    if 'c' in s: out.append(('v', R, M + g, B - g, h))
    return out


def seg_span(seg, X):
    if seg[0] == 'h':
        _, x0, x1, yc, h = seg
        if x0 <= X <= x1:
            k = min(h, X - x0, x1 - X)
            return yc - k, yc + k
    else:
        _, xc, y0, y1, h = seg
        dx = abs(X - xc)
        if dx <= h:
            return y0 + dx, y1 - dx
    return None


def temp_chars(centi):
    if centi == -0x8000:
        return '---', True
    v = (abs(centi) + 5) // 10 * (1 if centi >= 0 else -1)
    if -99 <= v <= 999:
        a = abs(v)
        lead = '-' if v < 0 else (str(a // 100) if a >= 100 else ' ')
        return lead + str(a // 10 % 10) + str(a % 10), True
    if -999 <= v < -99:
        return '-' + str(-v // 100) + str(-v // 10 % 10), False
    return '---', False


def hum_chars(hum):
    if hum == 0xFFFF:
        return '--'
    p = min(99, (hum + 50) // 100)
    return str(p // 10) + str(p % 10)


def volt_text(mv):
    if mv == 0:
        return "-.-- V"
    c = min(999, (mv + 5) // 10)
    return f"{c // 100}.{c // 10 % 10}{c % 10} V"


def bat_fill(mv):
    return 0 if mv <= 2000 else 18 if mv >= 3000 else (mv - 2000) * 18 // 1000


class Fonts:
    def __init__(self, d):
        d = Path(d)
        self.title = parse_bdf(d / 'spleen-8x16.bdf')
        self.sym = parse_bdf(d / 't0-13b-uni.bdf')
        self.small = parse_bdf(d / 'u8g2_squeezed_bold_7.bdf')


def static_layer(f, rules=True):
    im = Image.new('1', (W, H), 1)
    d = ImageDraw.Draw(im)
    px = im.load()
    render_text(f.title, TITLE, px, 6, 17)
    x, y0 = 236, 4
    for i in range(4):
        hh = 3 + 3 * i
        d.rectangle((x + i * 5, y0 + 12 - hh, x + i * 5 + 3, y0 + 12), fill=0 if i < 3 else None, outline=0)
    bx, by = BAT
    d.rectangle((bx, by, bx + 21, by + 10), outline=0)
    d.rectangle((bx + 22, by + 3, bx + 23, by + 7), fill=0)
    if rules:
        for (y,) in RULES:
            d.line((0, y, W - 1, y), 0)
        d.line((VRULE[0], VRULE[1], VRULE[0], VRULE[2]), 0)
    d.rounded_rectangle((9, 46, 15, 66), 3, outline=0)
    d.ellipse((6, 62, 18, 74), fill=0)
    d.rectangle((11, 54, 13, 64), fill=0)
    d.polygon([(223, 42), (229, 53), (217, 53)], fill=0)
    d.ellipse((216, 48, 230, 62), fill=0)
    d.ellipse((172, 32, 177, 37), outline=0)
    render_text(f.sym, "C", px, 179, 43)
    render_text(f.sym, "%", px, 282, 43)
    render_text(f.small, "HUMIDITY", px, 226, 86)
    render_text(f.small, VOLT_PREFIX, px, VOLT_X, VOLT_BASE)
    return im


def frame(f, centi, hum, mv):
    im = static_layer(f)
    px = im.load()
    segs, rects = [], []
    chars, dot = temp_chars(centi)
    for x, ch in zip(TEMP['slots'], chars):
        segs += segments(x, TEMP['y'], TEMP['w'], TEMP['ht'], TEMP['t'], ch)
    if dot:
        x, t, y, ht = TEMP['dot'], TEMP['t'], TEMP['y'], TEMP['ht']
        rects.append((x - 1, x + t - 2, y + ht - t, y + ht - 1))
    for x, ch in zip(HUM['slots'], hum_chars(hum)):
        segs += segments(x, HUM['y'], HUM['w'], HUM['ht'], HUM['t'], ch)
    n = bat_fill(mv)
    if n:
        bx, by = BAT
        rects.append((bx + 2, bx + 1 + n, by + 2, by + 8))
    for X in range(W):
        for s in segs:
            sp = seg_span(s, X)
            if sp:
                for Y in range(sp[0], sp[1] + 1):
                    px[X, Y] = 0
        for a, b, c, e in rects:
            if a <= X <= b:
                for Y in range(c, e + 1):
                    px[X, Y] = 0
    x = VOLT_X + sum(f.small[ord(c)]['dw'] for c in VOLT_PREFIX)
    render_text(f.small, volt_text(mv), px, x, VOLT_BASE)
    return im


def plane(im):
    """Gate-row plane: gate g is X = 295 - g; 16 octets of Y, MSB the top pixel, 1 white."""
    px = im.load()
    out = bytearray()
    for g in range(W):
        X = W - 1 - g
        for b in range(16):
            out.append(sum((1 if px[X, b * 8 + i] else 0) << (7 - i) for i in range(8)))
    return bytes(out)


def sprites(f):
    """Static layer without rules, cut into horizontal bands (row gaps of 4 or more)
    and then into column runs (gaps merged when cheaper than a table entry):
    (x0, w, y0, h, octets), sorted by x0."""
    px = static_layer(f, rules=False).load()
    ink = lambda X, Y: not px[X, Y]
    rows = [Y for Y in range(H) if any(ink(X, Y) for X in range(W))]
    bands = []
    for Y in rows:
        if bands and Y - bands[-1][1] < 5:
            bands[-1][1] = Y
        else:
            bands.append([Y, Y])
    out = []
    for b0, b1 in bands:
        cols = [X for X in range(W) if any(ink(X, Y) for Y in range(b0, b1 + 1))]
        runs = []
        for X in cols:
            if runs and (X - runs[-1][1] - 1) * ((b1 - b0 + 8) // 8) < 7:
                runs[-1][1] = X
            else:
                runs.append([X, X])
        for x0, x1 in runs:
            ys = [Y for X in range(x0, x1 + 1) for Y in range(b0, b1 + 1) if ink(X, Y)]
            y0, h = min(ys), max(ys) - min(ys) + 1
            data = []
            for X in range(x0, x1 + 1):
                v = sum(1 << (Y - y0) for Y in range(y0, y0 + h) if ink(X, Y))
                data += [(v >> (8 * k)) & 0xff for k in range((h + 7) // 8)]
            out.append((x0, x1 - x0 + 1, y0, h, data))
    return sorted(out)


def f4_table(f):
    rows = []
    for ch in F4_CHARS:
        c = f.small[ord(ch)]
        w, h, ox, oy = c['bbx']
        assert ox == 0 and oy >= 0 and oy + h <= 7 and w <= 15, (ch, c['bbx'])
        nb = (w + 7) // 8 * 8
        cols = []
        for i in range(w):
            v = 0
            for r, bits in enumerate(c['rows']):
                if bits >> (nb - 1 - i) & 1:
                    v |= 1 << (7 - oy - h + r)   # bit 0: 7 rows above the baseline
            cols.append(v)
        rows.append((ch, c['dw'], cols))
    return rows


NOTICE = """/* Generated by tools/epd/d3_art.py; do not edit.
 * Glyph pixels come from fonts of the u8g2 collection:
 * - title "%s": Spleen 8x16. Copyright (c) 2018-2022, Frederic Cambus.
 *   BSD-2-Clause (see docs/EPAPER.md for the full notice).
 * - "C" and "%%": t0-13b. Copyright (C) 2012-2025 Uwe Waldmann. MIT-style
 *   licence (see docs/EPAPER.md for the full notice).
 * - small text: u8g2 squeezed bold 7, public domain. */
"""


def hexrows(data):
    return "".join("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n"
                   for i in range(0, len(data), 16))


def emit_art(f):
    sp = sprites(f)
    ref = static_layer(f).load()
    im = Image.new('1', (W, H), 1)
    d = ImageDraw.Draw(im)
    for (y,) in RULES:
        d.line((0, y, W - 1, y), 0)
    d.line((VRULE[0], VRULE[1], VRULE[0], VRULE[2]), 0)
    px = im.load()
    for x0, w, y0, h, data in sp:
        n = (h + 7) // 8
        for i in range(w):
            for Y in range(h):
                if data[i * n + Y // 8] >> (Y % 8) & 1:
                    px[x0 + i, y0 + Y] = 0
    assert all(px[X, Y] == ref[X, Y] for X in range(W) for Y in range(H)), "sprites"
    blob = [b for s in sp for b in s[4]]
    f4 = f4_table(f)
    gl = [b for _, _, cols in f4 for b in cols]
    assert len(blob) < 65536 and len(gl) < 256
    L = [NOTICE % TITLE, "#ifndef EPD_D3_ART_H\n#define EPD_D3_ART_H\n\n"]
    L.append("/* Static sprites: first column, offset in d3_sprite_bits, width, top row,\n"
             " * height. Each column is ceil(height/8) octets, bit 0 the top row. */\n")
    L.append(f"#define EPD_D3_SPRITES {len(sp)}u\n")
    L.append("typedef struct { uint16_t x0, at; uint8_t width, top, height; } epd_d3_sprite_t;\n")
    L.append("static const MCU_CODE epd_d3_sprite_t d3_sprite[EPD_D3_SPRITES] = {\n")
    at = 0
    for x0, w, y0, h, data in sp:
        L.append(f"    {{ {x0}u, {at}u, {w}u, {y0}u, {h}u }},\n")
        at += len(data)
    L.append("};\n")
    L.append(f"static const MCU_CODE uint8_t d3_sprite_bits[{len(blob)}] = {{\n{hexrows(blob)}}};\n\n")
    L.append(f"/* Small font, glyphs \"{F4_CHARS}\": advance, width (columns), offset in\n"
             " * d3_f4_bits; a column octet's bit 0 is 7 rows above the baseline. */\n")
    L.append(f"#define EPD_D3_F4_GLYPHS {len(f4)}u\n")
    L.append("static const MCU_CODE uint8_t d3_f4[EPD_D3_F4_GLYPHS][3] = {\n")
    at = 0
    for ch, dw, cols in f4:
        L.append(f"    {{ {dw}u, {len(cols)}u, {at}u }}, /* '{ch}' */\n")
        at += len(cols)
    L.append("};\n")
    L.append(f"static const MCU_CODE uint8_t d3_f4_bits[{len(gl)}] = {{\n{hexrows(gl)}}};\n")
    vx = VOLT_X + sum(f.small[ord(c)]['dw'] for c in VOLT_PREFIX)
    L.append(f"#define EPD_D3_VOLT_X {vx}u\n#define EPD_D3_VOLT_TOP {VOLT_BASE - 7}u\n\n#endif\n")
    return "".join(L), len(blob) + 7 * len(sp) + len(gl) + 3 * len(f4)


# Value sets: centi-degrees, 0.01 %RH, mV.
CASES = [(2347, 4500, 2970), (2350, 4549, 3000), (-512, 9999, 2005), (12345, 0, 3600),
         (-0x8000, 0xFFFF, 0), (-1234, 50, 1999), (995, 9950, 2500), (-4, 100, 2799),
         (32767, 4500, 2970), (-10000, 4500, 2970), (-9949, 4500, 2970), (5, 4450, 9999)]


def emit_golden(f):
    L = ["/* Generated by tools/epd/d3_art.py; do not edit. CRC-32 (zlib) of the\n"
         " * 296x16-octet gate-row plane of the reference D3 frame. */\n",
         "static const struct { int16_t centi; uint16_t humidity, millivolts; uint32_t crc; } d3_golden[] = {\n"]
    for c, h, m in CASES:
        L.append(f"    {{ {c}, {h}u, {m}u, 0x{zlib.crc32(plane(frame(f, c, h, m))):08x}UL }},\n")
    L.append("};\n")
    return "".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts", required=True, type=Path)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--pbm", type=Path, help="also write the first case as a PBM")
    a = ap.parse_args()
    f = Fonts(a.fonts)
    art, size = emit_art(f)
    golden = emit_golden(f)
    if a.pbm:
        frame(f, *CASES[0]).save(a.pbm)
    if a.check:
        bad = [p for p, s in ((ART, art), (GOLDEN, golden)) if not p.exists() or p.read_text() != s]
        if bad:
            sys.exit(f"stale: {', '.join(map(str, bad))}")
        return
    ART.write_text(art)
    GOLDEN.write_text(golden)
    print(f"{ART.name}: ~{size} table octets; {GOLDEN.name}: {len(CASES)} cases")


if __name__ == "__main__":
    main()
