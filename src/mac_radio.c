/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_radio.h"
#include "clock.h"
#include "timebase.h"
#include <stddef.h>

#ifndef CC2530_MAC_RADIO
#error mac_radio requires the explicit co-owned lower-service profile
#endif

MCU_XDATA uint8_t mac_radio_shared_end;
MCU_XDATA mac_time_stamp_t mac_radio_raw;
MCU_XDATA mac_epoch_t mac_radio_epoch;
MCU_XDATA mac_epoch_stamp_t mac_radio_live;
MCU_XDATA radio_autoack_config_t mac_radio_config;
static MCU_XDATA mac_radio_diagnostics_t status;
static MCU_XDATA clock_diagnostics_t clock_status;
extern MCU_XDATA uint8_t mac_radio_reserved_end, _gptrput_PARM_2;
extern MCU_XDATA uint8_t __memcpy_PARM_2[3];

static mac_radio_result_t storage(uint16_t address, uint8_t size)
{
    uint16_t first = MMIO_XADDRESS(__memcpy_PARM_2);
    uint16_t last = MMIO_XADDRESS(&_gptrput_PARM_2);
    if (address >= 0x1e00 || size > 0x1e00u - address) return MAC_RADIO_INVALID_RANGE;
    if (address <= MMIO_XADDRESS(&mac_radio_reserved_end) ||
        (address <= last && (address >= first || first - address < size)))
        return MAC_RADIO_BUFFER_OWNERSHIP;
    return MAC_RADIO_READY;
}
static mac_radio_result_t bounds(uint32_t timeout, uint16_t limit)
{
    if (status.fault) return (mac_radio_result_t)status.fault;
    return timeout && timeout < TIMEBASE_HALF_RANGE && limit ?
           MAC_RADIO_READY : MAC_RADIO_INVALID_ARGUMENT;
}
static mac_radio_result_t fail(mac_radio_result_t result)
{
    status.result = status.fault = result;
    status.phase = MAC_RADIO_FAULT;
    return result;
}
static mac_radio_result_t sample(uint32_t timeout, uint16_t limit, uint8_t initialize)
{
    status.timer_result = mac_time_read_radio(timeout, limit, &mac_radio_raw);
    if (status.timer_result != MAC_TIME_OK) return fail(MAC_RADIO_TIMER_ERROR);
    if (initialize) {
        status.epoch_result = mac_epoch_start(&mac_radio_epoch, &mac_radio_raw, 0);
        if (status.epoch_result != MAC_EPOCH_OK) return fail(MAC_RADIO_EPOCH_ERROR);
    }
    status.epoch_result = mac_epoch_step(&mac_radio_epoch, &mac_radio_raw, &mac_radio_live);
    if (status.epoch_result != MAC_EPOCH_OK) return fail(MAC_RADIO_EPOCH_ERROR);
    status.last_live.symbols = mac_radio_live.symbols;
    status.last_live.fine = mac_radio_live.fine;
    status.has_time = 1;
    return MAC_RADIO_READY;
}

mac_radio_result_t mac_radio_init(const radio_autoack_config_t MCU_XDATA *configuration,
                                  uint32_t timeout, uint16_t limit)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (!configuration) return MAC_RADIO_INVALID_ARGUMENT;
    if (status.phase != MAC_RADIO_COLD) return MAC_RADIO_STATE;
    result = storage(MMIO_XADDRESS(configuration), sizeof(*configuration));
    if (result != MAC_RADIO_READY) return result;
    if (configuration->channel < 11 || configuration->channel > 26 ||
        configuration->power != RADIO_AUTOACK_POWER_05) return MAC_RADIO_INVALID_ARGUMENT;
    mac_radio_config = *configuration;
    status.phase = MAC_RADIO_STARTING;
    status.radio_result = 255;
    status.clock_result = clock_select_init(CLOCK_XOSC32, timeout, limit, &clock_status);
    if (status.clock_result != CLOCK_OK) return fail(MAC_RADIO_CLOCK_ERROR);
    status.timer_result = mac_time_init(timeout, limit);
    if (status.timer_result != MAC_TIME_OK) return fail(MAC_RADIO_TIMER_ERROR);
    result = sample(timeout, limit, 1);
    if (result != MAC_RADIO_READY) return result;
    status.radio_result = radio_autoack_acquire(&mac_radio_config, timeout, limit);
    if (status.radio_result != RADIO_AUTOACK_READY) return fail(MAC_RADIO_DRIVER_ERROR);
    status.phase = MAC_RADIO_RX;
    result = sample(timeout, limit, 0);
    if (result != MAC_RADIO_READY) return result;
    status.result = MAC_RADIO_READY;
    return MAC_RADIO_READY;
}

