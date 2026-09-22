/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_autoack.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>

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
} work;
/* SWRU191F pp.214,256-264; SWRS081B Table2 p.24. Address RAM first,
 * then the complete profile, all while idle. Only FSCAL1 has R/W0 high bits.
 */
static const MCU_CODE uint16_t settings[] = {
    0x6180, 0x6181, 0x6182, 0x6189, 0x618a, 0x6194, 0x6195,
    0x61b2, 0x61fa, 0x61ae, 0x618f, 0x6190, 0x6191
};
static const MCU_CODE uint8_t values[] = {
    0x01, 0x70, 0, 0x60, 0, 0x7f, 0, 0x15, 9, 0, 0, 5, 0x69
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
        if (MMIO_XREAD(0x6196) != 0xf8 ||
            (work.cca == 2 && MMIO_XREAD(0x6197) != 0x1a))
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
         radio_autoack_state != RADIO_AUTOACK_DRAIN_NOACK))
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
            if (radio_autoack_state == RADIO_AUTOACK_OFF_NOACK) {
                ROOM();
                MMIO_XWRITE(0x6180, 1); work.profile = 1; status.writes++;
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
MCU_XDATA uint8_t radio_autoack_reserved_end;
