/* SPDX-License-Identifier: BSD-3-Clause
 * Synthetic interval events, never a radio or board fixture.
 */
#include "mac_tx_interval.h"
#include <string.h>

#define CHECK(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)
#define CALL(c) do { uint16_t line = (c); if (line) return line; } while (0)

static const uint8_t golden[] = {
    0x61, 0x98, 0xa5, 0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xaa, 0x55, 0xcc
};
static mac_tx_interval_t tx, saved;
static mac_tx_interval_event_t event;
static mac_tx_interval_action_t action, saved_action;
static uint8_t body[125], copy[125], ack[3], length;
static uint32_t now, start, at;
static uint16_t fine, variant, fcf, first, end, index;
uint8_t mac_tx_interval_case;

static uint32_t ceiling(const mac_epoch_stamp_t *point)
{
    return point->symbols + (point->fine != 0u);
}

static void source(uint8_t kind)
{
    memset(&event, 0, sizeof(event));
    event.source.kind = kind;
    event.source.generation = tx.engine.generation;
    event.source.retry = tx.engine.retries;
    event.source.nb = tx.engine.nb;
    event.source.stamp = now;
}

static uint16_t step(void)
{
    CHECK(mac_tx_interval_step(&tx, now, &event, &action) == MAC_TX_OK);
    return 0;
}

static uint16_t poll(void)
{
    CHECK(mac_tx_interval_step(&tx, now, NULL, &action) == MAC_TX_OK);
    return 0;
}

static uint16_t begin(uint8_t requested, uint32_t epoch)
{
    start = now = epoch;
    CHECK(mac_tx_interval_init(&tx, 0x5a, now) == MAC_TX_OK);
    memcpy(body, golden, sizeof(golden));
    if (!requested)
        body[0] &= (uint8_t)~MAC_FLAG_ACK_REQUEST;
    CHECK(mac_tx_interval_submit(&tx, body, sizeof(golden), now, 100000, 1000) == MAC_TX_OK);
    CHECK(mac_tx_interval_copy(&tx, copy, sizeof(copy), &length) == MAC_TX_OK);
    CHECK(length == sizeof(golden) && copy[2] == 0x5a && body[2] == 0xa5);
    return 0;
}

static uint16_t draw(void)
{
    CALL(poll());
    CHECK(action.control.kind == MAC_TX_ACTION_RANDOM);
    source(MAC_TX_EVENT_RANDOM);
    CALL(step());
    CHECK(action.control.kind == MAC_TX_ACTION_ATTEMPT);
    at = action.control.at;
    return 0;
}

static uint16_t sent(uint16_t phase)
{
    CALL(draw());
    now = at + 62;
    source(MAC_TX_EVENT_SENT_INTERVAL);
    event.lower.symbols = at + 48;
    event.lower.fine = phase;
    event.upper.symbols = at + 60;
    event.upper.fine = phase;
    CALL(step());
    CHECK(tx.engine.transmissions == 1 && tx.engine.tx_end == 0);
    CHECK(tx.tx_lower.symbols == at + 48 && tx.tx_lower.fine == phase
        && tx.tx_upper.symbols == at + 60 && tx.tx_upper.fine == phase);
    CHECK(action.through.symbols == at + 114 && action.through.fine == phase);
    return 0;
}

static uint16_t ack_event(uint16_t phase)
{
    now = at + 104;
    source(MAC_TX_EVENT_ACK_INTERVAL);
    event.lower = tx.tx_lower;
    event.upper.symbols = at + 102;
    event.upper.fine = phase;
    ack[0] = 0x12; ack[1] = 0; ack[2] = 0x5a;
    event.source.bytes = ack;
    event.source.length = 3;
    return 0;
}

static uint16_t quiet(void)
{
    CHECK(tx.engine.phase == MAC_TX_STOPPING);
    CHECK(action.control.kind == MAC_TX_ACTION_QUIESCE);
    source(MAC_TX_EVENT_QUIESCED);
    CALL(step());
    CHECK(tx.engine.phase == MAC_TX_DONE || tx.engine.phase == MAC_TX_DRAW);
    return 0;
}

