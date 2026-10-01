/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdint.h>

volatile __xdata __at(0x1000) uint8_t output[1024];

uint8_t branches(uint8_t x)
{
    if (x < 7) return 13;
    if (x & 1) return x ^ 0x5a;
    if (x > 90) return x + 3;
    return x - 7;
}

uint8_t leaf(uint8_t x)
{
    return x ^ 0xa5;
}

uint8_t caller(uint8_t x)
{
    return leaf(x);
}

uint8_t nested(uint8_t x) __reentrant
{
    volatile uint8_t saved = x + 1;
    if (x & 2) return saved + branches(x);
    return saved + leaf(x);
}

uint8_t loop(uint8_t x)
{
    uint8_t y = 0;
    while (x) {
        y += x;
        if (x == 7) return y;
        --x;
    }
    return y;
}

void done(void) __naked
{
    __asm
    _test_done:
        sjmp _test_done
    __endasm;
}

void main(void)
{
    uint16_t i;
    for (i = 0; i < 256; ++i) {
        output[i] = branches((uint8_t)i);
        output[256 + i] = caller((uint8_t)i);
        output[512 + i] = nested((uint8_t)i);
        output[768 + i] = loop((uint8_t)i);
    }
    done();
}
