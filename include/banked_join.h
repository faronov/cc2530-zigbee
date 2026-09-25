/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef BANKED_JOIN_H
#define BANKED_JOIN_H

#if defined(CC2530_BANKED_JOIN) || defined(CC2530_BANKED_MAC)
#include "banked.h"
#define JOIN_FAR __banked
#else
#define JOIN_FAR
#endif

#endif
