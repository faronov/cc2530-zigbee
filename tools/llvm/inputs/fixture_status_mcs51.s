; SPDX-License-Identifier: BSD-3-Clause
; M0 debugger ABI: 32-byte object in the existing 64-byte XDATA reservation.
; Use a real typed XDATA section, not an absolute --defsym target.
        .section .mcs51.xdata.fixed.m0_status,"aw",@nobits
        .globl m0_status
        .type m0_status,@object
m0_status:
        .space 32
        .size m0_status, 32
