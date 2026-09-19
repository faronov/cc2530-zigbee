/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NS51_TRANSPORT_H
#define NS51_TRANSPORT_H

#include "control.h"

#define STIM_UART_RING 256u
#define STIM_UART_SLICE 32u
#define STIM_UART_TX_MS 100u

struct stim_uart {
    uint8_t rx[STIM_UART_RING];
    char line[STIM_LINE_MAX];
    unsigned head, count;
    size_t length, position;
    uint32_t started;
    uint16_t work;
    bool rx_fault, tx_fault, busy;
};

/* Same exclusive critical section as control; no SDK calls or waiting. */
bool stim_uart_put(struct stim_uart *u, uint8_t byte);
bool stim_uart_take(struct stim_uart *u, uint8_t *out);
bool stim_uart_begin(struct stim_uart *u, size_t length, uint32_t now);
bool stim_uart_advance(struct stim_uart *u, size_t requested, int written);
bool stim_uart_complete(struct stim_uart *u);
void stim_uart_tick(struct stim_uart *u, uint32_t now);

#endif
