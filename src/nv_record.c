/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nv_record.h"
#include <stddef.h>

MCU_XDATA uint8_t nv_record_fault;
MCU_XDATA nv_record_status_t nv_record_diagnostic;
MCU_XDATA uint8_t nv_record_stage[NV_RECORD_MAX], nv_record_chunk[FLASH_READ_MAX], nv_record_word[4];
static MCU_XDATA uint8_t header[12], footer[8], lengths[2];
static MCU_XDATA uint32_t generations[2], crc;
extern MCU_XDATA uint8_t nv_record_reserved_end;
static const MCU_CODE uint8_t magic[4] = {'N', 'V', 'R', '1'};
static const MCU_CODE uint8_t commit[4] = {'C', 'M', 'T', '1'};

static uint8_t caller(const void MCU_XDATA *object, uint8_t size)
{
    uint16_t address = MMIO_XADDRESS(object);
    return address > MMIO_XADDRESS(&nv_record_reserved_end) && address < 0x1e00u &&
        size <= 0x1e00u-address;
}

static nv_record_result_t finish(nv_record_result_t result)
{
    nv_record_diagnostic.result = result;
    return result;
}

static nv_record_result_t fault(nv_record_result_t result)
{
    nv_record_fault = result;
    return finish(result);
}

static void begin(void)
{
    uint8_t i;
    for (i = 0; i < offsetof(nv_record_status_t, erase_attempts); i++)
        ((uint8_t MCU_XDATA *)&nv_record_diagnostic)[i] = 0;
    nv_record_diagnostic.result = NV_RECORD_PENDING;
    nv_record_diagnostic.selected = 255;
    nv_record_diagnostic.reader = nv_record_diagnostic.writer = 255;
    nv_record_diagnostic.phase = 1;
}

static void crc_byte(uint8_t value)
{
    uint8_t i;
    crc ^= value;
    for (i = 0; i < 8; i++)
        crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320UL : 0);
}

