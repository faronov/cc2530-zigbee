; CC2530_RX_DIAG v1 block (include/cc2530_rx_diag.h), linked only by the
; FIRMWARE_VARIANT=rx-diag LLVM study image.  Like join_smoke_status, it is a
; typed NOBITS XDATA input section pinned by rx_diag.ld (SDCC uses __at), so
; it is outside .bss; join_smoke_initialize writes every byte at reset.
        .section .mcs51.xdata.fixed.rx_diag,"aw",@nobits
        .globl cc2530_rx_diag
        .type cc2530_rx_diag,@object
cc2530_rx_diag:
        .space 80
        .size cc2530_rx_diag, 80
