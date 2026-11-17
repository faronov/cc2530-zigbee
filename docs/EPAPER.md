# E-paper display (D3 and L8 layouts)

The opt-in `CC2530_EPD` build shows sensor values on a 2.9" 128×296
black/white e-paper panel on the LG ESL29 (H2) FPC (D3 layout), or a
Home Assistant dashboard on a 4.26" 800×480 SSD1677 panel ([L8](#l8-dashboard-426-ssd1677)). Integration, schedule and
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
| `EPD_SSD1677` | Waveshare 4.26" 800×480 | full only, OTP waveform (`22 F7`) | L8 layout only; L8 router firmware runs on H2 and draws the dashboard pushed by HA |

Not supported: IL3820 panels (no OTP waveform; they need an own LUT tuned on
the panel) and partial refresh on the UC8151D (needs an own register LUT
tuned on the panel). Vendor LUTs and OTP dumps are not part of this
repository.

### UC8151D partial refresh: plan

A commercial Telink sensor with a 128×296 UC81xx panel (static analysis of
its firmware; no values copied) does partial refresh as follows. The steps
use only the UC8151 datasheet commands; the LUT itself must be our own,
tuned on the panel.

1. `PSR` (0x00) with the register-LUT bit set, so 0x20–0x24 are used
   instead of the OTP waveform.
2. Two LUT sets in RAM: a full one and a short single-phase partial one;
   `0x20` VCOM, `0x21` WW, `0x22` BW, `0x23` WB, `0x24` BB are loaded
   before each refresh type.
3. Partial update: `0x50` CDI with a floating border (`0xD7` instead of
   `0x97`, so the border is not driven), `0x91` PTIN, `0x90` PTL with the
   window (HRST, HRED, VRST high/low, VRED high/low, PT_SCAN), the old frame
   to `0x10`, the new frame to `0x13`, `0x12` refresh, `0x92` PTOUT.
4. A full refresh with the full LUT and the normal border every N partials.

The same sensor stores only the glyphs it needs (digits in a few sizes,
`-`, `%`, `.`, a few marks) pre-rotated and column-major for the portrait
panel, instead of a general font.

