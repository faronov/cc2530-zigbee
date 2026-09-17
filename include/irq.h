/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_IRQ_H
#define CC2530_IRQ_H

#include <stdint.h>

typedef uint8_t irq_state_t;

typedef enum {
    IRQ_OK = 0,
    IRQ_INVALID_TOKEN
} irq_result_t;

#if defined(__SDCC)
#define IRQ_ABI __reentrant
#else
#define IRQ_ABI
#endif

/* Save EA as 0/1 and atomically disable it. Restore exactly that state.
 * Tokens belong to the saving context and must be restored once, in LIFO order.
 * Forged 0/1, reused or out-of-order tokens cannot be detected. No independent
 * EA writer may override an active section. This is not a peripheral/DMA lock.
 *
 * Restore rejects 2..255 before any interrupt/peripheral access, returning
 * IRQ_INVALID_TOKEN without changing EA or any other enable/flag register.
 * There are no output pointers; a token passed by value remains caller-owned.
 * Validate wider external values before narrowing to this byte-token API.
 * Enabling EA can admit an ISR before restore returns.
 *
 * SDCC implementations are register-only, reentrant leaves, also usable from
 * ABI-correct ISRs. Shared data still needs appropriate volatile qualifiers.
 * Ordinary CPU ABI clobbers are distinct from interrupt/peripheral accesses.
 * IRQ delivery/acknowledgment, priorities and ISR dispatch are not provided.
 */
irq_state_t irq_save_disable(void) IRQ_ABI;
irq_result_t irq_restore(irq_state_t token) IRQ_ABI;

#undef IRQ_ABI

#endif
