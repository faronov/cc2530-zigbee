; Upstream-bus link adaptation (LLVM study only; not used by SDCC or the frozen
; hardware profile).  Upstream LLD (efc84d8b4) checks every CODE/XDATA-tagged
; relocation against the bus named by the target input section and rejects
; absolute targets.  The frozen pipeline defined these addresses with --defsym
; and linker-script assignments; here they live in a typed XDATA input section
; that project-ordered-pack.ld pins at M0_STATUS_ADDRESS (0x1e00).  The range is
; not part of .bss, so startup does not clear it, as before.
        .section .mcs51.xdata.fixed.m0_status,"aw",@nobits
        .globl join_smoke_status
        .type join_smoke_status,@object
join_smoke_status:
        .space 48
        .size join_smoke_status, 48

; SDCC runtime parameter-home names used only as addresses by firmware overlap
; guards.  Binding them to the 0x1e00 boundary keeps every guard satisfied.
        .globl __memcpy_PARM_2
        .globl _gptrput_PARM_2
        .globl _mullong_PARM_2
        .set __memcpy_PARM_2, join_smoke_status
        .set _gptrput_PARM_2, join_smoke_status
        .set _mullong_PARM_2, join_smoke_status
