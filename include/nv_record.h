/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef NV_RECORD_H
#define NV_RECORD_H

#include "flash_write.h"

#define NV_RECORD_MAX 128u
#define NV_RECORD_ERASE_LIMIT 32u

typedef enum {
    NV_RECORD_OK = 0, NV_RECORD_RECOVERED, NV_RECORD_EMPTY,
    NV_RECORD_INVALID_ARGUMENT, NV_RECORD_BUFFER_OWNERSHIP, NV_RECORD_NO_SPACE,
    NV_RECORD_RECOVERY_REQUIRED, NV_RECORD_CORRUPT, NV_RECORD_CONFLICT,
    NV_RECORD_GENERATION_EXHAUSTED, NV_RECORD_ERASE_LIMIT_REACHED,
    NV_RECORD_READ_FAILED, NV_RECORD_WRITE_FAILED, NV_RECORD_PENDING,
    NV_RECORD_UNSUPPORTED
} nv_record_result_t;

typedef enum {
    NV_PAGE_UNCHECKED = 0, NV_PAGE_EMPTY, NV_PAGE_VALID, NV_PAGE_INCOMPLETE,
    NV_PAGE_FORMAT, NV_PAGE_INTEGRITY, NV_PAGE_UNSUPPORTED
} nv_record_page_state_t;

typedef struct {
    uint32_t generation;
    uint8_t result, selected, length, phase, reader, writer, recovered;
    uint8_t pages[2], erase_attempts[2];
} nv_record_status_t;

/* One opaque snapshot, 1..128 bytes, alternating the two reserved NV pages.
 * Foreground/non-reentrant; inherit all flash reader/writer ownership and
 * genuine reset requirements. No other NV writer may run during this epoch.
 * Link executor, reader, writer, this module, then callers. Caller objects
 * are complete stable ordinary XDATA after nv_record_reserved_end, below1E00.
 * No pointers/leases into private staging escape.
 *
 * Load publishes only a fully rechecked committed record. RECOVERED also
 * publishes data, but explicitly means the other page is damaged/incomplete.
 * Other results leave output unchanged; status gives length and page reasons.
 * Unknown committed versions, conflicting generations and no-valid-record
 * corruption never silently select/initialize data.
 *
 * Replace rescans, stages caller bytes, erases ONLY the inactive page, writes
 * header/body/integrity and programs a distinct commit word LAST. The old
 * record remains intact. Every replacement starts a freshly verified erase,
 * including after reset; no program-history import or append-to-blank guess.
 * allow_recovery is exactly0/1: zero rejects a degraded source before erase;
 * one explicitly permits replacing its damaged inactive page, retaining the
 * recovery indication in the result/status. Neither mode destroys a valid
 * unsupported format, guesses between conflicts or initializes corrupt media.
 * Generation starts1 and never wraps; maximum generation rejects replacement.
 *
 * At most32 erase attempts/page in one runtime epoch, consumed before command
 * entry. These VOLATILE counters bound this epoch only: lifetime wear and
 * pre-reset/interrupted erase history remain UNKNOWN, not recovered estimates.
 * Invalid arguments/ownership do no MMIO or diagnostic mutation. Flash service
 * and post-commit verification failures retain their first cause and block
 * later load/replace calls.
 * RAM fail-stop leaves PENDING at the admitted erase/program phase.
 *
 * CRC32 detects accidental corruption, not authentication. This is NOT a
 * security-counter/key/membership API or physical power-loss/endurance proof.
 * A valid older record after recovery must never authorize counter rollback.
 */
nv_record_result_t nv_record_load(uint8_t MCU_XDATA * volatile output, uint8_t capacity);
nv_record_result_t nv_record_replace(const uint8_t MCU_XDATA * volatile data, uint8_t length,
                                    uint16_t poll_limit, uint8_t allow_recovery);
/* Read-only diagnostic; phase0 means no admitted operation since reset.
 * Phases: scan1, load recheck2, erase3, body4, integrity5, commit6, verify7, done8.
 * selected=FF means no record selected; reader/writer=FF means not invoked.
 */
const nv_record_status_t MCU_XDATA *nv_record_status(void);

#endif
