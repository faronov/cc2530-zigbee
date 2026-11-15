# E-paper display (D3 layout)

The opt-in `CC2530_EPD` build shows sensor values on a 2.9" 128×296
black/white e-paper panel on the LG ESL29 (H2) FPC. Integration, schedule and
faults: [ED_JOIN.md](ED_JOIN.md#optional-e-paper-display). Pins, command
sequences and timing: [`include/epd.h`](../include/epd.h).

## Layout

The D3 layout is a 296×128 landscape frame (X right, Y down):

- static layer: title `LIVING ROOM`, a fixed signal-bars icon, thermometer
  and droplet icons, the `°C` and `%` units, the `HUMIDITY` label, the
  battery outline and the `BATTERY` prefix, two full-width rules (y 23 and
  107) and a vertical rule (x 210, y 30..100);
- temperature: three 7-segment digits (38×66, slots x 26, 70 and 126) and a
  decimal point. Values round to tenths: -9.9 .. 99.9 with the point,
  -10 .. -99 whole degrees without it, otherwise `---`; the ZCL invalid
  value 0x8000 shows `--.-`;
- humidity: two 7-segment digits (18×34, slots x 236 and 258), at most 99,
  `--` when invalid. Without a humidity sensor the layout shows
  `EPD_PLACEHOLDER_HUMIDITY` (45 %);
- battery: a gauge filled 0..18 columns for 2.0 .. 3.0 V and the voltage text
  (`2.01 V`, 10 mV steps, `-.-- V` when unknown).

There is no frame buffer. `epd_render()` builds each of the 296 gate rows
(one screen column, 16 octets) on the fly: rules by code, the static layer
from 11 column-major sprites, then the digits, point, gauge and voltage glyphs.

## Generator

[`tools/epd/d3_art.py`](../tools/epd/d3_art.py) holds the reference design.
It writes [`include/epd_d3_art.h`](../include/epd_d3_art.h) (sprites and the
voltage glyphs) and [`tests/epd_d3_golden.h`](../tests/epd_d3_golden.h)
(CRC-32 of the whole gate-row plane for 12 views):

```sh
python3 tools/epd/d3_art.py --fonts U8G2/tools/font/bdf [--check] [--pbm out.pbm]
```

It needs Pillow and the BDF fonts from the u8g2 repository
(github.com/olikraus/u8g2, `tools/font/bdf`, checked at d6c8499c). It
rebuilds the static layer from the sprites and checks it against the
reference before it writes anything. `--check` compares with the tracked
files. The host tests (`host-epd*`) compare the C renderer with the goldens,
so a layout change needs a regeneration.

## Panels

`CC2530_EPD_PANEL` selects the controller. All panels use the same FPC pins
and orientation (gate row 0 is the rightmost column).

| Option | Panel | Refresh | Evidence |
|---|---|---|---|
| `EPD_UC8151` (default) | H2 stock UC8151D | full only, OTP waveform, ~4.9 s | sequence and pins verified on H2 with a RAM pattern; D3 firmware not yet on the panel |
| `EPD_SSD1680` | Waveshare 2.9" V2 | full (OTP, 2.97 s); partial with an own 80-frame LUT (~1.7 s); a full refresh after 10 partials | bench sequence and timings on the panel, faint traces after a partial; firmware not yet on the panel |

Not supported: IL3820 panels (no OTP waveform; they need an own LUT tuned on
the panel) and partial refresh on the UC8151D (needs an own register LUT
tuned on the panel). Vendor LUTs and OTP dumps are not part of this
repository.

Variants: `autonomous-pm2-epd`, `autonomous-pm2-sht3x-epd`,
`autonomous-pm2-epd-factory` (UC8151D) and `autonomous-pm2-epd-ssd1680`.

## Font notices

The generated header contains glyph bitmaps from these fonts.

The `LIVING ROOM` title uses Spleen 8x16 1.9.1:

```
Copyright (c) 2018-2022, Frederic Cambus
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

The `C` of `°C` and the `%` use t0-13b (Unicode); the degree ring is drawn:

```
Copyright (C) 2012-2025 Uwe Waldmann

Permission is hereby granted, free of charge, to any person obtaining
a copy of this font software and associated files (the "Software"),
to deal in the Software without restriction, including without
limitation the rights to use, copy, modify, merge, publish,
distribute, embed, sublicense, and/or sell copies of the Software,
and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

(1) The above copyright notice, this permission notice, and the
    disclaimer below shall be included in all copies or substantial
    portions of the Software.

(2) If the design of any glyphs in the fonts that are contained in the
    Software or generated during the installation process is modified
    or if additional glyphs are added to the fonts, the fonts
    must be renamed. The new names may not contain the word "UW",
    irrespective of capitalisation; the new names may contain the word
    "ttyp0", irrespective of capitalisation, only if preceded by a
    foundry name different from "UW".

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

The small text (`HUMIDITY`, `BATTERY` and the voltage) uses u8g2 squeezed bold 7, public
domain.
