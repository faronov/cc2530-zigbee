/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef MAC_LINK_PEER_H
#define MAC_LINK_PEER_H
#include "bdb_join.h"
/* Public synthetic coordinator from test_bdb_join.c, not a TC implementation. */
extern const security_keys_config_t link_identity;
extern uint8_t link_beacon[48], link_response[27], link_peer_body[125];
extern uint8_t link_beacon_length, link_response_length, link_peer_length, link_peer_pending;
extern unsigned link_peer_tx, link_peer_checks, link_peer_app, link_peer_parent;
void link_peer_setup(bdb_join_config_t *config);
void link_peer_transmitted(const uint8_t *body, uint8_t length);
void link_peer_transport(uint8_t request_ack);
void link_peer_application(ed_packet_t *packet);
void link_peer_verify(void);
#endif
