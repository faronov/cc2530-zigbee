/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef AES_REFERENCE_H
#define AES_REFERENCE_H
#if defined(__SDCC)
#error The mathematical reference is host-only, never a production fallback
#endif
#include <stdint.h>
void aes_reference_encrypt(const uint8_t key[16], const uint8_t input[16], uint8_t output[16]);
void aes_reference_check(void);
void aes_mmo_reference(const uint8_t *message, unsigned length, uint8_t hash[16]);
#endif
