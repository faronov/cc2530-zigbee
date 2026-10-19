#include "m.h"
B1 u16 b1_f5w(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
{ P16(s,0,a); P16(s,2,b); P16(s,4,c); P16(s,6,d); P16(s,8,e); return a ^ e; }
B1 u16 b1_same(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e)
{ P16(s,0,a); P16(s,2,b); P16(s,4,c); P16(s,6,d); P16(s,8,e); return b ^ e; }
B1 u16 b1_regonly(u8 s, u16 a, u16 b)
{ P16(s,0,a); P16(s,2,b); return a + b; }
B1 void b1_driver(void)
{
    ret16[3] = b2_f5w(3, 0x0301, 0x0302, 0x0303, 0x0304, 0x3e3e);      /* bank1 -> bank2 */
    ret16[4] = b1_same(4, 0x0401, 0x0402, 0x0403, 0x0404, 0x4e4e);     /* same explicit bank, stack args */
    ret16[5] = b1_regonly(5, 0x0501, 0x0502);                         /* same bank, registers only */
}
