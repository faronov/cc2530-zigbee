/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_LINK_H
#define MAC_LINK_H

/* Opt-in interval-aware consumer profile (#40). Scan, POLL, association and
 * NWK/APS consumers own an observed interval transmitter instead of the exact
 * captured-event engine. No live sample or interval point is ever stored in a
 * field documented as a captured PHY end. Consumer deadlines remain uint32
 * 16-us symbols: a physical lower bound is floored and an upper bound is
 * rounded upward, so every projected window is conservative in the direction
 * required by its own rule. Without CC2530_MAC_LINK nothing here is defined.
 */
#if defined(CC2530_MAC_LINK)
#if !defined(CC2530_MAC_OBSERVED)
#error The interval consumer profile requires the observed interval MAC owner
#endif
#include "mac_tx_observed.h"

#define MAC_LINK_FLOOR(point) ((uint32_t)(point).symbols)
#define MAC_LINK_CEIL(point) ((uint32_t)((point).symbols + ((point).fine != 0u)))
#endif
#endif
