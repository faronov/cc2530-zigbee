/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_LINK_RAM_INTERNAL_H
#define MAC_LINK_RAM_INTERNAL_H
#include "mac_link_ram.h"
#include "cc2530_mmio.h"
#include <stddef.h>

#if !defined(CC2530_MAC_LINK_RAM)
#error Compact span helpers belong only to the opt-in RAM profile
#endif

/* These parameters are XDATA pointers, not generic pointers. On the target
 * their 16-bit conversion preserves the complete address. uintptr_t is used
 * only for native objects, where truncation to the target address is wrong.
 * Subtraction avoids end-address wrap and native unrelated-pointer ordering.
 */
#if defined(__SDCC)
typedef uint16_t link_ram_address_t;
#else
typedef uintptr_t link_ram_address_t;
#endif

static uint8_t link_ram_span(const void MCU_XDATA *p, uint16_t size)
{
    if (!p || !size) return 0;
#if defined(__SDCC)
    return (uint16_t)p < 0x1e00u && size <= 0x1e00u - (uint16_t)p;
#else
    return (uintptr_t)p <= UINTPTR_MAX - (size - 1u);
#endif
}

static uint8_t link_ram_disjoint(const void MCU_XDATA *a, uint16_t na,
                                 const void MCU_XDATA *b, uint16_t nb)
{
    link_ram_address_t aa = (link_ram_address_t)a, bb = (link_ram_address_t)b;
    if (!link_ram_span(a, na) || !link_ram_span(b, nb)) return 0;
    return aa <= bb ? bb - aa >= na : aa - bb >= nb;
}
#endif
