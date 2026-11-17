#!/usr/bin/env python3
"""Rasterise the L8 weather icons from Material Design Icons.

  l8_icons.py [--svg DIR]

Prints the MDI_ICONS table of tools/epd/l8_art.py: per Home Assistant weather
state, 48 rows of 48 bits (MSB left, 1 = ink). The SVGs are the
Pictogrammers Material Design Icons (Apache License 2.0, see docs/EPAPER.md),
@mdi/svg 7.4.47, the icon set Home Assistant uses for these states. Each icon
is rendered at 384 px (16 x the 24 px design grid), averaged over 8 x 8
blocks to 48 px and thresholded at alpha 40 (bold strokes). Needs cairosvg and Pillow; the
committed table is the reference, this only documents and repeats it.
"""
import argparse
import io
import urllib.request
from pathlib import Path

import cairosvg
from PIL import Image

VERSION = '7.4.47'
NAMES = ('sunny', 'night', 'partly-cloudy', 'cloudy', 'pouring', 'snowy', 'fog', 'lightning-rainy',
         'windy-variant')
THRESHOLD = 40


def svg(name, folder):
    if folder:
        return (Path(folder) / f'weather-{name}.svg').read_bytes()
    url = f'https://cdn.jsdelivr.net/npm/@mdi/svg@{VERSION}/svg/weather-{name}.svg'
    with urllib.request.urlopen(url) as r:
        return r.read()


def rows(data):
    png = cairosvg.svg2png(bytestring=data, output_width=384, output_height=384)
    alpha = Image.open(io.BytesIO(png)).convert('RGBA').split()[3]
    px = alpha.reduce(8).load()
    return [sum(1 << (47 - x) for x in range(48) if px[x, y] >= THRESHOLD) for y in range(48)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--svg', help='folder with weather-*.svg instead of downloading')
    a = ap.parse_args()
    print(f"# Material Design Icons {VERSION} (Apache-2.0), rasterised by tools/epd/l8_icons.py.")
    print('MDI_ICONS = (')
    for name in NAMES:
        r = rows(svg(name, a.svg))
        print(f"    # weather-{name}")
        print('    (' + ''.join(
            ('\n     ' if i % 4 == 0 else ' ') + f"0x{v:012x}," for i, v in enumerate(r)) + '),')
    print(')')


if __name__ == '__main__':
    main()
