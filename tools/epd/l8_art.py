#!/usr/bin/env python3
"""L8 layout: 800x480 dashboard for the 4.26" SSD1677 panel.

  l8_art.py --fonts U8G2/tools/font/bdf [--check] [--png OUT.png]

Writes include/epd_l8_art.h (the firmware tables) and tests/epd_l8_golden.h
(CRC-32 of reference planes; --check compares instead).

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
import zlib
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
    """Tenths for -9.9 .. 99.9, whole degrees for -10 .. -99, otherwise --.-.
    Three digit cells (0.0 .. 9.9 get a leading blank), so with fixed-width
    digits the point and the unit stay put."""
    if centi is None or centi == -0x8000:
        return [SEG_MINUS, SEG_MINUS, SEG_DOT, SEG_MINUS]
    neg = centi < 0
    a = (abs(centi) + 5) // 10
    if not a:
        neg = False
    if a <= (99 if neg else 999):
        s = ('-' if neg else '') + f'{a // 10}.{a % 10}'
        if not neg and a < 100:
            return [SEG_BLANK] + value_codes(s)
    elif neg and a <= 999:
        s = f'-{a // 10}'
    else:
        return [SEG_MINUS, SEG_MINUS, SEG_MINUS]
    return value_codes(s)


def hum_codes(hum):
    if hum is None or hum == 0xffff:
        return [SEG_MINUS, SEG_MINUS]
    h = min(99, (hum + 50) // 100)
    return ([SEG_BLANK] if h < 10 else []) + value_codes(str(h))


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


# Weather icons 1..9 (WEATHER order), 48x48 drawn at scale 2.
# Material Design Icons 7.4.47 (Apache-2.0), rasterised by tools/epd/l8_icons.py.
MDI_ICONS = (
    # weather-sunny
    (
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x000001800000, 0x000001800000, 0x000003c00000, 0x000007e00000,
     0x00000ff00000, 0x00000ff00000, 0x00001c380000, 0x000000000000,
     0x000000000000, 0x007e00007f00, 0x01fc0ff03f80, 0x01f83ffc1f80,
     0x01f87ffe1f00, 0x00f0ffff0f00, 0x00e1fe7f8f00, 0x0061f81f8600,
     0x0063f00fc600, 0x0003e007c000, 0x0003e007c000, 0x0003c003c000,
     0x0003c003c000, 0x0003e007c000, 0x0003e007c000, 0x0063f00fc600,
     0x0061f81f8600, 0x00f1fe7f8f00, 0x00f0ffff0f00, 0x00f87ffe1f00,
     0x01f83ffc1f80, 0x01fc0ff03f80, 0x00fe00007e00, 0x000000000000,
     0x000000000000, 0x00001c380000, 0x00000ff00000, 0x00000fe00000,
     0x000007e00000, 0x000003c00000, 0x000001800000, 0x000001800000,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,),
    # weather-night
    (
     0x000000000000, 0x000000000000, 0x000000300000, 0x000000300000,
     0x000000300000, 0x000000780000, 0x000000780000, 0x000e00780000,
     0x001f1fffe000, 0x003f0fffc000, 0x007f07ff8000, 0x00ff03ff0000,
     0x01ff01fe0000, 0x01ff01fe0000, 0x03ff83ff0000, 0x03ef83cf0000,
     0x07cf83870000, 0x07cf82010000, 0x0f8780000c00, 0x0f87c0000c00,
     0x0f87c0000c00, 0x0f83e0001e00, 0x0f03e000ffc0, 0x0f01f0007f80,
     0x0f01f8003f00, 0x0f00fc003f00, 0x0f807e003f00, 0x0f807f003300,
     0x0f803f800000, 0x0f801fe00000, 0x07c00ff80000, 0x07c003ffc000,
     0x03e001ffff00, 0x03f0007fff80, 0x01f0001fff80, 0x01f80003ff80,
     0x00fe00007f00, 0x007f0000fe00, 0x003fc003fc00, 0x001ffc3ff800,
     0x000ffffff000, 0x0003ffffc000, 0x0000ffff0000, 0x00003ffc0000,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,),
    # weather-partly-cloudy
    (
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x000000380000, 0x002000fc0000, 0x007e00fc0000, 0x007e001c0000,
     0x007800060000, 0x007000000000, 0x00607f800000, 0x0001ffe00000,
     0x0003fff80000, 0x0007fffc0000, 0x000fe1fc0000, 0x001f807e0000,
     0x001f003e1800, 0x003e001f1c00, 0x003e001f1f00, 0x043e001f0f00,
     0x0c3c0fff0e00, 0x1c3c3fff0c00, 0x3c3effff0800, 0x3e3fffff8000,
     0x0e3ffc3fc000, 0x061ff00fc000, 0x001fe007e000, 0x000fc003e000,
     0x00ff8001f000, 0x01ff8001f000, 0x03ff0000f000, 0x07ff0000f000,
     0x0fe00000ff80, 0x0f800000ffc0, 0x0f800000ffe0, 0x0f000000fff0,
     0x0f00000001f0, 0x0f80000000f0, 0x0f80000000f0, 0x0fe0000001f0,
     0x07fffffffff0, 0x03ffffffffe0, 0x01ffffffffc0, 0x00ffffffff80,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,),
    # weather-cloudy
    (
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x000000000000, 0x000000000000, 0x00001ff80000, 0x00007ffe0000,
     0x0000ffff0000, 0x0001ffff8000, 0x0003fc3fc000, 0x0007e007e000,
     0x000fc003f000, 0x000f8001f000, 0x00ff0000f800, 0x03fe00007800,
     0x07fe00007800, 0x0ffe00007800, 0x1fe400003fc0, 0x1f8000003fe0,
     0x3f0000003ff0, 0x3e0000003ff8, 0x3e00000001fc, 0x3c000000007c,
     0x3c000000007c, 0x3e000000003c, 0x3e000000003c, 0x3f000000007c,
     0x1f800000007c, 0x1fe0000001fc, 0x0ffffffffff8, 0x07fffffffff0,
     0x03ffffffffe0, 0x00ffffffffc0, 0x000000000000, 0x000000000000,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,),
    # weather-pouring
    (
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x00001ff80000, 0x00007ffe0000, 0x0000ffff0000, 0x0001ffff8000,
     0x0003fc3fc000, 0x0007e007e000, 0x000fc003f000, 0x000f8001f000,
     0x00ff0000f800, 0x03fe00007800, 0x07fe00007800, 0x0ffe00007800,
     0x1fe400003fc0, 0x1f8000003fe0, 0x3f0000003ff0, 0x3e0000003ff8,
     0x3e00000001fc, 0x3c000000007c, 0x3c000000007c, 0x3e000000003c,
     0x3e00e0e0e03c, 0x3f01f1f1f07c, 0x1f81f1f1f07c, 0x1fc1e1e1e1fc,
     0x0fc3e3e3e1f8, 0x07c3e3e3e1f0, 0x03c3e3e3e1e0, 0x0003c3c3c000,
     0x0007c7c7c000, 0x0007c7c7c000, 0x0007c7c7c000, 0x00078f878000,
     0x00078f878000, 0x00030f830000, 0x00000f800000, 0x00001f000000,
     0x00001f000000, 0x00001f000000, 0x00001e000000, 0x00000c000000,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,),
    # weather-snowy
    (
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x00001ff80000, 0x00007ffe0000, 0x0000ffff0000, 0x0001ffff8000,
     0x0003fc3fc000, 0x0007e007e000, 0x000fc003f000, 0x000f8001f000,
     0x00ff0000f800, 0x03fe00007800, 0x07fe00007800, 0x0ffe00007800,
     0x1fe400003fc0, 0x1f8000003fe0, 0x3f0000003ff0, 0x3e0000003ff8,
     0x3e00000001fc, 0x3c000000007c, 0x3c000000007c, 0x3e000000003c,
     0x3e000000003c, 0x3f000000007c, 0x1f800070007c, 0x1fe000f001fc,
     0x0ffc70f03ff8, 0x07fcf9f03ff0, 0x03fcfdf03fe0, 0x00fcfff03fc0,
     0x00007fefc000, 0x00003fffc000, 0x00001fffc000, 0x00007fff8000,
     0x0001fffe0000, 0x0003fff80000, 0x0003fffc0000, 0x0003f7fe0000,
     0x00000fff0000, 0x00000fbf0000, 0x00000f9f0000, 0x00000f0e0000,
     0x00000f000000, 0x00000e000000, 0x000000000000, 0x000000000000,),
    # weather-fog
    (
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x000000000000, 0x000000000000, 0x00001ff80000, 0x00007ffe0000,
     0x0000ffff0000, 0x0001ffff8000, 0x0003fc3fc000, 0x0007e007e000,
     0x000fc003f000, 0x000f8001f000, 0x00ff0000f800, 0x03fe00007800,
     0x07fe00007800, 0x0ffe00007800, 0x1fe400003fc0, 0x1f8000003fe0,
     0x3f0000003ff0, 0x3e0000003ff8, 0x3e00000001fc, 0x3c000000007c,
     0x3c000000007c, 0x3e000000003c, 0x000000000000, 0x000000000000,
     0x000000000000, 0x000000000000, 0x0ffffff3fff0, 0x0ffffff3fff0,
     0x0ffffff3fff0, 0x0ffffff3fff0, 0x000000000000, 0x000000000000,
     0x000000000000, 0x000000000000, 0x0ff3fffffff0, 0x0ff3fffffff0,
     0x0ff3fffffff0, 0x0ff3fffffff0, 0x000000000000, 0x000000000000,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,),
    # weather-lightning-rainy
    (
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x00001ff80000, 0x00007ffe0000, 0x0000ffff0000, 0x0001ffff8000,
     0x0003fc3fc000, 0x0007e007e000, 0x000fc003f000, 0x000f8001f000,
     0x00ff0000f800, 0x03fe00007800, 0x07fe00007800, 0x0ffe00007800,
     0x1fe400003fc0, 0x1f8000003fe0, 0x3f0000003ff0, 0x3e0000003ff8,
     0x3e00000001fc, 0x3c000000007c, 0x3c003f80007c, 0x3e003f80003c,
     0x3e003f00003c, 0x3f007f00007c, 0x1f807e00007c, 0x1fc0fe0001fc,
     0x0fc0fc000ff8, 0x07c0fc020ff0, 0x03c1ff870fe0, 0x0001ff8f8fc0,
     0x0003ff0f8000, 0x0003fe1fc000, 0x00001e3fe000, 0x00003c3fe000,
     0x00003c3fe000, 0x0000383fe000, 0x0000383fe000, 0x0000303fe000,
     0x0000301fc000, 0x0000200f8000, 0x000060000000, 0x000040000000,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,),
    # weather-windy-variant
    (
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x00000ff00000, 0x00003ffc0000, 0x00007ffe0000, 0x0000ffff0000,
     0x0001fe7f8000, 0x0003f00fc000, 0x0003e007c000, 0x0007c003e000,
     0x00ffc003e000, 0x01ffc003e000, 0x03ff8001e000, 0x07ff8001e000,
     0x0fe00001ff80, 0x0f800001ffc0, 0x0f800001ffe0, 0x0f000001fff0,
     0x0f00000001f0, 0x0f80000000f0, 0x0f80000000f0, 0x0fe0000001f0,
     0x07fffffffff0, 0x03ffffffffe0, 0x01ffffffffc0, 0x00ffffffff80,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,
     0x03fffffffe00, 0x03ffffffff00, 0x03ffffffff80, 0x03ffffffffc0,
     0x0000000007c0, 0x0000000003c0, 0x0000000003c0, 0x00000001e7c0,
     0x00000001ffc0, 0x00000001ff80, 0x00000000ff00, 0x000000007e00,
     0x000000000000, 0x000000000000, 0x000000000000, 0x000000000000,),
)

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
    """(kind, x, y, clip, codes); kind 1/2 text scale, 'L'/'M' numbers (fixed-width
    digits), 'S' slot values (size M, narrow digit 1)."""
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
        e.append(('S', x0 + 16, 324, x0 + 192, value_codes(val)))
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


def number_row(row, x, y, clip, codes, size, fy, font, narrow1=False):
    w, ht, h, gap = size
    dy = fy - y
    for c in codes:
        if c >= SEG_DIGIT:
            shift = 0
            if c in (SEG_DOT, SEG_COLON) or (narrow1 and c == SEG_DIGIT + 1):
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
        if kind in ('L', 'M', 'S'):
            number_row(row, x, y, clip, codes, SIZE_L if kind == 'L' else SIZE_M, fy, font, kind == 'S')
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
    icons = [[0] * 48] + [list(rows) for rows in MDI_ICONS]
    return font, sprites, icons


SAMPLE = dict(
    mv=2900, rssi=-63, time=(24 * 365 + 6 + 111) * 86400 + 14 * 3600 + 32 * 60,
    t_in=2340, h_in=4560, t_out=-512, h_out=8800, weather=6,
    slots=[('Мощность', '1.23 кВт'), ('CO2', '812 ppm'), ('Котёл', '58.5°C'), ('Завтра', '-3..+2')],
    message='Вынести мусор - вторник',
)
# 2024-02-29 23:59 local: 24 years (6 leap) + 59 days.
LEAP = ((24 * 365 + 6 + 59) * 86400 + 23 * 3600 + 59 * 60)
CASES = [
    SAMPLE,
    dict(mv=0, rssi=None, time=0, t_in=-0x8000, h_in=0xffff),
    dict(mv=3600, rssi=-100, time=LEAP, t_in=-995, h_in=9950, t_out=9999, h_out=0, weather=1,
         slots=[('1111', '1111'), ('Время', '12:30'), ('Ёлка ё', '-0,5 €'), ('x' * 30, '123456789012')],
         message='0123456789 ABCDEFGHIJ abcdefghij KLMNOPQRST uvwxyz!?'),
    dict(mv=1999, rssi=0, time=1, t_in=-4, h_in=50, t_out=32767, h_out=0xfffe, weather=2,
         message='Привет, мир! Ёё °'),
    dict(mv=2005, rssi=-63, time=86400 * 365, t_in=-1234, h_in=100, t_out=-10000, h_out=4449, weather=3),
    dict(mv=2500, rssi=-63, time=0xfffffffe, t_in=995, h_in=4450, t_out=-9949, h_out=4450, weather=4),
    dict(mv=2799, rssi=-63, time=7 * 86400 - 1, t_in=12345, h_in=0, t_out=5, h_out=1, weather=5,
         slots=[('', '1'), ('', '-'), ('', ':'), ('', '.')]),
    dict(mv=3000, rssi=-63, time=1000000000, t_in=2350, h_in=4549, t_out=-500, h_out=9999, weather=7),
    dict(mv=9999, rssi=27, time=500000000, t_in=0, h_in=5000, t_out=100, h_out=5000, weather=8),
    dict(mv=2970, rssi=-63, time=800000000, t_in=-9949, h_in=5000, t_out=-99, h_out=5000, weather=9),
]

NOTICE = """/* Generated by tools/epd/l8_art.py; do not edit.
 * Glyph pixels come from t0-16b-uni of the u8g2 collection. Copyright (C)
 * 2012-2025 Uwe Waldmann. MIT-style licence (see docs/EPAPER.md for the
 * full notice). Weather icons: Material Design Icons by Pictogrammers,
 * Apache License 2.0 (tools/epd/l8_icons.py). Other icons are drawn by the
 * generator. */
