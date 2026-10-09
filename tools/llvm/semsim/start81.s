  .section .text.0start,"ax"
  .globl __sim_start
__sim_start:
  mov sp, #0x81
  ljmp main
