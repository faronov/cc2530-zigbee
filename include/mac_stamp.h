/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_STAMP_H
#define MAC_STAMP_H
#include "mac_epoch.h"

/* Arithmetic only. Project sample into the CLOSED window [first, last],
 * without advancing/rewinding the live epoch. first is a READY epoch snapshot;
 * last and sample are coherent raw tuples from that SAME uninterrupted epoch.
 * Caller independently establishes ownership, coherence and true window
 * duration <0xFFFFFF00 fine increments. Numeric membership cannot detect an
 * old/overwritten capture with the same value, missed wraps or a foreign epoch.
 *
 * Non-reentrant with mac_epoch: one foreground owner; immutable, disjoint,
 * complete persistent ordinary-XDATA inputs/output outside all linked private
 * compiler/runtime scratch. No pointer is retained. No MMIO or radio recovery.
 *
 * OK publishes the exact symbols32/fine16 coordinate, including modulo2^32
 * software wrap. All inputs and error output are unchanged. NULL/bad raw
 * inputs return INVALID_ARGUMENT; a malformed first context INVALID_STATE;
 * a faulted context, ambiguous/reversed window or outside sample TIME_ERROR.
 * Equal endpoints and samples at either endpoint are allowed. Rejection does
 * not fault or reset the caller's live epoch. There is no PHY-end correction,
 * event identity/freshness proof, rounding or permission to feed mac_tx.
 */
mac_epoch_result_t mac_stamp_project(const mac_epoch_t MCU_XDATA *first,
                                     const mac_time_stamp_t MCU_XDATA *last,
                                     const mac_time_stamp_t MCU_XDATA *sample,
                                     mac_epoch_stamp_t MCU_XDATA *output);
#endif
