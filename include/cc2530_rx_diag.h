/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_RX_DIAG_H
#define CC2530_RX_DIAG_H

/* CC2530_RX_DIAG v1: opt-in H2 receive-path debugger ABI (docs/H2_RX_INVESTIGATION.md
 * section 10). Default builds do not define CC2530_RX_DIAG and contain neither the
 * object nor any hook. One shared source for SDCC and LLVM; the fixed address is
 * verified from each linked map/ELF by tools/rx_diag_decode.py.
 *
 * Write-only evidence: control logic never reads it, hooks add no MMIO/SFR or
 * timebase access (they copy values the driver already read), no payload, keys
 * or IEEE addresses. Read only from an already halted CPU (XDATA, 80 bytes).
 * Multi-byte fields are little-endian; ticks are 24-bit timebase ticks taken at
 * the start of a radio operation (not frame times). A service gap runs from the
 * start of the previous receive, or of the acquire/resume/send/attempt that left
 * the receiver on, to the start of the next receive: an upper bound on time the
 * RX FIFO was not drained. A stop starts an RX-off interval (no service gap
 * spans it). Counters saturate. Gaps saturate at 0xfffe ticks; 0xffff means no
 * valid previous stamp.
 */
#include <stddef.h>
#include <stdint.h>
#include "cc2530_mmio.h"

#define CC2530_RX_DIAG_ADDRESS 0x1e40UL
#define CC2530_RX_DIAG_SIZE 80u
#define CC2530_RX_DIAG_VERSION 1u
#define CC2530_RX_DIAG_LIMIT 0x1f00UL

enum { RX_DIAG_SITE_OBSERVE = 1, RX_DIAG_SITE_CONSUME = 2 };
enum { RX_DIAG_SERVICE_VALID = 1, RX_DIAG_OFF_VALID = 2 };

typedef struct {
    uint8_t signature[4];          /* "RXDG" */
    uint8_t version, size;
    uint8_t overflows, salvaged, flushes, rx_lost;
    uint16_t frames;               /* CRC-OK frames from receive and attempt */
    uint8_t bad_crc, first_valid;
    uint16_t max_service_gap;      /* see last_service_gap */
    uint16_t max_rx_off_gap;       /* stop start to the start of the next RX-on op */
    /* First RXOVERF latch: raw values the driver had already sampled. */
    uint8_t first_site, first_radio_state, first_radio_phase;
    uint8_t first_rferrf, first_fsmstat0, first_fsmstat1;
    uint8_t first_rxfifocnt, first_rxfirst, first_rxlast;
    uint8_t flags;                 /* private service/off stamp validity */
    uint32_t first_stamp;
    uint16_t first_gap, first_frames;
    uint8_t last_header[4];        /* FCF0, FCF1, DSN, PHR of the last FRAME */
    uint8_t first_header[4];       /* last_header frozen at the first latch */
    uint32_t service_stamp, off_stamp;
    /* First receive loss reported to the link driver (o->lossy). */
    uint8_t ctx_valid, ctx_bdb_phase, ctx_workspace, ctx_adapter_phase, ctx_adapter_goal;
    /* First BDB fail(); APS fields only when fail_workspace is RUNTIME. */
    uint8_t fail_valid, fail_bdb_phase, fail_result, fail_workspace;
    uint8_t fail_aps_result, fail_aps_retries, fail_aps_error;
    /* First uplink MAC failure (not ACKED/UNACKNOWLEDGED, not cancel/stop). */
    uint8_t tx_valid, tx_outcome, tx_retries, tx_nb, tx_special;
    uint8_t pad;                   /* zero; keeps every field naturally aligned */
    uint16_t last_service_gap;     /* gap before the latest receive */
    uint8_t reserved[8];
} cc2530_rx_diag_t;

#define RX_DIAG_AT(field, position) \
    typedef char rx_diag_abi_##field[(offsetof(cc2530_rx_diag_t, field) == (position)) ? 1 : -1]
