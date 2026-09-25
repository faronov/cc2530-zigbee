/* SPDX-License-Identifier: BSD-3-Clause
 * Synthetic caller commands only. Never flash this test composition.
 */
#include "mac_adapter.h"
#include <string.h>

volatile MCU_XDATA MCU_AT(0x1e00) uint8_t fixture_status[8];
MCU_XDATA mac_tx_interval_t fixture_tx;
MCU_XDATA mac_tx_interval_action_t fixture_action;
MCU_XDATA mac_tx_interval_event_t fixture_random;
MCU_XDATA mac_epoch_stamp_t fixture_clock, fixture_through;
MCU_XDATA radio_autoack_config_t fixture_config;
MCU_XDATA mac_adapter_observation_t fixture_observation;
MCU_XDATA mac_adapter_diagnostics_t fixture_diagnostics;
MCU_XDATA mac_attempt_record_t fixture_record;
MCU_XDATA uint8_t fixture_packet[125];
MCU_XDATA uint8_t fixture_op, fixture_return, fixture_policy, fixture_selector, fixture_random_byte;
MCU_XDATA uint8_t fixture_length, fixture_dsn;
MCU_XDATA uint16_t fixture_limit, fixture_work;
MCU_XDATA uint16_t fixture_config_ptr, fixture_tx_ptr, fixture_action_ptr, fixture_clock_ptr, fixture_through_ptr;
MCU_XDATA uint32_t fixture_timeout, fixture_lifetime, fixture_token;

void fixture_cycle(void)
{
    __asm
        .globl _fixture_before
    _fixture_before:
        nop
    __endasm;
    if (fixture_op == 0) fixture_return = mac_adapter_init(
        (const radio_autoack_config_t MCU_XDATA *)fixture_config_ptr, fixture_timeout, fixture_limit);
    else if (fixture_op == 1) fixture_return = mac_adapter_now(fixture_timeout, fixture_limit,
        (mac_epoch_stamp_t MCU_XDATA *)fixture_clock_ptr);
    else if (fixture_op == 2) fixture_return = mac_tx_interval_init(&fixture_tx, fixture_dsn, fixture_clock.symbols);
    else if (fixture_op == 3) fixture_return = mac_tx_interval_submit(&fixture_tx, fixture_packet, fixture_length,
        fixture_diagnostics.live.symbols, fixture_lifetime, fixture_work);
    else if (fixture_op == 4) fixture_return = mac_adapter_prepare(
        (const mac_tx_interval_t MCU_XDATA *)fixture_tx_ptr, fixture_policy, fixture_timeout, fixture_limit);
    else if (fixture_op == 5) {
        memset(&fixture_random, 0, sizeof(fixture_random));
        fixture_random.source.kind = fixture_selector == 3 ? MAC_TX_EVENT_CANCEL : MAC_TX_EVENT_RANDOM;
        fixture_random.source.value = fixture_random_byte;
        fixture_random.source.generation = fixture_tx.engine.generation;
        fixture_random.source.retry = fixture_tx.engine.retries;
        fixture_random.source.nb = fixture_tx.engine.nb;
        fixture_random.source.stamp = fixture_diagnostics.live.symbols;
        fixture_return = mac_tx_observed_step(&fixture_tx, fixture_diagnostics.live.symbols,
            fixture_selector == 1 || fixture_selector == 3 ? &fixture_random :
            fixture_selector == 2 && fixture_diagnostics.ready && fixture_observation.tx.source.kind ?
            &fixture_observation.tx : NULL, &fixture_action);
    } else if (fixture_op == 6) fixture_return = mac_adapter_accept(
        (const mac_tx_interval_t MCU_XDATA *)fixture_tx_ptr,
        (const mac_tx_interval_action_t MCU_XDATA *)fixture_action_ptr);
    else if (fixture_op == 7) fixture_return = mac_adapter_step(fixture_timeout, fixture_limit);
    else if (fixture_op == 8) fixture_return = mac_adapter_consume(fixture_token);
    else if (fixture_op == 9) fixture_return = mac_adapter_close(
        (const mac_epoch_stamp_t MCU_XDATA *)fixture_through_ptr);
    else if (fixture_op == 10) fixture_return = mac_adapter_unprepare(
        (const mac_tx_interval_t MCU_XDATA *)fixture_tx_ptr, fixture_timeout, fixture_limit);
    else fixture_return = mac_tx_interval_release(&fixture_tx);
    fixture_observation = *mac_adapter_observation();
    fixture_diagnostics = *mac_adapter_diagnostic();
    fixture_record = *mac_adapter_record();
    __asm
        .globl _fixture_done
    _fixture_done:
        nop
    __endasm;
}

void main(void)
{
    memcpy((void *)fixture_status, "MAD1", 4);
    fixture_status[4] = 1; fixture_status[5] = 8;
    fixture_status[6] = 0; fixture_status[7] = 0;
    for (;;) fixture_cycle();
}
