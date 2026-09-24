# Bounded CC2530F256 / SDCC banking foundation

This is an **original, standalone target fixture**, not a stack migration or
a board firmware profile. Never flash it. Its flash/interrupt inputs are
synthetic controller facts supplied by an offline model. No GPIO, display,
RF, USB or debugger access is performed by these files.

Owned implementation: `include/banked.h`, `src/banked.c`,
`tests/banked_fixture.h`, `tests/banked_fixture.c`, and
`tests/banked_fixture_bank{1,2,7}.c`. Build integration, image packing,
complete linked-image verification and modeled replay are described in
[the acceptance contract](BANKED_CODE.md).
No old source, shared SFR declaration, image limit or simulator gate changes.

## Sources and license

All the new C/header/documentation work is original BSD-3-Clause.
No third-party banking implementation, SDK object, programmer routine or
manual example was copied. ABI facts were obtained by compiling original
small C probes and inspecting SDCC's actual `.asm`, `.rel`, `.rst`, `.map`
and Intel HEX output:

* SDCC **4.2.0 #13081**, Debian `4.2.0+dfsg-1`, mcs51, model-large.
  SDCC and its linked startup/library objects retain their upstream licenses;
  this document does not relicense compiler-generated/runtime dependencies.
  This link pulls only `crtstart`, `crtclear`, `crtxclear`, `crtxinit` from
  `large/mcs51.lib`: their installed source notices identify Erik Petrich,
  copyright2004, GPL-2.0-or-later with the SDCC library linking exception.
  No SDCC banking library object is linked.
* TI **SWRU191F**, April 2009, revised April 2014:
  section **2.2.2, p27, Figures 2-2/2-3**, and **2.2.5, pp33-34** establish
  the common flash area, bank window, independent XBANK and XMAP.
  **FMAP (9F), MAP[2:0], p34** selects physical32-KiB banks0..7 on F256
  and resets to1. **MEMCTR (C7), p34** defines XMAP bit3 and XBANK bits2..0.
  **2.3.1 and CPU/stack registers, pp34-36; Table2-3, pp37-39** supply
  DPTR/DPS, register-bank, stack and instruction facts.
  Pinned local PDF SHA-256:
  `a8fe8e92db33ad79c7f371075b0a464602a6db747614625d9f8d3e6be990b877`.
* Applicable CC2530 errata **SWRZ031, April 2009**, sections1.1/1.2, pp2-3,
  concern variable-length DMA and Timer2 latching. Neither service is used
  by this runtime. They specify no alternative FMAP/CPU banking behavior.
  Reviewed PDF SHA-256:
  `43de66f7b9ec94726dca6ddcc771ca455a0be1ddc379c0fabf314b40718323b7`.
* Flash execution remains the repository's unchanged original
  `src/flash_exec.c`, with its existing SWRU191F pp73-77 provenance and
  `include/flash_exec.h` contract. No new flash algorithm is introduced.

The linker extended-address switch was confirmed on the installed linker,
including actual emitted instructions. An upstream linker memory-summary
diagnostic was consulted to explain the otherwise fixed64-KiB limit; it was
not used as a banking implementation or imported into the repository.

## Address convention and build handoff

All objects are compiled with:

```text
-mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror
-Iinclude -Itests -DCC2530_BOARD=0
```

Use `CC2530_BOARD=1` for the other board-definition compilation. These
standalone files intentionally do not link either board's GPIO policy.
Common code includes all startup, runtime, flash engine, ISR, constant-read
helper and error handling. Banked modules add:

| Module | Compile options | Link area base |
| --- | --- | --- |
| `banked_fixture_bank1.c` | `--codeseg BANK1 --constseg BANK1_CONST` | `BANK1=0x18000`, `BANK1_CONST=0x19000` |
| `banked_fixture_bank2.c` | `--codeseg BANK2 --constseg BANK2_CONST` | `BANK2=0x28000`, `BANK2_CONST=0x29000` |
| `banked_fixture_bank7.c` | `--codeseg BANK7 --constseg BANK7_CONST` | `BANK7=0x78000`, `BANK7_CONST=0x7e7f8` |

