/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef NWK_APS_INTERNAL_H
#define NWK_APS_INTERNAL_H

#include "nwk_aps.h"
#include "sdcc_volatile.h"
#include "security_keys.h"
#include "mac_link_workspace_guard_internal.h"
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_internal.h"
#define nwk_aps_work (link_work_arena.protocol.parent.nwk)
#endif

#if defined(CC2530_MAC_LINK)
#define NWK_APS_ENGINE(tx) (&(tx)->engine)
#define NWK_APS_CANCEL_SOURCE (nwk_aps_work.cancellation.source)
#define NWK_APS_STEP mac_tx_observed_step
#define NWK_APS_SUBMIT mac_tx_interval_submit
#define NWK_APS_RELEASE mac_tx_interval_release
#else
#define NWK_APS_ENGINE(tx) (tx)
#define NWK_APS_CANCEL_SOURCE (nwk_aps_work.cancellation)
#define NWK_APS_STEP mac_tx_step
#define NWK_APS_SUBMIT mac_tx_submit
#define NWK_APS_RELEASE mac_tx_release
#endif

typedef union {
    mac_header_t mac_header;
    NWK_APS_EVENT_T cancellation;
    nwk_frame_info_t hint;
} nwk_aps_work_t;

extern MCU_XDATA security_keys_status_t nwk_aps_keys;
#if !defined(CC2530_MAC_LINK_WORKSPACE)
extern MCU_XDATA nwk_aps_work_t nwk_aps_work;
#endif
void nwk_aps_complete(nwk_aps_t * SDCC_VOLATILE ctx, SDCC_VOLATILE uint8_t result);
nwk_aps_result_t nwk_aps_broadcast_slot(nwk_aps_t * SDCC_VOLATILE ctx, SDCC_VOLATILE uint16_t source,
    SDCC_VOLATILE uint8_t sequence, SDCC_VOLATILE uint32_t now, uint8_t * SDCC_VOLATILE slot);
void nwk_aps_broadcast_put(nwk_aps_t * SDCC_VOLATILE ctx, SDCC_VOLATILE uint8_t slot,
    SDCC_VOLATILE uint16_t source, SDCC_VOLATILE uint8_t sequence, SDCC_VOLATILE uint32_t now);
nwk_aps_result_t nwk_aps_transmit(nwk_aps_t * SDCC_VOLATILE ctx, SDCC_VOLATILE uint8_t acknowledgment,
    SDCC_VOLATILE uint32_t now);
nwk_aps_result_t nwk_aps_transmit_poll(nwk_aps_t * SDCC_VOLATILE ctx, SDCC_VOLATILE uint32_t now);
/* Short/short PAN-compressed MAC header to the current parent; ack request. */
void nwk_aps_poll_header(SDCC_VOLATILE uint8_t rejoin);

#endif
