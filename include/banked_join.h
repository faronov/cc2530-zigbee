/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef BANKED_JOIN_H
#define BANKED_JOIN_H

#include "cc2530_mmio.h"

#if defined(CC2530_BANKED_JOIN) || defined(CC2530_BANKED_MAC)
#if defined(CC2530_CLANG)
#define JOIN_FAR
#else
#include "banked.h"
#define JOIN_FAR __banked
#endif
#else
#define JOIN_FAR
#endif

#endif
