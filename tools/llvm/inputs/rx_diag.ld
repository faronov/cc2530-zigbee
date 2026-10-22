/* FIRMWARE_VARIANT=rx-diag only (appended to project-ordered-pack.ld). */
SECTIONS
{
  .mcs51_rx_diag 0x1e40 (NOLOAD) : { KEEP(*(.mcs51.xdata.fixed.rx_diag)) }
}
ASSERT(ADDR(.mcs51_rx_diag) == 0x1e40 && SIZEOF(.mcs51_rx_diag) == 80, "RX_DIAG not 80 bytes at 0x1e40")
ASSERT(ADDR(.mcs51_fixed_xdata) + SIZEOF(.mcs51_fixed_xdata) <= 0x1e40, "RX_DIAG overlaps M0 status")
ASSERT(ADDR(.mcs51_rx_diag) + SIZEOF(.mcs51_rx_diag) <= 0x1f00, "RX_DIAG reaches the IRAM alias")
