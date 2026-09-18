/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_tx.h"
#include "timebase.h"
#include <stddef.h>

MCU_XDATA uint8_t radio_tx_fault;
extern MCU_XDATA uint8_t radio_tx_reserved_end, _gptrput_PARM_2;
/* SWRU191F pp.256,260-267; SWRS081B Table2 p.24. No guessed power scale. */
static const MCU_CODE uint16_t settings[] = {
    0x618a, 0x6180, 0x6182, 0x61b2, 0x61fa, 0x61ae, 0x618f, 0x6190, 0x6196, 0x6197
};
static const MCU_CODE uint8_t values[] = {0, 0x0c, 0, 0x15, 9, 0, 0, 5, 0xf8, 0x1a};
typedef struct {
    uint32_t start, previous, deadline;
    uint16_t limit;
    uint8_t command, frequency, restoring, started;
} tx_wait_t;

#if defined(__SDCC)
static void cca_settle(void) __naked
{
    /* SWRU191F 23.8.12 p.222: CCA updates four system clocks after RSSI_VALID.
     * Each NOP needs at least one system clock (Table2-3). Not an RF delay model.
     */
    __asm
        nop
        nop
        nop
        nop
        ret
    __endasm;
}
#else
static void cca_settle(void) { host_mmio_system_cycles(4); }
#endif

static radio_tx_result_t observe(radio_tx_diagnostics_t MCU_XDATA *d,
                                  const tx_wait_t MCU_XDATA *w)
{
    uint8_t command, i;
    d->sample_valid = 0;
    if (MMIO_READ(SOC_IEN0) || MMIO_READ(SOC_IEN1) || MMIO_READ(SOC_IEN2) ||
        (MMIO_READ(SOC_SLEEPCMD) & 7u) != 4u) return RADIO_TX_UNSUPPORTED_STATE;
    command = MMIO_READ(SOC_CLKCONCMD);
    if ((command & 0x47u) || MMIO_READ(SOC_CLKCONSTA) != command)
        return RADIO_TX_UNSUPPORTED_STATE;
    if (command != w->command) return RADIO_TX_STATE_CHANGED;
    if (MMIO_XREAD(0x624a) != 0xa5 || MMIO_XREAD(0x61e1) & 0xe0u ||
        MMIO_XREAD(0x6189) != 0x40 || MMIO_XREAD(0x61a3) ||
        MMIO_XREAD(0x61a4) || MMIO_XREAD(0x61a5) ||
        MMIO_XREAD(0x61a8) != 0x85 || MMIO_XREAD(0x61a9) != 0x14 ||
        MMIO_XREAD(0x61b8) != 0x75 || MMIO_XREAD(0x61b9) != 8 ||
        MMIO_XREAD(0x6191) != 0x69 || MMIO_XREAD(0x618e) != 0x0f)
        return RADIO_TX_UNSUPPORTED_STATE;
    for (i = 0; i < d->writes; i++)
        if ((MMIO_XREAD(settings[i]) & (uint8_t)(i == 5 ? 3 : 255)) !=
            (i == 6 ? w->frequency : i == 0 ? w->restoring : values[i]))
            return RADIO_TX_STATE_CHANGED;
    d->rx_enable = MMIO_XREAD(0x618b); d->fsm0 = MMIO_XREAD(0x6192);
    d->signals = MMIO_XREAD(0x6193); d->rssi_valid = MMIO_XREAD(0x6199);
    d->rx_count = MMIO_XREAD(0x619b); d->tx_count = MMIO_XREAD(0x619c);
    d->rx_first = MMIO_XREAD(0x619d); d->rx_last = MMIO_XREAD(0x619e);
    d->rx_packet = MMIO_XREAD(0x619f);
    d->tx_first = MMIO_XREAD(0x61a1); d->tx_last = MMIO_XREAD(0x61a2);
    d->errors = MMIO_READ(SOC_RFERRF);
    d->flags0 = MMIO_READ(SOC_RFIRQF0); d->flags1 = MMIO_READ(SOC_RFIRQF1);
    d->sample_valid = 1;
    if ((d->rx_enable & 0x7f) || (d->fsm0 & 0x80) || (d->rssi_valid & 0xfe) ||
        (d->flags0 & 1) || (d->flags1 & 0xf9) || ((d->rx_first | d->rx_last) & 0x80))
        return RADIO_TX_STATE_CHANGED;
    if (d->errors || (d->signals & 0xc0u) == 0x40u) return RADIO_TX_CONTROLLER_ERROR;
    if (d->rx_count > 128 || d->tx_count > 128) return RADIO_TX_COUNT_ERROR;
    if (w->started && d->txdone && !(d->flags1 & 2)) return RADIO_TX_STATE_CHANGED;
    if (w->started && (d->flags1 & 2)) d->txdone = 1;
    return RADIO_TX_PHY_DONE; /* Observation status, not a public TX result. */
}