RX_DIAG_AT(signature, 0);
RX_DIAG_AT(version, 4);
RX_DIAG_AT(size, 5);
RX_DIAG_AT(overflows, 6);
RX_DIAG_AT(salvaged, 7);
RX_DIAG_AT(flushes, 8);
RX_DIAG_AT(rx_lost, 9);
RX_DIAG_AT(frames, 10);
RX_DIAG_AT(bad_crc, 12);
RX_DIAG_AT(first_valid, 13);
RX_DIAG_AT(max_service_gap, 14);
RX_DIAG_AT(max_rx_off_gap, 16);
RX_DIAG_AT(first_site, 18);
RX_DIAG_AT(first_radio_state, 19);
RX_DIAG_AT(first_radio_phase, 20);
RX_DIAG_AT(first_rferrf, 21);
RX_DIAG_AT(first_fsmstat0, 22);
RX_DIAG_AT(first_fsmstat1, 23);
RX_DIAG_AT(first_rxfifocnt, 24);
RX_DIAG_AT(first_rxfirst, 25);
RX_DIAG_AT(first_rxlast, 26);
RX_DIAG_AT(flags, 27);
RX_DIAG_AT(first_stamp, 28);
RX_DIAG_AT(first_gap, 32);
RX_DIAG_AT(first_frames, 34);
RX_DIAG_AT(last_header, 36);
RX_DIAG_AT(first_header, 40);
RX_DIAG_AT(service_stamp, 44);
RX_DIAG_AT(off_stamp, 48);
RX_DIAG_AT(ctx_valid, 52);
RX_DIAG_AT(ctx_bdb_phase, 53);
RX_DIAG_AT(ctx_workspace, 54);
RX_DIAG_AT(ctx_adapter_phase, 55);
RX_DIAG_AT(ctx_adapter_goal, 56);
RX_DIAG_AT(fail_valid, 57);
RX_DIAG_AT(fail_bdb_phase, 58);
RX_DIAG_AT(fail_result, 59);
RX_DIAG_AT(fail_workspace, 60);
RX_DIAG_AT(fail_aps_result, 61);
RX_DIAG_AT(fail_aps_retries, 62);
RX_DIAG_AT(fail_aps_error, 63);
RX_DIAG_AT(tx_valid, 64);
RX_DIAG_AT(tx_outcome, 65);
RX_DIAG_AT(tx_retries, 66);
RX_DIAG_AT(tx_nb, 67);
RX_DIAG_AT(tx_special, 68);
RX_DIAG_AT(pad, 69);
RX_DIAG_AT(last_service_gap, 70);
RX_DIAG_AT(reserved, 72);
#undef RX_DIAG_AT
typedef char rx_diag_abi_total[(sizeof(cc2530_rx_diag_t) == CC2530_RX_DIAG_SIZE) ? 1 : -1];
/* After the 64-byte M0/JSN1 reservation at 0x1e00, below the IRAM alias. */
typedef char rx_diag_abi_fits[(CC2530_RX_DIAG_ADDRESS >= 0x1e40UL &&
    CC2530_RX_DIAG_ADDRESS + CC2530_RX_DIAG_SIZE <= CC2530_RX_DIAG_LIMIT) ? 1 : -1];
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "CC2530_RX_DIAG v1 is little-endian"
#endif

#if defined(CC2530_RX_DIAG)
/* SDCC: __at below; LLVM: typed fixed section (tools/llvm/inputs/rx_diag_mcs51.s). */
extern volatile MCU_XDATA MCU_AT(CC2530_RX_DIAG_ADDRESS) cc2530_rx_diag_t cc2530_rx_diag;
#define RX_DIAG (cc2530_rx_diag)
#define RX_DIAG_COUNT(field) do { if ((uint8_t)(RX_DIAG.field + 1u)) RX_DIAG.field++; } while (0)
/* Startup only (join_smoke_initialize), before any radio operation. */
#define RX_DIAG_RESET(index) do { \
    for ((index) = 0; (index) < CC2530_RX_DIAG_SIZE; (index)++) \
        ((volatile uint8_t MCU_XDATA *)&RX_DIAG)[(index)] = 0; \
    RX_DIAG.signature[0] = 'R'; RX_DIAG.signature[1] = 'X'; \
    RX_DIAG.signature[2] = 'D'; RX_DIAG.signature[3] = 'G'; \
    RX_DIAG.version = CC2530_RX_DIAG_VERSION; RX_DIAG.size = CC2530_RX_DIAG_SIZE; \
} while (0)
#endif

#endif
