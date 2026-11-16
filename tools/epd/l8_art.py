#!/usr/bin/env python3
"""L8 layout: 800x480 dashboard for the 4.26" SSD1677 panel.

The reference renderer works like the firmware: every screen row is built on
its own into 100 octets (MSB left, bit 1 white) from

- RULES: filled rectangles;
- sprites: row-major 1-bit bitmaps (static icons and labels) blitted at any x;
- text elements: 8x16 glyphs (t0-16b-uni, Latin, Cyrillic and the degree
  sign) at scale 1 or 2;
- number elements: 7-segment digits whose bars are hexagons (the D3 shape,
  evaluated per row), the decimal point, the colon, then glyphs (units) at
  scale 2 with their top on the digit top;
- the weather icon: a 48x48 bitmap at scale 2;
- the battery gauge: a filled rectangle 0..36 columns wide.

Element strings are code strings: 0..GLYPHS-1 glyph indices, 0xE0..0xE9 the
segment digits, 0xEA minus, 0xEB a digit-wide blank, 0xEC the point, 0xED the
colon. The firmware converts written UTF-8 text to codes once, the renderer
only reads codes.
"""
import argparse
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from d3_art import parse_bdf  # noqa: E402

W, H = 800, 480
OCTETS = W // 8
ASCENT = 12

