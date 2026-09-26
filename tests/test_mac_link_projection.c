/* SPDX-License-Identifier: BSD-3-Clause
 * Real radio/epoch services over synthetic MMIO; never access equipment.
 */
#define MAC_HANDOFF_MAIN projection_component_main
#include "test_mac_handoff.c"

static mac_attempt_record_t projection_saved;
static mac_epoch_stamp_t projection_stamps[5];
static unsigned projection_active, projection_count, projection_failure;
static unsigned payload_length, payload_replaced;

mac_epoch_result_t mac_attempt_projection_step(mac_epoch_t MCU_XDATA *ctx,
    const mac_time_stamp_t MCU_XDATA *raw, mac_epoch_stamp_t MCU_XDATA *output)
{
    unsigned i;
    mac_epoch_result_t result;
    if (projection_active) {
        assert(ctx == &mac_attempt_first && projection_count < 5);
        assert(!memcmp(&receipt, &projection_saved, sizeof(receipt)));
        projection_count++;
        if (projection_count == 1) {
            /* Unused private tail bytes must not leak into the public frame. */
            for (i = mac_attempt_raw.frame.length; i < 125; i++)
                mac_attempt_raw.frame.body[i] = 0xb6;
        }
        if (projection_count == projection_failure) {
            mac_time_stamp_t invalid = *raw;
            invalid.fine = MAC_TIME_FINE_PERIOD;
            return mac_epoch_step(ctx, &invalid, output);
        }
    }
    result = mac_epoch_step(ctx, raw, output);
    if (projection_active && result == MAC_EPOCH_OK) {
        i = raw == &mac_attempt_raw.before ? 0 : raw == &mac_attempt_raw.armed ? 1 :
            raw == &mac_attempt_raw.tx ? 2 : raw == &mac_attempt_raw.rx ? 3 : 4;
        assert(i != 4 || raw == &mac_attempt_raw.last);
        projection_stamps[i] = *output;
    }
    return result;
}

static void check_stamp(const mac_epoch_stamp_t *actual, const mac_epoch_stamp_t *expected)
{
    assert(actual->symbols == expected->symbols && actual->fine == expected->fine);
}

static void replace_payload(void)
{
    if (payload_replaced || !packets) return;
    assert(packets == 1 && remaining == 6 && count == 6 && !rfd_reads);
    /* Replace the model's default ACK before its first FIFO read. */
    head = tail = count = packets = packet_head = packet_tail = remaining = 0;
    enqueue(payload_length + 2, bad_reply ? 0x69 : 0xe9, 0x5a);
    payload_replaced = 1;
}

static uint8_t projection_load(uint8_t address, uint8_t value)
{
    replace_payload();
    return handoff_load(address, value);
}

static uint8_t projection_xload(uint16_t address)
{
    replace_payload();
    return handoff_xload(address);
}

