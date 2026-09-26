/* SPDX-License-Identifier: BSD-3-Clause
 * Host-only interception; every projection still uses the real epoch engine.
 */
#include "mac_attempt.h"
mac_epoch_result_t mac_attempt_projection_step(mac_epoch_t MCU_XDATA *ctx,
    const mac_time_stamp_t MCU_XDATA *raw, mac_epoch_stamp_t MCU_XDATA *output);
#define mac_epoch_step mac_attempt_projection_step
#include "mac_attempt.c"