Link object order is **flash_exec, banked, banked_fixture,
banked_fixture_bank1, banked_fixture_bank2, banked_fixture_bank7**. The first
two form a reserved XDATA prefix; caller buffers must follow their sentinels.
Link-only options, in addition to the common compiler options:

```text
--iram-size 0x100 --xram-loc 0 --xram-size 0x1e00
--code-size 0x80000 --stack-size 0x5b -Wl-r
-Wl-bBANK1=0x18000 -Wl-bBANK2=0x28000 -Wl-bBANK7=0x78000
-Wl-bBANK1_CONST=0x19000 -Wl-bBANK2_CONST=0x29000
-Wl-bBANK7_CONST=0x7e7f8
```

`-Wl-r` enables the linker's extended-address representation/limit. It does
**not** select a different CPU or make mcs51 calls24-bit: the linked startup
and banked fixture still use two-byte return addresses, `LCALL`/`LJMP`,
`RET` and `RETI`. `--model-huge` is not used. Raising `--code-size` alone
failed the genuine link. Deprecated `--stack-loc` did not establish the
claimed stack placement and is not part of this profile.

For common bytes, virtual and physical addresses are identical below8000.
For a bank N byte:

```text
virtual = (N << 16) | CPU_address,   N=1..7, CPU_address=8000..ffff
physical = N * 8000 + (CPU_address - 8000)       [hexadecimal constants]
```

Reject virtual bank0 aliases in the upper window, unassigned areas, crossing
bank spans, physical collisions, holes in required objects, and all
NV/configuration output. In bank7, usable CPU addresses end at **e7ff**:
physical **3e800..3f7ff** are NV pages125/126 and **3f800..3ffff** is the
entire lock/configuration page127. The information page is never an image
destination. Bank7's eight-byte CODE constant ends exactly at physical3e7ff.
The larger *virtual* link limit is not a physical capacity or allocation
proof, and must never replace the unchanged old `--code-size 0x8000`.

## Compiler call and return ABI

Actual emitted direct calls have this shape:

```text
mov r0,#callee
mov r1,#(callee >> 8)
mov r2,#(callee >> 16)
lcall __sdcc_banked_call
```

The supported three-byte banked pointer stores the same low/high/bank bytes;
SDCC loads them into R0/R1/R2 and calls the **same** trampoline. The fixture's
pointer type is `uint32_t (*)(uint32_t) __banked`. A nonreentrant pointer
call with a second parameter was genuinely rejected by SDCC with error92;
it is not implemented by an indirect successful stub.

SDCC passes/returns16-bit scalars in DPL/DPH and32-bit scalars in
DPL/DPH/B/A, least-significant byte first. Additional direct-call parameters
are named static XDATA slots, e.g. `_banked_fixture_bank2_PARM_2`.
Each banked C epilogue emits `LJMP __sdcc_banked_ret`, not `RET`.
Ordinary common C functions retain normal `LCALL`/`RET`.

The original call trampoline:

1. Checks the incoming stack before any push, so SPff cannot wrap into IRAM0.
2. Saves A/PSW, requires CPU register bank0/DPS0, XMAP clear, valid FMAP,
   active depth below8, bank1..7, an upper-window target, and no bank7 NV/
   lock target. XBANK may independently be0..7.
3. Increments the DATA depth, restores argument A/PSW, pushes caller FMAP,
   selects and reads back the target bank, pushes target low/high, and
   `RET`s into the genuine compiled target.