static uint16_t boundaries(void)
{
    for (variant = 0; variant < 2; variant++) {
        for (fine = first; fine < end; fine++) {
            CALL(begin(1, variant ? UINT32_C(0xffffffc0) : 0));
            CALL(sent(fine));
            CHECK(action.control.kind == MAC_TX_ACTION_COLLECT);
            CALL(ack_event(fine));
            CALL(step());
            CHECK(tx.engine.outcome == MAC_TX_ACKED && tx.engine.pending);
            CHECK(tx.engine.ready_at == at + 114 + (fine != 0u));
            CALL(quiet());
            CHECK(mac_tx_interval_release(&tx) == MAC_TX_OK);
            CHECK(mac_tx_interval_submit(&tx, body, sizeof(golden), now, 100000, 1000) == MAC_TX_OK);
            CHECK(tx.engine.frame[2] == 0x5b && tx.engine.next_dsn == 0x5c);
            CALL(draw());
            CHECK(action.control.at == start + 114 + (fine != 0u));

            CALL(begin(1, variant ? UINT32_C(0xffffffc0) : 0));
            CALL(sent(fine));
            CALL(ack_event(fine));
            if (++event.upper.fine == 512) {
                event.upper.fine = 0;
                event.upper.symbols++;
            }
            CALL(step());
            CHECK(tx.engine.outcome == MAC_TX_TIMING_UNCERTAIN && !tx.engine.retry_pending);
            CALL(quiet());
            CHECK(tx.engine.phase == MAC_TX_DONE);
        }
    }
    return 0;
}

static uint16_t legacy_fcf(void)
{
    for (index = first; index < end; index++) {
        fcf = (uint16_t)(index * 8u + 2u);
        CALL(begin(1, 0)); CALL(sent(511)); CALL(ack_event(511));
        ack[0] = (uint8_t)fcf; ack[1] = (uint8_t)(fcf >> 8);
        CALL(step());
        CHECK(tx.engine.outcome == MAC_TX_ACKED
            && tx.engine.pending == ((fcf & MAC_FLAG_PENDING) != 0));
        CALL(quiet());
    }
    return 0;
}

static uint16_t closure_and_retry(void)
{
    uint8_t retry;
    CALL(begin(1, UINT32_C(0xffffffc0)));
    for (retry = 0; retry < 4; retry++) {
        if (retry == 0) {
            CALL(sent(1));
        } else {
            CALL(draw());
            now = at + 62;
            source(MAC_TX_EVENT_SENT_INTERVAL);
            event.lower.symbols = at + 48; event.lower.fine = 1;
            event.upper.symbols = at + 60; event.upper.fine = 1;
            CALL(step());
        }
        now = at + 120;
        CALL(poll());
        CHECK(tx.engine.phase == MAC_TX_ACK_WAIT && tx.engine.outcome == MAC_TX_OUTCOME_NONE);
        CHECK(action.control.kind == MAC_TX_ACTION_NONE);
        source(MAC_TX_EVENT_RX_CLOSED);
        event.upper = action.through;
        CALL(step());
        CHECK(tx.engine.outcome == MAC_TX_NO_ACK
            && tx.engine.ready_at == at + 127 && tx.engine.frame[2] == 0x5a);
        CHECK(tx.engine.transmissions == retry + 1);
        CALL(quiet());
        CHECK(tx.engine.phase == (retry == 3 ? MAC_TX_DONE : MAC_TX_DRAW));
    }
    CHECK(tx.engine.retries == 3 && tx.engine.outcome == MAC_TX_NO_ACK);

    CALL(begin(1, 0)); CALL(sent(1));
    now = at + 120;
    source(MAC_TX_EVENT_RX_CLOSED);
    event.upper = action.through;
    event.upper.fine = 0;
    CALL(step());
    CHECK(tx.engine.phase == MAC_TX_FAULT && tx.engine.outcome == MAC_TX_ADAPTER_ERROR);
    CHECK(mac_tx_interval_release(&tx) == MAC_TX_STATE);

    CALL(begin(1, 0)); CALL(sent(17)); CALL(ack_event(17));
    ack[2]++;
    CALL(step());
    CHECK(tx.engine.outcome == MAC_TX_NO_ACK && tx.engine.ready_at == at + 127);
    CALL(quiet());
    CHECK(tx.engine.retries == 1 && tx.engine.phase == MAC_TX_DRAW);
    return 0;
}

