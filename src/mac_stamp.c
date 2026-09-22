/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_stamp.h"
#include <stddef.h>

static MCU_XDATA mac_epoch_t work;
static MCU_XDATA mac_epoch_stamp_t endpoint, candidate;

mac_epoch_result_t mac_stamp_project(const mac_epoch_t MCU_XDATA *first,
                                     const mac_time_stamp_t MCU_XDATA *last,
                                     const mac_time_stamp_t MCU_XDATA *sample,
                                     mac_epoch_stamp_t MCU_XDATA *output)
{
    mac_epoch_result_t result;
    if (!first || !last || !sample || !output) return MAC_EPOCH_INVALID_ARGUMENT;
    work = *first;
    result = mac_epoch_step(&work, last, &endpoint);
    if (result != MAC_EPOCH_OK) return result;
    work = *first;
    result = mac_epoch_step(&work, sample, &candidate);
    if (result != MAC_EPOCH_OK) return result;
    /* Both sub-arcs must be forward and shorter than half a raw revolution.
     * With the validated whole window, this excludes samples beyond last.
     */
    result = mac_epoch_step(&work, last, &endpoint);
    if (result != MAC_EPOCH_OK) return result;
    output->symbols = candidate.symbols;
    output->fine = candidate.fine;
    return MAC_EPOCH_OK;
}