mac_radio_result_t mac_radio_now(uint32_t timeout, uint16_t limit,
                                mac_epoch_stamp_t MCU_XDATA *output)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (!output) return MAC_RADIO_INVALID_ARGUMENT;
    if (status.phase < MAC_RADIO_RX || status.phase > MAC_RADIO_OFF) return MAC_RADIO_STATE;
    result = storage(MMIO_XADDRESS(output), sizeof(*output));
    if (result != MAC_RADIO_READY) return result;
    result = sample(timeout, limit, 0);
    if (result != MAC_RADIO_READY) return result;
    output->symbols = mac_radio_live.symbols;
    output->fine = mac_radio_live.fine;
    status.result = MAC_RADIO_READY;
    return MAC_RADIO_READY;
}

static mac_radio_result_t operate(uint8_t operation, const uint8_t MCU_XDATA *body,
    uint8_t length, radio_autoack_frame_t MCU_XDATA *output, uint32_t timeout, uint16_t limit)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if ((operation == 0 && !output) || (operation == 1 && (!body || !length || length > 125)))
        return MAC_RADIO_INVALID_ARGUMENT;
    if ((operation == 0 || operation == 2) ?
        (status.phase != MAC_RADIO_RX && status.phase != MAC_RADIO_DRAINING
#if defined(CC2530_MAC_ATTEMPT)
         && !(operation == 2 && status.phase == MAC_RADIO_PREPARED)
#endif
        ) :
        status.phase != MAC_RADIO_OFF) return MAC_RADIO_STATE;
    if (operation == 0 || operation == 1) {
        result = storage(operation == 0 ? MMIO_XADDRESS(output) : MMIO_XADDRESS(body),
                         operation == 0 ? sizeof(*output) : length);
        if (result != MAC_RADIO_READY) return result;
    }
    result = sample(timeout, limit, 0);
    if (result != MAC_RADIO_READY) return result;
    if (operation == 0) {
        status.radio_result = radio_autoack_receive(timeout, limit, output);
        if (status.radio_result == RADIO_AUTOACK_FRAME) result = MAC_RADIO_FRAME;
        else if (status.radio_result == RADIO_AUTOACK_BAD_CRC) result = MAC_RADIO_BAD_CRC;
        else if (status.radio_result == RADIO_AUTOACK_EMPTY) result = MAC_RADIO_EMPTY;
        else return fail(MAC_RADIO_DRIVER_ERROR);
    } else if (operation == 1) {
        status.radio_result = radio_autoack_send(body, length, timeout, limit);
        if (status.radio_result == RADIO_AUTOACK_TX_DONE) result = MAC_RADIO_TX_DONE;
        else if (status.radio_result == RADIO_AUTOACK_CCA_BUSY) result = MAC_RADIO_CCA_BUSY;
        else return fail(MAC_RADIO_DRIVER_ERROR);
        status.phase = MAC_RADIO_RX;
    } else if (operation == 2) {
        status.radio_result = radio_autoack_stop(timeout, limit);
        if (status.radio_result == RADIO_AUTOACK_STOPPED) {
            status.phase = MAC_RADIO_OFF; result = MAC_RADIO_STOPPED;
        } else if (status.radio_result == RADIO_AUTOACK_DRAIN) {
            status.phase = MAC_RADIO_DRAINING; result = MAC_RADIO_DRAIN;
        } else return fail(MAC_RADIO_DRIVER_ERROR);
    } else {
        status.radio_result = radio_autoack_resume(timeout, limit);
        if (status.radio_result != RADIO_AUTOACK_READY) return fail(MAC_RADIO_DRIVER_ERROR);
        status.phase = MAC_RADIO_RX; result = MAC_RADIO_READY;
    }
    status.result = result;
    return result;
}
mac_radio_result_t mac_radio_receive(uint32_t timeout, uint16_t limit,
                                     radio_autoack_frame_t MCU_XDATA *output)
{
    return operate(0, NULL, 0, output, timeout, limit);
}
mac_radio_result_t mac_radio_send(const uint8_t MCU_XDATA *body, uint8_t length,
                                  uint32_t timeout, uint16_t limit)
{
    return operate(1, body, length, NULL, timeout, limit);
}
mac_radio_result_t mac_radio_stop(uint32_t timeout, uint16_t limit)
{
    return operate(2, NULL, 0, NULL, timeout, limit);
}
mac_radio_result_t mac_radio_resume(uint32_t timeout, uint16_t limit)
{
    return operate(3, NULL, 0, NULL, timeout, limit);
}
const mac_radio_diagnostics_t MCU_XDATA *mac_radio_diagnostic(void) { return &status; }
#if defined(CC2530_MAC_ATTEMPT)
static radio_autoack_attempt_t MCU_XDATA * MCU_XDATA attempt_output;
static mac_epoch_t MCU_XDATA * MCU_XDATA attempt_first;
static mac_radio_result_t attempt_storage(uint16_t a, uint16_t b)
{
    mac_radio_result_t result = storage(a, sizeof(radio_autoack_attempt_t));
    if (result != MAC_RADIO_READY) return result;
    result = storage(b, sizeof(mac_epoch_t));
    if (result != MAC_RADIO_READY) return result;
    if (a <= b ? b-a < (uint16_t)sizeof(radio_autoack_attempt_t) :
        a-b < (uint16_t)sizeof(mac_epoch_t)) return MAC_RADIO_BUFFER_OWNERSHIP;
    return MAC_RADIO_READY;
}
mac_radio_result_t mac_radio_prepare(const uint8_t MCU_XDATA *body,
                                             uint8_t length, uint32_t timeout, uint16_t limit)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (!body || !length || length > 125) return MAC_RADIO_INVALID_ARGUMENT;
    if (status.phase != MAC_RADIO_OFF) return MAC_RADIO_STATE;
    result = storage(MMIO_XADDRESS(body), length);
    if (result != MAC_RADIO_READY) return result;
    result = sample(timeout, limit, 0);
    if (result != MAC_RADIO_READY) return result;
    status.radio_result = radio_autoack_prepare(body, length, timeout, limit);
    if (status.radio_result != RADIO_AUTOACK_READY) return fail(MAC_RADIO_DRIVER_ERROR);
    status.phase = MAC_RADIO_PREPARED; status.result = MAC_RADIO_READY;
    return MAC_RADIO_READY;
}

