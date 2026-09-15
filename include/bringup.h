/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_BRINGUP_H
#define CC2530_BRINGUP_H

#include <stddef.h>
#include "board.h"

#define M0_STATUS_ADDRESS 0x1e00UL
#define M0_STATUS_RESERVED 64u
#define M0_STATUS_SIZE 32u
#define M0_IRAM_ALIAS 0x1f00UL
#define M0_ABI_VERSION 1u

enum m0_phase {
    M0_INITIALIZING = 1,
    M0_BOOTSTRAP_READY = 2
};

/* A debugger ABI, not a network packet. All fields are individual bytes. */
typedef struct {
    uint8_t signature[4];
    uint8_t abi_version;
    uint8_t byte_size;
    uint8_t phase;
    uint8_t board;
    uint8_t heartbeat;
    uint8_t policy;
    uint8_t ports[3];
    uint8_t directions[3];
    uint8_t selections[3];
    uint8_t pulls[3];
    uint8_t analog;
    uint8_t routing;
    uint8_t clock_request;
    uint8_t clock_status;
    uint8_t interrupt_enables[3];
    uint8_t reserved[3];
} m0_status_t;

#define ABI_AT(field, position) \
    typedef char m0_abi_##field[(offsetof(m0_status_t, field) == (position)) ? 1 : -1]
ABI_AT(signature, 0);
ABI_AT(abi_version, 4);
ABI_AT(byte_size, 5);
ABI_AT(phase, 6);
ABI_AT(board, 7);
ABI_AT(heartbeat, 8);
ABI_AT(policy, 9);
ABI_AT(ports, 10);
ABI_AT(directions, 13);
ABI_AT(selections, 16);
ABI_AT(pulls, 19);
ABI_AT(analog, 22);
ABI_AT(routing, 23);
ABI_AT(clock_request, 24);
ABI_AT(clock_status, 25);
ABI_AT(interrupt_enables, 26);
ABI_AT(reserved, 29);
#undef ABI_AT
typedef char m0_abi_size[(sizeof(m0_status_t) == M0_STATUS_SIZE) ? 1 : -1];
typedef char m0_reservation_fits[
    (M0_STATUS_SIZE <= M0_STATUS_RESERVED && M0_STATUS_ADDRESS + M0_STATUS_RESERVED <= M0_IRAM_ALIAS) ? 1 : -1];

extern volatile MCU_XDATA MCU_AT(M0_STATUS_ADDRESS) m0_status_t m0_status;

/* Foreground only. No interrupt handler or networking caller exists at M0. */
void bringup_initialize(void);
void bringup_tick(void);
unsigned char _sdcc_external_startup(void);

#endif
