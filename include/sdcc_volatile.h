/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef SDCC_VOLATILE_H
#define SDCC_VOLATILE_H

/* Preserve SDCC's spill-avoidance qualifier for ordinary staging state.
 * Hardware/MMIO and secure-wipe accesses keep plain volatile qualifiers.
 */
#if defined(__SDCC)
#define SDCC_VOLATILE volatile
#else
#define SDCC_VOLATILE
#endif

#endif