static uint8_t idle(const radio_tx_diagnostics_t MCU_XDATA *d)
{
    return !d->rx_enable && !(d->fsm0 & 0x40) && !(d->signals & 0x27);
}
static uint8_t prepared(const radio_tx_diagnostics_t MCU_XDATA *d, uint8_t length)
{
    return !d->rx_count && !d->rx_first && !d->rx_last && !d->rx_packet &&
        !(d->signals & 0xc0) && !d->tx_first &&
        d->tx_count == (length ? length+1u : 0) && d->tx_last == d->tx_count;
}
static radio_tx_result_t poll(tx_wait_t MCU_XDATA *w, radio_tx_diagnostics_t MCU_XDATA *d)
{
    uint32_t now;
    bool expired;
    radio_tx_result_t result;
    if (d->polls == w->limit) return RADIO_TX_POLL_LIMIT;
    result = observe(d, w);
    now = timebase_read_awake_ticks24(); d->polls++;
    d->elapsed_ticks = (now - w->start) & TIMEBASE_TICKS_MASK;
    if (result != RADIO_TX_PHY_DONE) return result;
    d->timebase_status = timebase_expired(now, w->deadline, &expired);
    if (d->timebase_status != TIMEBASE_OK) return RADIO_TX_TIMEBASE_ERROR;
    if (d->elapsed_ticks >= TIMEBASE_HALF_RANGE ||
        ((now - w->previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
        return RADIO_TX_COUNTER_RANGE;
    if (expired) return RADIO_TX_TIMEOUT;
    w->previous = now;
    return RADIO_TX_PHY_DONE;
}

/* Every hardware action below has a prior observation and remaining poll.
 * Failure has no cleanup path: a timeout is not permission to abort RF.
 */
#define POLL() do { result = poll(&w, d); if (result != RADIO_TX_PHY_DONE) goto failed; } while (0)
#define ROOM() do { if (d->polls == limit) { result = RADIO_TX_POLL_LIMIT; goto failed; } } while (0)
#define REQUIRE(test, error) do { if (!(test)) { result = (error); goto failed; } } while (0)
static radio_tx_result_t operate(uint8_t operation, uint8_t channel, uint8_t power,
                                 uint8_t length, uint32_t timeout, uint16_t limit,
                                 radio_tx_diagnostics_t MCU_XDATA *d)
{
    tx_wait_t w;
    radio_tx_result_t result, outcome;
    uint16_t address, helper;
    uint8_t i;
    if (radio_tx_fault) return (radio_tx_result_t)radio_tx_fault;
    if (operation < 1 || operation > 3 || channel < 11 || channel > 26 ||
        power != RADIO_TX_POWER_05 || (operation != 3 && (!length || length > 125)) ||
        !timeout || timeout >= TIMEBASE_HALF_RANGE || !limit || d == NULL)
        return RADIO_TX_INVALID_ARGUMENT;
    address = MMIO_XADDRESS(d); helper = MMIO_XADDRESS(&_gptrput_PARM_2);
    if (address >= 0x1e00 || sizeof(*d) > 0x1e00u-address) return RADIO_TX_INVALID_RANGE;
    if (address <= MMIO_XADDRESS(&radio_tx_reserved_end) ||
        (helper >= address && helper-address < (uint16_t)sizeof(*d))) return RADIO_TX_BUFFER_OWNERSHIP;
    for (i = 0; i < sizeof(*d); i++) ((uint8_t MCU_XDATA *)d)[i] = 0;
    d->cca = 255;
    w.command = MMIO_READ(SOC_CLKCONCMD);
    w.frequency = (uint8_t)(11u + 5u*(channel-11u));
    w.restoring = w.started = 0;
    result = observe(d, &w); if (result != RADIO_TX_PHY_DONE) goto failed;
    REQUIRE(idle(d) && prepared(d, length) && MMIO_XREAD(0x618a) == 1, RADIO_TX_NOT_PREPARED);
    /* SWRU191F 23.4.2 p.214: TXFIFO=6080..60FF; 6000 is RXFIFO. */
    if (length) REQUIRE(MMIO_XREAD(0x6080) == length+2u, RADIO_TX_NOT_PREPARED);
    w.limit = limit; w.start = timebase_read_awake_ticks24(); w.previous = w.start;
    d->timebase_status = timebase_deadline_after(w.start, timeout, &w.deadline);
    REQUIRE(d->timebase_status == TIMEBASE_OK, RADIO_TX_TIMEBASE_ERROR);
    d->phase = 1;
    for (i = 0; i < sizeof(values); i++) {
        POLL(); REQUIRE(idle(d) && prepared(d, length), RADIO_TX_STATE_CHANGED); ROOM();
        MMIO_XWRITE(settings[i], i == 6 ? w.frequency : values[i]); d->writes++;
        POLL(); REQUIRE(idle(d) && prepared(d, length), RADIO_TX_STATE_CHANGED);
        d->verified = d->writes;
    }
    /* R/W0: clear only owned TXDONE, preserve other low-six flags, R0[7:6]=0. */
    ROOM(); MMIO_WRITE(SOC_RFIRQF1, 0x3d); d->actions |= 32;
    POLL(); REQUIRE(!(d->flags1 & 2) && idle(d) && prepared(d, length), RADIO_TX_STATE_CHANGED);
    outcome = RADIO_TX_PHY_DONE;
    if (operation == RADIO_TX_DIRECT) {
        ROOM(); d->phase = 4; w.started = 1;
        MMIO_WRITE(SOC_RFST, 0xe9); d->actions |= 4;
    } else {
        ROOM(); d->phase = 2;
        MMIO_WRITE(SOC_RFST, 0xe3); d->actions |= 1;
        do { POLL(); } while (d->rx_enable != 0x80 || !(d->signals & 4) ||
                             !d->rssi_valid || (d->fsm0 & 0x40));
        cca_settle();
        POLL(); REQUIRE(d->rx_enable == 0x80 && (d->signals & 4) && d->rssi_valid &&
                        !(d->fsm0 & 0x40) && !(d->flags1 & 2), RADIO_TX_STATE_CHANGED);
        ROOM(); d->phase = 3;
        if (operation == 3) {
            MMIO_WRITE(SOC_RFST, 0xeb); d->actions |= 2;
        } else {
            w.started = 1;
            MMIO_WRITE(SOC_RFST, 0xea); d->actions |= 4;
        }
        POLL(); REQUIRE(d->rx_enable == 0x80, RADIO_TX_STATE_CHANGED);
        d->cca = (d->signals & 8) ? 1 : 0;
        if (operation == 3 || !d->cca) {
            REQUIRE(!(d->flags1 & 2) && !(d->signals & 2), RADIO_TX_STATE_CHANGED);
            outcome = d->cca ? RADIO_TX_CCA_CLEAR : RADIO_TX_CCA_BUSY;
        }
        ROOM(); d->phase = 5;
        MMIO_XWRITE(0x618d, 0x80); d->actions |= 8;
    }
    do {
        POLL();
        if (outcome != RADIO_TX_PHY_DONE)
            REQUIRE(!(d->flags1 & 2) && !(d->signals & 2), RADIO_TX_STATE_CHANGED);
    } while (!idle(d) || (outcome == RADIO_TX_PHY_DONE && !d->txdone));
    ROOM(); d->phase = 6; w.restoring = 1;
    MMIO_XWRITE(0x618a, 1); d->actions |= 16;
    POLL(); REQUIRE(idle(d), RADIO_TX_STATE_CHANGED);
    if (outcome != RADIO_TX_PHY_DONE) REQUIRE(!(d->flags1 & 2), RADIO_TX_STATE_CHANGED);
    d->radio_idle = 1; d->phase = 7;
    return outcome;
failed:
    radio_tx_fault = result;
    return result;
}

radio_tx_result_t radio_tx_send_init(radio_tx_mode_t mode, uint8_t channel, uint8_t power,
                                    uint8_t body_length, uint32_t timeout, uint16_t limit,
                                    radio_tx_diagnostics_t MCU_XDATA *diagnostics)
{
    /* Public mode3 is invalid: CCA-only has its own empty-FIFO entry. */
    if (radio_tx_fault) return (radio_tx_result_t)radio_tx_fault;
    if (mode != RADIO_TX_DIRECT && mode != RADIO_TX_IF_CLEAR) return RADIO_TX_INVALID_ARGUMENT;
    return operate(mode, channel, power, body_length, timeout, limit, diagnostics);
}
radio_tx_result_t radio_tx_cca_init(uint8_t channel, uint32_t timeout, uint16_t limit,
                                   radio_tx_diagnostics_t MCU_XDATA *diagnostics)
{
    return operate(3, channel, RADIO_TX_POWER_05, 0, timeout, limit, diagnostics);
}
MCU_XDATA uint8_t radio_tx_reserved_end;
