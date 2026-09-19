/* SPDX-License-Identifier: BSD-3-Clause */
#include "transport.h"

bool stim_uart_put(struct stim_uart *u, uint8_t byte)
{
    if (u->rx_fault || u->count == STIM_UART_RING) {
        u->rx_fault = true;
        return false;
    }
    u->rx[(u->head + u->count) % STIM_UART_RING] = byte;
    ++u->count;
    return true;
}

bool stim_uart_take(struct stim_uart *u, uint8_t *out)
{
    if (out == NULL) {
        u->rx_fault = true;
        return false;
    }
    if (u->rx_fault || u->count == 0u) {
        return false;
    }
    *out = u->rx[u->head];
    u->head = (u->head + 1u) % STIM_UART_RING;
    --u->count;
    return true;
}

bool stim_uart_begin(struct stim_uart *u, size_t length, uint32_t now)
{
    if (u->tx_fault || u->busy || length == 0u || length > STIM_LINE_MAX) {
        u->tx_fault = true;
        return false;
    }
    u->position = 0;
    u->length = length;
    u->started = now;
    u->work = 0;
    u->busy = true;
    return true;
}

bool stim_uart_advance(struct stim_uart *u, size_t requested, int written)
{
    if (u->tx_fault || !u->busy || requested == 0u || requested > STIM_UART_SLICE ||
        requested > u->length - u->position || written <= 0 || (size_t)written > requested) {
        u->tx_fault = true;
        return false;
    }
    u->position += (size_t)written;
    return true;
}

bool stim_uart_complete(struct stim_uart *u)
{
    if (u->tx_fault || !u->busy || u->position != u->length) {
        u->tx_fault = true;
        return false;
    }
    u->busy = false;
    return true;
}

void stim_uart_tick(struct stim_uart *u, uint32_t now)
{
    if (u->busy) {
        if ((uint32_t)(now - u->started) >= STIM_UART_TX_MS || u->work == STIM_WORK_MAX) {
            u->tx_fault = true;
        } else {
            ++u->work;
        }
    }
}
