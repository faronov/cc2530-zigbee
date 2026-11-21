# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
"""ZHA quirk for the LG ESL29 with the 4.26" L8 dashboard (cluster 0xFC10).

Copy into the ZHA custom_quirks_path and reload the ZHA entry. The cluster
attributes are written by tools/ha/lg_esl29_l8.yaml through
zha.set_zigbee_cluster_attribute; the quirk gives them types so the strings
and integers are encoded correctly. Frames are sent without a manufacturer
code (the firmware accepts both). See docs/EPAPER.md.
"""

try:
    # Newer ZHA: the quirks v2 API lives in zha-quirks.
    from zhaquirks.builder import QuirkBuilder
    from zhaquirks.clusters import CustomCluster
except ImportError:
    # Older zigpy where the quirks v2 API lives in zigpy itself.
    from zigpy.quirks import CustomCluster
    from zigpy.quirks.v2 import QuirkBuilder
import zigpy.types as t
from zigpy.zcl.foundation import BaseAttributeDefs, ZCLAttributeDef


class L8Weather(t.enum8):
    """Weather icon codes (HA conditions)."""

    none = 0
    sunny = 1
    clear_night = 2
    partlycloudy = 3
    cloudy = 4
    rainy = 5
    snowy = 6
    fog = 7
    lightning = 8
    windy = 9


def _string(attr_id: int) -> ZCLAttributeDef:
    return ZCLAttributeDef(id=attr_id, type=t.CharacterString, access="rw",
                           is_manufacturer_specific=False)


class L8DisplayCluster(CustomCluster):
    """Dashboard data pushed by Home Assistant."""

    cluster_id = 0xFC10
    name = "L8 display"
    ep_attribute = "l8_display"

    class AttributeDefs(BaseAttributeDefs):
        local_time = ZCLAttributeDef(id=0x0000, type=t.uint32_t, access="rw",
                                     is_manufacturer_specific=False)
        refresh_interval = ZCLAttributeDef(id=0x0001, type=t.uint16_t, access="rw",
                                           is_manufacturer_specific=False)
        outdoor_temperature = ZCLAttributeDef(id=0x0010, type=t.int16s, access="rw",
                                              is_manufacturer_specific=False)
        outdoor_humidity = ZCLAttributeDef(id=0x0011, type=t.uint16_t, access="rw",
                                           is_manufacturer_specific=False)
        weather = ZCLAttributeDef(id=0x0012, type=L8Weather, access="rw",
                                  is_manufacturer_specific=False)
        label_1 = _string(0x0020)
        value_1 = _string(0x0021)
        label_2 = _string(0x0022)
        value_2 = _string(0x0023)
        label_3 = _string(0x0024)
        value_3 = _string(0x0025)
        label_4 = _string(0x0026)
        value_4 = _string(0x0027)
        message = _string(0x0030)
        cluster_revision = ZCLAttributeDef(id=0xFFFD, type=t.uint16_t, access="r",
                                           mandatory=True)


(
    QuirkBuilder("cc2530-zigbee", "LG-ESL29")
    .replaces(L8DisplayCluster)
    .number(
        L8DisplayCluster.AttributeDefs.refresh_interval.name,
        L8DisplayCluster.cluster_id,
        min_value=60,
        max_value=28800,
        step=60,
        unit="s",
        mode="box",
        translation_key="refresh_interval",
        fallback_name="Refresh interval",
    )
    .add_to_registry()
)
