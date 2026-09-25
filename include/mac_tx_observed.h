/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_TX_OBSERVED_H
#define MAC_TX_OBSERVED_H
#include "mac_tx_interval.h"

#if defined(CC2530_MAC_OBSERVED)
#if !defined(CC2530_MAC_INTERVAL)
#error Observed MAC events require the explicit interval owner
#endif
#define MAC_TX_EVENT_BUSY_INTERVAL 11u
#define MAC_TX_EVENT_RETIRED 12u

/* Uses the same interval owner, initialization, submission and release.
 * Old interval events keep their contracts; the old step rejects these two
 * new sources rather than interpreting them as captured physical events.
 *
 * BUSY_INTERVAL proves one real busy CCA in [lower,upper], no transmission,
 * and completed physical stop/drain with no future buffer use for the action.
 * lower is no earlier than the requested at+8 symbols. Report time may be
 * later than the CCA bound because real cleanup is required first.
 *
 * RETIRED proves no future TX/buffer access for this action at upper. It is
 * NOT a captured RF-off time: an independent RX/ACK lease may remain live.
 * Only upper is used. If TX was uncertain, it conservatively bounds the last
 * possible TX completion for IFS; a fault is not proof of retirement.
 *
 * Both echo the actual action identity, use source.stamp as report time, and
 * require report >= ceil(upper). now is a real observed symbol boundary, never
 * a fabricated clock value on failure. The adapter reports clock loss out of
 * band and retains the outstanding owner instead of feeding a stale stamp.
 * ATTEMPT.at remains the requested schedule; these bounds do not attest exact
 * hardware CCA start, calibration or PHY conformance.
 */
mac_tx_result_t mac_tx_observed_step(mac_tx_interval_t * volatile tx, uint32_t now,
    const mac_tx_interval_event_t * volatile event,
    mac_tx_interval_action_t * volatile action) JOIN_FAR;
#endif
#endif