static uint32_t decode(const uint8_t MCU_XDATA *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void encode(uint8_t MCU_XDATA *bytes, uint32_t value)
{
    uint8_t i;
    for (i = 0; i < 4; i++) { bytes[i] = (uint8_t)value; value >>= 8; }
}

static nv_record_result_t scan_page(uint8_t page, uint8_t capture)
{
    uint16_t offset, position;
    uint8_t i, value, erased = 1, padding = 0, state;
    crc = 0xffffffffUL;
    nv_record_diagnostic.pages[page] = NV_PAGE_UNCHECKED;
    for (offset = 0; offset < FLASH_PAGE_SIZE; offset += FLASH_READ_MAX) {
        nv_record_diagnostic.reader = flash_nv_read(page, offset, nv_record_chunk, FLASH_READ_MAX);
        if (nv_record_diagnostic.reader != FLASH_OK) return fault(NV_RECORD_READ_FAILED);
        for (i = 0; i < FLASH_READ_MAX; i++) {
            position = offset+i; value = nv_record_chunk[i];
            if (value != 255) erased = 0;
            if (position < 12) header[position] = value;
            if (position < 2040) {
                crc_byte(value);
                if (position >= 12u+header[6] && value != 255) padding = 1;
                if (capture && position >= 12 && position < 12u+NV_RECORD_MAX)
                    nv_record_stage[position-12] = value;
            } else footer[position-2040] = value;
        }
    }
    state = erased ? NV_PAGE_EMPTY : NV_PAGE_VALID;
    if (!erased) {
        for (i = 0; i < 4; i++)
            if (footer[4+i] != commit[i]) state = NV_PAGE_INCOMPLETE;
        if (state == NV_PAGE_VALID) {
            for (i = 0; i < 4; i++) if (header[i] != magic[i]) state = NV_PAGE_FORMAT;
            if (state == NV_PAGE_VALID && header[4] != 1) state = NV_PAGE_UNSUPPORTED;
            if (state == NV_PAGE_VALID && (header[5] || header[7] || !header[6] ||
                header[6] > NV_RECORD_MAX || padding || !decode(header+8))) state = NV_PAGE_FORMAT;
            if (state == NV_PAGE_VALID && decode(footer) != (crc ^ 0xffffffffUL))
                state = NV_PAGE_INTEGRITY;
        }
    }
    nv_record_diagnostic.pages[page] = state;
    generations[page] = decode(header+8); lengths[page] = header[6];
    return NV_RECORD_OK;
}

static nv_record_result_t select_record(void)
{
    uint8_t i, selected, valid = 0;
    nv_record_result_t result;
    for (i = 0; i < 2; i++) {
        result = scan_page(i, 0);
        if (result != NV_RECORD_OK) return result;
        if (nv_record_diagnostic.pages[i] == NV_PAGE_VALID) valid |= (uint8_t)(1u << i);
    }
    if (nv_record_diagnostic.pages[0] == NV_PAGE_UNSUPPORTED ||
        nv_record_diagnostic.pages[1] == NV_PAGE_UNSUPPORTED) return NV_RECORD_UNSUPPORTED;
    if (!valid) return nv_record_diagnostic.pages[0] == NV_PAGE_EMPTY &&
        nv_record_diagnostic.pages[1] == NV_PAGE_EMPTY ? NV_RECORD_EMPTY : NV_RECORD_CORRUPT;
    if (valid == 3) {
        selected = generations[1] > generations[0] ? 1 : 0;
        if (!generations[selected] || generations[selected]-generations[selected ^ 1u] != 1)
            return NV_RECORD_CONFLICT;
    } else selected = valid == 1 ? 0 : 1;
    nv_record_diagnostic.selected = selected;
    nv_record_diagnostic.length = lengths[selected];
    nv_record_diagnostic.generation = generations[selected];
    if (nv_record_diagnostic.pages[selected ^ 1u] != NV_PAGE_VALID &&
        nv_record_diagnostic.pages[selected ^ 1u] != NV_PAGE_EMPTY) {
        nv_record_diagnostic.recovered = 1;
        return NV_RECORD_RECOVERED;
    }
    return NV_RECORD_OK;
}

nv_record_result_t nv_record_load(uint8_t MCU_XDATA * volatile output, uint8_t capacity)
{
    uint8_t i, selected;
    nv_record_result_t result;
    if (nv_record_fault) return (nv_record_result_t)nv_record_fault;
    if (output == NULL || !capacity) return NV_RECORD_INVALID_ARGUMENT;
    if (!caller(output, capacity)) return NV_RECORD_BUFFER_OWNERSHIP;
    begin(); result = select_record();
    if (result != NV_RECORD_OK && result != NV_RECORD_RECOVERED) return finish(result);
    if (capacity < nv_record_diagnostic.length) return finish(NV_RECORD_NO_SPACE);
    selected = nv_record_diagnostic.selected;
    nv_record_diagnostic.phase = 2;
    if (scan_page(selected, 1) != NV_RECORD_OK) return (nv_record_result_t)nv_record_fault;
    if (nv_record_diagnostic.pages[selected] != NV_PAGE_VALID ||
        generations[selected] != nv_record_diagnostic.generation ||
        lengths[selected] != nv_record_diagnostic.length) return finish(NV_RECORD_CORRUPT);
    for (i = 0; i < nv_record_diagnostic.length; i++) output[i] = nv_record_stage[i];
    nv_record_diagnostic.phase = 8;
    return finish(result);
}

static nv_record_result_t program(uint8_t page, uint16_t offset, uint16_t limit)
{
    nv_record_diagnostic.writer = FLASH_WRITE_PENDING;
    nv_record_diagnostic.writer = flash_nv_program(page, offset, nv_record_word, limit);
    if (nv_record_diagnostic.writer != FLASH_WRITE_OK) return fault(NV_RECORD_WRITE_FAILED);
    return NV_RECORD_OK;
}

nv_record_result_t nv_record_replace(const uint8_t MCU_XDATA * volatile data, uint8_t length,
                                    uint16_t poll_limit, uint8_t allow_recovery)
{
    uint16_t position;
    uint8_t i, page, value;
    nv_record_result_t result;
    if (nv_record_fault) return (nv_record_result_t)nv_record_fault;
    if (data == NULL || !length || length > NV_RECORD_MAX || !poll_limit || allow_recovery > 1)
        return NV_RECORD_INVALID_ARGUMENT;
    if (!caller(data, length)) return NV_RECORD_BUFFER_OWNERSHIP;
    begin(); result = select_record();
    if (result == NV_RECORD_RECOVERED && !allow_recovery) return finish(NV_RECORD_RECOVERY_REQUIRED);
    if (result != NV_RECORD_OK && result != NV_RECORD_RECOVERED && result != NV_RECORD_EMPTY)
        return finish(result);
    if (nv_record_diagnostic.generation == 0xffffffffUL) return finish(NV_RECORD_GENERATION_EXHAUSTED);
    page = nv_record_diagnostic.selected == 255 ? 0 : nv_record_diagnostic.selected ^ 1u;
    if (nv_record_diagnostic.erase_attempts[page] >= NV_RECORD_ERASE_LIMIT)
        return finish(NV_RECORD_ERASE_LIMIT_REACHED);
    for (i = 0; i < length; i++) nv_record_stage[i] = data[i];
    for (i = 0; i < 4; i++) header[i] = magic[i];
    header[4] = 1; header[5] = header[7] = 0; header[6] = length;
    encode(header+8, nv_record_diagnostic.generation+1);
    crc = 0xffffffffUL;
    for (position = 0; position < 2040; position++)
        crc_byte(position < 12 ? header[position] :
                 position < 12u+length ? nv_record_stage[position-12] : 255);
    encode(footer, crc ^ 0xffffffffUL);
    nv_record_diagnostic.phase = 3;
    nv_record_diagnostic.erase_attempts[page]++;
    nv_record_diagnostic.writer = FLASH_WRITE_PENDING;
    nv_record_diagnostic.writer = flash_nv_erase(page, poll_limit);
    if (nv_record_diagnostic.writer != FLASH_WRITE_OK) return fault(NV_RECORD_WRITE_FAILED);
    nv_record_diagnostic.phase = 4;
    for (position = 0; position < 12u+length; position += 4) {
        for (i = 0; i < 4; i++) {
            value = position+i < 12 ? header[position+i] :
                position+i < 12u+length ? nv_record_stage[position+i-12] : 255;
            nv_record_word[i] = value;
        }
        if (program(page, position, poll_limit) != NV_RECORD_OK) return (nv_record_result_t)nv_record_fault;
    }
    nv_record_diagnostic.phase = 5;
    for (i = 0; i < 4; i++) nv_record_word[i] = footer[i];
    if (program(page, 2040, poll_limit) != NV_RECORD_OK) return (nv_record_result_t)nv_record_fault;
    nv_record_diagnostic.phase = 6;
    for (i = 0; i < 4; i++) nv_record_word[i] = commit[i];
    if (program(page, 2044, poll_limit) != NV_RECORD_OK) return (nv_record_result_t)nv_record_fault;
    nv_record_diagnostic.phase = 7;
    if (scan_page(page, 0) != NV_RECORD_OK) return (nv_record_result_t)nv_record_fault;
    if (nv_record_diagnostic.pages[page] != NV_PAGE_VALID ||
        generations[page] != nv_record_diagnostic.generation+1 || lengths[page] != length)
        return fault(NV_RECORD_CORRUPT);
    nv_record_diagnostic.selected = page; nv_record_diagnostic.length = length;
    nv_record_diagnostic.generation++; nv_record_diagnostic.phase = 8;
    return finish(nv_record_diagnostic.recovered ? NV_RECORD_RECOVERED : NV_RECORD_OK);
}

const nv_record_status_t MCU_XDATA *nv_record_status(void)
{
    return &nv_record_diagnostic;
}

MCU_XDATA uint8_t nv_record_reserved_end;
