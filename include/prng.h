/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef PRNG_H
#define PRNG_H

#include "cc2530_mmio.h"

typedef enum {
    PRNG_OK = 0, PRNG_INVALID_SEED, PRNG_INVALID_ARGUMENT, PRNG_INVALID_RANGE,
    PRNG_BUFFER_OWNERSHIP, PRNG_NOT_SEEDED, PRNG_UNSUPPORTED_STATE,
    PRNG_STATE_CHANGED, PRNG_POLL_LIMIT
} prng_result_t;

/* Deterministic hardware LFSR, not entropy or a cryptographic RNG.
 * Foreground/non-reentrant, exclusive PRNG/CRC/CSP, ADC, clock and IRQ ownership.
 * Known awake, stable undivided RC16/XOSC32; IEN0/1/2=0. ADC must be quiescent
 * with no pending single conversion (ST=0 alone cannot prove that history),
 * STSEL=11; no RF/CSP consumer or other RNDL/RNDH/ADCCON1 writer. No sleep/reset
 * or ownership handoff during an epoch. Clock settings may change only
 * between successfully idle calls; they must remain stable within a call.
 * This API does not establish that history, configure clocks/ADC, or mask
 * interrupts, and cannot detect every transient/previous ownership violation.
 *
 * Explicit seed: all uint16_t values except 0000 and 8003. Writes high then
 * low to RNDL, verifies the loaded state, and does not advance it. Explicit
 * reseeding is allowed only after successful idle history, never after fault.
 * Reset's FFFF is not an implicit seed accepted by prng_next16.
 *
 * next16 issues exactly one RCTRL=01 command (13 feedback shifts), observes
 * its documented self-clear within positive limit (1..255) full control
 * polls, then reads the complete 16-bit state. Reads alone do not advance it.
 * This is a poll bound, not calibrated time or entropy. Valid states have
 * period 32767, not 65535; neither state period nor distinct outputs is quality.
 *
 * output: complete writable 2-byte XDATA object below 1E00, after the entire
 * linked prng private prefix (through prng_reserved_end), with no concurrent
 * observer/writer. Link the driver before callers and prove map allocations.
 * Wider external values must be validated before conversion to these types.
 * Output is unchanged on every failure; successful two-byte CPU publication
 * is not atomic. Invalid seed/argument/range/ownership and NOT_SEEDED have
 * no MMIO or retained-state effects (compiler argument scratch is private).
 * Other errors latch the original fault; later calls do no MMIO or caller
 * output/retained-state replacement. A late command may still
 * complete after error: no implicit retry/reseed/stop/restore/clock repair.
 * Only separately established full-reset recovery starts a new fault-free
 * epoch; clearing C storage alone is not recovery.
 */
prng_result_t prng_seed_explicit(uint16_t seed);
prng_result_t prng_next16(uint16_t MCU_XDATA *output, uint8_t limit);

#endif
