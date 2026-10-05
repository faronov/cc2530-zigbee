.section .mcs51.common.flash_exec_template,"ax"
.globl flash_exec_template
flash_exec_template:
  mov dptr, #flash_exec_work
  movx a, @dptr
  mov b, a
  inc dptr
  movx a, @dptr
  mov r4, a
  inc dptr
  movx a, @dptr
  mov r5, a
  inc dptr
  movx a, @dptr
  mov r6, a
  inc dptr
  movx a, @dptr
  mov r7, a
  inc dptr
  movx a, @dptr
  mov r0, a
  inc dptr
  movx a, @dptr
  mov r1, a
  inc dptr
  mov dptr, #0x6270
  mov a, b
  movx @dptr, a
  movx a, @dptr
  xrl a, b
  cjne a, #0x80, .Lram_bad
  mov a, b
  jnb 0xe1, .Lram_wait
  mov dptr, #0x6273
  mov a, r4
  movx @dptr, a
  mov a, r5
  movx @dptr, a
  mov a, r6
  movx @dptr, a
  mov a, r7
  movx @dptr, a
  sjmp .Lram_wait
.Lram_bad:
  setb 0xf7
.Lram_wait:
  mov dptr, #0x6270
.Lram_poll:
  movx a, @dptr
  mov r4, a
  anl a, #0x83
  jz .Lram_done
  mov a, r0
  jnz .Lram_dec_low
  dec r1
.Lram_dec_low:
  dec r0
  mov a, r0
  orl a, r1
  jnz .Lram_poll
  mov dptr, #flash_exec_work
  mov a, #0x07
  movx @dptr, a
  inc dptr
  mov a, r4
  movx @dptr, a
.Lram_stop:
  sjmp .Lram_stop
.Lram_done:
  mov a, r4
  jb 0xe5, .Lram_abort
  mov a, b
  jb 0xe7, .Lram_error
  anl a, #0x0c
  xrl a, r4
  jnz .Lram_error
  clr a
  sjmp .Lram_return
.Lram_abort:
  mov a, #0x05
  sjmp .Lram_return
.Lram_error:
  mov a, #0x06
.Lram_return:
  mov dptr, #flash_exec_work
  movx @dptr, a
  inc dptr
  mov a, r4
  movx @dptr, a
  ret
.globl flash_exec_template_end
flash_exec_template_end:

.section .mcs51.common.text,"ax"
.globl flash_exec_enter_ram
.type flash_exec_enter_ram,@function
flash_exec_enter_ram:
  mov dptr, #(flash_exec_ram + 0x8000)
  clr a
  jmp @a+dptr
.size flash_exec_enter_ram, .-flash_exec_enter_ram
