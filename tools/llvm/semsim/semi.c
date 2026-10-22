// Static-initializer and constant-expression generic pointer proof.
typedef unsigned char u8;
typedef unsigned long u32;
typedef u8 __generic *gp;
#define NI __attribute__((noinline))
__data u8 d[4]; __idata u8 i[4]; __pdata u8 p[4]; __xdata u8 x[4];
const __code u8 c[4] = {0x61, 0x62, 0x63, 0x64};
__attribute__((section(".mcs51.xdata.simresult"))) volatile __xdata u8 R[256];
typedef union { gp p; u32 v; } gbits;
NI u32 bits(gp q) { gbits g; g.p = q; return g.v; }
NI gp from_d(u8 __data *q) { return (gp)q; }
NI gp from_i(u8 __idata *q) { return (gp)q; }
NI gp from_p(u8 __pdata *q) { return (gp)q; }
NI gp from_x(u8 __xdata *q) { return (gp)q; }
NI gp from_c(const u8 __code *q) { return (gp)q; }
NI u8 rd(gp q, u8 k) { return q[k]; }
const gp __code gt[6] = { (gp)d, (gp)(i + 1), (gp)p + 2, (gp)(x + 3), (gp)c, 0 };
struct S { u8 k; gp g; u8 z; };
const struct S __code s = { 7, (gp)(d + 2), 9 };
NI gp k0(void) { return (gp)(u8 __data *)0x30; }
NI gp k1(void) { return (gp)(u8 __data *)0x30 + 3; }
NI gp k2(void) { return (gp)(const u8 __code *)0x8000; }
NI gp k3(void) { return (gp)(u8 __pdata *)0x12 + 1; }
NI gp k4(void) { return (gp)(u8 __xdata *)0x1234; }
NI gp k5(void) { return (gp)(u8 __data *)0; }

int main(void) {
  u8 n = 0;
  d[2] = 0x13; i[1] = 0x22; p[2] = 0x33; x[3] = 0x44;
  R[n++] = bits(gt[0]) == bits(from_d(d));
  R[n++] = bits(gt[1]) == bits(from_i(i + 1));
  R[n++] = bits(gt[2]) == bits(from_p(p + 2));
  R[n++] = bits(gt[3]) == bits(from_x(x + 3));
  R[n++] = bits(gt[4]) == bits(from_c(c));
  R[n++] = bits(gt[5]) == 0;
  R[n++] = rd(gt[0], 2) == 0x13 && rd(gt[1], 0) == 0x22 && rd(gt[2], 0) == 0x33 &&
           rd(gt[3], 0) == 0x44 && rd(gt[4], 3) == 0x64;
  R[n++] = s.k == 7 && s.z == 9 && bits(s.g) == bits(from_d(d + 2)) && rd(s.g, 0) == 0x13;
  R[n++] = bits(k0()) == 0x400030UL;
  R[n++] = bits(k1()) == 0x400033UL;
  R[n++] = bits(k2()) == 0x808000UL;
  R[n++] = bits(k3()) == 0x600013UL;
  R[n++] = bits(k4()) == 0x001234UL;
  R[n++] = bits(k5()) == 0;
  R[0xFE] = n;
  R[0xFF] = 0xA5;
  for (;;) {}
}
