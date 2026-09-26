/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_autoack.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>
#if defined(CC2530_MAC_ATTEMPT)
#include "mac_epoch.h"
#endif

MCU_XDATA uint8_t radio_autoack_state, radio_autoack_fault;
extern MCU_XDATA uint8_t radio_autoack_reserved_end, _gptrput_PARM_2;
extern MCU_XDATA uint8_t __memcpy_PARM_2[3];
#if defined(CC2530_MAC_RADIO)
/* Parent's first XDATA object, after timebase/clock/mac_time/radio/mac_epoch. */
extern MCU_XDATA uint8_t mac_radio_shared_end;
#define PRIVATE_END mac_radio_shared_end
#else
#define PRIVATE_END radio_autoack_reserved_end
#endif
static MCU_XDATA radio_autoack_config_t owned;
static MCU_XDATA radio_autoack_frame_t staged;
static MCU_XDATA radio_autoack_diagnostics_t status;
static MCU_XDATA struct {
    uint32_t start, previous, deadline;
    uint16_t limit;
    uint8_t clock, configured, expected_mask, head, remaining, byte, index;
    uint8_t profile, cca, tx_count, tx_first, tx_last;
#if defined(CC2530_MAC_ATTEMPT)
    uint8_t search_disabled, prepared_length;
#endif
} work;
/* SWRU191F pp.214,256-264; SWRS081B Table2 p.24. Address RAM first,
 * then the complete profile, all while idle. Only FSCAL1 has R/W0 high bits.
 */
static const MCU_CODE uint16_t settings[] = {
    0x6180, 0x6181, 0x6182, 0x6189, 0x618a, 0x6194, 0x6195,
    0x61b2, 0x61fa, 0x61ae, 0x618f, 0x6190, 0x6191
};
static const MCU_CODE uint8_t values[] = {
    RADIO_AUTOACK_NORMAL_FILTER, 0x70, 0, 0x60, 0, 0x7f, 0, 0x15, 9, 0, 0, 5, 0x69
};

static uint16_t setting_address(uint8_t index)
{
    return index < 12 ? 0x616au + index : settings[index - 12u];
}

static uint8_t setting_value(uint8_t index)
{
    if (index < 8) return owned.ieee[index];
    if (index == 8) return (uint8_t)owned.pan;
    if (index == 9) return (uint8_t)(owned.pan >> 8);
    if (index == 10) return (uint8_t)owned.short_address;
    if (index == 11) return (uint8_t)(owned.short_address >> 8);
    if (index == 12 && (work.profile & 2u)) return 0x0c;
#if defined(CC2530_MAC_ATTEMPT)
    if (index == 15 && work.search_disabled) return 0x4c;
#endif
    if (index == 15 && (work.profile & 1u)) return 0x40;
    if (index == 22) return (uint8_t)(11u + 5u * (owned.channel - 11u));
    return values[index - 12u];
}

static radio_autoack_result_t storage(uint16_t address, uint8_t size)
{
    /* The linked proof binds the entire memcpy/memset/gptrput scratch suffix. */
    uint16_t first = MMIO_XADDRESS(__memcpy_PARM_2);
    uint16_t helper = MMIO_XADDRESS(&_gptrput_PARM_2);
    if (address >= 0x1e00 || size > 0x1e00u - address)
        return RADIO_AUTOACK_INVALID_RANGE;
    if (address <= MMIO_XADDRESS(&PRIVATE_END) ||
        (address <= helper && (address >= first || first - address < size)))
        return RADIO_AUTOACK_BUFFER_OWNERSHIP;
    return RADIO_AUTOACK_READY;
}

static radio_autoack_result_t observe(void)
{
    uint8_t i, value, clock;
    status.sample_valid = 0;
    if (MMIO_READ(SOC_IEN0) || MMIO_READ(SOC_IEN1) || MMIO_READ(SOC_IEN2) ||
        (MMIO_READ(SOC_SLEEPCMD) & 7u) != 4u ||
        MMIO_READ(SOC_DMAARM) || MMIO_READ(SOC_DMAREQ))
        return RADIO_AUTOACK_UNSUPPORTED_STATE;
    clock = MMIO_READ(SOC_CLKCONCMD);
    if ((clock & 0x47u) || MMIO_READ(SOC_CLKCONSTA) != clock)
        return RADIO_AUTOACK_UNSUPPORTED_STATE;
    if (clock != work.clock) return RADIO_AUTOACK_STATE_CHANGED;
    if (MMIO_XREAD(0x624a) != 0xa5 || MMIO_XREAD(0x61e1) ||
        MMIO_XREAD(0x61a3) || MMIO_XREAD(0x61a4) || MMIO_XREAD(0x61a5) ||
        MMIO_XREAD(0x61a8) != 0x85 || MMIO_XREAD(0x61a9) != 0x14 ||
        MMIO_XREAD(0x61b8) != 0x75 || MMIO_XREAD(0x61b9) != 8 ||
        MMIO_XREAD(0x618e) != 0x0f)
        return RADIO_AUTOACK_UNSUPPORTED_STATE;
    for (i = 0; i < work.configured; i++) {
        value = MMIO_XREAD(setting_address(i));
        if (i == 21) value &= 3;
        if (value != setting_value(i)) return RADIO_AUTOACK_STATE_CHANGED;
    }
    status.mask = MMIO_XREAD(0x618b);
    status.calibration = MMIO_XREAD(0x6192);
    status.signals = MMIO_XREAD(0x6193);
    status.count = MMIO_XREAD(0x619b);
    status.first = MMIO_XREAD(0x619d);
    status.last = MMIO_XREAD(0x619e);
    status.packet = MMIO_XREAD(0x619f);
    status.rssi_valid = MMIO_XREAD(0x6199);
    if (work.cca) {
#if defined(CC2530_MAC_ATTEMPT)
        if (MMIO_XREAD(0x6196) != (work.cca >= 3 ? RADIO_AUTOACK_CCA_THRESHOLD_RAW : 0xf8) ||
            (work.cca == 2 && MMIO_XREAD(0x6197) != 0x1a) ||
            (work.cca == 4 && MMIO_XREAD(0x6197) != 0x0a))
#else
        if (MMIO_XREAD(0x6196) != 0xf8 ||
            (work.cca == 2 && MMIO_XREAD(0x6197) != 0x1a))
#endif
            return RADIO_AUTOACK_STATE_CHANGED;
        work.tx_count = MMIO_XREAD(0x619c);
        work.tx_first = MMIO_XREAD(0x61a1);
        work.tx_last = MMIO_XREAD(0x61a2);
    }
    status.errors = MMIO_READ(SOC_RFERRF);
    status.flags0 = MMIO_READ(SOC_RFIRQF0);
    status.flags1 = MMIO_READ(SOC_RFIRQF1);
    status.sample_valid = 1;
    if (status.mask != work.expected_mask || (status.calibration & 0x80u) ||
        (status.rssi_valid & 0xfeu) || (status.flags0 & 1u) || (status.flags1 & 0xf8u) ||
        ((status.first | status.last) & 0x80u))
        return RADIO_AUTOACK_STATE_CHANGED;
    if (status.errors || (status.signals & 0xc0u) == 0x40u)
        return RADIO_AUTOACK_CONTROLLER_ERROR;
    if (status.count > 128 || (work.cca &&
        (work.tx_count > 128 || work.tx_first > 128 || work.tx_last > 128)))
        return RADIO_AUTOACK_FIFO_ERROR;
    if ((radio_autoack_state == RADIO_AUTOACK_RX ||
         radio_autoack_state == RADIO_AUTOACK_RX_NOACK) && work.expected_mask &&
        !(status.signals & 3u))
        return RADIO_AUTOACK_STATE_CHANGED;
    return RADIO_AUTOACK_READY;
}