static uint16_t failure_cases(void)
{
    for (variant = 0; variant < 13; variant++) {
        CALL(begin(1, 0)); CALL(sent(1));
        now = at + 120;
        source(MAC_TX_EVENT_RX_CLOSED);
        event.upper = action.through;
        switch (variant) {
        case 0: event.source.generation++; break;
        case 1: event.source.retry++; break;
        case 2: event.source.nb++; break;
        case 3: event.source.stamp = now + 1; break;
        case 4: event.source.stamp = 0; event.upper.symbols = event.upper.fine = 0; break;
        case 5: CALL(ack_event(1)); event.source.length = 2; break;
        case 6: CALL(ack_event(1)); ack[0] = 1; break;
        case 7:
            CALL(ack_event(1)); now = event.source.stamp = at + 120;
            event.lower.symbols = event.upper.symbols = at + 115;
            break;
        case 8: source(MAC_TX_EVENT_CANCEL); break;
        case 9: source(MAC_TX_EVENT_FAILURE); break;
        case 10: now = start + 100000; event.source.stamp = now; break;
        case 11: now--; now -= MAC_TX_HALF; source(MAC_TX_EVENT_CANCEL); break;
        default:
            CALL(ack_event(1)); event.upper = event.lower;
            break;
        }
        CALL(step());
        if (variant < 8)
            CHECK(tx.engine.phase == MAC_TX_ACK_WAIT && tx.engine.outcome == MAC_TX_OUTCOME_NONE);
        else if (variant == 8 || variant == 10) {
            CHECK(tx.engine.outcome == (variant == 8 ? MAC_TX_CANCELLED : MAC_TX_LIFETIME));
            CALL(quiet());
        } else
            CHECK(tx.engine.phase == MAC_TX_FAULT);
    }
    CALL(begin(0, 0)); CALL(sent(511));
    CHECK(tx.engine.outcome == MAC_TX_UNACKNOWLEDGED && tx.engine.ready_at == at + 73);
    CALL(quiet());
    CALL(begin(1, 0)); CALL(sent(511)); CALL(ack_event(511)); CALL(step());
    now += MAC_TX_STOP_SYMBOLS;
    source(MAC_TX_EVENT_QUIESCED); CALL(step());
    CHECK(tx.engine.phase == MAC_TX_FAULT && tx.engine.outcome == MAC_TX_STOP_FAILED);
    return 0;
}

static uint16_t invalid_and_stale(void)
{
    for (variant = 0; variant < 8; variant++) {
        CALL(begin(1, 0)); CALL(sent(10)); CALL(ack_event(10));
        switch (variant) {
        case 0: event.source.kind = MAC_TX_EVENT_SENT; break;
        case 1: event.source.kind = MAC_TX_EVENT_ACK; break;
        case 2: event.source.kind = 11; break;
        case 3: event.source.bytes = NULL; break;
        case 4: event.lower.fine = 512; break;
        case 5: event.upper.fine = 512; break;
        case 6: event.upper = event.lower; event.upper.symbols--; break;
        default: event.upper.symbols = now; event.upper.fine = 1; break;
        }
        saved = tx;
        memset(&action, 0x69, sizeof(action)); saved_action = action;
        CHECK(mac_tx_interval_step(&tx, now, &event, &action) == MAC_TX_INVALID);
        CHECK(!memcmp(&tx, &saved, sizeof(tx)) && !memcmp(&action, &saved_action, sizeof(action)));
    }
    CALL(begin(1, 0)); CALL(draw());
    now = 70; CALL(poll());
    source(MAC_TX_EVENT_SENT_INTERVAL);
    event.source.stamp = 65;
    event.lower.symbols = 48; event.upper.symbols = 60;
    now = 75; CALL(step());
    CHECK(tx.engine.phase == MAC_TX_RADIO && tx.engine.transmissions == 0);
    event.source.stamp = now; CALL(step());
    CHECK(tx.engine.phase == MAC_TX_ACK_WAIT && tx.engine.transmissions == 1);
    CALL(step());
    CHECK(tx.engine.transmissions == 1 && action.control.kind == MAC_TX_ACTION_NONE);
    CHECK(mac_tx_interval_init(NULL, 0, 0) == MAC_TX_INVALID);
    CHECK(mac_tx_interval_release(NULL) == MAC_TX_INVALID);
    return 0;
}

