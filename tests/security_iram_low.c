/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Physical reservation for the separately verified resident DATA profile.
 */
#if !defined(__SDCC) || !defined(CC2530_SECURITY_RESIDENT)
#error This reservation belongs only to the checked resident SDCC profile
#endif
__data unsigned char security_iram_low[24];
