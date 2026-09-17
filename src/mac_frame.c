/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_frame.h"

#include <stddef.h>
#include <string.h>

static inline uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static uint8_t command_size(uint8_t identifier)
{
    switch (identifier) {
    case MAC_COMMAND_ASSOCIATION_REQUEST:
    case MAC_COMMAND_DISASSOCIATION:
        return 2;
    case MAC_COMMAND_ASSOCIATION_RESPONSE:
        return 4;
    case MAC_COMMAND_DATA_REQUEST:
    case MAC_COMMAND_BEACON_REQUEST:
        return 1;
    default:
        return 0;
    }
}

static mac_codec_result_t command_policy(const mac_command_t *command)
{
    switch (command->identifier) {
    case MAC_COMMAND_ASSOCIATION_REQUEST:
        if (command->capability & 0x30u)
            return MAC_CODEC_INVALID_COMMAND;
        break;
    case MAC_COMMAND_ASSOCIATION_RESPONSE:
        if (command->status > MAC_ASSOCIATION_PAN_ACCESS_DENIED
                || (command->status == MAC_ASSOCIATION_SUCCESS && command->short_address == 0xffffu)
                || (command->status != MAC_ASSOCIATION_SUCCESS && command->short_address != 0xffffu))
            return MAC_CODEC_INVALID_COMMAND;
        break;
    case MAC_COMMAND_DISASSOCIATION:
        if (command->reason != MAC_DISASSOCIATION_COORDINATOR_REQUEST
                && command->reason != MAC_DISASSOCIATION_DEVICE_REQUEST)
            return MAC_CODEC_INVALID_COMMAND;
        break;
    case MAC_COMMAND_DATA_REQUEST:
    case MAC_COMMAND_BEACON_REQUEST:
        break;
    default:
        return MAC_CODEC_UNSUPPORTED_COMMAND;
    }
    return MAC_CODEC_OK;
}

mac_codec_result_t mac_command_decode(const uint8_t *payload, uint16_t length,
                                      mac_command_t *result)
{
    mac_command_t candidate;
    mac_codec_result_t status;
    uint8_t size;

    if (payload == NULL || result == NULL)
        return MAC_CODEC_INVALID_ARGUMENT;
    if (length > MAC_COMMAND_MAX_PAYLOAD)
        return MAC_CODEC_TOO_LONG;
    if (length == 0u)
        return MAC_CODEC_TRUNCATED;
    size = command_size(payload[0]);
    if (size == 0u)
        return MAC_CODEC_UNSUPPORTED_COMMAND;
    if (length < size)
        return MAC_CODEC_TRUNCATED;
    if (length != size)
        return MAC_CODEC_INVALID_COMMAND;
    memset(&candidate, 0, sizeof(candidate));
    candidate.identifier = payload[0];
    if (candidate.identifier == MAC_COMMAND_ASSOCIATION_REQUEST)
        candidate.capability = payload[1];
    else if (candidate.identifier == MAC_COMMAND_ASSOCIATION_RESPONSE) {
        candidate.short_address = read_le16(payload + 1);
        candidate.status = payload[3];
    } else if (candidate.identifier == MAC_COMMAND_DISASSOCIATION)
        candidate.reason = payload[1];
    status = command_policy(&candidate);
    if (status != MAC_CODEC_OK)
        return status;
    *result = candidate;
    return MAC_CODEC_OK;
}

mac_codec_result_t mac_command_encode(const mac_command_t *command,
                                      uint8_t *payload, uint16_t capacity, uint8_t *length)
{
    mac_codec_result_t status;
    uint8_t size;

    if (command == NULL || payload == NULL || length == NULL)
        return MAC_CODEC_INVALID_ARGUMENT;
    status = command_policy(command);
    if (status != MAC_CODEC_OK)
        return status;
    size = command_size(command->identifier);
    if (capacity < size)
        return MAC_CODEC_BUFFER_TOO_SMALL;
    payload[0] = command->identifier;
    if (command->identifier == MAC_COMMAND_ASSOCIATION_REQUEST)
        payload[1] = command->capability;
    else if (command->identifier == MAC_COMMAND_ASSOCIATION_RESPONSE) {
        write_le16(payload + 1, command->short_address);
        payload[3] = command->status;
    } else if (command->identifier == MAC_COMMAND_DISASSOCIATION)
        payload[1] = command->reason;
    *length = size;
    return MAC_CODEC_OK;
}