Stack on target entry, bottom to top: caller's two-byte return address,
saved caller FMAP. A nested call repeats this frame. A banked return jumps
to common code, checks stack/depth/CPU/XMAP, preserves scalar return
registers, restores and reads back saved FMAP, then returns to the caller.
R0/R3/R4 are scratch/call-clobbered; they are **not** scalar argument/return
registers in this profile. No hidden per-call XDATA or allocator is used.

`banked_depth` and `banked_fault` are two actual allocated DATA bytes.
Depth increments/decrements are atomic8051 instructions; interrupt entry
can occur even within a trampoline. Saved mappings are on the hardware
stack, not a shared singleton. A fault disables EA and stays at common
`_banked_stop`, with code1 target,2 state,3 depth,4 stack,5 mapping. There is
no reset, retry or unsafe return. Mapping readback failure is terminal on
entry, return and constant-read restoration.

### Deliberately bounded supported shapes

This foundation admits this fixture's nonreentrant direct scalar calls,
common calls, and one-register-argument banked pointers, not arbitrary C.
No recursion, nested/higher-priority interrupts, callbacks into an active
foreground function, stack-auto, xstack, alternate CPU register banks,
alternate DPS, `--parms-in-bank1`, alternate call convention, aggregates,
64-bit returns, varargs, setjmp/longjmp, or mixed calling conventions.
Do not infer ABI support from a type that happens to compile.

The header rejects other compiler versions/models and exposed stack-auto/
xstack macros. SDCC does **not** expose a `--parms-in-bank1` predefined macro;
the final build must reject that flag by its fixed compiler-command and
emitted-ABI proof. Likewise, final acceptance must allowlist the fixture's
function signatures, actual call sites and parameter storage, rejecting
unsupported shapes rather than reporting their execution successful.
The helper's depth limit does not make static C parameters recursive.
The depth limit8 counts **all** active banked frames, including the IRQ
leaf. An interruptible foreground must therefore leave at least one free
depth slot and separately reserve the complete ISR stack requirement.
Depth8 or a trampoline's maximum admitted SP is not an interruptible
caller budget. The admitted fixture's measured complete call graph has
that headroom; arbitrary future callers need their own proof.

## Explicit foreign-bank constants

```c
uint8_t banked_code_read(uint8_t bank, uint16_t address, uint8_t length,
                         uint8_t __xdata *destination);
```

This common-CODE, **foreground/nonreentrant** helper admits1..32 bytes:
bank0 addresses0000..7fff, banks1..6 addresses8000..ffff, and bank7
addresses8000..e7ff. Subtraction-based end checks reject16-bit wrap and all
cross-boundary spans. No ordinary CODE pointer represents a foreign bank:
the caller supplies the bank explicitly, and only the common helper
dereferences a temporary16-bit CODE address after selecting FMAP.

Output is caller-owned XDATA, strictly after `banked_reserved_end`,
entirely below1e00, disjoint from other live objects. DATA/IDATA, generic,
CODE, SFR and XDATA pointers are not interchangeable. The helper does not
borrow the M0 status block or IRAM alias as an output buffer.

Returned statuses are OK0, invalid source/span1, invalid output2 and
unsupported CPU/mapping state3. Returned errors leave output and FMAP
unchanged. A mapping hardware/readback failure instead fail-stops and may
leave partial output; that is not a returned error or a successful read.
XMAP is rejected conservatively even for addresses above9fff.

The helper does not mask EA while copying. Only the cooperating,
FMAP-preserving interrupt path below may preempt; the ISR must not call
this helper or another foreground function. No other mapping owner, DMA,
flash writer or code bank modification may run concurrently.

## Interrupt transparency and memory allocation

The common `banked_fixture_isr` is a real SDCC `__interrupt(0)` function:
its vector at0003 is a linked LJMP, and its epilogue ends in **RETI**.
The compiler saves BIT_BANK, A, B, DPL, DPH, R7..R0, PSW and selects CPU
register bank0. The wrapper additionally stacks FMAP, DPS and MPAGE,
calls distinct `banked_fixture_irq_leaf` using the emitted banked-call ABI,
then restores these values and the compiler-generated context.
DPTR1 is untouched; entry DPS0 is a profile precondition.