mac_radio_result_t mac_radio_attempt(uint16_t window, uint32_t timeout, uint16_t limit,
    radio_autoack_attempt_t MCU_XDATA *output, mac_epoch_t MCU_XDATA *first)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (!output || !first || !window || window > 4096) return MAC_RADIO_INVALID_ARGUMENT;
    if (status.phase != MAC_RADIO_PREPARED) return MAC_RADIO_STATE;
    result = attempt_storage(MMIO_XADDRESS(output), MMIO_XADDRESS(first));
    if (result != MAC_RADIO_READY) return result;
    attempt_output = output; attempt_first = first;
    result = sample(timeout, limit, 0);
    if (result != MAC_RADIO_READY) return result;
    *attempt_first = mac_radio_epoch;
    status.radio_result = radio_autoack_attempt(window, timeout, limit, attempt_output);
    if (status.radio_result == RADIO_AUTOACK_FRAME) result = MAC_RADIO_FRAME;
    else if (status.radio_result == RADIO_AUTOACK_BAD_CRC) result = MAC_RADIO_BAD_CRC;
    else if (status.radio_result == RADIO_AUTOACK_EMPTY) result = MAC_RADIO_EMPTY;
    else if (status.radio_result == RADIO_AUTOACK_CCA_BUSY) result = MAC_RADIO_CCA_BUSY;
    else return fail(MAC_RADIO_DRIVER_ERROR);
    status.epoch_result = mac_epoch_step(&mac_radio_epoch, &attempt_output->last, &mac_radio_live);
    if (status.epoch_result != MAC_EPOCH_OK) return fail(MAC_RADIO_EPOCH_ERROR);
    status.last_live = mac_radio_live;
    status.phase = MAC_RADIO_RX; status.result = result;
    return result;
}
#endif
#if defined(CC2530_MAC_HANDOFF)
MCU_XDATA radio_autoack_handoff_clock_t mac_radio_handoff_clock;
mac_radio_result_t mac_radio_attempt_now(uint32_t timeout, uint16_t limit,
                                        mac_epoch_stamp_t MCU_XDATA *output)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (!output) return MAC_RADIO_INVALID_ARGUMENT;
    if ((status.phase < MAC_RADIO_RX || status.phase > MAC_RADIO_OFF) &&
        status.phase != MAC_RADIO_PREPARED) return MAC_RADIO_STATE;
    result = storage(MMIO_XADDRESS(output), sizeof(*output));
    if (result != MAC_RADIO_READY) return result;
    result = sample(timeout, limit, 0);
    if (result != MAC_RADIO_READY) return result;
    *output = mac_radio_live;
    status.result = MAC_RADIO_READY;
    return MAC_RADIO_READY;
}

mac_radio_result_t mac_radio_handoff(uint32_t timeout, uint16_t limit)
{
    mac_radio_result_t result = bounds(timeout, limit);
    if (result != MAC_RADIO_READY) return result;
    if (status.phase != MAC_RADIO_RX || !radio_autoack_handoff_eligible())
        return MAC_RADIO_STATE;
    result = sample(timeout, limit, 0);
    if (result != MAC_RADIO_READY) return result;
    status.radio_result = radio_autoack_handoff(timeout, limit, &mac_radio_handoff_clock);
    if (status.radio_result != RADIO_AUTOACK_READY) return fail(MAC_RADIO_DRIVER_ERROR);
    result = sample(timeout, limit, 0);
    if (result != MAC_RADIO_READY) return result;
    status.result = MAC_RADIO_READY;
    return MAC_RADIO_READY;
}
#endif
MCU_XDATA uint8_t mac_radio_reserved_end;
