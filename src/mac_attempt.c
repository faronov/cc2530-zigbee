/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_attempt.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>

#ifndef CC2530_MAC_ATTEMPT
#error mac_attempt requires its explicit co-owned profile
#endif

MCU_XDATA radio_autoack_attempt_t mac_attempt_raw;
static MCU_XDATA mac_attempt_record_t staged;
MCU_XDATA mac_epoch_t mac_attempt_first;
MCU_XDATA uint16_t mac_attempt_slot;
MCU_XDATA uint8_t mac_attempt_fault;
static MCU_XDATA uint8_t length;
static MCU_XDATA mac_radio_diagnostics_t failed_status;
static mac_attempt_record_t MCU_XDATA * MCU_XDATA receipt_output;
static MCU_XDATA uint8_t last_result;
static MCU_XDATA uint16_t duration;
extern MCU_XDATA uint8_t mac_attempt_reserved_end, _gptrput_PARM_2, __memcpy_PARM_2[3];

static mac_radio_result_t bounds(uint32_t timeout, uint16_t limit)
{
    if (mac_attempt_fault) return (mac_radio_result_t)mac_attempt_fault;
    if (mac_radio_diagnostic()->fault) return (mac_radio_result_t)mac_radio_diagnostic()->fault;
    return timeout && timeout < TIMEBASE_HALF_RANGE && limit ? MAC_RADIO_READY : MAC_RADIO_INVALID_ARGUMENT;
}

static mac_radio_result_t storage(uint16_t address, uint8_t size)
{
    uint16_t first = MMIO_XADDRESS(__memcpy_PARM_2), last = MMIO_XADDRESS(&_gptrput_PARM_2);
    if (address >= 0x1e00 || size > 0x1e00u-address) return MAC_RADIO_INVALID_RANGE;
    if (address <= MMIO_XADDRESS(&mac_attempt_reserved_end) ||
        (address <= last && (address >= first || first-address < size)))
        return MAC_RADIO_BUFFER_OWNERSHIP;
    return MAC_RADIO_READY;
}

mac_radio_result_t mac_attempt_init(const radio_autoack_config_t MCU_XDATA *configuration,
                                    uint32_t timeout, uint16_t limit)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (!configuration) return MAC_RADIO_INVALID_ARGUMENT;
    result = storage(MMIO_XADDRESS(configuration), sizeof(*configuration));
    if (result != MAC_RADIO_READY) return result;
    return mac_radio_init(configuration, timeout, limit);
}

mac_radio_result_t mac_attempt_prepare(const uint8_t MCU_XDATA *body, uint8_t size,
                                       uint32_t timeout, uint16_t limit)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (!body || !size || size > 125) return MAC_RADIO_INVALID_ARGUMENT;
    if (mac_attempt_slot == 65535u) return MAC_RADIO_STATE;
    result = storage(MMIO_XADDRESS(body), size);
    if (result != MAC_RADIO_READY) return result;
    result = mac_radio_prepare(body, size, timeout, limit);
    if (result == MAC_RADIO_READY) {
        mac_attempt_slot++; length = size; duration = 16u + 2u*size;
    }
    return result;
}