"""
ART = Path(__file__).resolve().parents[2] / 'include' / 'epd_l8_art.h'
GOLDEN = Path(__file__).resolve().parents[2] / 'tests' / 'epd_l8_golden.h'


def hexrows(data):
    return "".join("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",\n"
                   for i in range(0, len(data), 16))


def nested(items):
    """C initializer rows, one braced row per item."""
    return "".join("    {\n" + "".join("    " + l for l in hexrows(d).splitlines(True)) + "    },\n"
                   if len(d) > 16 else "    { " + hexrows(d).strip().rstrip(',') + " },\n"
                   for d in items)


def row_octets(rows, w):
    n = (w + 7) // 8
    return [b for v in rows for b in (v << (n * 8 - w)).to_bytes(n, 'big')]


def emit_art(font, sprites, icons):
    L = [NOTICE, "#ifndef EPD_L8_ART_H\n#define EPD_L8_ART_H\n\n"]
    gbits, gtable, gseen = [], [], {}
    for g in font:
        nz = [r for r, b in enumerate(g) if b]
        top, last = (nz[0], nz[-1]) if nz else (15, 0)
        data = tuple(g[top:last + 1])
        if data not in gseen:
            gseen[data] = len(gbits)
            gbits += data
        gtable.append((gseen[data], top << 4 | last))
    L.append(f"/* Glyphs: ASCII 20..7E, U+0410..U+044F, Ё, ё, °; 16 rows, MSB left.\n"
             f" * Rows top..last (span: top << 4 | last; blank when top > last) start\n"
             f" * at offset at of l8_glyph_bits; the other rows are blank. */\n"
             f"#define EPD_L8_GLYPHS {len(font)}u\n#define EPD_L8_QMARK {QMARK}u\n"
             f"#define EPD_L8_DEGREE {INDEX[0xb0]}u\n")
    L.append("typedef struct { uint16_t at; uint8_t span; } epd_l8_glyph_t;\n")
    L.append("static const MCU_CODE epd_l8_glyph_t l8_glyph_index[EPD_L8_GLYPHS] = {\n")
    L += [f"    {{ {at}u, 0x{sp:02x}u }},\n" for at, sp in gtable]
    L.append(f"}};\nstatic const MCU_CODE uint8_t l8_glyph_bits[{len(gbits)}] = {{\n{hexrows(gbits)}}};\n\n")
    blob, table, seen = [], [], {}
    for x, y, w, h, rows in sprites:
        data = tuple(row_octets(rows, w))
        if data not in seen:
            seen[data] = len(blob)
            blob += data
        table.append((x, y, (w + 7) // 8, h, seen[data]))
    L.append("/* Static sprites: left column, top row, octets per row, rows, offset in\n"
             " * l8_sprite_bits; rows are MSB left. */\n")
    L.append(f"#define EPD_L8_SPRITES {len(table)}u\n")
    L.append("typedef struct { uint16_t x; uint16_t y; uint8_t octets, height; uint16_t at; } epd_l8_sprite_t;\n")
    L.append("static const MCU_CODE epd_l8_sprite_t l8_sprite[EPD_L8_SPRITES] = {\n")
    L += [f"    {{ {x}u, {y}u, {n}u, {h}u, {at}u }},\n" for x, y, n, h, at in table]
    L.append(f"}};\nstatic const MCU_CODE uint8_t l8_sprite_bits[{len(blob)}] = {{\n{hexrows(blob)}}};\n\n")
    L.append("/* Filled rectangles x0, y0, x1, y1 (inclusive). */\n")
    L.append(f"#define EPD_L8_RULES {len(RULES)}u\n")
    L.append("static const MCU_CODE uint16_t l8_rule[EPD_L8_RULES][4] = {\n")
    L += [f"    {{ {a}u, {b}u, {c}u, {d}u }},\n" for a, b, c, d in RULES]
    L.append("};\n\n")
    irows, iseen, index = [], {}, []
    for rows in icons[1:]:
        octets = row_octets(rows, 48)
        index.append([])
        for r in range(48):
            data = tuple(octets[r * 6:r * 6 + 6])
            if data not in iseen:
                iseen[data] = len(irows)
                irows.append(data)
            index[-1].append(iseen[data])
    assert len(irows) <= 256
    ic = [b for d in irows for b in d] + [i for x in index for i in x]
    L.append(f"/* Weather icons 1..{len(icons) - 1} ({', '.join(WEATHER[1:])}): 48 rows, each\n"
             " * an index into l8_icon_row (6 octets), drawn at scale 2. */\n")
    L.append(f"#define EPD_L8_ICONS {len(icons) - 1}u\n")
    L.append(f"static const MCU_CODE uint8_t l8_icon_row[{len(irows)}][6] = {{\n"
             f"{nested([bytes(d) for d in irows])}}};\n")
    L.append(f"static const MCU_CODE uint8_t l8_icon[EPD_L8_ICONS][48] = {{\n"
             f"{nested([bytes(x) for x in index])}}};\n\n")
    L.append("/* Weekday glyphs; day 0 (2000-01-01) was a Saturday. */\n")
    L.append("static const MCU_CODE uint8_t l8_weekday[7][2] = {\n")
    L += [f"    {{ {a}u, {b}u }}, /* {d} */\n" for d, (a, b) in zip(WEEKDAYS, map(glyph_codes, WEEKDAYS))]
    L.append("};\n\n#endif\n")
    return "".join(L), 3 * len(font) + len(gbits) + len(blob) + 8 * len(table) + 8 * len(RULES) + len(ic) + 14


def plane(v, font, sprites, icons):
    """Gate-row plane: gate row g carries screen row 479 - g."""
    return b''.join(render_row(v, H - 1 - g, font, sprites, icons) for g in range(H))


def cstr(s):
    out = []
    for b in s.encode():
        if 0x20 <= b < 0x7f and chr(b) not in '"\\?':
            if out and out[-1].startswith('\\x') and chr(b) in '0123456789abcdefABCDEF':
                out.append('""')
            out.append(chr(b))
        else:
            out.append(f'\\x{b:02x}')
    return '"' + ''.join(out) + '"'


def emit_golden(font, sprites, icons):
    L = ["/* Generated by tools/epd/l8_art.py; do not edit. Reference L8 views and\n"
         " * the CRC-32 (zlib) of their 480x100-octet gate-row planes. rssi 0x7FFF\n"
         " * means unknown. */\n",
         "static const struct {\n    uint16_t mv; int16_t rssi; uint32_t time; int16_t t_in; uint16_t h_in;\n"
         "    int16_t t_out; uint16_t h_out; uint8_t weather;\n"
         "    const char *label[4], *value[4], *message;\n    uint32_t crc;\n} l8_golden[] = {\n"]
    for v in CASES:
        slots = list(v.get('slots', [])) + [('', '')] * (4 - len(v.get('slots', [])))
        rssi = v.get('rssi')
        L.append(f"    {{ {v['mv']}u, {0x7fff if rssi is None else rssi}, {v.get('time') or 0}UL, "
                 f"{v['t_in']}, {v['h_in']}u, {v.get('t_out', -0x8000)}, {v.get('h_out', 0xffff)}u, "
                 f"{v.get('weather', 0)}u,\n")
        L.append(f"      {{ {', '.join(cstr(a) for a, _ in slots)} }},\n")
        L.append(f"      {{ {', '.join(cstr(b) for _, b in slots)} }},\n")
        L.append(f"      {cstr(v.get('message', ''))}, 0x{zlib.crc32(plane(v, font, sprites, icons)):08x}UL }},\n")
    L.append("};\n")
    return "".join(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--fonts', required=True, help='u8g2 tools/font/bdf directory')
    ap.add_argument('--check', action='store_true', help='compare with the tracked files')
    ap.add_argument('--png', help='also render SAMPLE into this PNG')
    a = ap.parse_args()
    font, sprites, icons = load(a.fonts)
    if a.png:
        to_image(frame(SAMPLE, font, sprites, icons)).save(a.png)
    art, size = emit_art(font, sprites, icons)
    golden = emit_golden(font, sprites, icons)
    if a.check:
        bad = [p for p, s in ((ART, art), (GOLDEN, golden)) if not p.exists() or p.read_text() != s]
        if bad:
            sys.exit(f"stale: {', '.join(map(str, bad))}")
        return
    ART.write_text(art)
    GOLDEN.write_text(golden)
    print(f"{ART.name}: {size} table octets; {GOLDEN.name}: {len(CASES)} cases")


if __name__ == '__main__':
    main()
