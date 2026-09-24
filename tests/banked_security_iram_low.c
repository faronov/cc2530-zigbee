/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#if !defined(__SDCC) || !defined(CC2530_BANKED_SECURITY)
#error Only the checked banked security profile owns this reservation
#endif
__data unsigned char banked_security_iram_low[22];
