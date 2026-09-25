/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef BANKED_SECURITY_H
#define BANKED_SECURITY_H

#if defined(CC2530_BANKED_SECURITY) || defined(CC2530_BANKED_JOIN)
#include "banked.h"
#define SECURITY_FAR __banked
#else
#define SECURITY_FAR
#endif

#if defined(CC2530_BANKED_JOIN)
#define WIRE_FAR
#else
#define WIRE_FAR SECURITY_FAR
#endif

#endif
