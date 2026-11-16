// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
//
// Zigbee2MQTT external converter for the LG ESL29 with the 4.26" L8
// dashboard (SSD1677, cluster 0xFC10, docs/EPAPER.md). Copy to
// <z2m data>/external_converters/ and restart Zigbee2MQTT.
//
// Publish to zigbee2mqtt/<name>/set, for example
//   {"label_1": "Сеть", "value_1": "5.67 кВт", "outdoor_temperature": 2.8,
//    "outdoor_humidity": 94, "weather": "rainy", "sync_time": "sync"}
// The display redraws once, 10 s after the last write that changed
// something. Strings are UTF-8; Latin, Cyrillic, Ёё and ° are shown, other
// characters become '?'. Labels hold 22 glyphs, values 12, the message 48.
// sync_time writes the coordinator's local time; send it every few hours.

import {Zcl} from "zigbee-herdsman";
import * as m from "zigbee-herdsman-converters/lib/modernExtend";
import * as exposes from "zigbee-herdsman-converters/lib/exposes";

const e = exposes.presets;
const ea = exposes.access;
const CLUSTER = "l8Display";

const localSeconds = () => {
    const now = new Date();
    return Math.floor(now.getTime() / 1000) - now.getTimezoneOffset() * 60 - 946684800;
};

const attributes = {
    localTime: {name: "localTime", ID: 0x0000, type: Zcl.DataType.UINT32, write: true},
    refreshInterval: {name: "refreshInterval", ID: 0x0001, type: Zcl.DataType.UINT16, write: true, min: 60, max: 28800},
    outdoorTemperature: {name: "outdoorTemperature", ID: 0x0010, type: Zcl.DataType.INT16, write: true},
    outdoorHumidity: {name: "outdoorHumidity", ID: 0x0011, type: Zcl.DataType.UINT16, write: true},
    weather: {name: "weather", ID: 0x0012, type: Zcl.DataType.ENUM8, write: true, max: 9},
    message: {name: "message", ID: 0x0030, type: Zcl.DataType.CHAR_STR, write: true},
};
for (let k = 0; k < 4; k++) {
    attributes[`label${k + 1}`] = {name: `label${k + 1}`, ID: 0x0020 + 2 * k, type: Zcl.DataType.CHAR_STR, write: true};
    attributes[`value${k + 1}`] = {name: `value${k + 1}`, ID: 0x0021 + 2 * k, type: Zcl.DataType.CHAR_STR, write: true};
}

const glyphs = (max) => (value) => {
    if (typeof value !== "string") throw new Error("Value must be a string");
    if ([...value].length > max) throw new Error(`At most ${max} characters`);
};

const text = (name, attribute, max, description) =>
    m.text({name, cluster: CLUSTER, attribute, description, access: "STATE_SET", validate: glyphs(max)});

const syncTime = {
    key: ["sync_time"],
    convertSet: async (entity, key, value, meta) => {
        await entity.write(CLUSTER, {localTime: localSeconds()});
        return {};
    },
};

const timeOnAnnounce = async (event) => {
    if (event.type !== "deviceAnnounce") return;
    const endpoint = event.data.device.getEndpoint(1);
    try {
        await endpoint.write(CLUSTER, {localTime: localSeconds()});
    } catch {
        // The next sync_time sets it.
    }
};

export default {
    zigbeeModel: ["LG-ESL29"],
    model: "LG-ESL29",
    vendor: "cc2530-zigbee",
    description: 'LG ESL29 with a 4.26" e-paper dashboard',
    extend: [
        m.deviceAddCustomCluster(CLUSTER, {name: CLUSTER, ID: 0xfc10, attributes, commands: {}, commandsResponse: {}}),
        m.temperature(),
        m.humidity(),
        m.battery(),
        m.numeric({
            name: "refresh_interval",
            cluster: CLUSTER,
            attribute: "refreshInterval",
            description: "Seconds between scheduled refreshes",
            unit: "s",
            valueMin: 60,
            valueMax: 28800,
            valueStep: 60,
            access: "ALL",
            entityCategory: "config",
        }),
        m.numeric({
            name: "outdoor_temperature",
            cluster: CLUSTER,
            attribute: "outdoorTemperature",
            description: "Outdoor temperature shown on the display",
            unit: "°C",
            scale: 100,
            valueMin: -99,
            valueMax: 99,
            valueStep: 0.1,
            access: "STATE_SET",
        }),
        m.numeric({
            name: "outdoor_humidity",
            cluster: CLUSTER,
            attribute: "outdoorHumidity",
            description: "Outdoor humidity shown on the display",
            unit: "%",
            scale: 100,
            valueMin: 0,
            valueMax: 100,
            valueStep: 1,
            access: "STATE_SET",
        }),
        m.enumLookup({
            name: "weather",
            cluster: CLUSTER,
            attribute: "weather",
            description: "Weather icon",
            lookup: {
                none: 0,
                sunny: 1,
                "clear-night": 2,
                partlycloudy: 3,
                cloudy: 4,
                rainy: 5,
                snowy: 6,
                fog: 7,
                lightning: 8,
                windy: 9,
            },
            access: "STATE_SET",
        }),
        text("label_1", "label1", 22, "Slot 1 label"),
        text("value_1", "value1", 12, "Slot 1 value"),
        text("label_2", "label2", 22, "Slot 2 label"),
        text("value_2", "value2", 12, "Slot 2 value"),
        text("label_3", "label3", 22, "Slot 3 label"),
        text("value_3", "value3", 12, "Slot 3 value"),
        text("label_4", "label4", 22, "Slot 4 label"),
        text("value_4", "value4", 12, "Slot 4 value"),
        text("message", "message", 48, "Bottom line"),
    ],
    toZigbee: [syncTime],
    exposes: [e.enum("sync_time", ea.SET, ["sync"]).withDescription("Write the local time")],
    onEvent: timeOnAnnounce,
};
