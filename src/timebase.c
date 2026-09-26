/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "timebase.h"
#include "cc2530_mmio.h"
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_guard_internal.h"
#endif

#include <stddef.h>

uint32_t timebase_read_awake_ticks24(void)
{
    uint8_t low, middle, high;

    /* SWRU191F 11.1/11.4 pp.129-131: ST0 latches all three bytes.
     * Separate full expressions sequence volatile reads; writes set compare.
     */
    low = MMIO_READ(SOC_ST0);
    middle = MMIO_READ(SOC_ST1);
    high = MMIO_READ(SOC_ST2);
    return (uint32_t)low | ((uint32_t)middle << 8) | ((uint32_t)high << 16);
}

timebase_result_t timebase_deadline_after(uint32_t now, uint32_t delay,
                                          uint32_t *deadline)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, deadline, sizeof(*deadline), 1)) return TIMEBASE_INVALID_ARGUMENT;
#endif
    if (now > TIMEBASE_TICKS_MASK || delay >= TIMEBASE_HALF_RANGE || deadline == NULL)
        return TIMEBASE_INVALID_ARGUMENT;
    *deadline = (now + delay) & TIMEBASE_TICKS_MASK;
    return TIMEBASE_OK;
}

timebase_result_t timebase_expired(uint32_t now, uint32_t deadline, bool *expired)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, expired, sizeof(*expired), 1)) return TIMEBASE_INVALID_ARGUMENT;
#endif
    uint32_t delta;

    if (now > TIMEBASE_TICKS_MASK || deadline > TIMEBASE_TICKS_MASK || expired == NULL)
        return TIMEBASE_INVALID_ARGUMENT;
    delta = (now - deadline) & TIMEBASE_TICKS_MASK;
    if (delta == TIMEBASE_HALF_RANGE)
        return TIMEBASE_AMBIGUOUS;
    *expired = delta < TIMEBASE_HALF_RANGE;
    return TIMEBASE_OK;
}
