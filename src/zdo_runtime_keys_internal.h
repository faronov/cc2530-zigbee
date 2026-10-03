/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZDO_RUNTIME_KEYS_INTERNAL_H
#define ZDO_RUNTIME_KEYS_INTERNAL_H

/* Private to zdo_runtime.c, included after every other header. */
#include "security_keys.h"

#if defined(CC2530_MAC_LINK_DIRECT)
/* DIRECT shares NWK/APS's refresh-before-use key status snapshot. Every read
 * of the private name follows zdo_runtime's own security_keys_status() in the
 * same activation, with no intervening call that can refresh it. */
extern MCU_XDATA security_keys_status_t nwk_aps_keys;
#define keys nwk_aps_keys
#define ZDO_RUNTIME_KEYS_STORAGE extern MCU_XDATA
#else
#define ZDO_RUNTIME_KEYS_STORAGE static MCU_XDATA
#endif

#endif
