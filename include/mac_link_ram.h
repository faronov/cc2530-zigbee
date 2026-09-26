/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_LINK_RAM_H
#define MAC_LINK_RAM_H

/* Experimental composition only, not a default or a complete MCU image.
 * The existing serialized/disjoint-object foreground contracts still apply.
 * No ISR, callback, recursive entry, stack-auto or linker scratch alias.
 * Every translation unit and caller must select the same profile. The BDB
 * phase union has a different extent; do not mix it with exact/noncompact
 * contexts or provide the old mac_join_staged linker binding.
 *
 * Join/POLL use typed returning views of their real caller control. Admission
 * errors preserve all caller bytes; processed failures are committed state.
 * Returning work is wiped and views are dropped on each public return. An
 * occupied/faulted context is never scratch for another call or another layer.
 */
#if defined(CC2530_MAC_LINK_RAM)
#if !defined(CC2530_MAC_LINK)
#error CC2530_MAC_LINK_RAM requires CC2530_MAC_LINK
#endif
#if defined(__SDCC) && (!defined(CC2530_BANKED_JOIN) || !defined(CC2530_JOIN_WORKSPACE))
#error The target RAM profile requires the banked join workspace profile
#endif
#if defined(CC2530_HOST_TEST)
/* Read-only test observations, absent from target CODE and its public ABI. */
unsigned char mac_link_ram_join_clean(void);
unsigned char mac_link_ram_poll_clean(void);
#endif
#endif
#endif
