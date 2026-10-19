INCLUDE "@CC2530_SDCC_ORDER_PACK_LD@"
/* Study-only: the M0 status block and the SDCC runtime parameter-home guard
   names (aliases, see fixed_xdata_mcs51.s) live in a typed XDATA input section
   pinned at 0x1e00. Upstream LLD rejects XDATA-tagged relocations against
   absolute symbols, so --defsym/absolute assignments are no longer used. */
SECTIONS
{
  .mcs51_fixed_xdata 0x1e00 (NOLOAD) : { KEEP(*(.mcs51.xdata.fixed.m0_status)) }
}
ASSERT(ADDR(.mcs51_fixed_xdata) == 0x1e00 && SIZEOF(.mcs51_fixed_xdata) == 48, "M0 status not at 0x1e00")
ASSERT(ADDR(.data) + SIZEOF(.data) <= 0x1e00, "CC2530 .data overlaps M0 status")
ASSERT(ADDR(.bss) + SIZEOF(.bss) <= 0x1e00, "CC2530 .bss overlaps M0 status")
ASSERT(0x38000 + SIZEOF(.bank7) <= 0x3e800, "bank7 overlaps NV partition 0x3e800")
