/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_time.h"
#include "timebase.h"
#include <stddef.h>

MCU_XDATA uint8_t mac_time_fault, mac_time_ready;
static MCU_XDATA uint8_t saved_clock;
static MCU_XDATA mac_time_diagnostics_t status;
static MCU_XDATA mac_time_stamp_t staged;
static MCU_XDATA struct {
    uint32_t start, previous, deadline;
    uint16_t limit;
    uint8_t control, select, event;
#if defined(CC2530_MAC_RADIO)
    uint8_t radio;
#endif
} work;
extern MCU_XDATA uint8_t mac_time_reserved_end, _gptrput_PARM_2;
#if defined(CC2530_MAC_RADIO)
/* Parent's first XDATA object, linked after ALL lower-service private storage. */
extern MCU_XDATA uint8_t mac_radio_shared_end;
extern MCU_XDATA uint8_t __memcpy_PARM_2[3];
#define PRIVATE_END mac_radio_shared_end
/* Internal selectors: 0=quiescent read, 1=init, 2=co-owned radio read. */
#define INITIALIZING (initialize == 1)
#else
#define PRIVATE_END mac_time_reserved_end
#define INITIALIZING initialize
#endif

static mac_time_result_t observe(void)
{
    uint8_t command;
    if (MMIO_READ(SOC_IEN0) || MMIO_READ(SOC_IEN1) || MMIO_READ(SOC_IEN2) ||
        (MMIO_READ(SOC_SLEEPCMD) & 7u) != 4u ||
        MMIO_READ(SOC_DMAARM) || MMIO_READ(SOC_DMAREQ))
        return MAC_TIME_UNSUPPORTED_STATE;
    command = MMIO_READ(SOC_CLKCONCMD);
    if ((command & 0x47u) || MMIO_READ(SOC_CLKCONSTA) != command)
        return MAC_TIME_UNSUPPORTED_STATE;
    if (command != saved_clock) return MAC_TIME_STATE_CHANGED;
    if (MMIO_XREAD(0x624a) != 0xa5 || MMIO_XREAD(0x61e1) ||
#if defined(CC2530_MAC_RADIO)
        /* SWRU191F pp260,262-263: only the co-owned reader permits RX requests,
         * CAL_RUNNING, PLL/SFD and RX/TX_ACTIVE. Reserved FSMSTAT0.7 stays zero.
         * This is NOT evidence of owner history; the caller must know it.
         */
        (!work.radio && MMIO_XREAD(0x618b)) ||
        (MMIO_XREAD(0x6192) & (work.radio ? 0x80u : 0xc0u)) ||
        (!work.radio && (MMIO_XREAD(0x6193) & 0x27u)) || MMIO_READ(SOC_RFERRF) ||
#else
        MMIO_XREAD(0x618b) || (MMIO_XREAD(0x6192) & 0xc0u) ||
        (MMIO_XREAD(0x6193) & 0x27u) || MMIO_READ(SOC_RFERRF) ||
#endif
        MMIO_XREAD(0x61a3) || MMIO_XREAD(0x61a4) || MMIO_XREAD(0x61a5) ||
        MMIO_READ(SOC_T2IRQM))
        return MAC_TIME_UNSUPPORTED_STATE;
    status.control = MMIO_READ(SOC_T2CTRL);
    status.select = MMIO_READ(SOC_T2MSEL);
    status.irq_flags = MMIO_READ(SOC_T2IRQF);
    if ((status.control & 0xfbu) != work.control ||
        (work.control != 9 && (status.control & 4)) ||
        status.select != work.select || (status.irq_flags & 0xc0u) ||
        MMIO_READ(SOC_T2EVTCFG) != work.event)
        return MAC_TIME_STATE_CHANGED;
    return MAC_TIME_OK;
}

