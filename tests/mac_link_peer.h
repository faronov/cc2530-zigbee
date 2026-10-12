/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef MAC_LINK_PEER_H
#define MAC_LINK_PEER_H
#include "bdb_join.h"
/* Public synthetic coordinator from test_bdb_join.c, not a TC implementation. */
extern const security_keys_config_t link_identity;
extern uint8_t link_beacon[48], link_response[27], link_peer_body[125];
extern uint8_t link_beacon_length, link_response_length, link_peer_length, link_peer_pending;
extern unsigned link_peer_tx, link_peer_checks, link_peer_app, link_peer_parent;
/* Last APS-acknowledged application frame. */
extern uint16_t link_peer_report_cluster;
extern uint8_t link_peer_report[], link_peer_report_length, link_peer_report_counter;
void link_peer_setup(bdb_join_config_t *config);
/* Config, beacon and Association Response only; no DUT state is touched. */
void link_peer_config(bdb_join_config_t *config);
/* Coordinator state across a modeled DUT power cycle (secured rejoin). */
#define LINK_PEER_STATE_MAX 96
extern unsigned link_peer_rejoins;
extern uint8_t link_peer_mute_rejoin;
unsigned link_peer_save(uint8_t *out);
void link_peer_recover(bdb_join_config_t *config, const uint8_t *in, unsigned length);
void link_peer_transmitted(const uint8_t *body, uint8_t length);
void link_peer_transport(uint8_t request_ack);
void link_peer_application(ed_packet_t *packet);
void link_peer_receive_application(void);
#if defined(CC2530_REPLAY_POLICY_C)
void link_peer_receive_service(void);
#endif
void link_peer_verify(void);
#if defined(CC2530_PARENT_RECOVERY)
void link_peer_known_parent(security_keys_parent_t *parent);
void link_peer_alternate(uint8_t exhausted);
#endif
#endif
