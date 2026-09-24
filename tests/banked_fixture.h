/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef BANKED_FIXTURE_H
#define BANKED_FIXTURE_H

#include "banked.h"

extern volatile __xdata __at(0x1e00) uint8_t banked_fixture_status[64];
extern const __code uint8_t banked_fixture_const1[4];
extern const __code uint8_t banked_fixture_const2[4];
extern const __code uint8_t banked_fixture_const7[8];

typedef uint32_t (*banked_fixture_pointer_t)(uint32_t value) __banked;
extern banked_fixture_pointer_t __xdata banked_fixture_pointer;

uint16_t banked_fixture_word(uint16_t value, uint16_t salt) __banked;
uint32_t banked_fixture_bank1(uint32_t value, uint16_t salt) __banked;
uint32_t banked_fixture_bank2(uint32_t value, uint16_t salt) __banked;
uint32_t banked_fixture_bank7(uint32_t value, uint16_t salt) __banked;
uint32_t banked_fixture_pointer_leaf(uint32_t value) __banked;
uint32_t banked_fixture_irq_leaf(uint32_t value) __banked;
uint32_t banked_fixture_irq_foreground(uint32_t value) __banked;
uint8_t banked_fixture_flash(void) __banked;

/* Common functions, intentionally not declared __banked. */
uint32_t banked_fixture_common(uint32_t value);
uint8_t banked_fixture_flash_common(void);

#endif