static mac_time_result_t poll(void)
{
    uint32_t now;
    bool expired;
    mac_time_result_t result;
    if (status.polls == work.limit) return MAC_TIME_WORK_LIMIT;
    result = observe();
    now = timebase_read_awake_ticks24(); status.polls++;
    status.elapsed_ticks = (now-work.start) & TIMEBASE_TICKS_MASK;
    if (result != MAC_TIME_OK) return result;
    status.timebase_status = timebase_expired(now, work.deadline, &expired);
    if (status.timebase_status != TIMEBASE_OK) return MAC_TIME_TIMEBASE_ERROR;
    if (status.elapsed_ticks >= TIMEBASE_HALF_RANGE ||
        ((now-work.previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
        return MAC_TIME_COUNTER_RANGE;
    if (expired) return MAC_TIME_TIMEOUT;
    work.previous = now;
    return MAC_TIME_OK;
}

#define REQUIRE(condition, error) do { if (!(condition)) { result = (error); goto failed; } } while (0)
#define POLL() do { result = poll(); if (result != MAC_TIME_OK) goto failed; } while (0)
#define ROOM() REQUIRE(status.polls != work.limit, MAC_TIME_WORK_LIMIT)
static mac_time_result_t operate(uint8_t initialize, uint32_t timeout, uint16_t limit,
                                 mac_time_stamp_t MCU_XDATA *output)
{
    mac_time_result_t result;
    uint16_t address, helper;
#if defined(CC2530_MAC_RADIO)
    uint16_t first;
#endif
    uint8_t i, low, high, a, b, c;
    if (mac_time_fault) return (mac_time_result_t)mac_time_fault;
    if (INITIALIZING && mac_time_ready) return MAC_TIME_ALREADY_INITIALIZED;
    if (!INITIALIZING && !mac_time_ready) return MAC_TIME_NOT_INITIALIZED;
    if (!timeout || timeout >= TIMEBASE_HALF_RANGE || !limit ||
        (!INITIALIZING && output == NULL)) return MAC_TIME_INVALID_ARGUMENT;
    if (!INITIALIZING) {
        address = MMIO_XADDRESS(output); helper = MMIO_XADDRESS(&_gptrput_PARM_2);
        if (address >= 0x1e00 || sizeof(*output) > 0x1e00u-address)
            return MAC_TIME_INVALID_RANGE;
#if defined(CC2530_MAC_RADIO)
        /* Exact combined proof binds the complete memcpy/memset/gptr suffix. */
        first = MMIO_XADDRESS(__memcpy_PARM_2);
        if (address <= MMIO_XADDRESS(&PRIVATE_END) ||
            (address <= helper && (address >= first || first-address < (uint16_t)sizeof(*output))))
#else
        if (address <= MMIO_XADDRESS(&PRIVATE_END) ||
            (helper >= address && helper-address < (uint16_t)sizeof(*output)))
#endif
            return MAC_TIME_BUFFER_OWNERSHIP;
    }
#if defined(CC2530_MAC_RADIO)
    /* No rejected argument/state/storage call may install a private mode. */
    work.radio = initialize == 2;
#endif
    for (i = 0; i < sizeof(status); i++) ((uint8_t MCU_XDATA *)&status)[i] = 0;
    status.result = MAC_TIME_PENDING; status.phase = 1;
    if (INITIALIZING) {
        saved_clock = MMIO_READ(SOC_CLKCONCMD);
        work.control = 2; work.select = work.event = 0;
    } else { work.control = 9; work.select = 0; work.event = 0x77; }
    result = observe(); if (result != MAC_TIME_OK) goto failed;
    if (INITIALIZING) {
        REQUIRE(!status.irq_flags, MAC_TIME_STATE_CHANGED);
        /* Stopped, untouched reset counters only. No counter write/adoption. */
        low = MMIO_READ(SOC_T2M0); high = MMIO_READ(SOC_T2M1);
        a = MMIO_READ(SOC_T2MOVF0); b = MMIO_READ(SOC_T2MOVF1); c = MMIO_READ(SOC_T2MOVF2);
        REQUIRE(!(low | high | a | b | c), MAC_TIME_STATE_CHANGED);
    } else REQUIRE(status.control == 13, MAC_TIME_STATE_CHANGED);
    work.limit = limit; work.start = timebase_read_awake_ticks24(); work.previous = work.start;
    status.timebase_status = timebase_deadline_after(work.start, timeout, &work.deadline);
    REQUIRE(status.timebase_status == TIMEBASE_OK, MAC_TIME_TIMEBASE_ERROR);
    POLL();
    if (INITIALIZING) {
        /* SWRU191F pp203-206: CC253x fields, NOT CC2541 long compares. */
        ROOM(); status.phase = 2; work.event = 0x77;
        MMIO_WRITE(SOC_T2EVTCFG, 0x77); POLL();
        ROOM(); status.phase = 3; work.control = 8;
        MMIO_WRITE(SOC_T2CTRL, 8); POLL(); /* LATCH_MODE=1, SYNC=RUN=0 */
        ROOM(); status.phase = 4; work.select = 0x22;
        MMIO_WRITE(SOC_T2MSEL, 0x22); POLL();
        /* Each period write is low-byte-first, overflow commits on byte2.
         * Positive periods avoid any special interpretation of period zero.
         * Values at the period are replaced by zero (SWRU191F p204).
         */
        ROOM();
        MMIO_WRITE(SOC_T2M0, 0); MMIO_WRITE(SOC_T2M1, 2);
        MMIO_WRITE(SOC_T2MOVF0, 255); MMIO_WRITE(SOC_T2MOVF1, 255); MMIO_WRITE(SOC_T2MOVF2, 255);
        low = MMIO_READ(SOC_T2M0); high = MMIO_READ(SOC_T2M1);
        a = MMIO_READ(SOC_T2MOVF0); b = MMIO_READ(SOC_T2MOVF1); c = MMIO_READ(SOC_T2MOVF2);
        REQUIRE(!low && high == 2 && a == 255 && b == 255 && c == 255, MAC_TIME_STATE_CHANGED);
        POLL();
        ROOM(); status.phase = 5; work.select = 0;
        MMIO_WRITE(SOC_T2MSEL, 0); POLL();
        ROOM(); status.phase = 6; work.control = 9;
        MMIO_WRITE(SOC_T2CTRL, 9); /* first start MUST be asynchronous */
        do { POLL(); } while (status.control != 13);
    } else REQUIRE(status.control == 13, MAC_TIME_STATE_CHANGED);
    status.phase = 7;
    for (;;) {
        ROOM();
        /* SWRZ031 1.2: a low FF can be paired with next-cycle upper bytes.
         * Save the destructive read ONCE. Discard the WHOLE sample on FF;
         * a later read must trigger a new complete latch, never reuse bytes.
         */
        low = MMIO_READ(SOC_T2M0);
        if (low == 255) {
            status.discarded++; POLL();
            REQUIRE(status.control == 13, MAC_TIME_STATE_CHANGED);
            continue;
        }
        high = MMIO_READ(SOC_T2M1);
        a = MMIO_READ(SOC_T2MOVF0);
        b = MMIO_READ(SOC_T2MOVF1);
        c = MMIO_READ(SOC_T2MOVF2);
        staged.fine = (uint16_t)low | ((uint16_t)high << 8);
        staged.periods = (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16);
        POLL();
        REQUIRE(status.control == 13, MAC_TIME_STATE_CHANGED);
        REQUIRE(staged.fine < MAC_TIME_FINE_PERIOD && staged.periods < MAC_TIME_OVERFLOW_PERIOD,
                MAC_TIME_COUNT_ERROR);
        break;
    }
    if (INITIALIZING) mac_time_ready = 1;
    else { output->fine = staged.fine; output->periods = staged.periods; }
    status.phase = 8; status.result = MAC_TIME_OK;
    return MAC_TIME_OK;
failed:
    mac_time_fault = result; status.result = result;
    return result;
}
mac_time_result_t mac_time_init(uint32_t timeout, uint16_t poll_limit)
{
    return operate(1, timeout, poll_limit, NULL);
}
mac_time_result_t mac_time_read_live(uint32_t timeout, uint16_t poll_limit,
                                    mac_time_stamp_t MCU_XDATA *output)
{
    return operate(0, timeout, poll_limit, output);
}
#if defined(CC2530_MAC_RADIO)
mac_time_result_t mac_time_read_radio(uint32_t timeout, uint16_t poll_limit,
                                     mac_time_stamp_t MCU_XDATA *output)
{
    return operate(2, timeout, poll_limit, output);
}
#endif
const mac_time_diagnostics_t MCU_XDATA *mac_time_diagnostic(void) { return &status; }
MCU_XDATA uint8_t mac_time_reserved_end;
