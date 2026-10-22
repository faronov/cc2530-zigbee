#include "m.h"
volatile u8 out[20][16];
volatile u16 ret16[20];
volatile u32 ret32[20];
volatile u8 done;
typedef u16 (*f5w_t)(u8, u16, u16, u16, u16, u16);
volatile f5w_t fp_auto = a_f5w, fp_common = c_f5w;

__attribute__((noinline)) void matrix_done(void) { done = 0xa5; for (;;) {} }

int main(void)
{
    ret16[0] = b1_f5w(0, 0x0101, 0x0202, 0x0303, 0x0404, 0x1234);    /* root -> explicit bank, weak thunk */
    ret16[1] = a_f6b(1, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);          /* root -> autobank, u8 stack args */
    ret32[2] = a_fmix(2, 0x01020304, 0x05060708, 0x090a0b0c, 0x0d);   /* root -> autobank, u32 stack arg */
    b1_driver();                                                      /* slots 3,4,5 */
    a_driver();                                                       /* slots 6,7,13 */
    ret16[8] = fp_auto(8, 0x0801, 0x0802, 0x0803, 0x0804, 0x5678);    /* pointer -> autobank */
    ret16[9] = fp_common(9, 0x0901, 0x0902, 0x0903, 0x0904, 0x9abc); /* pointer -> common */
    ret16[10] = a_fva(10, 3, 0x1111, 0x2222, 0x3333);                 /* varargs autobank */
    ret16[11] = c_fva(11, 3, 0x4444, 0x5555, 0x6666);                 /* varargs common */
    ret32[12] = a_outer(12, 0x0c01, 0x0c02, 0x0c03, 0x0c04, 0x0cee);  /* nested auto -> bank2 */
    matrix_done();
    return 0;
}
