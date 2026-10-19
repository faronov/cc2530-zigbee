/* SPDX-License-Identifier: BSD-3-Clause
 * Minimal freestanding string declarations shared by SDCC and Clang MCS-51.
 */
#ifndef CC2530_STRING_H
#define CC2530_STRING_H

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);

#endif
