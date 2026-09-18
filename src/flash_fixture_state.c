/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash_fixture.h"

volatile MCU_XDATA flash_fixture_state_t flash_fixture_state;
volatile MCU_XDATA uint8_t flash_fixture_mailbox[8];
MCU_XDATA uint8_t flash_fixture_word[4];
static MCU_XDATA uint16_t remaining;
#define state flash_fixture_state

static void budget(void)
{
    state.remaining_low = (uint8_t)remaining;
    state.remaining_high = (uint8_t)(remaining >> 8);
}
static void fault(uint8_t reason)
{
    state.reason = reason;
    state.phase = FF_FAULT;
}
void flash_fixture_initialize(void)
{
    uint8_t i;
    bringup_initialize();
    for (i = 0; i < 8; i++) flash_fixture_mailbox[i] = 0;
    state.signature[0] = 'M'; state.signature[1] = '2';
    state.signature[2] = 'F'; state.signature[3] = 'L';
    state.version = 1; state.size = sizeof(state);
    state.phase = FF_DISARMED; state.reason = FF_NONE;
    state.page = state.result = 255; state.step = state.checks = 0;
    state.guards[0] = 0x69; state.guards[1] = 0x96;
    remaining = FLASH_FIXTURE_POLLS; budget();
    flash_fixture_word[0] = 0x12; flash_fixture_word[1] = 0x34;
    flash_fixture_word[2] = 0x56; flash_fixture_word[3] = 0x78;
}

void flash_fixture_poll(void)
{
    uint8_t i, any, opcode, token;
    if (state.phase == FF_END || state.phase == FF_FAULT) return;
    if (state.phase == FF_DISARMED || state.phase == FF_ARMED) {
        any = 0;
        for (i = 0; i < 8; i++) any |= flash_fixture_mailbox[i];
        if (!any) {
            if (--remaining == 0) fault(FF_TIMEOUT);
            budget();
            return;
        }
        opcode = state.phase == FF_DISARMED ? 0xa6 : 0x59;
        token = state.phase == FF_DISARMED ? 0x3c : 0xc3;
        if (flash_fixture_mailbox[0] != opcode ||
            flash_fixture_mailbox[1] != (uint8_t)~opcode ||
            flash_fixture_mailbox[2] >= FLASH_NV_PAGE_COUNT ||
            flash_fixture_mailbox[3] != (uint8_t)~flash_fixture_mailbox[2] ||
            flash_fixture_mailbox[4] != token || flash_fixture_mailbox[5] != (uint8_t)~token ||
            flash_fixture_mailbox[6] != 0x69 || flash_fixture_mailbox[7] != 0x96 ||
            (state.phase == FF_ARMED && flash_fixture_mailbox[2] != state.page)) {
            fault(FF_PACKET); return;
        }
        state.page = flash_fixture_mailbox[2];
        for (i = 0; i < 8; i++) flash_fixture_mailbox[i] = 0;
        state.phase++;
        remaining = state.phase == FF_ARMED ? FLASH_FIXTURE_POLLS : 0;
        budget();
        return; /* RUN never performs a command in the packet-consumption call. */
    }
    if (state.phase != FF_RUNNING || state.step > 4) { fault(FF_PACKET); return; }
    state.result = FLASH_WRITE_PENDING;
    if (state.step == 1)
        state.result = flash_nv_erase(state.page, FLASH_FIXTURE_COMMAND_POLLS);
    else
        state.result = flash_nv_program(state.page, state.step == 4 ? 2044 : 0,
                                       flash_fixture_word, FLASH_FIXTURE_COMMAND_POLLS);
    if (state.step == 0 || state.step == 3) {
        if (state.result != (state.step == 0 ? FLASH_WRITE_HISTORY_UNKNOWN : FLASH_WRITE_WORD_USED)) {
            fault(FF_HISTORY); return;
        }
        state.checks |= state.step == 0 ? 1 : 2;
    } else if (state.result != FLASH_WRITE_OK) { fault(FF_SERVICE); return; }
    state.step++;
    if (state.step == 5) { bringup_tick(); state.phase = FF_END; }
}