static uint8_t idle(void)
{
    return !status.mask && !(status.calibration & 0x40u) && !(status.signals & 0x27u);
}

static radio_autoack_result_t stopped(void)
{
    if (!idle()) return RADIO_AUTOACK_STATE_CHANGED;
    if (((status.first + status.count) & 127u) != status.last)
        return RADIO_AUTOACK_FIFO_ERROR;
    return RADIO_AUTOACK_READY;
}

static radio_autoack_result_t progress(void)
{
    uint32_t now;
    bool expired;
    if (status.polls == work.limit) return RADIO_AUTOACK_WORK_LIMIT;
    now = timebase_read_awake_ticks24();
    status.polls++;
    status.elapsed_ticks = (now - work.start) & TIMEBASE_TICKS_MASK;
    status.timebase_status = timebase_expired(now, work.deadline, &expired);
    if (status.timebase_status != TIMEBASE_OK || status.elapsed_ticks >= TIMEBASE_HALF_RANGE ||
        ((now - work.previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
        return RADIO_AUTOACK_TIME_ERROR;
    if (expired) return RADIO_AUTOACK_TIMEOUT;
    work.previous = now;
    return RADIO_AUTOACK_READY;
}

static radio_autoack_result_t poll(void)
{
    radio_autoack_result_t result;
    if (status.polls == work.limit) return RADIO_AUTOACK_WORK_LIMIT;
    result = observe();
    if (result != RADIO_AUTOACK_READY) return result;
    return progress();
}

static uint8_t read_fifo(void)
{
    return MMIO_READ(SOC_RFD);
}

/* A complete head frame cannot be removed by reception/filtering of a later
 * frame. Do not require a live FIFO's total count to decrement by exactly one:
 * concurrent arrival/rejection can change its tail. Pin the consumed head.
 */
static radio_autoack_result_t consume(void)
{
    radio_autoack_result_t result = progress();
    uint8_t count;
    if (result != RADIO_AUTOACK_READY) return result;
    if (status.polls == work.limit) return RADIO_AUTOACK_WORK_LIMIT;
    if (MMIO_READ(SOC_RFERRF)) return RADIO_AUTOACK_CONTROLLER_ERROR;
    count = MMIO_XREAD(0x619b);
    if (count < work.remaining || count > 128 || MMIO_XREAD(0x619d) != work.head)
        return RADIO_AUTOACK_FIFO_ERROR;
    work.byte = read_fifo();
    status.bytes_read++;
    work.head = (work.head + 1u) & 127u;
    work.remaining--;
    return RADIO_AUTOACK_READY;
}

#define CHECK(call) do { result = (call); if (result != RADIO_AUTOACK_READY) goto failed; } while (0)
#define REQUIRE(test, error) do { if (!(test)) { result = (error); goto failed; } } while (0)
#define ROOM() REQUIRE(status.polls < work.limit, RADIO_AUTOACK_WORK_LIMIT)
#if defined(CC2530_MAC_ATTEMPT)
static radio_autoack_result_t receive_head(void)
{
    radio_autoack_result_t result;
    uint8_t i;
    work.head = status.first; work.remaining = status.phr + 1u;
    CHECK(consume());
    REQUIRE((work.byte & 127u) == status.phr, RADIO_AUTOACK_FIFO_ERROR);
    staged.length = status.phr - 2u;
    for (i = 0; i < staged.length; i++) {
        CHECK(consume()); staged.body[i] = work.byte;
    }
    CHECK(consume()); staged.rssi_raw = work.byte;
    CHECK(consume()); staged.crc_correlation = work.byte;
    CHECK(poll());
    REQUIRE(status.first == work.head, RADIO_AUTOACK_FIFO_ERROR);
    if (radio_autoack_state == RADIO_AUTOACK_DRAINING ||
        radio_autoack_state == RADIO_AUTOACK_DRAIN_NOACK)
        CHECK(stopped());
    return RADIO_AUTOACK_READY;
failed:
    return result;
}
#endif
static radio_autoack_result_t operate(uint8_t operation,
    const radio_autoack_config_t MCU_XDATA *configuration,
    radio_autoack_frame_t MCU_XDATA *output, uint32_t timeout, uint16_t limit)
{
    radio_autoack_result_t result;
    uint8_t i;
    if (radio_autoack_fault) return (radio_autoack_result_t)radio_autoack_fault;
    if (!timeout || timeout >= TIMEBASE_HALF_RANGE || !limit ||
        (operation == 0 && configuration == NULL) || (operation == 1 && output == NULL))
        return RADIO_AUTOACK_INVALID_ARGUMENT;
    if ((operation == 0 && radio_autoack_state != RADIO_AUTOACK_COLD) ||
        (operation == 3 && radio_autoack_state != RADIO_AUTOACK_OFF &&
         radio_autoack_state != RADIO_AUTOACK_OFF_NOACK) ||
        ((operation == 1 || operation == 2) && radio_autoack_state != RADIO_AUTOACK_RX &&
         radio_autoack_state != RADIO_AUTOACK_DRAINING &&
         radio_autoack_state != RADIO_AUTOACK_RX_NOACK &&
         radio_autoack_state != RADIO_AUTOACK_DRAIN_NOACK
#if defined(CC2530_MAC_ATTEMPT)
         && !(operation == 2 && radio_autoack_state == RADIO_AUTOACK_ATTEMPT_PREPARED)
#endif
         ))
        return RADIO_AUTOACK_STATE;
    if (operation == 0) {
        result = storage(MMIO_XADDRESS(configuration), sizeof(*configuration));
        if (result != RADIO_AUTOACK_READY) return result;
        if (configuration->channel < 11 || configuration->channel > 26 ||
            configuration->power != RADIO_AUTOACK_POWER_05)
            return RADIO_AUTOACK_INVALID_ARGUMENT;
    } else if (operation == 1) {
        result = storage(MMIO_XADDRESS(output), sizeof(*output));
        if (result != RADIO_AUTOACK_READY) return result;
    }
    memset(&status, 0, sizeof(status));
    work.limit = limit;
    work.start = timebase_read_awake_ticks24(); work.previous = work.start;
    status.timebase_status = timebase_deadline_after(work.start, timeout, &work.deadline);
    REQUIRE(status.timebase_status == TIMEBASE_OK, RADIO_AUTOACK_TIME_ERROR);
    if (operation == 0 || operation == 3) {
        if (operation == 0) {
            owned = *configuration;
            work.clock = MMIO_READ(SOC_CLKCONCMD);
            work.configured = work.expected_mask = 0;
            work.profile = work.cca = 0;
#if defined(CC2530_MAC_ATTEMPT)
            work.search_disabled = work.prepared_length = 0;
#endif
            status.phase = 1;
            CHECK(poll());
            REQUIRE(idle() && !status.count && !(status.signals & 0xc0u) &&
                !status.first && !status.last && !status.packet &&
                !MMIO_XREAD(0x619c) && !MMIO_XREAD(0x61a1) && !MMIO_XREAD(0x61a2) &&
                MMIO_XREAD(0x6189) == 0x40 && MMIO_XREAD(0x618a) == 1,
                RADIO_AUTOACK_UNSUPPORTED_STATE);
            for (work.index = 0; work.index < 25; work.index++) {
                ROOM();
                MMIO_XWRITE(setting_address(work.index), setting_value(work.index));
                work.configured++; status.writes++;
                CHECK(poll());
                REQUIRE(idle() && !status.count && !(status.signals & 0xc0u),
                        RADIO_AUTOACK_STATE_CHANGED);
                status.verified = status.writes;
            }
        } else {
            status.phase = 8;
            CHECK(poll());
            CHECK(stopped());
            REQUIRE(status.flags1 & 4u, RADIO_AUTOACK_STATE_CHANGED);
            REQUIRE(!status.count && !(status.signals & 0xc0u), RADIO_AUTOACK_FIFO_ERROR);
#if defined(CC2530_MAC_ATTEMPT)
            if (work.search_disabled) {
                ROOM();
                MMIO_XWRITE(0x6189, 0x40); work.search_disabled = 0; status.writes++;
                CHECK(poll()); CHECK(stopped());
                REQUIRE(!status.count && !(status.signals & 0xc0u), RADIO_AUTOACK_FIFO_ERROR);
            }
#endif
            if (radio_autoack_state == RADIO_AUTOACK_OFF_NOACK) {
                ROOM();
                MMIO_XWRITE(0x6180, RADIO_AUTOACK_NORMAL_FILTER); work.profile = 1; status.writes++;
                CHECK(poll()); CHECK(stopped());
                REQUIRE(!status.count && !(status.signals & 0xc0u), RADIO_AUTOACK_FIFO_ERROR);
                ROOM();
                MMIO_XWRITE(0x6189, 0x60); work.profile = 0; status.writes++;
                CHECK(poll()); CHECK(stopped());
                REQUIRE(!status.count && !(status.signals & 0xc0u), RADIO_AUTOACK_FIFO_ERROR);
            }
        }
        ROOM(); status.phase = operation == 0 ? 2 : 9;
        MMIO_XWRITE(0x618c, 1); status.writes++;
        work.expected_mask = 1;
        do { CHECK(poll()); }
        while ((status.calibration & 0x40u) || (status.signals & 7u) != 5u ||
               !status.rssi_valid);
        radio_autoack_state = RADIO_AUTOACK_RX;
        status.phase = operation == 0 ? 3 : 10; result = RADIO_AUTOACK_READY;
    } else if (operation == 1) {
        status.phase = 4;
        CHECK(poll());
        if (radio_autoack_state == RADIO_AUTOACK_DRAINING ||
            radio_autoack_state == RADIO_AUTOACK_DRAIN_NOACK)
            CHECK(stopped());
        if (!(status.signals & 0x40u)) {
            REQUIRE(radio_autoack_state == RADIO_AUTOACK_RX ||
                    radio_autoack_state == RADIO_AUTOACK_RX_NOACK ||
                    (!status.count && !(status.signals & 0xc0u)),
                    RADIO_AUTOACK_FIFO_ERROR);
            result = RADIO_AUTOACK_EMPTY;
            goto finished;
        }
        status.phr = MMIO_XREAD(0x619a) & 127u;
        REQUIRE(status.phr >= 5 && status.count >= status.phr + 1u, RADIO_AUTOACK_FIFO_ERROR);
#if defined(CC2530_MAC_ATTEMPT)
        CHECK(receive_head());
#else
        work.head = status.first; work.remaining = status.phr + 1u;
        CHECK(consume());
        REQUIRE((work.byte & 127u) == status.phr, RADIO_AUTOACK_FIFO_ERROR);
        staged.length = status.phr - 2u;
        for (i = 0; i < staged.length; i++) {
            CHECK(consume()); staged.body[i] = work.byte;
        }
        CHECK(consume()); staged.rssi_raw = work.byte;
        CHECK(consume()); staged.crc_correlation = work.byte;
        CHECK(poll());
        REQUIRE(status.first == work.head, RADIO_AUTOACK_FIFO_ERROR);
        if (radio_autoack_state == RADIO_AUTOACK_DRAINING ||
            radio_autoack_state == RADIO_AUTOACK_DRAIN_NOACK)
            CHECK(stopped());
#endif
        for (i = 0; i < staged.length; i++) output->body[i] = staged.body[i];
        output->length = staged.length;
        output->rssi_raw = staged.rssi_raw;
        output->crc_correlation = staged.crc_correlation;
        result = (staged.crc_correlation & 128u) ? RADIO_AUTOACK_FRAME : RADIO_AUTOACK_BAD_CRC;
    } else {
        status.phase = 5;
        CHECK(poll());
        if (radio_autoack_state == RADIO_AUTOACK_RX ||
            radio_autoack_state == RADIO_AUTOACK_RX_NOACK) {
            ROOM();
            /* R/W0: clear only the old RFIDLE flag, preserve TX/ACK flags. */
            MMIO_WRITE(SOC_RFIRQF1, 0x3b); status.writes++;
            CHECK(poll());
            REQUIRE(!(status.flags1 & 4u), RADIO_AUTOACK_STATE_CHANGED);
            ROOM();
            MMIO_XWRITE(0x618d, 1); status.writes++;
            work.expected_mask = 0;
            do { CHECK(poll()); } while (!idle() || !(status.flags1 & 4u));
            radio_autoack_state = work.profile ? RADIO_AUTOACK_DRAIN_NOACK : RADIO_AUTOACK_DRAINING;
        }
        CHECK(stopped());
        status.phase = 6;
        if (status.count) {
            REQUIRE(status.signals & 0x40u, RADIO_AUTOACK_FIFO_ERROR);
            result = RADIO_AUTOACK_DRAIN;
        } else {
            REQUIRE(!(status.signals & 0xc0u), RADIO_AUTOACK_FIFO_ERROR);
            radio_autoack_state = work.profile ? RADIO_AUTOACK_OFF_NOACK : RADIO_AUTOACK_OFF;
            status.phase = 7;
            result = RADIO_AUTOACK_STOPPED;
        }
    }
finished:
    status.result = result;
    return result;
failed:
    radio_autoack_fault = result; radio_autoack_state = RADIO_AUTOACK_FAULT;
    status.result = result;
    return result;
}

radio_autoack_result_t radio_autoack_acquire(
    const radio_autoack_config_t MCU_XDATA *configuration, uint32_t timeout, uint16_t limit)
{
    return operate(0, configuration, NULL, timeout, limit);
}
radio_autoack_result_t radio_autoack_receive(
    uint32_t timeout, uint16_t limit, radio_autoack_frame_t MCU_XDATA *output)
{
    return operate(1, NULL, output, timeout, limit);
}
radio_autoack_result_t radio_autoack_stop(uint32_t timeout, uint16_t limit)
{
    return operate(2, NULL, NULL, timeout, limit);
}
radio_autoack_result_t radio_autoack_resume(uint32_t timeout, uint16_t limit)
{
    return operate(3, NULL, NULL, timeout, limit);
}

#if defined(__SDCC)
static void cca_settle(void) __naked
{
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

static uint8_t tx_loaded(uint8_t length)
{
    return work.tx_count == length && !work.tx_first && work.tx_last == length;
}

static radio_autoack_result_t tx_idle(void)
{
    radio_autoack_result_t result = stopped();
    if (result != RADIO_AUTOACK_READY) return result;
    return !status.count && !(status.signals & 0xc0u) ? RADIO_AUTOACK_READY : RADIO_AUTOACK_FIFO_ERROR;
}

radio_autoack_result_t radio_autoack_send(
    const uint8_t MCU_XDATA *body, uint8_t length, uint32_t timeout, uint16_t limit)
{
    radio_autoack_result_t result;
    if (radio_autoack_fault) return (radio_autoack_result_t)radio_autoack_fault;
#if defined(CC2530_MAC_ATTEMPT)
    if (work.cca >= 3 || work.search_disabled) return RADIO_AUTOACK_STATE;
#endif
    if (!body || !length || length > 125 || !timeout || timeout >= TIMEBASE_HALF_RANGE || !limit)
        return RADIO_AUTOACK_INVALID_ARGUMENT;
    if (radio_autoack_state != RADIO_AUTOACK_OFF && radio_autoack_state != RADIO_AUTOACK_OFF_NOACK)
        return RADIO_AUTOACK_STATE;
    result = storage(MMIO_XADDRESS(body), length);
    if (result != RADIO_AUTOACK_READY) return result;
    memset(&status, 0, sizeof(status));
    work.limit = limit;
    work.start = timebase_read_awake_ticks24(); work.previous = work.start;
    status.timebase_status = timebase_deadline_after(work.start, timeout, &work.deadline);
    REQUIRE(status.timebase_status == TIMEBASE_OK, RADIO_AUTOACK_TIME_ERROR);
    status.phase = 11;
    CHECK(poll()); CHECK(tx_idle());
    REQUIRE(status.flags1 & 4u, RADIO_AUTOACK_STATE_CHANGED);
    if (!work.profile) {
        ROOM();
        MMIO_XWRITE(0x6189, 0x40); work.profile = 1; status.writes++;
        CHECK(poll()); CHECK(tx_idle());
        ROOM();
        MMIO_XWRITE(0x6180, 0x0c); work.profile = 3; status.writes++;
        CHECK(poll()); CHECK(tx_idle());
    }
    if (!work.cca) {
        ROOM();
        MMIO_XWRITE(0x6196, 0xf8); work.cca = 1; status.writes++;
        CHECK(poll()); CHECK(tx_idle());
        ROOM();
        MMIO_XWRITE(0x6197, 0x1a); work.cca = 2; status.writes++;
        CHECK(poll()); CHECK(tx_idle());
    }
    ROOM(); status.phase = 12;
    MMIO_WRITE(SOC_RFST, 0xee); status.writes++;
    CHECK(poll()); CHECK(tx_idle());
    REQUIRE(tx_loaded(0), RADIO_AUTOACK_FIFO_ERROR);
    for (work.index = 0; work.index <= length; work.index++) {
        ROOM();
        MMIO_WRITE(SOC_RFD, work.index ? body[work.index - 1u] : (uint8_t)(length + 2u));
        status.writes++;
        CHECK(poll()); CHECK(tx_idle());
        REQUIRE(tx_loaded(work.index + 1u), RADIO_AUTOACK_FIFO_ERROR);
    }
    ROOM(); status.phase = 13;
    MMIO_WRITE(SOC_RFIRQF1, 0x3d); status.writes++;
    CHECK(poll()); CHECK(tx_idle());
    REQUIRE(!(status.flags1 & 2u), RADIO_AUTOACK_STATE_CHANGED);
    ROOM();
    MMIO_XWRITE(0x618c, 1); work.expected_mask = 1; status.writes++;
    do { CHECK(poll()); }
    while ((status.calibration & 0x40u) || (status.signals & 7u) != 5u || !status.rssi_valid);
    cca_settle();
    CHECK(poll());
    REQUIRE(!(status.calibration & 0x40u) && (status.signals & 7u) == 5u &&
            status.rssi_valid && !(status.flags1 & 2u) && tx_loaded(length + 1u),
            RADIO_AUTOACK_STATE_CHANGED);
    ROOM(); status.phase = 14;
    MMIO_WRITE(SOC_RFST, 0xea); status.writes++;
    CHECK(poll());
    if (!(status.signals & 8u)) {
        REQUIRE(!(status.flags1 & 2u) && !(status.signals & 2u) && tx_loaded(length + 1u),
                RADIO_AUTOACK_STATE_CHANGED);
        result = RADIO_AUTOACK_CCA_BUSY;
    } else {
        status.phase = 15;
        while (!(status.flags1 & 2u) || (status.signals & 2u)) {
            CHECK(poll());
            REQUIRE(status.signals & 8u, RADIO_AUTOACK_STATE_CHANGED);
        }
        result = RADIO_AUTOACK_TX_DONE;
    }
    radio_autoack_state = RADIO_AUTOACK_RX_NOACK;
    status.phase = 16; status.result = result;
    return result;
failed:
    radio_autoack_fault = result; radio_autoack_state = RADIO_AUTOACK_FAULT;
    status.result = result;
    return result;
}
const radio_autoack_diagnostics_t MCU_XDATA *radio_autoack_diagnostic(void) { return &status; }
#if defined(CC2530_MAC_ATTEMPT)
static MCU_XDATA mac_epoch_t attempt_epoch;
static MCU_XDATA mac_epoch_stamp_t attempt_delta;
static radio_autoack_attempt_t MCU_XDATA * MCU_XDATA attempt_output;
/* Keep long-lived arithmetic in XDATA, not SDCC's scarce DATA spill cache. */
static volatile MCU_XDATA struct {
    uint32_t ticks, duration, until;
    uint8_t head, phr;
} attempt;

static radio_autoack_result_t attempt_elapsed(
    const mac_time_stamp_t MCU_XDATA *first, const mac_time_stamp_t MCU_XDATA *last)
{
    if (mac_epoch_start(&attempt_epoch, first, 0) != MAC_EPOCH_OK ||
        mac_epoch_step(&attempt_epoch, last, &attempt_delta) != MAC_EPOCH_OK ||
        attempt_delta.symbols > 65535UL)
        return RADIO_AUTOACK_ATTEMPT_TIMER_ERROR;
    attempt.ticks = attempt_delta.symbols * 512UL + attempt_delta.fine - first->fine;
    return RADIO_AUTOACK_READY;
}

static radio_autoack_result_t attempt_sample(uint32_t timeout, uint16_t limit,
                                             mac_time_stamp_t MCU_XDATA *output)
{
    return mac_time_attempt_read(timeout, limit, output) == MAC_TIME_OK ?
        RADIO_AUTOACK_READY : RADIO_AUTOACK_ATTEMPT_TIMER_ERROR;
}

radio_autoack_result_t radio_autoack_prepare(
    const uint8_t MCU_XDATA *body, uint8_t length, uint32_t timeout, uint16_t limit)
{
    radio_autoack_result_t result;
    if (radio_autoack_fault) return (radio_autoack_result_t)radio_autoack_fault;
    if (!body || !length || length > 125 || !timeout || timeout >= TIMEBASE_HALF_RANGE || !limit)
        return RADIO_AUTOACK_INVALID_ARGUMENT;
    if (radio_autoack_state != RADIO_AUTOACK_OFF && radio_autoack_state != RADIO_AUTOACK_OFF_NOACK)
        return RADIO_AUTOACK_STATE;
    result = storage(MMIO_XADDRESS(body), length);
    if (result != RADIO_AUTOACK_READY) return result;
    memset(&status, 0, sizeof(status)); work.limit = limit;
    work.start = timebase_read_awake_ticks24(); work.previous = work.start;
    status.timebase_status = timebase_deadline_after(work.start, timeout, &work.deadline);
    REQUIRE(status.timebase_status == TIMEBASE_OK, RADIO_AUTOACK_TIME_ERROR);
    status.phase = 17;
    CHECK(poll()); CHECK(tx_idle());
    REQUIRE(status.flags1 & 4u, RADIO_AUTOACK_STATE_CHANGED);
    if (!work.profile) {
        ROOM();
        MMIO_XWRITE(0x6189, 0x40); work.profile = 1; status.writes++;
        CHECK(poll()); CHECK(tx_idle());
        ROOM();
        MMIO_XWRITE(0x6180, 0x0c); work.profile = 3; status.writes++;
        CHECK(poll()); CHECK(tx_idle());
    }
    if (work.cca != 4) {
        ROOM();
        MMIO_XWRITE(0x6196, RADIO_AUTOACK_CCA_THRESHOLD_RAW); work.cca = 3; status.writes++;
        CHECK(poll()); CHECK(tx_idle());
        ROOM();
        MMIO_XWRITE(0x6197, 0x0a); work.cca = 4; status.writes++;
        CHECK(poll()); CHECK(tx_idle());
    }
    ROOM();
    MMIO_WRITE(SOC_RFST, 0xee); status.writes++;
    CHECK(poll()); CHECK(tx_idle());
    REQUIRE(tx_loaded(0), RADIO_AUTOACK_FIFO_ERROR);
    for (work.index = 0; work.index <= length; work.index++) {
        ROOM();
        MMIO_WRITE(SOC_RFD, work.index ? body[work.index-1u] : (uint8_t)(length+2u));
        status.writes++;
        CHECK(poll()); CHECK(tx_idle());
        REQUIRE(tx_loaded(work.index+1u), RADIO_AUTOACK_FIFO_ERROR);
    }
    ROOM();
    MMIO_WRITE(SOC_RFIRQF1, 0x3d); status.writes++;
    CHECK(poll()); CHECK(tx_idle());
    REQUIRE(!(status.flags1 & 2u), RADIO_AUTOACK_STATE_CHANGED);
    ROOM();
    MMIO_XWRITE(0x6189, 0x4c); work.search_disabled = 1; status.writes++;
    CHECK(poll()); CHECK(tx_idle());
    work.prepared_length = length;
    radio_autoack_state = RADIO_AUTOACK_ATTEMPT_PREPARED;
    status.result = RADIO_AUTOACK_READY;
    return RADIO_AUTOACK_READY;
failed:
    radio_autoack_fault = result; radio_autoack_state = RADIO_AUTOACK_FAULT;
    status.result = result;
    return result;
}

/* This hook writes provisional wrapper-owned staging, never a public receipt.
 * Full time/radio checks bracket the finite hot region. No MMIO reset/recovery.
 */
radio_autoack_result_t radio_autoack_attempt(
    uint16_t window, uint32_t timeout, uint16_t limit,
    radio_autoack_attempt_t MCU_XDATA *output)
{
    radio_autoack_result_t result, received;
    uint8_t flags, signals;
    if (radio_autoack_fault) return (radio_autoack_result_t)radio_autoack_fault;
    if (!output || !window || window > 4096 || !timeout || timeout >= TIMEBASE_HALF_RANGE || !limit)
        return RADIO_AUTOACK_INVALID_ARGUMENT;
    if (radio_autoack_state != RADIO_AUTOACK_ATTEMPT_PREPARED) return RADIO_AUTOACK_STATE;
    result = storage(MMIO_XADDRESS(output), sizeof(*output));
    if (result != RADIO_AUTOACK_READY) return result;
    attempt_output = output;
    memset(&status, 0, sizeof(status)); work.limit = limit;
    work.start = timebase_read_awake_ticks24(); work.previous = work.start;
    status.timebase_status = timebase_deadline_after(work.start, timeout, &work.deadline);
    REQUIRE(status.timebase_status == TIMEBASE_OK, RADIO_AUTOACK_TIME_ERROR);
    CHECK(poll()); CHECK(tx_idle());
    REQUIRE(work.search_disabled && work.cca == 4 && !(status.flags1 & 2u) &&
            tx_loaded(work.prepared_length+1u), RADIO_AUTOACK_STATE_CHANGED);
    REQUIRE(mac_time_attempt_begin(timeout, limit, &attempt_output->last) == MAC_TIME_OK,
            RADIO_AUTOACK_ATTEMPT_TIMER_ERROR);
    attempt_output->transmitted = attempt_output->received = attempt_output->sampled_cca =
        attempt_output->within_window = 0;
    attempt.duration = (16UL + 2UL*work.prepared_length)*512UL;
    attempt.until = attempt.duration + (uint32_t)window*512UL;
    ROOM(); status.phase = 18;
    MMIO_XWRITE(0x618c, 1); work.expected_mask = 1; status.writes++;
    do { CHECK(poll()); }
    while ((status.calibration & 0x40u) || (status.signals & 7u) != 5u || !status.rssi_valid);
    CHECK(attempt_sample(timeout, limit, &attempt_output->before));
    do {
        CHECK(progress());
        CHECK(attempt_sample(timeout, limit, &attempt_output->armed));
        CHECK(attempt_elapsed(&attempt_output->before, &attempt_output->armed));
    } while (attempt.ticks < 8UL*512UL);
    cca_settle();
    CHECK(poll());
    REQUIRE(!(status.calibration & 0x40u) && (status.signals & 7u) == 5u &&
            status.rssi_valid && !(status.flags1 & 2u) && !status.count &&
            !(status.signals & 0xc0u) && tx_loaded(work.prepared_length+1u),
            RADIO_AUTOACK_STATE_CHANGED);
    ROOM(); status.phase = 19;
    CHECK(attempt_sample(timeout, limit, &attempt_output->before));
    MMIO_WRITE(SOC_RFST, 0xea); status.writes++;
    signals = MMIO_XREAD(0x6193);
    attempt_output->sampled_cca = (signals >> 3) & 1u;
    if (attempt_output->sampled_cca) {
        /* Positive own TX state prevents admission before the submitted TX.
         * Earliest-end check below also proves this TX cannot already be over.
         */
        while (!(signals & 2u)) {
            CHECK(progress());
            REQUIRE(!MMIO_READ(SOC_RFERRF), RADIO_AUTOACK_CONTROLLER_ERROR);
            REQUIRE(!(MMIO_READ(SOC_RFIRQF1) & 2u),
                    RADIO_AUTOACK_ATTEMPT_LATE_ARM);
            signals = MMIO_XREAD(0x6193);
        }
        REQUIRE(!(signals & 1u), RADIO_AUTOACK_STATE_CHANGED);
    } else {
        REQUIRE(!(signals & 2u) && !(MMIO_READ(SOC_RFIRQF1) & 2u),
                RADIO_AUTOACK_STATE_CHANGED);
    }
    MMIO_XWRITE(0x6189, 0x40); work.search_disabled = 0; status.writes++;
    REQUIRE(MMIO_XREAD(0x6189) == 0x40, RADIO_AUTOACK_STATE_CHANGED);
    CHECK(attempt_sample(timeout, limit, &attempt_output->armed));
    if (!attempt_output->sampled_cca) { received = RADIO_AUTOACK_CCA_BUSY; goto complete; }
    CHECK(attempt_elapsed(&attempt_output->before, &attempt_output->armed));
    REQUIRE(attempt.ticks < attempt.duration,
            RADIO_AUTOACK_ATTEMPT_LATE_ARM);
    received = RADIO_AUTOACK_EMPTY; status.phase = 20;
    for (;;) {
        CHECK(progress());
        REQUIRE(!MMIO_READ(SOC_RFERRF), RADIO_AUTOACK_CONTROLLER_ERROR);
        flags = MMIO_READ(SOC_RFIRQF1);
        REQUIRE(!(flags & 0xf8u), RADIO_AUTOACK_STATE_CHANGED);
        if (!attempt_output->transmitted && (flags & 2u)) {
            CHECK(attempt_sample(timeout, limit, &attempt_output->tx));
            CHECK(attempt_elapsed(&attempt_output->before, &attempt_output->tx));
            REQUIRE(attempt.ticks >= attempt.duration, RADIO_AUTOACK_STATE_CHANGED);
            attempt_output->transmitted = 1;
        }
        signals = MMIO_XREAD(0x6193);
        REQUIRE((signals & 0xc0u) != 0x40u, RADIO_AUTOACK_CONTROLLER_ERROR);
        if (signals & 0x40u) {
            REQUIRE(attempt_output->transmitted, RADIO_AUTOACK_STATE_CHANGED);
            attempt.head = MMIO_XREAD(0x619d);
            attempt.phr = MMIO_XREAD(0x619a) & 127u;
            work.byte = MMIO_XREAD(0x619b);
            REQUIRE(!(attempt.head & 128u) && MMIO_XREAD(0x619f) == attempt.head &&
                    attempt.phr >= 5 && work.byte <= 128 && work.byte >= attempt.phr+1u &&
                    !MMIO_READ(SOC_RFERRF), RADIO_AUTOACK_FIFO_ERROR);
            CHECK(attempt_sample(timeout, limit, &attempt_output->rx));
            CHECK(attempt_elapsed(&attempt_output->before, &attempt_output->rx));
            attempt_output->within_window = attempt.ticks <= attempt.until;
            CHECK(poll());
            REQUIRE(status.first == attempt.head &&
                    (MMIO_XREAD(0x619a) & 127u) == attempt.phr, RADIO_AUTOACK_FIFO_ERROR);
            ROOM();
            radio_autoack_state = RADIO_AUTOACK_RX_NOACK;
            status.phr = attempt.phr;
            CHECK(receive_head());
            attempt_output->frame = staged;
            received = (staged.crc_correlation & 128u) ? RADIO_AUTOACK_FRAME : RADIO_AUTOACK_BAD_CRC;
            attempt_output->received = 1;
            break;
        }
        CHECK(attempt_sample(timeout, limit, &attempt_output->last));
        CHECK(attempt_elapsed(&attempt_output->before, &attempt_output->last));
        if (attempt.ticks >= attempt.until && attempt_output->transmitted) {
            break;
        }
    }
complete:
    CHECK(poll());
    REQUIRE(work.tx_count == work.prepared_length+1u &&
            work.tx_last == work.prepared_length+1u, RADIO_AUTOACK_FIFO_ERROR);
    REQUIRE(mac_time_attempt_end(timeout, limit, &attempt_output->last) == MAC_TIME_OK,
            RADIO_AUTOACK_ATTEMPT_TIMER_ERROR);
    CHECK(poll());
    radio_autoack_state = RADIO_AUTOACK_RX_NOACK;
    status.phase = 21; status.result = received;
    return received;
failed:
    radio_autoack_fault = result; radio_autoack_state = RADIO_AUTOACK_FAULT;
    status.result = result;
    return result;
}
#endif
#if defined(CC2530_MAC_HANDOFF)
#if defined(__SDCC)
static uint8_t handoff_clear_sfd(void) __naked
{
    __asm
        mov dptr,#0x6193
        movx a,@dptr
        jb acc.5,00001$
        mov _SOC_RFIRQF0,#0xfd
        movx a,@dptr
        anl a,#0x20
        orl a,#1
        sjmp 00002$
    00001$:
        mov a,#0x20
    00002$:
        mov dpl,a
        ret
    __endasm;
}
#else
static uint8_t handoff_clear_sfd(void)
{
    uint8_t signals = MMIO_XREAD(0x6193);
    if (!(signals & 0x20u)) {
        MMIO_WRITE(SOC_RFIRQF0, 0xfd);
        signals = MMIO_XREAD(0x6193);
        return (signals & 0x20u) | 1u;
    }
    return signals & 0x20u;
}
#endif

uint8_t radio_autoack_handoff_eligible(void)
{
    return !radio_autoack_fault && radio_autoack_state == RADIO_AUTOACK_RX_NOACK &&
        status.phase == 21 && status.result == RADIO_AUTOACK_FRAME &&
        work.cca == 4 && work.profile == 3 && !work.search_disabled &&
        work.prepared_length >= 3 && status.bytes_read == 6 &&
        staged.length == 3 && (staged.crc_correlation & 0x80u) &&
        (staged.body[0] & 7u) == 2u;
}

static radio_autoack_result_t handoff_empty(void)
{
    return !status.count && !(status.signals & 0xe2u) &&
        status.first == work.head && status.last == work.head ?
        RADIO_AUTOACK_READY : RADIO_AUTOACK_HANDOFF_RACE;
}

static radio_autoack_result_t handoff_sample(mac_time_stamp_t MCU_XDATA * volatile output)
{
    const mac_time_stamp_t MCU_XDATA *raw;
    mac_time_result_t sampled;
    radio_autoack_result_t result;
    do {
        sampled = mac_time_handoff_read();
        if (sampled == MAC_TIME_OK) {
            raw = mac_time_handoff_value();
            output->fine = raw->fine; output->periods = raw->periods;
            return RADIO_AUTOACK_READY;
        }
        if (sampled != MAC_TIME_PENDING) return RADIO_AUTOACK_ATTEMPT_TIMER_ERROR;
        result = progress();
        if (result != RADIO_AUTOACK_READY) return result;
    } while (status.polls < work.limit);
    return RADIO_AUTOACK_WORK_LIMIT;
}

radio_autoack_result_t radio_autoack_handoff(volatile uint32_t timeout, volatile uint16_t limit,
                                            radio_autoack_handoff_clock_t MCU_XDATA * volatile clock)
{
    volatile MCU_XDATA radio_autoack_result_t result;
    if (radio_autoack_fault) return (radio_autoack_result_t)radio_autoack_fault;
    if (!clock || !timeout || timeout >= TIMEBASE_HALF_RANGE || !limit)
        return RADIO_AUTOACK_INVALID_ARGUMENT;
    if (!radio_autoack_handoff_eligible()) return RADIO_AUTOACK_STATE;
    result = storage(MMIO_XADDRESS(clock), sizeof(*clock));
    if (result != RADIO_AUTOACK_READY) return result;
    memset(&status, 0, sizeof(status)); work.limit = limit;
    work.start = timebase_read_awake_ticks24(); work.previous = work.start;
    status.timebase_status = timebase_deadline_after(work.start, timeout, &work.deadline);
    REQUIRE(status.timebase_status == TIMEBASE_OK, RADIO_AUTOACK_TIME_ERROR);
    status.phase = 22;
    CHECK(poll()); CHECK(handoff_empty());
    REQUIRE(work.expected_mask == 1 && work.tx_count == work.prepared_length + 1u &&
            work.tx_last == work.prepared_length + 1u &&
            (MMIO_XREAD(0x6081) & 0x20u) && MMIO_XREAD(0x6083) == staged.body[2],
            RADIO_AUTOACK_STATE_CHANGED);
    ROOM();
    REQUIRE(mac_time_attempt_begin(timeout, limit, &clock->last) == MAC_TIME_OK,
            RADIO_AUTOACK_ATTEMPT_TIMER_ERROR);
    CHECK(handoff_sample(&clock->before));
    work.byte = handoff_clear_sfd();
    if (work.byte & 1u) status.writes++;
    REQUIRE(!(work.byte & 0x20u), RADIO_AUTOACK_HANDOFF_RACE);
    CHECK(handoff_sample(&clock->after));
    REQUIRE(mac_time_attempt_end(timeout, limit, &clock->last) == MAC_TIME_OK,
            RADIO_AUTOACK_ATTEMPT_TIMER_ERROR);
    CHECK(attempt_elapsed(&clock->before, &clock->after));
    /* Flash-cache stalls preclude a cycle-count timing assertion. Require a
     * measured interval below one symbol, strictly shorter than the PHR byte.
     */
    REQUIRE(attempt.ticks < 512u, RADIO_AUTOACK_HANDOFF_RACE);
    CHECK(poll()); CHECK(handoff_empty());
    REQUIRE(!(status.flags0 & 2u), RADIO_AUTOACK_HANDOFF_RACE);
    ROOM();
    MMIO_XWRITE(0x6189, 0x60); work.profile = 2; status.writes++;
    CHECK(poll()); CHECK(handoff_empty());
    REQUIRE(!(status.flags0 & 2u), RADIO_AUTOACK_HANDOFF_RACE);
    ROOM();
    MMIO_XWRITE(0x6180, RADIO_AUTOACK_NORMAL_FILTER); work.profile = 0; status.writes++;
    CHECK(poll()); CHECK(handoff_empty());
    REQUIRE(!(status.flags0 & 2u), RADIO_AUTOACK_HANDOFF_RACE);
    radio_autoack_state = RADIO_AUTOACK_RX;
    status.phase = 23; status.result = RADIO_AUTOACK_READY;
    return RADIO_AUTOACK_READY;
failed:
    radio_autoack_fault = result; radio_autoack_state = RADIO_AUTOACK_FAULT;
    status.result = result;
    return result;
}
#endif
#if defined(CC2530_MAC_RECONFIG)
/* SWRU191F pp.214,256: frame-filter address RAM and FREQCTRL, written only
 * while idle. owned changes with each byte so every full-profile observation
 * compares the exact intended register set, including the unchanged rest.
 */
static uint8_t reconfigure_value(const radio_autoack_config_t MCU_XDATA *configuration)
{
    if (work.index == 8) return (uint8_t)configuration->pan;
    if (work.index == 9) return (uint8_t)(configuration->pan >> 8);
    if (work.index == 10) return (uint8_t)configuration->short_address;
    if (work.index == 11) return (uint8_t)(configuration->short_address >> 8);
    return (uint8_t)(11u + 5u * (configuration->channel - 11u));
}

radio_autoack_result_t radio_autoack_configure(
    const radio_autoack_config_t MCU_XDATA *configuration, uint32_t timeout, uint16_t limit)
{
    radio_autoack_result_t result;
    uint8_t i, value;
    if (radio_autoack_fault) return (radio_autoack_result_t)radio_autoack_fault;
    if (!configuration || !timeout || timeout >= TIMEBASE_HALF_RANGE || !limit)
        return RADIO_AUTOACK_INVALID_ARGUMENT;
    if (radio_autoack_state != RADIO_AUTOACK_OFF && radio_autoack_state != RADIO_AUTOACK_OFF_NOACK)
        return RADIO_AUTOACK_STATE;
    result = storage(MMIO_XADDRESS(configuration), sizeof(*configuration));
    if (result != RADIO_AUTOACK_READY) return result;
    if (configuration->channel < 11 || configuration->channel > 26 ||
        configuration->power != owned.power)
        return RADIO_AUTOACK_INVALID_ARGUMENT;
    for (i = 0; i < 8; i++)
        if (configuration->ieee[i] != owned.ieee[i]) return RADIO_AUTOACK_INVALID_ARGUMENT;
    memset(&status, 0, sizeof(status)); work.limit = limit;
    work.start = timebase_read_awake_ticks24(); work.previous = work.start;
    status.timebase_status = timebase_deadline_after(work.start, timeout, &work.deadline);
    REQUIRE(status.timebase_status == TIMEBASE_OK, RADIO_AUTOACK_TIME_ERROR);
    status.phase = 24;
    CHECK(poll()); CHECK(stopped());
    REQUIRE(status.flags1 & 4u, RADIO_AUTOACK_STATE_CHANGED);
    REQUIRE(!status.count && !(status.signals & 0xc0u), RADIO_AUTOACK_FIFO_ERROR);
    for (work.index = 8; work.index != 23; work.index = work.index == 11 ? 22 : work.index + 1u) {
        value = reconfigure_value(configuration);
        if (value == setting_value(work.index)) continue;
        ROOM();
        if (work.index == 8) owned.pan = (owned.pan & 0xff00u) | value;
        else if (work.index == 9) owned.pan = (owned.pan & 0x00ffu) | ((uint16_t)value << 8);
        else if (work.index == 10) owned.short_address = (owned.short_address & 0xff00u) | value;
        else if (work.index == 11)
            owned.short_address = (owned.short_address & 0x00ffu) | ((uint16_t)value << 8);
        else owned.channel = configuration->channel;
        MMIO_XWRITE(setting_address(work.index), value); status.writes++;
        CHECK(poll()); CHECK(stopped());
        REQUIRE(!status.count && !(status.signals & 0xc0u), RADIO_AUTOACK_FIFO_ERROR);
        status.verified = status.writes;
    }
    status.phase = 25; status.result = RADIO_AUTOACK_READY;
    return RADIO_AUTOACK_READY;
failed:
    radio_autoack_fault = result; radio_autoack_state = RADIO_AUTOACK_FAULT;
    status.result = result;
    return result;
}
#endif
MCU_XDATA uint8_t radio_autoack_reserved_end;