static uint16_t work_and_admission(void)
{
    CALL(begin(1, 0));
    for (variant = 0; variant < 5; variant++) {
        CALL(draw());
        now = at + 8;
        source(MAC_TX_EVENT_BUSY);
        CALL(step());
        CHECK(tx.engine.transmissions == 0 && tx.engine.nb == variant + 1);
    }
    CHECK(tx.engine.phase == MAC_TX_DONE && tx.engine.outcome == MAC_TX_CHANNEL_ACCESS);
    CALL(begin(1, 0)); CALL(sent(10));
    for (index = 0; index < 998; index++)
        CALL(poll());
    CHECK(tx.engine.phase == MAC_TX_STOPPING && tx.engine.outcome == MAC_TX_WORK_LIMIT);
    CALL(quiet());
    CALL(begin(1, 0)); CALL(draw());
    now = at + 70;
    source(MAC_TX_EVENT_SENT_INTERVAL);
    event.lower.symbols = at + 47; event.lower.fine = 511;
    event.upper.symbols = at + 60;
    CALL(step());
    CHECK(tx.engine.phase == MAC_TX_FAULT && tx.engine.outcome == MAC_TX_ADAPTER_ERROR
        && tx.engine.uncertain && tx.engine.transmissions == 0);
    saved = tx;
    source(MAC_TX_EVENT_QUIESCED); CALL(step());
    CHECK(!memcmp(&tx, &saved, sizeof(tx)));
    CHECK(mac_tx_interval_release(&tx) == MAC_TX_STATE);
    for (variant = 16; variant <= 125; variant++) {
        if (variant > 17 && variant != 125)
            continue;
        now = 0;
        CHECK(mac_tx_interval_init(&tx, 0xff, now) == MAC_TX_OK);
        memset(body, 0, sizeof(body));
        memcpy(body, golden, sizeof(golden));
        CHECK(mac_tx_interval_submit(&tx, body, variant, now, 100000, 1000) == MAC_TX_OK);
        CHECK(tx.engine.next_dsn == 0 && tx.engine.frame[2] == 0xff);
        CALL(draw());
        now = at + 300;
        source(MAC_TX_EVENT_SENT_INTERVAL);
        event.lower.symbols = at + 24u + 2u * variant;
        event.lower.fine = 511;
        event.upper = event.lower;
        CALL(step());
        source(MAC_TX_EVENT_ACK_INTERVAL);
        event.lower = tx.tx_lower;
        event.upper = action.through;
        now = event.source.stamp = ceiling(&event.upper) + 1;
        ack[0] = 2; ack[1] = 0; ack[2] = 0xff;
        event.source.bytes = ack; event.source.length = 3;
        /* Keep report order after the deliberately delayed SENT report. */
        if ((uint32_t)(now - tx.engine.last) >= MAC_TX_HALF)
            now = event.source.stamp = tx.engine.last;
        CALL(step());
        CHECK(tx.engine.outcome == MAC_TX_ACKED
            && tx.engine.ready_at == ceiling(&event.upper) + (variant <= 16 ? 12u : 40u));
        CALL(quiet());
    }
    return 0;
}

static uint16_t run_case(uint8_t selected)
{
    CHECK(selected < 52);
    if (selected < 16) {
        first = (uint16_t)selected * 32u; end = first + 32u;
        return boundaries();
    }
    if (selected < 48) {
        first = (uint16_t)(selected - 16u) * 256u; end = first + 256u;
        return legacy_fcf();
    }
    if (selected == 48) return closure_and_retry();
    if (selected == 49) return failure_cases();
    if (selected == 50) {
        CALL(invalid_and_stale());
        CHECK(ceiling(&tx.tx_upper) == 60);
        return 0;
    }
    return work_and_admission();
}

#if defined(__SDCC)
volatile __xdata __at(0x1e00) uint8_t mac_tx_interval_result[8];
void main(void)
{
    uint16_t result = run_case(mac_tx_interval_case);
    memcpy((void *)mac_tx_interval_result, "MTI1", 4);
    mac_tx_interval_result[4] = 1;
    mac_tx_interval_result[5] = 8;
    mac_tx_interval_result[6] = (uint8_t)result;
    mac_tx_interval_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _mac_tx_interval_done
    _mac_tx_interval_done:
        nop
    __endasm;
    for (;;) {}
}
#else
#include <stdio.h>
int main(void)
{
    uint16_t result = 0;
    for (mac_tx_interval_case = 0; mac_tx_interval_case < 52; mac_tx_interval_case++) {
        result = run_case(mac_tx_interval_case);
        if (result)
            break;
    }
    if (result)
        fprintf(stderr, "MAC interval case%u line%u variant%u fine%u kind%u phase%u now%lu report%lu\n",
                mac_tx_interval_case, result, variant, fine, event.source.kind, tx.engine.phase,
                (unsigned long)now, (unsigned long)event.source.stamp);
    else
        puts("MAC interval: fractional/wrap/FCF/closure/retry/fault cases passed");
    return result != 0;
}
#endif