mac_radio_result_t mac_attempt_run(uint16_t window, uint32_t timeout, uint16_t limit,
                                   mac_attempt_record_t MCU_XDATA *output)
{
    mac_radio_result_t result = bounds(timeout, limit);
    uint8_t i;
    if (result != MAC_RADIO_READY) return result;
    if (!output || !window || window > 4096) return MAC_RADIO_INVALID_ARGUMENT;
    result = storage(MMIO_XADDRESS(output), sizeof(*output));
    if (result != MAC_RADIO_READY) return result;
    result = mac_radio_attempt(window, timeout, limit, &mac_attempt_raw, &mac_attempt_first);
    if (result != MAC_RADIO_FRAME && result != MAC_RADIO_BAD_CRC &&
        result != MAC_RADIO_EMPTY && result != MAC_RADIO_CCA_BUSY) return result;
    receipt_output = output; last_result = result;
    memset(&staged, 0, sizeof(staged));
#define PROJECT(raw, output) do { \
    if (mac_epoch_step(&mac_attempt_first, &(raw), &(output)) != MAC_EPOCH_OK) { \
        mac_attempt_fault = MAC_RADIO_EPOCH_ERROR; \
        failed_status = *mac_radio_diagnostic(); \
        failed_status.fault = failed_status.result = MAC_RADIO_EPOCH_ERROR; \
        failed_status.phase = MAC_RADIO_FAULT; \
        return MAC_RADIO_EPOCH_ERROR; \
    } \
} while (0)
    PROJECT(mac_attempt_raw.before, staged.tx_lower);
    PROJECT(mac_attempt_raw.armed, staged.armed);
    if (mac_attempt_raw.transmitted) {
        staged.tx_lower.symbols += duration;
        PROJECT(mac_attempt_raw.tx, staged.tx_upper);
    } else memset(&staged.tx_lower, 0, sizeof(staged.tx_lower));
    if (mac_attempt_raw.received) {
        PROJECT(mac_attempt_raw.rx, staged.rx_upper);
        staged.frame.length = mac_attempt_raw.frame.length;
        staged.frame.rssi_raw = mac_attempt_raw.frame.rssi_raw;
        staged.frame.crc_correlation = mac_attempt_raw.frame.crc_correlation;
        for (i = 0; i < staged.frame.length; i++)
            staged.frame.body[i] = mac_attempt_raw.frame.body[i];
    }
    PROJECT(mac_attempt_raw.last, staged.last);
#undef PROJECT
    staged.slot = mac_attempt_slot; staged.length = length;
    staged.transmitted = mac_attempt_raw.transmitted;
    staged.received = mac_attempt_raw.received;
    staged.within_window = mac_attempt_raw.within_window;
    *receipt_output = staged;
    return (mac_radio_result_t)last_result;
}

mac_radio_result_t mac_attempt_receive(uint32_t timeout, uint16_t limit,
                                       radio_autoack_frame_t MCU_XDATA *output)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (!output) return MAC_RADIO_INVALID_ARGUMENT;
    result = storage(MMIO_XADDRESS(output), sizeof(*output));
    if (result != MAC_RADIO_READY) return result;
    return mac_radio_receive(timeout, limit, output);
}
mac_radio_result_t mac_attempt_stop(uint32_t timeout, uint16_t limit)
{
    mac_radio_result_t result = bounds(timeout, limit);
    return result == MAC_RADIO_READY ? mac_radio_stop(timeout, limit) : result;
}
mac_radio_result_t mac_attempt_resume(uint32_t timeout, uint16_t limit)
{
    mac_radio_result_t result = bounds(timeout, limit);
    return result == MAC_RADIO_READY ? mac_radio_resume(timeout, limit) : result;
}
const mac_radio_diagnostics_t MCU_XDATA *mac_attempt_diagnostic(void)
{
    if (mac_attempt_fault) return &failed_status;
    return mac_radio_diagnostic();
}
#if defined(CC2530_MAC_HANDOFF)
mac_radio_result_t mac_attempt_now(uint32_t timeout, uint16_t limit,
                                   mac_epoch_stamp_t MCU_XDATA *output)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (!output) return MAC_RADIO_INVALID_ARGUMENT;
    result = storage(MMIO_XADDRESS(output), sizeof(*output));
    if (result != MAC_RADIO_READY) return result;
    return mac_radio_attempt_now(timeout, limit, output);
}

mac_radio_result_t mac_attempt_handoff(uint32_t timeout, uint16_t limit)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (last_result != MAC_RADIO_FRAME || staged.slot != mac_attempt_slot ||
        !staged.transmitted || !staged.received || !staged.within_window)
        return MAC_RADIO_STATE;
    return mac_radio_handoff(timeout, limit);
}
#endif
MCU_XDATA uint8_t mac_attempt_reserved_end;