The leaf has separate static XDATA argument storage, no calls, no DATA
temporaries and no overlay allocation. It is not used by foreground.
That separation matters: merely saving FMAP/registers would not preserve
overlapping SDCC parameter or overlay storage.

Vector0 is a **generic8051 modeled injection point**, not an implemented
CC2530 RFERR dispatcher or hardware acknowledgment. The parent must raise
a real modeled interrupt and observe RETI, not invoke the ISR as a C
function. Only one interrupt level/source is allowed. No ISR is admitted
during the real flash command; all interrupt enables must be zero there.

The six-object link places:

* register bank0 at00..07; flash permanent DATA at08..0f;
* runtime depth/fault at10/11; the six-byte foreground overlay at12..17;
* compiler bits at20 and BIT_BANK at21;
* **SSEG22..7c, 91 explicitly reserved bytes; initial SP21**;
* ordinary XDATA below1e00, with status1e00..1e3f separately reserved.

There is no extra RAM at XDATA1f00..1fff: it must alias IRAM in replay,
including stack and compiler state. Upper IRAM and gaps are not an implicit
pool. Call admission accepts incoming SP23..79, return admission24..7a;
the respective temporary pushes cannot exceed7c. This is not a complete
C stack-depth proof: the parent must inspect the whole linked call graph
and measure uninterrupted high water, including IRQ prologue/epilogue and
flash execution. Preserve the15-second simulator limit. Old SP7c/image
guards do not automatically prove this new frame layout.

## Fixture observation contract

The status object is the public synthetic
`volatile __xdata __at(0x1e00) uint8_t banked_fixture_status[64]`.
Unlisted bytes remain zero. Multi-byte results below are little endian.

| Offset | Meaning / normal value |
| --- | --- |
| 0..5 | ASCII `BNK1`, ABI1, length64 |
| 6 | phase:1 calls,2 constants,3 IRQ,4 flash,5 complete |
| 7 | terminal fixture failure:1 arithmetic,2 pointer/restoration,3 constants,4 negative constants,5 IRQ,6 flash,7 runtime state |
| 8..9 |16-bit result `ca1a` |
| 10..13 | nested32-bit result `332cd55f` |
| 14..17 | indirect32-bit result `d00d8154` |
| 18..21 | ISR32-bit result `416a916d` |
| 22 | interrupt count, exactly1 |
| 23..27 | initial FMAP and restoration observations, normally1 |
| 28 | flash result,0 on normal idle completion |
| 29..30 | FMAP before/after real common flash command, both7 |
| 31 | restored MEMCTR, normally2 |
| 32..39 | last usable bank7 CODE constant `d3 6e a1 4c 87 29 f0 5b` |
| 40 |12 returned constant-helper rejection checks completed |
| 41..42 | final runtime depth/fault, both0 |
| 43 | nested path bit mask,15: bank1,bank2,bank7,common |
| 44 | FMAP restored **inside ISR after leaf return**,2 at the designated injection point |
| 48..51 | interrupted foreground result `ffffffff` |

Exported linked marker symbols (leading underscore is the linker spelling):

```text
_banked_fixture_before_calls       _banked_fixture_after_calls
_banked_fixture_before_constants   _banked_fixture_after_constants
_banked_fixture_irq_window         [bank2; inject real interrupt here]
_banked_fixture_irq_enter          _banked_fixture_irq_exit
_banked_fixture_after_irq
_banked_fixture_flash_before       _banked_fixture_flash_after
_banked_fixture_done               _banked_fixture_failed
_banked_fault_entry                _banked_stop
__sdcc_banked_call                 __sdcc_banked_ret
```

Markers are NOPs except terminal loops and the fault entry. Additional
interrupt replay may use actual trampoline instructions, including the
short transitions before/after FMAP updates. Such a replay must account
for the deliberately point-specific status44 expectation, rather than
change the preserved-state predicate to hide a failure.

