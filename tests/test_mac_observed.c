/* SPDX-License-Identifier: BSD-3-Clause */
#include "mac_tx_observed.h"
#define main interval_baseline_main
#include "test_mac_tx_interval.c"
#undef main

static uint16_t observed(void)
{
    CHECK(mac_tx_observed_step(&tx, now, &event, &action) == MAC_TX_OK);
    return 0;
}

static uint16_t observed_cases(void)
{
    uint8_t busy;
    for (variant = 0; variant < 2; variant++) {
        for (fine = 0; fine < 512; fine++) {
            CALL(begin(1, variant ? UINT32_C(0xffffffc0) : 0));
            for (busy = 0; busy < 5; busy++) {
                CALL(draw());
                now = at + 20;
                source(MAC_TX_EVENT_BUSY_INTERVAL);
                event.lower.symbols = at + 8; event.lower.fine = fine;
                event.upper.symbols = at + 10; event.upper.fine = fine;
                CALL(observed());
                CHECK(tx.engine.nb == busy+1 && tx.engine.transmissions == 0 &&
                      tx.engine.tx_end == 0 && tx.engine.frame[2] == 0x5a);
                CHECK(tx.engine.phase == (busy == 4 ? MAC_TX_DONE : MAC_TX_DRAW));
            }
            CHECK(tx.engine.outcome == MAC_TX_CHANNEL_ACCESS);

            CALL(begin(1, variant ? UINT32_C(0xffffffc0) : 0));
            CALL(sent(fine)); CALL(ack_event(fine)); CALL(step());
            source(MAC_TX_EVENT_RETIRED);
            event.upper.symbols = now-1; event.upper.fine = fine;
            CALL(observed());
            CHECK(tx.engine.phase == MAC_TX_DONE && tx.engine.outcome == MAC_TX_ACKED);
            CHECK(tx.engine.ready_at == at+114+(fine != 0));
        }
    }
    CALL(begin(1, 0)); CALL(draw());
    now = at+20; source(MAC_TX_EVENT_BUSY_INTERVAL);
    event.lower.symbols = at+7; event.lower.fine = 511; event.upper.symbols = at+10;
    CALL(observed());
    CHECK(tx.engine.phase == MAC_TX_FAULT && tx.engine.outcome == MAC_TX_ADAPTER_ERROR);
    CHECK(mac_tx_interval_release(&tx) == MAC_TX_STATE);

    CALL(begin(1, 0)); CALL(draw());
    now = at+20; source(MAC_TX_EVENT_BUSY_INTERVAL);
    event.lower.symbols = at+8; event.upper.symbols = at+10;
    event.source.generation++;
    CALL(observed()); CHECK(tx.engine.phase == MAC_TX_RADIO && !tx.engine.nb);
    event.source.generation--;
    for (variant = 0; variant < 4; variant++) {
        event.lower.fine = variant == 0 ? 512 : 0;
        event.upper.fine = variant == 1 ? 512 : 0;
        event.lower.symbols = at + (variant == 2 ? 11 : 8);
        event.source.stamp = at + (variant == 3 ? 9 : 20);
        memcpy(&saved, &tx, sizeof(tx)); memcpy(&saved_action, &action, sizeof(action));
        CHECK(mac_tx_observed_step(&tx, now, &event, &action) == MAC_TX_INVALID);
        CHECK(!memcmp(&saved, &tx, sizeof(tx)) && !memcmp(&saved_action, &action, sizeof(action)));
    }
    CALL(begin(1, 0)); CALL(draw());
    now = at+1; source(MAC_TX_EVENT_CANCEL); CALL(step());
    CHECK(tx.engine.phase == MAC_TX_STOPPING && tx.engine.uncertain);
    now = at+3; source(MAC_TX_EVENT_RETIRED);
    event.upper.symbols = at+2; event.upper.fine = 511; CALL(observed());
    CHECK(tx.engine.phase == MAC_TX_DONE && tx.engine.outcome == MAC_TX_CANCELLED &&
          tx.engine.ready_at == at+3+MAC_TX_ACK_SYMBOLS+12);

    CALL(begin(1, 0)); CALL(sent(3));
    now = at+115; source(MAC_TX_EVENT_RX_CLOSED);
    event.upper = action.through; CALL(step());
    CHECK(tx.engine.retry_pending && tx.engine.phase == MAC_TX_STOPPING);
    source(MAC_TX_EVENT_RETIRED); event.upper.symbols = now; CALL(observed());
    CHECK(tx.engine.phase == MAC_TX_DRAW && tx.engine.retries == 1 && tx.engine.nb == 0);

    CALL(begin(1, 0)); CALL(draw());
    now = at+1; source(MAC_TX_EVENT_CANCEL); CALL(step());
    now = tx.engine.stop_at; source(MAC_TX_EVENT_RETIRED);
    event.upper.symbols = now; CALL(observed());
    CHECK(tx.engine.phase == MAC_TX_FAULT && tx.engine.outcome == MAC_TX_STOP_FAILED);
    CHECK(mac_tx_observed_step(NULL, now, &event, &action) == MAC_TX_INVALID);
    CHECK(mac_tx_observed_step(&tx, now, &event, NULL) == MAC_TX_INVALID);
    CHECK(mac_tx_interval_step(&tx, now, &event, &action) == MAC_TX_INVALID);
    return 0;
}

int main(void)
{
    uint16_t result;
    if (interval_baseline_main()) return 1;
    result = observed_cases();
    if (result) fprintf(stderr, "Observed MAC failure line%u fine%u phase%u\n", result, fine, tx.engine.phase);
    else puts("Observed MAC: bounded busy/retirement, all fine phases, wrap, stale/invalid and cleanup rules PASS.");
    return result != 0;
}
