/* SPDX-License-Identifier: BSD-3-Clause */
extern __xdata unsigned char foo_home[3], bar_home[4], baz_home[2];
__xdata volatile unsigned char ordinary;

void main(void)
{
    ordinary = foo_home[0] + bar_home[0] + baz_home[0];
    for (;;) {}
}
