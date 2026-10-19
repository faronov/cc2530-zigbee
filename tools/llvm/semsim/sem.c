// Generic pointer semantic proof. Each check writes one result byte to
// XDATA 0x100+n (1 = pass); 0x1FF = 0xA5 marks completion.
typedef unsigned char u8;
typedef unsigned long u32;
#define NI __attribute__((noinline))
__data u8 dv[4];
__idata u8 iv[2];
__pdata u8 pv[2];
__xdata u8 xv[2];
__code const u8 cv[2] = {0x51, 0x52};
__attribute__((section(".mcs51.xdata.simresult"))) volatile __xdata u8 R[256];
typedef union { u8 __generic *p; u32 v; u8 b[4]; } gbits;
struct gs { u8 __generic *p; u8 tag; };
volatile __xdata struct gs gsv;
u8 __generic * volatile __xdata garr[3];

NI u32 bits(u8 __generic *p) { gbits g; g.p = p; return g.v; }
NI u8 __generic *from_d(u8 __data *p) { return (u8 __generic *)p; }
NI u8 __generic *from_i(u8 __idata *p) { return (u8 __generic *)p; }
NI u8 __generic *from_p(u8 __pdata *p) { return (u8 __generic *)p; }
NI u8 __generic *from_x(u8 __xdata *p) { return (u8 __generic *)p; }
NI const u8 __generic *from_c(const u8 __code *p) { return (const u8 __generic *)p; }
NI u8 __data *to_d(u8 __generic *p) { return (u8 __data *)p; }
NI u8 __xdata *to_x(u8 __generic *p) { return (u8 __xdata *)p; }
NI u8 rd(const u8 __generic *p, u8 i) { return p[i]; }
NI void wr(u8 __generic *p, u8 i, u8 v) { p[i] = v; }
NI u8 eq(const u8 __generic *a, const u8 __generic *b) { return a == b; }
NI u8 isnull(const u8 __generic *a) { return a == 0; }
NI int diff(const u8 __generic *a, const u8 __generic *b) { return a - b; }
u8 __data * volatile __xdata dnull;   // runtime null DATA pointer
const u8 __code * volatile __xdata cnull;
u8 __pdata * volatile __xdata pnull;
u8 __xdata * volatile __xdata xnull;

int main(void) {
  u8 n = 0;
  dv[0]=0x11; dv[1]=0x12; dv[2]=0x13; dv[3]=0x14; iv[0]=0x21; iv[1]=0x22;
  pv[0]=0x31; pv[1]=0x32; xv[0]=0x41; xv[1]=0x42; dnull=0; cnull=0; pnull=0; xnull=0;
  u8 __data *dp = dv;
  // conversions, exact representation
  R[n++] = bits(from_d(dv)) == (0x400000UL | (u8)(unsigned)dv);
  R[n++] = bits(from_i(iv)) == (0x400000UL | (u8)(unsigned)iv);
  R[n++] = bits(from_p(pv)) == (0x600000UL | (u8)(unsigned)pv);
  R[n++] = bits(from_x(xv)) == (u32)(unsigned)xv;
  R[n++] = bits((u8 __generic *)from_c(cv)) == (0x800000UL | (unsigned)cv);
  // address of a DATA global as a value
  R[n++] = (u8)(unsigned)dp != 0 && *dp == 0x11;
  // null: constant and runtime conversions
  R[n++] = bits((u8 __generic *)(u8 __data *)0) == 0;
  R[n++] = bits(from_d(dnull)) == 0 && isnull(from_d(dnull));
  R[n++] = isnull((const u8 __generic *)from_c(cnull));
  R[n++] = isnull(from_p(pnull)) && isnull(from_x(xnull));
  R[n++] = !isnull(from_d(dv)) && !isnull(from_x(xv + 1));
  // equality
  R[n++] = eq(from_d(dv), from_d(dp));
  R[n++] = eq(from_d(dv), from_i((u8 __idata *)dv));  // same IRAM byte
  R[n++] = !eq(from_x(xv), from_p(pv));
  R[n++] = !eq(from_d(dv), from_d(dv + 1));
  // dereference through each space
  R[n++] = rd(from_d(dv), 2) == 0x13 && rd(from_i(iv), 1) == 0x22 &&
           rd(from_p(pv), 1) == 0x32 && rd(from_x(xv), 1) == 0x42 &&
           rd(from_c(cv), 1) == 0x52;
  wr(from_d(dv), 3, 0x99); wr(from_i(iv), 0, 0x98); wr(from_p(pv), 0, 0x97);
  wr(from_x(xv), 0, 0x96);
  R[n++] = dv[3] == 0x99 && iv[0] == 0x98 && pv[0] == 0x97 && xv[0] == 0x96;
  // round trip
  R[n++] = to_d(from_d(dv + 2)) == dv + 2;
  R[n++] = to_x(from_x(xv + 1)) == xv + 1;
  R[n++] = to_d(from_d(dnull)) == 0;
  // arithmetic keeps the tag
  R[n++] = bits(from_d(dv) + 3) == (0x400000UL | (u8)((unsigned)dv + 3));
  R[n++] = diff(from_c(cv) + 1, from_c(cv)) == 1;
  R[n++] = *(from_c(cv) + 1) == 0x52;
  // storage: struct and array of generic pointers
  gsv.p = from_p(pv); gsv.tag = 0x5A;
  R[n++] = sizeof(struct gs) == 5 && gsv.tag == 0x5A && bits(gsv.p) == (0x600000UL | (u8)(unsigned)pv);
  garr[0] = from_x(xv); garr[1] = from_d(dv); garr[2] = (u8 __generic *)from_c(cv);
  R[n++] = sizeof(garr) == 12 && *garr[1] == 0x11 && *garr[2] == 0x51 &&
           bits(garr[1]) == (0x400000UL | (u8)(unsigned)dv);
  R[n++] = sizeof(u8 __data *) == 1 && sizeof(u8 __generic *) == 4 &&
           sizeof(u8 __xdata *) == 2 && sizeof(u8 __code *) == 2;
  R[0xFE] = n;
  R[0xFF] = 0xA5;
  for (;;) {}
}