static void projection_case(mac_radio_result_t outcome, unsigned failure, unsigned size)
{
    unsigned i, expected = outcome == MAC_RADIO_CCA_BUSY ? 3 : outcome == MAC_RADIO_EMPTY ? 4 : 5;
    case_number = 1000 + 10 * outcome + failure;
    response_fcf = 2;
    clock_address = 0xf00;
    handoff_reset();
    handoff_call(0, MAC_RADIO_READY);
    handoff_call(1, MAC_RADIO_STOPPED);
    ((uint8_t *)&frame.value)[0] = 0x61;
    ((uint8_t *)&frame.value)[2] = 0x5a;
    handoff_call(2, MAC_RADIO_READY);
    cca_clear = outcome != MAC_RADIO_CCA_BUSY;
    reply_after_tx = outcome != MAC_RADIO_EMPTY;
    bad_reply = outcome == MAC_RADIO_BAD_CRC;
    payload_length = size;
    payload_replaced = 0;
    host_mmio_read_hook = projection_load;
    host_mmio_xread_hook = projection_xload;
    memcpy(&projection_saved, &receipt, sizeof(receipt));
    memset(projection_stamps, 0, sizeof(projection_stamps));
    projection_failure = failure;
    projection_count = 0;
    projection_active = 1;
    handoff_call(3, failure ? MAC_RADIO_EPOCH_ERROR : outcome);
    projection_active = 0;
    assert(projection_count == (failure ? failure : expected));
    if (failure) {
        assert(!memcmp(&receipt, &projection_saved, sizeof(receipt)));
        assert(mac_attempt_diagnostic()->fault == MAC_RADIO_EPOCH_ERROR);
        assert(mac_attempt_diagnostic()->phase == MAC_RADIO_FAULT);
        for (i = 0; i < 8; i++) handoff_call(i, MAC_RADIO_EPOCH_ERROR);
        assert(projection_count == failure);
        return;
    }
    assert(receipt.slot == mac_attempt_slot && receipt.length == body_length);
    assert(receipt.transmitted == mac_attempt_raw.transmitted);
    assert(receipt.received == mac_attempt_raw.received);
    assert(receipt.within_window == mac_attempt_raw.within_window);
    if (receipt.transmitted)
        projection_stamps[0].symbols += 16u + 2u * body_length;
    else
        memset(&projection_stamps[0], 0, sizeof(projection_stamps[0]));
    check_stamp(&receipt.tx_lower, &projection_stamps[0]);
    check_stamp(&receipt.armed, &projection_stamps[1]);
    check_stamp(&receipt.tx_upper, &projection_stamps[2]);
    check_stamp(&receipt.rx_upper, &projection_stamps[3]);
    check_stamp(&receipt.last, &projection_stamps[4]);
    if (receipt.received) {
        assert(payload_replaced && receipt.frame.length == size);
        assert(receipt.frame.rssi_raw == mac_attempt_raw.frame.rssi_raw);
        assert(receipt.frame.crc_correlation == mac_attempt_raw.frame.crc_correlation);
        assert(!memcmp(receipt.frame.body, mac_attempt_raw.frame.body, size));
    } else {
        assert(!receipt.frame.length && !receipt.frame.rssi_raw && !receipt.frame.crc_correlation);
    }
    for (i = receipt.frame.length; i < 125; i++) assert(!receipt.frame.body[i]);
    handoff_started = 1;
    if (outcome == MAC_RADIO_FRAME && size == 3)
        handoff_call(7, MAC_RADIO_READY);
    else
        handoff_call(7, MAC_RADIO_STATE);
    handoff_started = 0;
    reply_after_tx = 0;
    reply_remaining = 0;
    if (mode == 7) {
        mode = 2; XR(0x6192) = 0; XR(0x6193) = (XR(0x6193) & 0xc8) | 5; XR(0x6199) = 1;
    }
    handoff_call(1, MAC_RADIO_STOPPED);
    handoff_call(5, MAC_RADIO_READY);
}

int main(int argc, char **argv)
{
    static const mac_radio_result_t outcomes[] = {
        MAC_RADIO_FRAME, MAC_RADIO_BAD_CRC, MAC_RADIO_EMPTY, MAC_RADIO_CCA_BUSY
    };
    static const unsigned sizes[] = {4, 64, 125};
    unsigned i, failure, limit;
    assert(projection_component_main(argc, argv) == 0);
    for (i = 0; i < sizeof(outcomes) / sizeof(outcomes[0]); i++) {
        limit = outcomes[i] == MAC_RADIO_CCA_BUSY ? 3 : outcomes[i] == MAC_RADIO_EMPTY ? 4 : 5;
        for (failure = 0; failure <= limit; failure++) projection_case(outcomes[i], failure, 3);
    }
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        projection_case(MAC_RADIO_FRAME, 0, sizes[i]);
    for (failure = 1; failure <= 5; failure++)
        projection_case(MAC_RADIO_FRAME, failure, 125);
    puts("Compact projection: 29 scenarios/22 injected projection failures; atomic receipt and retained handoff PASS.");
    return 0;
}