static mac_codec_result_t beacon_fields(const uint8_t *payload, uint16_t length,
                                        mac_beacon_info_t *result)
{
    mac_beacon_info_t candidate;
    uint8_t i;

    if (length > 4u + 8u * MAC_BEACON_MAX_PENDING + MAC_BEACON_MAX_PAYLOAD)
        return MAC_CODEC_TOO_LONG;
    if (length < 4u)
        return MAC_CODEC_TRUNCATED;
    if ((payload[1] & 0x20u) || (payload[2] & 0x78u))
        return MAC_CODEC_INVALID_BEACON;
    if (payload[2] & 7u)
        return MAC_CODEC_UNSUPPORTED_BEACON;
    if (payload[3] & 0x88u)
        return MAC_CODEC_INVALID_BEACON;
    memset(&candidate, 0, sizeof(candidate));
    candidate.short_count = payload[3] & 7u;
    candidate.extended_count = (payload[3] >> 4) & 7u;
    if (candidate.short_count + candidate.extended_count > MAC_BEACON_MAX_PENDING)
        return MAC_CODEC_INVALID_BEACON;
    candidate.short_offset = 4;
    candidate.extended_offset = (uint8_t)(4u + 2u * candidate.short_count);
    candidate.payload_offset = (uint8_t)(candidate.extended_offset + 8u * candidate.extended_count);
    if (length < candidate.payload_offset)
        return MAC_CODEC_TRUNCATED;
    if (length > candidate.payload_offset + MAC_BEACON_MAX_PAYLOAD)
        return MAC_CODEC_TOO_LONG;
    for (i = 4; i < candidate.extended_offset; i += 2)
        if (read_le16(payload + i) == 0xffffu)
            return MAC_CODEC_INVALID_BEACON;
    candidate.superframe_specification = read_le16(payload);
    candidate.gts_permit = payload[2] >> 7;
    candidate.payload_length = (uint8_t)(length - candidate.payload_offset);
    if (result != NULL)
        *result = candidate;
    return MAC_CODEC_OK;
}

mac_codec_result_t mac_beacon_decode(const uint8_t *payload, uint16_t length,
                                     mac_beacon_info_t *result)
{
    if (payload == NULL || result == NULL)
        return MAC_CODEC_INVALID_ARGUMENT;
    return beacon_fields(payload, length, result);
}

static mac_codec_result_t header_shape(const mac_header_t *header,
                                       uint8_t *destination_size, uint8_t *source_size,
                                       uint8_t *header_size)
{
    if (header->version > 1u || ((header->type == MAC_FRAME_BEACON || header->type == MAC_FRAME_ACK
                                || header->type == MAC_FRAME_COMMAND)
                                && header->version != 0u))
        return MAC_CODEC_UNSUPPORTED_VERSION;
    if (header->flags & MAC_FLAG_SECURITY)
        return MAC_CODEC_UNSUPPORTED_SECURITY;
    if (header->flags & (uint8_t)~(MAC_FLAG_PENDING | MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION))
        return MAC_CODEC_INVALID_HEADER;
    if (header->type == MAC_FRAME_BEACON) {
        if (header->destination_mode != MAC_ADDRESS_NONE
                || (header->source_mode != MAC_ADDRESS_SHORT && header->source_mode != MAC_ADDRESS_EXTENDED))
            return MAC_CODEC_UNSUPPORTED_ADDRESSING;
        if (header->flags & (MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION))
            return MAC_CODEC_INVALID_HEADER;
        *destination_size = 0;
        *source_size = header->source_mode == MAC_ADDRESS_SHORT ? 2u : 8u;
        *header_size = (uint8_t)(5u + *source_size);
        return MAC_CODEC_OK;
    }
    if (header->type == MAC_FRAME_ACK) {
        if (header->destination_mode != MAC_ADDRESS_NONE || header->source_mode != MAC_ADDRESS_NONE
                || (header->flags & (MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION)))
            return MAC_CODEC_INVALID_HEADER;
        *destination_size = 0;
        *source_size = 0;
        *header_size = 3;
        return MAC_CODEC_OK;
    }
    if (header->type != MAC_FRAME_DATA && header->type != MAC_FRAME_COMMAND)
        return MAC_CODEC_UNSUPPORTED_TYPE;
    if (header->destination_mode > MAC_ADDRESS_EXTENDED || header->destination_mode == 1u
            || header->source_mode > MAC_ADDRESS_EXTENDED || header->source_mode == 1u)
        return MAC_CODEC_UNSUPPORTED_ADDRESSING;
    if ((header->destination_mode == MAC_ADDRESS_NONE || header->source_mode == MAC_ADDRESS_NONE)
            && (header->type == MAC_FRAME_DATA || (header->flags & MAC_FLAG_PAN_COMPRESSION)
                || header->destination_mode == header->source_mode))
        return MAC_CODEC_UNSUPPORTED_ADDRESSING;
    *destination_size = header->destination_mode == MAC_ADDRESS_NONE ? 0u
                        : (header->destination_mode == MAC_ADDRESS_SHORT ? 2u : 8u);
    *source_size = header->source_mode == MAC_ADDRESS_NONE ? 0u
                   : (header->source_mode == MAC_ADDRESS_SHORT ? 2u : 8u);
    *header_size = (uint8_t)(3u + *destination_size + *source_size
                            + (*destination_size ? 2u : 0u)
                            + ((*source_size && !(header->flags & MAC_FLAG_PAN_COMPRESSION)) ? 2u : 0u));
    return MAC_CODEC_OK;
}

