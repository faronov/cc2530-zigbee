#include "m.h"
B2 u16 b2_f5w(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
{ P16(s,0,a); P16(s,2,b); P16(s,4,c); P16(s,6,d); P16(s,8,e); return c ^ e; }
B2 u32 b2_inner(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
{ P16(s,0,a); P16(s,2,b); P16(s,4,c); P16(s,6,d); P16(s,8,e); return ((u32)e << 16) | a; }