### Real flash/XMAP composition

The bank7 `banked_fixture_flash` calls a common wrapper, which calls the
unchanged `flash_exec_command(PROGRAM, 0, 0, word, 4)`. This wrapper is
necessary: errors can retain XMAP, so it must trap in common CODE **before**
returning into a banked caller that overlaps8000..9fff.

The backend's “bank0/DPS0 ABI” describes **CPU register bank0**, not FMAP0.
Actual emitted flash code has no FMAP access or guard. Its existing
MEMCTR-high-bits, IRQ/DMA, clock, chip/controller, copying, verification,
idle/restore guards remain real. CPU bank/DPS are caller preconditions.
The copied123-byte RAM routine, bounded FCTL polling and RAM-only busy
exhaustion loop are unchanged. A busy command must never be bypassed by
injecting a success return or running the common error wrapper in flash.

Initialize the model's FMAP to1, DPS/PSW/interrupt priorities and enables
to0. The fixture disables enables during startup, enabling only the
synthetic vector0 window with IEN0=81 and disabling it after the call.
Before flash, provide the following **controller facts**, not C mocks:

| Space | Address=value |
| --- | --- |
| SFR | C6=C9, 9E=C9, BE=04, D6=00, D7=00, C7=02 |
| XDATA/XREG | 624A=A5, 6276=44, 6277=FF, 6270=04 |

The common CODE arrays `banked_fixture_controller_sfr[12]` contain address/
value pairs and `banked_fixture_controller_xdata[12]` low-address/high-
address/value triples for exactly these facts. They are not MMIO writes.

The model must honor real FADDR writes/readback (6271=00,6272=FA), the real
MEMCTR XMAP write/readback, and XMAP's SRAM plus **IRAM alias** decoders.
RAM code writes FCTL=06; accepted controller readback is86. It then writes
`12 34 56 78` to FWDATA6273 and polls the real controller up to4 times.
Normal completion exposes FCTL04 before the limit, permitting common-code
checks and XMAP restoration to MEMCTR02 while FMAP stays7.
The fixture proves idle completion, not verified programmed data or durable
NV semantics. The parent's flash model separately owns physical effects.

At exhausted busy, the real backend records `FLASH_EXEC_RAM_STOP=7` and
never returns from RAM; `_banked_fixture_flash_after` must remain unreached.
On an idle error, the wrapper records the nonzero result and remains at
`_banked_fixture_failed`; it does not restore XMAP, retry or return into7.

## Evidence boundary

Focused work on these files includes genuine SDCC compilation/linking,
HEX checksum and virtual/physical overlap/bound inspection, startup and
parameter/return/ISR listing inspection, and repository/diff guardrails.
The same target-only header intentionally does not masquerade as a native
host implementation.

The inspected standalone link in `build/banked-agent/` has **4676 populated
CODE bytes**: common4075, bank1 178, bank2 187, bank7 236, including their
constants. It uses **224 ordinary XDATA +64 reserved status bytes**,26
occupied non-stack IRAM bytes and91 separately reserved stack bytes.
Both board-definition compilations produced byte-identical Intel HEX:
SHA-256 `851dabd2e1571beee40407d63ecf9cf4b85a925b551d7612e082f506922361c7`.
Stack-auto, xstack and model-small compilations were checked to fail.
These are focused artifact facts, not an executed high-water or complete
image-mutation result. Generated files are not source assets or flashing inputs.

Complete parent-owned image mutation tests, ABI allowlisting, alias-aware
modeled replay (including timeout, ignored mappings, invalid targets/depth/
stack, output spans, real interrupt entry/RETI and XMAP) remain explicit
acceptance gates until run. No physical CODE-bank mux, interrupt, USB,
flash timing/power-cut, RF or board behavior is hardware-observed here.
