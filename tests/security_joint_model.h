/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef SECURITY_JOINT_MODEL_H
#define SECURITY_JOINT_MODEL_H
#include <stdint.h>
#include <setjmp.h>

/* Host only; reset models a complete power cycle, never an operation handoff.
 * erase=1 explicitly supplies synthetic virgin NV, never production recovery.
 * The returned 4096 bytes may be copied for test snapshots/corruption.
 */
void security_joint_reset(uint8_t erase);
uint8_t *security_joint_nv(void);
unsigned security_joint_flash_commands(void);
unsigned security_joint_flash_erases(uint8_t page);
unsigned security_joint_aes_blocks(void);
void security_joint_trace(uint8_t enabled);
/* Absolute command number in current power epoch; kind1 before command,
 * kind2 after completion, kind3 torn (bits completed). env must remain live.
 * NULL disables injection. longjmp(*env,1) is power cut, 2 RAM fail-stop.
 */
void security_joint_cut(jmp_buf *env, unsigned command, uint8_t kind, unsigned bits);
void security_joint_stall_flash(unsigned command);
void security_joint_stall_aes(unsigned block);
void security_joint_fail_read(unsigned relative_read);
#endif