CODEPOINTS = list(range(0x20, 0x7f)) + list(range(0x410, 0x450)) + [0x401, 0x451, 0xb0]
INDEX = {cp: i for i, cp in enumerate(CODEPOINTS)}
QMARK = INDEX[ord('?')]
SEG_DIGIT, SEG_MINUS, SEG_BLANK, SEG_DOT, SEG_COLON = 0xe0, 0xea, 0xeb, 0xec, 0xed
SEG = {'0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc', '5': 'afgcd',
       '6': 'afgedc', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg'}
MASK = {k: sum(1 << 'abcdefg'.index(c) for c in v) for k, v in SEG.items()}
# Sizes: digit width w, height ht (w + 1 and ht + 1 pixels), half stroke h, gap.
SIZE_L = (52, 100, 7, 10)
SIZE_M = (30, 56, 4, 6)
WEEKDAYS = ('Сб', 'Вс', 'Пн', 'Вт', 'Ср', 'Чт', 'Пт')  # 2000-01-01 was a Saturday
WEATHER = ('none', 'sunny', 'clear-night', 'partlycloudy', 'cloudy', 'rainy', 'snowy', 'fog',
           'lightning', 'windy')


# --- codes ------------------------------------------------------------------
def glyph_codes(s):
    return [INDEX.get(ord(c), QMARK) for c in s]


def value_codes(s):
    """HA value text: digits, '-', '.', ',' and ':' become segment codes."""
    out = []
    for c in s:
        if c.isdigit():
            out.append(SEG_DIGIT + int(c))
        elif c == '-':
            out.append(SEG_MINUS)
        elif c in '.,':
            out.append(SEG_DOT)
        elif c == ':':
            out.append(SEG_COLON)
        else:
            out.append(INDEX.get(ord(c), QMARK))
    return out


def temp_codes(centi):
    """Tenths for -9.9 .. 99.9, whole degrees for -10 .. -99, otherwise --.-."""
    if centi is None or centi == -0x8000:
        return [SEG_MINUS, SEG_MINUS, SEG_DOT, SEG_MINUS]
    neg = centi < 0
    a = (abs(centi) + 5) // 10
    if not a:
        neg = False
    if a <= (99 if neg else 999):
        s = ('-' if neg else '') + f'{a // 10}.{a % 10}'
    elif neg and a <= 999:
        s = f'-{a // 10}'
    else:
        return [SEG_MINUS, SEG_MINUS, SEG_MINUS]
    return value_codes(s)


def hum_codes(hum):
    if hum is None or hum == 0xffff:
        return [SEG_MINUS, SEG_MINUS]
    return value_codes(str(min(99, (hum + 50) // 100)))


def volt_codes(mv):
    if not mv:
        return glyph_codes('-.-- V')
    a = min(999, (mv + 5) // 10)
    return glyph_codes(f'{a // 100}.{a % 100:02d} V')


def bat_fill(mv):
    return 0 if mv <= 2000 else 36 if mv >= 3000 else (mv - 2000) * 36 // 1000


def clock_codes(seconds):
    """Local seconds since 2000-01-01 00:00 as 'Пн 21.04 14:32'."""
    if not seconds:
        return glyph_codes('-- --.-- --:--')
    days, rest = divmod(seconds, 86400)
    wd = WEEKDAYS[days % 7]
    y = 2000
    while True:
        n = 366 if y % 4 == 0 else 365
        if days < n:
            break
        days -= n
        y += 1
    months = (31, 29 if y % 4 == 0 else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
    m = 0
    while days >= months[m]:
        days -= months[m]
        m += 1
    return glyph_codes(f'{wd} {days + 1:02d}.{m + 1:02d} {rest // 3600:02d}:{rest % 3600 // 60:02d}')


def rssi_codes(dbm):
    return glyph_codes('RSSI --' if dbm is None else f'RSSI {dbm}')


# --- art ----------------------------------------------------------------------
def font_table(font):
    table = []
    for cp in CODEPOINTS:
        g = font.get(cp) or font[ord('?')]
        w, h, ox, oy = g['bbx']
        nb = (w + 7) // 8 * 8
        rows = [0] * 16
        for r, v in enumerate(g['rows']):
            Y = ASCENT - oy - h + r
            for i in range(w):
                if v >> (nb - 1 - i) & 1 and 0 <= Y < 16 and 0 <= ox + i < 8:
                    rows[Y] |= 0x80 >> (ox + i)
        table.append(rows)
    return table


def bitmap(w, h, draw):
    """Draw with PIL (ink 1 on 0) into a w x h image, return rows of ints."""
    im = Image.new('1', (w, h), 0)
    draw(ImageDraw.Draw(im), w, h)
    px = im.load()
    return [sum(1 << (w - 1 - x) for x in range(w) if px[x, y]) for y in range(h)]


def battery_outline(d, w, h):
    d.rectangle([0, 0, 43, 21], outline=1, width=2)
    d.rectangle([44, 6, 47, 15], fill=1)


def zigbee_icon(d, w, h):
    c, r = 16, 15
    d.ellipse([c - r, c - r, c + r, c + r], fill=1)
    d.polygon([(c - 8, c - 8), (c + 8, c - 8), (c + 8, c - 5), (c - 3, c + 5), (c + 8, c + 5),
               (c + 8, c + 8), (c - 8, c + 8), (c - 8, c + 5), (c + 3, c - 5), (c - 8, c - 5)], fill=0)


def thermometer(s):
    def draw(d, w, h):
        tw, br = round(22 * s), round(19 * s)
        cx, bulb = w // 2, h - 1 - br
        d.rounded_rectangle([cx - tw // 2, 0, cx + tw // 2, bulb], radius=tw // 2, outline=1,
                            width=max(2, round(4 * s)))
        d.ellipse([cx - br, bulb - br, cx + br, bulb + br], fill=1)
        ir = round(br * 0.6)
        d.ellipse([cx - ir - 3, bulb - ir - 3, cx + ir + 3, bulb + ir + 3], fill=0)
        d.ellipse([cx - ir, bulb - ir, cx + ir, bulb + ir], fill=1)
        d.rectangle([cx - max(1, round(3 * s)), round(18 * s), cx + max(1, round(3 * s)), bulb], fill=1)
    return draw


def drop(s):
    def draw(d, w, h):
        R = round(19 * s)

        def shape(r, tip):
            cx, cy = w // 2, h - 1 - R
            a = math.degrees(math.acos(r / (cy - tip)))
            pts = [(cx + r * math.cos(math.radians(k)), cy + r * math.sin(math.radians(k)))
                   for k in range(round(270 + a), round(630 - a) + 1)]
            return [(cx, tip)] + pts
        d.polygon(shape(R, 0), fill=1)
        d.polygon(shape(R - max(3, round(5 * s)), max(7, round(13 * s))), fill=0)
    return draw


def label(font, s):
    def draw(d, w, h):
        rows = font_table_cache[1]
        for k, code in enumerate(glyph_codes(s)):
            for r in range(16):
                for i in range(8):
                    if rows[code][r] & 0x80 >> i:
                        d.point((k * 8 + i, r), fill=1)
    return draw


# Weather icons, 48x48 drawn at scale 2.
def sun(d, cx=24, cy=24, r=9, ray=(13, 20), width=3):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=1, width=width)
    for k in range(8):
        a = math.radians(k * 45)
        d.line([(cx + ray[0] * math.cos(a), cy + ray[0] * math.sin(a)),
                (cx + ray[1] * math.cos(a), cy + ray[1] * math.sin(a))], fill=1, width=width)


def cloud(d, dx=0, dy=0, fill_inside=True):
    parts = [(14, 24, 8), (25, 18, 11), (36, 25, 8)]
    for cx, cy, r in parts:
        d.ellipse([dx + cx - r, dy + cy - r, dx + cx + r, dy + cy + r], fill=1)
    d.rectangle([dx + 6, dy + 24, dx + 44, dy + 33], fill=1)
    for cx, cy, r in parts:
        r -= 3
        d.ellipse([dx + cx - r, dy + cy - r, dx + cx + r, dy + cy + r], fill=0)
    d.rectangle([dx + 9, dy + 24, dx + 41, dy + 30], fill=0)


def icon_sunny(d, w, h):
    sun(d)


def icon_night(d, w, h):
    d.ellipse([8, 8, 40, 40], fill=1)
    d.ellipse([18, 3, 46, 31], fill=0)
    d.ellipse([11, 11, 37, 37], outline=0, width=0)


def icon_partly(d, w, h):
    sun(d, 17, 15, 7, (10, 14), 2)
    d.rectangle([4, 20, 47, 47], fill=0)
    d.ellipse([10, 12, 40, 42], fill=0)
    cloud(d, 0, 8)


def icon_cloudy(d, w, h):
    cloud(d, 0, 2)


def icon_rainy(d, w, h):
    cloud(d, 0, -6)
    for x in (14, 24, 34):
        d.line([(x + 3, 32), (x - 3, 44)], fill=1, width=3)


def icon_snowy(d, w, h):
    cloud(d, 0, -6)
    for x, y in ((14, 36), (24, 42), (34, 36)):
        for a in (0, 60, 120):
            r = math.radians(a)
            d.line([(x - 4 * math.cos(r), y - 4 * math.sin(r)), (x + 4 * math.cos(r), y + 4 * math.sin(r))],
                   fill=1, width=2)


def icon_fog(d, w, h):
    cloud(d, 0, -8)
    for y, x0, x1 in ((32, 6, 42), (38, 2, 38), (44, 10, 46)):
        d.rectangle([x0, y, x1, y + 2], fill=1)


def icon_lightning(d, w, h):
    cloud(d, 0, -6)
    d.polygon([(26, 26), (16, 38), (23, 38), (19, 47), (31, 33), (24, 33), (28, 26)], fill=1)


def icon_windy(d, w, h):
    for y, x1, r in ((14, 34, 6), (24, 40, 0), (34, 30, 6)):
        d.rectangle([4, y, x1, y + 2], fill=1)
        if r:
            d.arc([x1 - r, y - 2 * r + 2, x1 + r, y + 2], -90, 90, fill=1, width=3)


def icon_none(d, w, h):
    pass


ICONS = (icon_none, icon_sunny, icon_night, icon_partly, icon_cloudy, icon_rainy, icon_snowy,
         icon_fog, icon_lightning, icon_windy)

font_table_cache = [None, None]

# --- layout -------------------------------------------------------------------
RULES = (
    (0, 0, 799, 1), (0, 478, 799, 479), (0, 0, 1, 479), (798, 0, 799, 479),
    (0, 52, 799, 53), (0, 284, 799, 285), (0, 400, 799, 401),
    (188, 10, 189, 42), (540, 10, 541, 42),
    (399, 62, 400, 276),
    (199, 292, 200, 392), (399, 292, 400, 392), (599, 292, 600, 392),
)
BAT = (16, 15)            # outline top-left; gauge x + 4 .., y + 4 .. y + 17
WEATHER_AT = (420, 86)
LEFT, RIGHT = 16, 416


def sprite_defs():
    return [
        (BAT[0], BAT[1], 48, 22, battery_outline),
        (204, 10, 32, 32, zigbee_icon),
        (LEFT, 86, 40, 101, thermometer(1.0)),
        (24, 214, 32, 44, drop(0.75)),
        (444, 214, 32, 44, drop(0.75)),
        (LEFT, 62, 32, 16, label(None, 'ДОМА')),
        (RIGHT, 62, 40, 16, label(None, 'УЛИЦА')),
    ]


def elements(v):
    """(kind, x, y, clip, codes); kind 1/2 text scale, 'L'/'M' numbers."""
    deg_c = glyph_codes('°C')
    pct = glyph_codes('%')
    e = [
        (2, 72, 10, 184, volt_codes(v['mv'])),
        (2, 248, 10, 536, rssi_codes(v.get('rssi'))),
        (2, 560, 10, 784, clock_codes(v.get('time'))),
        ('L', 72, 86, 396, temp_codes(v['t_in']) + deg_c),
        ('M', 72, 206, 396, hum_codes(v['h_in']) + pct),
        ('L', 528, 86, 796, temp_codes(v.get('t_out')) + deg_c),
        ('M', 528, 206, 796, hum_codes(v.get('h_out')) + pct),
    ]
    for k, (lab, val) in enumerate(v.get('slots', [])):
        x0 = k * 200
        e.append((1, x0 + 16, 296, x0 + 192, glyph_codes(lab)))
        e.append(('M', x0 + 16, 324, x0 + 192, value_codes(val)))
    e.append((2, 16, 424, 784, glyph_codes(v.get('message', ''))))
    return e


# --- row renderer (mirrored by src/epd_l8.c) -------------------------------------
class Row:
    def __init__(self):
        self.o = bytearray(b'\xff' * OCTETS)

    def span(self, x0, x1):
        for x in range(max(0, x0), min(W - 1, x1) + 1):
            self.o[x >> 3] &= ~(0x80 >> (x & 7)) & 0xff

    def bits(self, x, value, n):
        """Ink the n-bit value (MSB left) at x."""
        for i in range(n):
            if value >> (n - 1 - i) & 1 and 0 <= x + i < W:
                self.o[(x + i) >> 3] &= ~(0x80 >> ((x + i) & 7)) & 0xff


def double(value, n):
    out = 0
    for i in range(n):
        out = out << 2 | (3 if value >> (n - 1 - i) & 1 else 0)
    return out


def seg_spans(mask, dy, w, ht, h):
    """x spans (inclusive) of a digit's row dy (0..ht)."""
    out, mid = [], ht // 2
    for bit, c in ((0x01, h), (0x40, mid), (0x08, ht - h)):
        d = abs(dy - c)
        if mask & bit and d <= h and h + 1 + d <= w - h - 1 - d:
            out.append((h + 1 + d, w - h - 1 - d))
    for upper in (1, 0):
        # Without the a (d) bar the upper (lower) verticals run to the digit edge.
        top = 0 if not mask & 0x01 else h + 1
        bottom = ht if not mask & 0x08 else ht - h - 1
        k = min(h, dy - top, mid - 1 - dy) if upper else min(h, dy - mid - 1, bottom - dy)
        if k < 0:
            continue
        for xc, bit in ((h, 0x20 if upper else 0x10), (w - h, 0x02 if upper else 0x04)):
            if mask & bit:
                out.append((xc - k, xc + k))
    return out


def number_row(row, x, y, clip, codes, size, fy, font):
    w, ht, h, gap = size
    dy = fy - y
    for c in codes:
        if c >= SEG_DIGIT:
            shift = 0
            if c in (SEG_DOT, SEG_COLON, SEG_DIGIT + 1):
                adv = 2 * h + 1
                if c == SEG_DIGIT + 1:
                    shift = w - 2 * h
            else:
                adv = w + 1
            if x + adv > clip:
                return
            if 0 <= dy <= ht:
                if c == SEG_DOT:
                    if dy >= ht - 2 * h:
                        row.span(x, x + 2 * h)
                elif c == SEG_COLON:
                    q = ht // 4
                    if abs(dy - q) <= h or abs(dy - 3 * q) <= h:
                        row.span(x, x + 2 * h)
                else:
                    mask = 0x40 if c == SEG_MINUS else 0 if c == SEG_BLANK else MASK[str(c - SEG_DIGIT)]
                    for a, b in seg_spans(mask, dy, w, ht, h):
                        row.span(x + a - shift, x + b - shift)
            x += adv + gap
        else:
            if x + 16 > clip:
                return
            r = dy >> 1
            if 0 <= r < 16:
                row.bits(x, double(font[c][r], 8), 16)
            x += 16


def text_row(row, x, y, clip, codes, scale, fy, font):
    r = (fy - y) // scale
    if not 0 <= r < 16:
        return
    for c in codes:
        if x + 8 * scale > clip:
            return
        g = font[c][r] if c < len(font) else font[QMARK][r]
        row.bits(x, double(g, 8) if scale == 2 else g, 8 * scale)
        x += 8 * scale


def render_row(v, fy, font, sprites, icons):
    row = Row()
    for x0, y0, x1, y1 in RULES:
        if y0 <= fy <= y1:
            row.span(x0, x1)
    for x, y, w, h, rows in sprites:
        if y <= fy < y + h:
            row.bits(x, rows[fy - y], w)
    if BAT[1] + 4 <= fy <= BAT[1] + 17 and bat_fill(v['mv']):
        row.span(BAT[0] + 4, BAT[0] + 3 + bat_fill(v['mv']))
    wx, wy = WEATHER_AT
    r = (fy - wy) >> 1
    if 0 <= r < 48 and v.get('weather'):
        row.bits(wx, double(icons[v['weather']][r], 48), 96)
    for kind, x, y, clip, codes in elements(v):
        if kind in ('L', 'M'):
            number_row(row, x, y, clip, codes, SIZE_L if kind == 'L' else SIZE_M, fy, font)
        else:
            text_row(row, x, y, clip, codes, kind, fy, font)
    return bytes(row.o)


def frame(v, font, sprites, icons):
    return b''.join(render_row(v, y, font, sprites, icons) for y in range(H))


def to_image(plane):
    return Image.frombytes('1', (W, H), plane)


def load(fonts):
    font = font_table(parse_bdf(Path(fonts) / 't0-16b-uni.bdf'))
    font_table_cache[1] = font
    sprites = [(x, y, w, h, bitmap(w, h, draw)) for x, y, w, h, draw in sprite_defs()]
    icons = [bitmap(48, 48, draw) for draw in ICONS]
    return font, sprites, icons


SAMPLE = dict(
    mv=2900, rssi=-63, time=(24 * 365 + 6 + 111) * 86400 + 14 * 3600 + 32 * 60,
    t_in=2340, h_in=4560, t_out=-512, h_out=8800, weather=6,
    slots=[('Мощность', '1.23 кВт'), ('CO2', '812 ppm'), ('Котёл', '58.5°C'), ('Завтра', '-3..+2')],
    message='Вынести мусор - вторник',
)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--fonts', required=True, help='u8g2 tools/font/bdf directory')
    ap.add_argument('--png', help='render SAMPLE into this PNG')
    a = ap.parse_args()
    font, sprites, icons = load(a.fonts)
    if a.png:
        im = to_image(frame(SAMPLE, font, sprites, icons))
        im.save(a.png)
        print(a.png)


if __name__ == '__main__':
    main()
