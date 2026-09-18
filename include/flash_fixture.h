/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef FLASH_FIXTURE_H
#define FLASH_FIXTURE_H
#include "bringup.h"
#include "flash_write.h"

#define FLASH_FIXTURE_POLLS 256u
#define FLASH_FIXTURE_COMMAND_POLLS 65535u
enum { FF_DISARMED = 1, FF_ARMED, FF_RUNNING, FF_END, FF_FAULT };
enum { FF_NONE, FF_TIMEOUT, FF_PACKET, FF_HISTORY, FF_SERVICE };
typedef struct {
    uint8_t signature[4], version, size, phase, reason, page, step, result, checks;
    uint8_t remaining_low, remaining_high, guards[2];
} flash_fixture_state_t;
typedef char flash_fixture_size[(sizeof(flash_fixture_state_t) == 16) ? 1 : -1];
extern volatile MCU_XDATA flash_fixture_state_t flash_fixture_state;
/* Write only while halted at flash_fixture_wait, in DISARMED/ARMED.
 * [opcode, ~opcode, page, ~page, token, ~token, 0x69, 0x96].
 * ARM: opcode A6/token3C; RUN: opcode59/tokenC3, same page.
 * This accident-prevention protocol is NOT authentication or authorization.
 */
extern volatile MCU_XDATA uint8_t flash_fixture_mailbox[8];
void flash_fixture_initialize(void);
void flash_fixture_poll(void);
#endif
