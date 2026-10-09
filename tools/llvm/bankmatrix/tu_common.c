#include "m.h"
#include <stdarg.h>
COMMON u16 c_f5w(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
{ P16(s,0,a); P16(s,2,b); P16(s,4,c); P16(s,6,d); P16(s,8,e); return a + e; }
COMMON int c_fva(u8 s, u8 n, ...)
{ va_list ap; int sum = 0; u8 i; va_start(ap, n);
  for (i = 0; i < n; ++i) { int v = va_arg(ap, int); P16(s, 2*i, v); sum += v; }
  va_end(ap); return sum; }
