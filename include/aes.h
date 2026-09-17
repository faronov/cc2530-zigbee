/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef AES_H
#define AES_H

#include "cc2530_mmio.h"

typedef enum {
    AES_OK = 0, AES_INVALID_ARGUMENT, AES_INVALID_RANGE, AES_BUFFER_OWNERSHIP,
    AES_UNSUPPORTED_STATE, AES_BUSY, AES_PENDING, AES_STATE_CHANGED,
    AES_TIMEOUT, AES_POLL_LIMIT, AES_TIMEBASE_ERROR, AES_COUNTER_RANGE
} aes_result_t;

typedef struct {
    uint32_t elapsed_ticks;
    uint16_t polls;
    uint8_t timebase_status, phase, submitted, input_complete, output_drained, published;
    uint8_t configured, arms, ack_issued, dma_acked, enc_ack_issued, enc_acked;
    uint8_t arm, request, irq, ircon, control, enc_flags;
    uint8_t cfg0_low, cfg0_high, cfg1_low, cfg1_high, sample_valid;
} aes_diagnostics_t;

/* Encrypt exactly one 16-byte block, not an ECB message/security API.
 * Foreground/non-reentrant; exclusive AES, DMA (all channels), clock and ST0
 * ownership; IEN0/1/2=0, awake stable undivided RC16/XOSC32. Before ANY DMA
 * register access, independently establish clear debug DMA_PAUSE or no debug
 * session. History must be full reset/C initialization or only this API's
 * successfully completed/drained operations; idle samples cannot prove it.
 *
 * key/input: 16 readable bytes in unbanked CODE below 8000 or ordinary XDATA
 * below 1E00, non-null; no generic DATA/PDATA pointers. output/diagnostics:
 * writable XDATA below 1E00. All XDATA objects exclude the complete linked
 * AES/timebase private prefix and runtime-helper scratch. Diagnostics must be
 * disjoint from key/input/output. Output must not overlap an XDATA key; input/
 * output overlap is supported because both inputs are staged before DMA.
 * Caller owns complete objects and excludes concurrent observers/writers.
 * Wider external values must be checked BEFORE conversion to these C types.
 *
 * timeout is positive and <800000 raw ticks; limit is positive. One deadline
 * and poll budget cover staging through checked acknowledgment/publication
 * decision, followed by a fixed, non-failing 16-byte CPU copy. Publication is
 * not an atomic bus transaction. No failure path changes caller output.
 * Argument/range/ownership errors also leave diagnostics/MMIO unchanged.
 *
 * Every other failure latches its original result. Later calls do no MMIO,
 * staging/descriptor replacement or diagnostic/output writes. Error is not
 * quiescence: DMA may still change PRIVATE staging after return. Those objects
 * remain owned until genuine full-reset recovery; no reset/abort/wipe API.
 * DMA never retains caller pointers. No software encryption fallback.
 * Hardware key/IV and software copies/spills are NOT securely erased; this
 * is not key management. See docs/ARCHITECTURE.md for retention and evidence.
 *
 * Diagnostics contain only phase/count/status, never secret bytes.
 * phase: 0 entry/staging, 1 key, 2 IV, 3 block, 4 final publication.
 * submitted/input_complete/dma_acked: key=1, IV=2, block=4.
 * sample_valid: bit0 current full SFR sample, bit1 current timed poll.
 * Other fields may retain an earlier sample; zero is not an unread observation.
 * polls counts post-entry observation attempts, including a shortened failure.
 * Completion/drain may be observed on failure; only published means success.
 */
aes_result_t aes128_encrypt_block(const uint8_t *key, const uint8_t *input,
                                  uint8_t MCU_XDATA *output, uint32_t timeout,
                                  uint16_t limit, aes_diagnostics_t MCU_XDATA *diagnostics);

#endif
