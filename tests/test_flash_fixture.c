/* SPDX-License-Identifier: BSD-3-Clause
 * Reuse only the native controller/window model, never target firmware.
 */
#define main flash_write_foundation_main
#include "test_flash_write.c"
#undef main
#include "flash_fixture.h"

extern uint8_t flash_fixture_word[4];
static uint16_t fixture_address(const volatile void *p)
{
    if (p == flash_fixture_word) return 0x400;
    return address(p);
}
static void begin(void)
{
    reset();
    host_mmio_write_hook = NULL;
    assert(_sdcc_external_startup() == 0);
    read_count = write_count = 0;
    host_mmio_write_hook = store;
    host_mmio_xaddress_hook = fixture_address;
    flash_fixture_initialize();
    assert(flash_fixture_state.phase == FF_DISARMED && !events);
}
static void packet(uint8_t command, uint8_t page)
{
    uint8_t token = command == 0xa6 ? 0x3c : 0xc3;
    const uint8_t bytes[8] = {command, (uint8_t)~command, page, (uint8_t)~page,
                             token, (uint8_t)~token, 0x69, 0x96};
    memcpy((void *)flash_fixture_mailbox, bytes, 8);
}
static void poll(void)
{
    if (!setjmp(terminal)) flash_fixture_poll();
    consume();
}
static void frozen(void)
{
    unsigned before = events, i;
    flash_fixture_state_t saved = flash_fixture_state;
    for (i = 0; i < 512; i++) { packet(0xa6, 0); poll(); }
    assert(events == before && !memcmp(&saved, (const void *)&flash_fixture_state, 16));
}
int main(void)
{
    unsigned page, stage, i, bit, failure, cases = 0;
    for (stage = 0; stage < 2; stage++) {
        begin();
        if (stage) { packet(0xa6, 0); poll(); }
        for (i = 0; i < 255; i++) poll();
        assert(flash_fixture_state.phase == (stage ? FF_ARMED : FF_DISARMED));
        poll();
        assert(flash_fixture_state.phase == FF_FAULT && flash_fixture_state.reason == FF_TIMEOUT && !events);
        frozen(); cases++;
    }
    for (stage = 0; stage < 2; stage++)
        for (i = 0; i < 8; i++) for (bit = 0; bit < 8; bit++) {
            begin();
            if (stage) { packet(0xa6, 0); poll(); }
            packet(stage ? 0x59 : 0xa6, 0); flash_fixture_mailbox[i] ^= 1u << bit;
            poll();
            assert(flash_fixture_state.phase == FF_FAULT && flash_fixture_state.reason == FF_PACKET && !events);
            frozen(); cases++;
        }
    for (page = 0; page < 256; page++) {
        begin(); packet(0x59, page); poll();
        assert(flash_fixture_state.reason == FF_PACKET && !events); cases++;
        begin(); packet(0xa6, page); poll();
        if (page >= 2) { assert(flash_fixture_state.reason == FF_PACKET && !events); cases++; continue; }
        packet(0x59, page ^ 1); poll();
        assert(flash_fixture_state.reason == FF_PACKET && !events); cases++;
    }
    for (page = 0; page < 2; page++) {
        begin(); memset(nv, 255, sizeof(nv));
        /* Last permitted observation in each independent arming window. */
        for (i = 0; i < 255; i++) poll();
        packet(0xa6, page); poll();
        for (i = 0; i < 255; i++) poll();
        packet(0x59, page); poll();
        assert(!events && !commands && flash_fixture_state.phase == FF_RUNNING);
        for (i = 0; i < 5; i++) poll();
        assert(flash_fixture_state.phase == FF_END && flash_fixture_state.checks == 3 &&
               commands == 3 && flash_write_known == (1u << page) && m0_status.heartbeat == 1 &&
               !memcmp(nv+page*2048, "\x12\x34\x56\x78", 4) &&
               !memcmp(nv+page*2048+2044, "\x12\x34\x56\x78", 4));
        frozen(); cases++;
        /* Genuine reset clears the mailbox/history, not flash; resume does no command. */
        begin();
        for (i = 0; i < 256; i++) poll();
        assert(!commands && flash_fixture_state.reason == FF_TIMEOUT); cases++;
    }
    for (failure = IGNORED; failure <= ERASE_RESIDUE; failure++) {
        begin(); packet(0xa6, 0); poll(); packet(0x59, 0); poll(); poll();
        if (failure == DROP_PROGRAM || failure == PARTIAL_PROGRAM) poll();
        mode = failure; poll();
        if (failure == STUCK) {
            assert(flash_fixture_state.phase == FF_RUNNING && flash_fixture_state.result == 10 &&
                   flash_write_status.result == 10 && flash_exec_work[7] == 7);
        } else {
            assert(flash_fixture_state.phase == FF_FAULT && flash_fixture_state.reason == FF_SERVICE);
            frozen();
        }
        cases++;
    }
    begin(); packet(0xa6, 0); poll(); packet(0x59, 0); poll();
    flash_write_known = 1; /* Explicit synthetic violation of UNKNOWN precondition. */
    poll();
    assert(flash_fixture_state.reason == FF_HISTORY); frozen(); cases++;
    printf("Flash fixture: %u native handshake/sequence/fault cases PASS (synthetic only)\n", cases);
    return 0;
}
