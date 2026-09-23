/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef SECURITY_AES_MODEL_H
#define SECURITY_AES_MODEL_H

#include <stdint.h>

void security_aes_reset(void);
void security_aes_stall(unsigned block);
unsigned security_aes_blocks(void);
void security_aes_trace(uint8_t enabled);

#endif