static mac_codec_result_t address_policy(const mac_header_t *header)
{
    if (header->type == MAC_FRAME_ACK)
        return MAC_CODEC_OK;
    if ((header->flags & MAC_FLAG_PAN_COMPRESSION) && header->source_pan != header->destination_pan)
        return MAC_CODEC_INVALID_HEADER;
    if (header->source_mode == MAC_ADDRESS_SHORT && read_le16(header->source) == 0xffffu)
        return MAC_CODEC_INVALID_HEADER;
    if (header->type == MAC_FRAME_BEACON && (header->source_pan == 0xffffu
            || (header->source_mode == MAC_ADDRESS_SHORT && read_le16(header->source) == 0xfffeu)))
        return MAC_CODEC_INVALID_HEADER;
    if ((header->flags & MAC_FLAG_ACK_REQUEST) && header->destination_mode == MAC_ADDRESS_SHORT
            && read_le16(header->destination) == 0xffffu)
        return MAC_CODEC_INVALID_HEADER;
    return MAC_CODEC_OK;
}

static mac_codec_result_t command_header_policy(const mac_header_t *header,
                                                const uint8_t *payload, uint16_t length,
                                                uint8_t transmitting)
{
    mac_command_t command;
    mac_codec_result_t status;
    uint8_t compressed = (header->flags & MAC_FLAG_PAN_COMPRESSION) != 0u;

    if (length == 0u)
        return MAC_CODEC_TRUNCATED;
    status = mac_command_decode(payload, length, &command);
    if (status != MAC_CODEC_OK)
        return status;
    /* Pending is required to be zero on transmission, ignored on reception. */
    if (transmitting && (header->flags & MAC_FLAG_PENDING))
        return MAC_CODEC_INVALID_HEADER;
    if (command.identifier == MAC_COMMAND_BEACON_REQUEST) {
        if (header->destination_mode != MAC_ADDRESS_SHORT || header->source_mode != MAC_ADDRESS_NONE
                || header->destination_pan != 0xffffu || read_le16(header->destination) != 0xffffu
                || (header->flags & MAC_FLAG_ACK_REQUEST))
            return MAC_CODEC_INVALID_HEADER;
        return MAC_CODEC_OK;
    }
    if (!(header->flags & MAC_FLAG_ACK_REQUEST)
            || (header->destination_mode == MAC_ADDRESS_SHORT && read_le16(header->destination) >= 0xfffeu)
            || (header->source_mode == MAC_ADDRESS_SHORT && read_le16(header->source) >= 0xfffeu))
        return MAC_CODEC_INVALID_HEADER;
    if (command.identifier == MAC_COMMAND_DATA_REQUEST) {
        if (header->source_mode == MAC_ADDRESS_NONE
                || compressed != (header->destination_mode != MAC_ADDRESS_NONE))
            return MAC_CODEC_INVALID_HEADER;
        return MAC_CODEC_OK;
    }
    if (header->source_mode != MAC_ADDRESS_EXTENDED || header->destination_mode == MAC_ADDRESS_NONE)
        return MAC_CODEC_INVALID_HEADER;
    if (command.identifier == MAC_COMMAND_ASSOCIATION_REQUEST) {
        if (compressed || header->source_pan != 0xffffu)
            return MAC_CODEC_INVALID_HEADER;
    } else if (!compressed || (command.identifier == MAC_COMMAND_ASSOCIATION_RESPONSE
                              && header->destination_mode != MAC_ADDRESS_EXTENDED))
        return MAC_CODEC_INVALID_HEADER;
    return MAC_CODEC_OK;
}