Variants: `autonomous-pm2-epd`, `autonomous-pm2-sht3x-epd`,
`autonomous-pm2-epd-factory` (UC8151D), `autonomous-pm2-epd-ssd1680`,
`autonomous-pm2-epd-ssd1677` (L8) and `autonomous-pm2-epd-ssd1677-router`
(L8 with a router-parent initial join, `CC2530_ROUTER_JOIN`,
[ED_JOIN.md](ED_JOIN.md#router-parent-initial-join-cc2530_router_join)).

## L8 dashboard (4.26" SSD1677)

`CC2530_EPD_PANEL=EPD_SSD1677` selects the L8 layout
([`src/epd_l8.c`](../src/epd_l8.c)): an 800×480 landscape frame, rendered
row by row like D3 (100 octets per row, no frame buffer).

- status bar: battery gauge (0..36 columns for 2.0 .. 3.0 V) and voltage,
  Zigbee icon and the RSSI of the last received frame, local clock
  (`Пн 21.04 14:32`, `-- --.-- --:--` until a time is written);
- left half `ДОМА`: the device's temperature and humidity (7-segment);
- right half `УЛИЦА`: outdoor temperature and humidity and a 96×96 weather
  icon (a Material Design Icons weather icon, 48×48 drawn twice the size),
  all pushed by Home Assistant;
- temperatures and humidities use fixed-width digit cells: three for a
  temperature (`21.4`, `-3.2`, and `5.3` behind a blank cell; at -10 °C and
  below `-12` without a point) and two for a humidity (a blank cell in front
  of a single digit), so the point and the unit stay in place;
- four slots: a label (scale 1) and a value (7-segment for digits, `-`,
  `.`, `,` and `:`, glyphs otherwise; a narrow `1`, so long values fit);
- a message line (scale 2).

Text uses the t0-16b-uni 8×16 font (ASCII, Cyrillic, `Ёё` and `°`); other
characters show `?`. [`tools/epd/l8_art.py`](../tools/epd/l8_art.py) is the
reference design and writes [`include/epd_l8_art.h`](../include/epd_l8_art.h)
and [`tests/epd_l8_golden.h`](../tests/epd_l8_golden.h); `host-epd-ssd1677`
compares the C renderer with the goldens:

```sh
python3 tools/epd/l8_art.py --fonts U8G2/tools/font/bdf [--check] [--png out.png]
```

The weather icons are a table in `l8_art.py`;
[`tools/epd/l8_icons.py`](../tools/epd/l8_icons.py) documents and repeats
it from the pinned `@mdi/svg` SVGs (needs cairosvg and Pillow):

```sh
python3 tools/epd/l8_icons.py [--svg DIR]
```

### Cluster 0xFC10

A cluster in the manufacturer-specific range on the application endpoint
(server). Frames with or without a manufacturer code are accepted. All
attributes are readable; all but ClusterRevision are writable.

| Id | Type | Attribute | Range / unknown |
|---|---|---|---|
| 0x0000 | uint32 | local time, seconds since 2000-01-01 00:00 | 0 unknown |
| 0x0001 | uint16 | refresh interval, s | 60..28800, default 300 |
| 0x0010 | int16 | outdoor temperature, 0.01 °C | 0x8000 unknown |
| 0x0011 | uint16 | outdoor humidity, 0.01 %RH | ≤ 10000, 0xFFFF unknown |
| 0x0012 | enum8 | weather: 0 none, 1 sunny, 2 clear night, 3 partly cloudy, 4 cloudy, 5 rainy, 6 snowy, 7 fog, 8 lightning, 9 windy | 0..9 |
| 0x0020 + 2k | charStr | slot k label (k 0..3) | 22 glyphs |
| 0x0021 + 2k | charStr | slot k value | 12 glyphs |
| 0x0030 | charStr | message | 48 glyphs |
| 0xFFFD | uint16 | ClusterRevision 1 | read-only (0x88) |

Out-of-range values return 0x87. Strings are UTF-8 and cut to the field;
reads return the stored text as UTF-8.

The clock counts on from the last time write. A write that changes what is
shown (a time write only when the time was unknown) redraws the panel once
the last write is 10 s old and the last refresh started at least 30 s
before, so a batch of writes gives one refresh. Otherwise the panel
refreshes every interval when the view changed (indoor values, battery,
RSSI, clock). Each refresh is a full one (OTP waveform).

### Home Assistant and Zigbee2MQTT

[`tools/ha/`](../tools/ha) holds the integrations for the device
`cc2530-zigbee` / `LG-ESL29`. After flashing the L8 firmware, re-pair (or
re-interview) the device so the coordinator sees cluster 0xFC10.

- ZHA: copy [`zha_quirk_lg_esl29_l8.py`](../tools/ha/zha_quirk_lg_esl29_l8.py)
  to the custom quirks directory (`zha: custom_quirks_path:`, for example
  `/config/custom_zha_quirks/`) and reload ZHA. It adds the cluster as
  `l8_display` and a refresh interval number entity.
- HA package: copy [`lg_esl29_l8.yaml`](../tools/ha/lg_esl29_l8.yaml) to
  `/config/packages/` (`homeassistant: packages: !include_dir_named packages`)
  and edit the IEEE address, weather entity and slots in `script.l8_push`.
  Its automation writes the clock, outdoor weather, slot labels, values and
  interval at start and every 5 minutes (the device keeps them in RAM only,
  so a reset shows dashes until the next push);
  `script.l8_message` writes the message line.
- Zigbee2MQTT: copy [`lg_esl29_l8.mjs`](../tools/ha/lg_esl29_l8.mjs) to
  `external_converters/` and restart Z2M. It exposes every attribute as a
  settable entity, writes the clock on device announce and adds a
  `sync_time` action.

The device is a sleepy end device: writes are delivered on its next poll.

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

The `C` of `°C` and the `%` use t0-13b (Unicode); the degree ring is drawn.
The L8 text uses t0-16b-uni from the same family. Both carry this notice:

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

## Icon notice

The L8 weather icons are rasterised from Material Design Icons
(`@mdi/svg` 7.4.47) by [Pictogrammers](https://pictogrammers.com/),
licensed under the Apache License, Version 2.0
(<https://www.apache.org/licenses/LICENSE-2.0>). They are converted to
48×48 bitmaps (modified). The other icons are drawn by the generator.
