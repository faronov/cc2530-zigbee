/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "prng.h"
#include <stddef.h>

MCU_XDATA uint8_t prng_fault, prng_seeded;
static MCU_XDATA uint16_t previous;
static MCU_XDATA uint8_t clock_command, adc_control;
extern MCU_XDATA uint8_t prng_reserved_end;

static uint8_t valid_state(uint16_t value)
{
    return value != 0 && value != 0x8003u;
}

static prng_result_t observe(uint8_t MCU_XDATA *control)
{
    uint8_t ien0, ien1, ien2, sleep, command, status;
    ien0 = MMIO_READ(SOC_IEN0);
    ien1 = MMIO_READ(SOC_IEN1);
    ien2 = MMIO_READ(SOC_IEN2);
    sleep = MMIO_READ(SOC_SLEEPCMD);
    command = MMIO_READ(SOC_CLKCONCMD);
    status = MMIO_READ(SOC_CLKCONSTA);
    if (ien0 || ien1 || ien2 || (sleep & 7u) != 4u || command != status ||
        (command & 7u) != ((command & 0x40u) ? 1u : 0u) ||
        ((command & 0x40u) && !(command & 0x38u)))
        return PRNG_UNSUPPORTED_STATE;
    *control = MMIO_READ(SOC_ADCCON1);
    if ((*control & 0x73u) != 0x33u)
        return PRNG_UNSUPPORTED_STATE;
    if (command != clock_command || (*control & 0xf3u) != adc_control)
        return PRNG_STATE_CHANGED;
    return PRNG_OK;
}

static uint16_t read_state(void)
{
    uint8_t low, high;
    low = MMIO_READ(SOC_RNDL);
    high = MMIO_READ(SOC_RNDH);
    return (uint16_t)low | ((uint16_t)high << 8);
}

static prng_result_t entry(void)
{
    uint8_t control;
    prng_result_t result;
    clock_command = MMIO_READ(SOC_CLKCONCMD);
    adc_control = MMIO_READ(SOC_ADCCON1) & 0xf3u;
    result = observe(&control);
    if (result != PRNG_OK) return result;
    return (control & 0x0cu) ? PRNG_UNSUPPORTED_STATE : PRNG_OK;
}

prng_result_t prng_seed_explicit(uint16_t seed)
{
    uint8_t control;
    uint16_t value;
    prng_result_t result;
    if (prng_fault) return (prng_result_t)prng_fault;
    if (!valid_state(seed)) return PRNG_INVALID_SEED;
    result = entry();
    if (result != PRNG_OK) goto failed;
    if (prng_seeded && read_state() != previous) { result = PRNG_STATE_CHANGED; goto failed; }
    /* SWRU191F 14.2.2/14.3 pp.144-145: each RNDL write shifts low to high. */
    MMIO_WRITE(SOC_RNDL, seed >> 8);
    MMIO_WRITE(SOC_RNDL, seed);
    value = read_state();
    result = observe(&control);
    if (result != PRNG_OK) goto failed;
    if ((control & 0x0cu) || value != seed) { result = PRNG_STATE_CHANGED; goto failed; }
    previous = value;
    prng_seeded = 1;
    return PRNG_OK;
failed:
    prng_fault = result;
    return result;
}

prng_result_t prng_next16(uint16_t MCU_XDATA *output, uint8_t limit)
{
    uint16_t address, value;
    uint8_t control, polls;
    prng_result_t result;
    if (prng_fault) return (prng_result_t)prng_fault;
    if (output == NULL || !limit) return PRNG_INVALID_ARGUMENT;
    address = MMIO_XADDRESS(output);
    if (address >= 0x1dffu) return PRNG_INVALID_RANGE;
    if (address <= MMIO_XADDRESS(&prng_reserved_end)) return PRNG_BUFFER_OWNERSHIP;
    if (!prng_seeded) return PRNG_NOT_SEEDED;
    result = entry();
    if (result != PRNG_OK) goto failed;
    value = read_state();
    if (!valid_state(value) || value != previous) { result = PRNG_STATE_CHANGED; goto failed; }
    result = observe(&control);
    if (result != PRNG_OK) goto failed;
    if (control & 0x0cu) { result = PRNG_STATE_CHANGED; goto failed; }
    /* ST is R/W1/H0, EOC is read-only (12.2.10 p.136): never replay ST=1.
     * Preserve STSEL, set reserved low bits11, command one 13-shift update.
     */
    MMIO_WRITE(SOC_ADCCON1, (adc_control & 0x30u) | 7u);
    for (polls = 0; polls < limit; polls++) {
        result = observe(&control);
        if (result != PRNG_OK) goto failed;
        if (!(control & 0x0cu)) break;
        if ((control & 0x0cu) != 4) { result = PRNG_STATE_CHANGED; goto failed; }
    }
    if (polls == limit) { result = PRNG_POLL_LIMIT; goto failed; }
    value = read_state();
    result = observe(&control);
    if (result != PRNG_OK) goto failed;
    if ((control & 0x0cu) || !valid_state(value) || value == previous) {
        result = PRNG_STATE_CHANGED; goto failed;
    }
    previous = value;
    *output = value;
    return PRNG_OK;
failed:
    prng_fault = result;
    return result;
}

MCU_XDATA uint8_t prng_reserved_end;