mac_codec_result_t mac_frame_decode(const uint8_t *body, uint16_t length,
                                    mac_frame_info_t *result)
{
    mac_frame_info_t candidate;
    mac_codec_result_t status;
    uint8_t destination_size, source_size, header_size, position, i;

    if (body == NULL || result == NULL)
        return MAC_CODEC_INVALID_ARGUMENT;
    if (length > MAC_FRAME_MAX_BODY)
        return MAC_CODEC_TOO_LONG;
    if (length < 3u)
        return MAC_CODEC_TRUNCATED;
    /* Legacy frames cannot use sequence suppression or information elements. */
    if (body[1] & 0x03u)
        return MAC_CODEC_INVALID_HEADER;
    memset(&candidate, 0, sizeof(candidate));
    candidate.header.type = body[0] & 0x07u;
    candidate.header.flags = body[0] & 0xf8u;
    candidate.header.version = (body[1] >> 4) & 0x03u;
    candidate.header.destination_mode = (body[1] >> 2) & 0x03u;
    candidate.header.source_mode = (body[1] >> 6) & 0x03u;
    candidate.header.sequence = body[2];
    status = header_shape(&candidate.header, &destination_size, &source_size, &header_size);
    if (status != MAC_CODEC_OK)
        return status;
    if (length < header_size)
        return MAC_CODEC_TRUNCATED;
    if (candidate.header.type == MAC_FRAME_ACK && length != header_size)
        return MAC_CODEC_INVALID_HEADER;
    position = 3;
    if (destination_size) {
        candidate.header.destination_pan = read_le16(body + position);
        position += 2;
        for (i = 0; i < destination_size; i++)
            candidate.header.destination[i] = body[position++];
    }
    if (source_size) {
        if (candidate.header.flags & MAC_FLAG_PAN_COMPRESSION) {
            candidate.header.source_pan = candidate.header.destination_pan;
        } else {
            candidate.header.source_pan = read_le16(body + position);
            position += 2;
        }
        for (i = 0; i < source_size; i++)
            candidate.header.source[i] = body[position++];
    }
    status = address_policy(&candidate.header);
    if (status != MAC_CODEC_OK)
        return status;
    if (candidate.header.type == MAC_FRAME_COMMAND) {
        status = command_header_policy(&candidate.header, body + header_size,
                                       length - header_size, 0);
        if (status != MAC_CODEC_OK)
            return status;
    }
    if (candidate.header.type == MAC_FRAME_BEACON) {
        status = beacon_fields(body + header_size, length - header_size, NULL);
        if (status != MAC_CODEC_OK)
            return status;
    }
    candidate.payload_offset = header_size;
    candidate.payload_length = (uint8_t)(length - header_size);
    *result = candidate;
    return MAC_CODEC_OK;
}

mac_codec_result_t mac_frame_encode(const mac_header_t *header,
                                    const uint8_t *payload, uint16_t payload_length,
                                    uint8_t *body, uint16_t capacity, uint8_t *length)
{
    mac_codec_result_t status;
    uint8_t destination_size, source_size, header_size, position, i;

    if (header == NULL || body == NULL || length == NULL || (payload == NULL && payload_length != 0u))
        return MAC_CODEC_INVALID_ARGUMENT;
    status = header_shape(header, &destination_size, &source_size, &header_size);
    if (status != MAC_CODEC_OK)
        return status;
    status = address_policy(header);
    if (status != MAC_CODEC_OK)
        return status;
    if (header->type == MAC_FRAME_ACK && payload_length != 0u)
        return MAC_CODEC_INVALID_HEADER;
    if (payload_length > MAC_FRAME_MAX_BODY - header_size)
        return MAC_CODEC_TOO_LONG;
    if (header->type == MAC_FRAME_COMMAND) {
        status = command_header_policy(header, payload, payload_length, 1);
        if (status != MAC_CODEC_OK)
            return status;
    }
    if (header->type == MAC_FRAME_BEACON) {
        status = beacon_fields(payload, payload_length, NULL);
        if (status != MAC_CODEC_OK)
            return status;
    }
    if (capacity < header_size + payload_length)
        return MAC_CODEC_BUFFER_TOO_SMALL;
    body[0] = (uint8_t)(header->type | header->flags);
    body[1] = (uint8_t)((header->destination_mode << 2) | (header->version << 4)
                       | (header->source_mode << 6));
    body[2] = header->sequence;
    position = 3;
    if (destination_size) {
        write_le16(body + position, header->destination_pan);
        position += 2;
        for (i = 0; i < destination_size; i++)
            body[position++] = header->destination[i];
    }
    if (source_size) {
        if (!(header->flags & MAC_FLAG_PAN_COMPRESSION)) {
            write_le16(body + position, header->source_pan);
            position += 2;
        }
        for (i = 0; i < source_size; i++)
            body[position++] = header->source[i];
    }
    for (i = 0; i < payload_length; i++)
        body[position++] = payload[i];
    *length = position;
    return MAC_CODEC_OK;
}
