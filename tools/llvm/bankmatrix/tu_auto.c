#include "m.h"
#include <stdarg.h>
NI u16 a_f6b(u8 s, u8 a, u8 b, u8 c, u8 d, u8 e, u8 f)
{ P8(s,0,a); P8(s,1,b); P8(s,2,c); P8(s,3,d); P8(s,4,e); P8(s,5,f); return (u16)e << 8 | f; }
NI u32 a_fmix(u8 s, u32 a, u32 b, u32 c, u8 d)
{ P32(s,0,a); P32(s,4,b); P32(s,8,c); P8(s,12,d); return c + d; }
NI u16 a_f5w(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
{ P16(s,0,a); P16(s,2,b); P16(s,4,c); P16(s,6,d); P16(s,8,e); return d ^ e; }
NI int a_fva(u8 s, u8 n, ...)
{ va_list ap; int sum = 0; u8 i; va_start(ap, n);
  for (i = 0; i < n; ++i) { int v = va_arg(ap, int); P16(s, 2*i, v); sum += v; }
  va_end(ap); return sum; }
NI u32 a_outer(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
{ u32 r = b2_inner(s, a, b, c, d, e); P16(s, 10, e); return r + 1; }
static NI u16 a_local(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
{ P16(s,0,a); P16(s,2,b); P16(s,4,c); P16(s,6,d); P16(s,8,e); return a ^ b ^ e; }
static NI u16 a_common_same(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
    __attribute__((section(".mcs51.common.text.same")));
static NI u16 a_common_same(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
{ P16(s,0,a); P16(s,2,b); P16(s,4,c); P16(s,6,d); P16(s,8,e); return e - a; }
NI void a_driver(void)
{
    ret16[6] = c_f5w(6, 0x0601, 0x0602, 0x0603, 0x0604, 0x6e6e);       /* auto -> common, other TU */
    ret16[7] = a_common_same(7, 0x0701, 0x0702, 0x0703, 0x0704, 0x7e7e); /* auto -> common, same TU */
    ret16[13] = a_local(13, 0x0d01, 0x0d02, 0x0d03, 0x0d04, 0xdede);   /* auto -> static auto */
}
