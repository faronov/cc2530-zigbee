/* Runtime ABI matrix for CC2530 bank-call entry protocols. Every callee stores
 * the arguments it received into out[slot]; the checker compares them with the
 * values the caller passed. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
extern volatile u8 out[20][16];
extern volatile u16 ret16[20];
extern volatile u32 ret32[20];
#define P8(s, i, v) (out[s][i] = (u8)(v))
#define P16(s, i, v) (P8(s, i, v), P8(s, (i) + 1, (u16)(v) >> 8))
#define P32(s, i, v) (P16(s, i, (u16)(v)), P16(s, (i) + 2, (u16)((u32)(v) >> 16)))
#define B1 __attribute__((noinline, section(".bank1.text.m")))
#define B2 __attribute__((noinline, section(".bank2.text.m")))
#define COMMON __attribute__((noinline, section(".mcs51.common.text.m")))
#define NI __attribute__((noinline))

/* tu_b1.c: explicit bank 1 */
u16 b1_f5w(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e);          /* decl w/o section */
void b1_driver(void);
/* tu_b2.c: explicit bank 2, declared with its section for cross-bank callers */
B2 u16 b2_f5w(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e);
B2 u32 b2_inner(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e);
/* tu_auto.c: linker-assigned banks */
u16 a_f6b(u8 s, u8 a, u8 b, u8 c, u8 d, u8 e, u8 f);
u32 a_fmix(u8 s, u32 a, u32 b, u32 c, u8 d);
u16 a_f5w(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e);
int a_fva(u8 s, u8 n, ...);
u32 a_outer(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e);
void a_driver(void);
/* tu_common.c: common flash */
u16 c_f5w(u8 s, u16 a, u16 b, u16 c, u16 d, u16 e);
int c_fva(u8 s, u8 n, ...);
