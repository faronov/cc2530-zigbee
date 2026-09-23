/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * A second object keeps the bit-addressable bytes20..21 out of this pool.
 */
#if !defined(__SDCC) || !defined(CC2530_SECURITY_RESIDENT)
#error This reservation belongs only to the checked resident SDCC profile
#endif
__data unsigned char security_iram_high[37];
