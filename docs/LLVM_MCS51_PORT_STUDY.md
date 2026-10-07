# LLVM/Clang MCS-51 port study — cc2530-zigbee

Status: **initial structural study, stopped at a stop condition**
("physical internal RAM / stack cannot fit"). Nothing in this document
describes a production-ready toolchain.

Firmware configuration: `BOARD=lg_esl29_rev03`, `JOIN_SMOKE_KEY_MODE=default-tc`
(production join-smoke image, `production-defines.txt`).
Evidence lives in the study workspace under
`~/WORK/llvm-mcs51-zigbee-study/build/llvm-study/` (abbreviated `E/` below)
and `~/WORK/llvm-mcs51-zigbee-study/evidence/`. Giant binaries are not committed.

Conventions: "verified" = measured in this study; "sourced" = cited from a
primary document; "assumption"/"unknown" are labelled as such.

---------------------------------------------------------------------------

## 1. Environment and pinned identities

| Item | Value |
| --- | --- |
| LLVM fork / tag | `devbis/llvm-mcs51`, tag `23.1.2-mcs51-20261004` |
| Commit | `44a8e3000d4d8a66481fdf37ccd97ad7f0ea5afd` (clang/lld `--version` report it) |
| Release archive | `mcs51-llvm-toolchain-linux-amd64.tar.gz` sha256 `86101efe…a67` |
| Upstream CI for the tag | 5 platform jobs + publish: success (`evidence/ci-runs.json`, `ci-build-run-jobs.txt`) |
| Local lit (shipped tools, MCS51 tests) | 148/148 pass (`evidence/local-lit.txt`) |
| `clang_compat.diff` | sha256 `09a6e192…891c`, 62 files, +934/−383 |
| Firmware base | `5f7cb0d` (merge of PR #91), worktrees `wt-base` (SDCC), `wt-patched`, `wt-llvm` |
| SDCC references | released 4.2.0 ("A") and frozen TM, numbers from `cc2530-zigbee-tm-ownership/docs/SDCC_TRUNK_BASELINE.md` (read-only) |
| Simulator | ucsim s51 from `sdcc-4.2.0+dfsg` built for the study (`E/join-simulator/`) |

Branches produced:

* firmware `llvm-study/compat-base` (in `wt-llvm`):
  `7ddd8b4` diff applied unchanged → `fe95e6b` → `3777356` → `f123c76` →
  `310a54c` → `4e42693` → `5feb899` → `4e44d42` → `55a5552` (+ this report).
* toolchain `study/fixes` (worktree `llvm-fix`, on top of `44a8e3000`):
  `ac98e2734` lld local-thunk bank, `257eb1499` startup clear,
  `2f70dd019` clang ShortAlign, `002530b7b` DAG-combine OOB,
  `4c0e1ff15` constant-byte-grouping end-iterator dereference.
  Toolchain and firmware changes are kept strictly separate.

## 2. LLVM backend ABI (verified from source + generated code)

* Calling convention (`MCS51CallingConv.td`): i8 args R7,R6,R5,R4; i16 args in
  pseudo-register pairs IP0..IP3 (IRAM `IM0..IM7`); rest on the hardware stack;
  i8 result in A, i16 in IP pairs.
* **Callee-saved: R2–R7 and IM8–IM23** (16 IRAM pseudo-registers). Saves are
  done by common helpers `__mcs51_save_imN/_rN` that `push` onto the 8051
  hardware stack.
* Locals: every addressable local is `alloca addrspace(2)` (IDATA) on the
  hardware stack, then `addrspacecast` to the generic/default pointer.
* Pseudo-registers occupy IRAM 0x30–0x47 (24 B); SP reset 0x7F, stack
  0x80–0xFF = **128 B**. IRAM 0x08–0x2F and 0x48–0x7F unused by this image.
* Banking: linker auto-bank. Cross-bank call thunk in common:
  `push 0x9f; mov 0x9f,#bank; lcall f; pop 0x9f; ret` → **5 B of stack per
  banked nesting level** (3 thunk + 2 callee return).
* Default pointer = XDATA 16-bit; `__code` 16-bit; `__generic` 4 bytes (§9).
* No interrupt handlers exist in this firmware (all IEN = 0), so ISR ABI was not
  exercised.

## 3. `clang_compat.diff` audit

Applies cleanly (`git apply`, commit `7ddd8b4`) but is **incomplete** for the
current tree: 8 additional study compat commits were needed (§3.3).
111 substantive hunks were read (`E/audit/substantive_hunks.diff`).

### 3.1 Classification

| Cat | Files / items |
| --- | --- |
| A syntax | `toolchain/clang/include/string.h`; `cc2530_mmio.h` CC2530_CLANG detection, `DECLARE_SFR` empty, `MMIO_*` via `__sfr` casts; `MCU_CODE`/`MCU_GENERIC` macros; `TIMEBASE_POINTER(_CAST)` in timebase.h/.c and ~30 call sites (aes, clock, dma, mac_time, radio_*, irq/timebase fixtures); explicit generic casts in ccm_star, zigbee_mmo; `status.c` `MMIO_READ` wrappers |
| B address space | `MCU_XDATA` empty (default = xdata); `MCU_GENERIC=__generic` (4 B); aes.c `pointer_location` union with `uint32_t` & 0xffffff |
| C linker/startup | Makefile LLVM targets (`--no-check-sections`, `--defsym mac_join_staged=…`, per-file −O2/−O1 for nwk_aps/security_keys), `flash_exec_mcs51.s`, bringup.h `CC2530_EXTERNAL_STARTUP`, startup.c rename, README |
| D SDCC workaround removed | `sdcc_volatile.h` `SDCC_VOLATILE`/`MCS51_PARAM_VOLATILE` (326 sites, 32 files); aes.c `_gptrput_PARM_2` overlap check; verify_firmware.py SDCC 4.5 vector tolerance |
| E ABI/layout | mac_tx.c memcpy of control block → field copies, SDCC layout asserts removed; banked_join_fixture size asserts (clang staged offset — **wrong**, see §7) |
| F banking | `JOIN_FAR`/`SECURITY_FAR` empty under clang; `MCU_COMMON_CODE` section attribute (flash_exec idle, zdo_runtime_step) |
| G fixture/test | timebase_fixture naked hooks; banked_join_fixture; tests |
| H potentially semantic | zdo_runtime.c restructure (noinline helpers, reordered clang path, `expire_query` order); bringup.c IEN0/1/2=0 + `board_early_off` under clang; `#if __clang__` blocks in bdb_join, nwk_aps, security_keys; dma.c `arm0` via MMIO_WRITE; mac_tx copy change |

Category H is **not justified** by the patch itself. Several H/E/A hunks are
not guarded by `__clang__` and change the SDCC build (§4). They were used for the
LLVM measurements but are flagged unproven.

### 3.2 High-risk items

1. **MCU_XDATA / MCU_CODE / MCU_GENERIC** — reduced programs `E/abi/gptr*.c`.
   See §9: two backend defects, no firmware exposure found.
2. **MCU_AT** — §8. Empty under clang. The single production object is placed by
   `--defsym`. Test images are not proven.
3. **SFR** — `E/abi/sfr.c/.s`, `sfr.json`. Volatile reads are kept separate and
   addresses are correct. `MMIO_SET/CLEAR` compile to
   `mov a,sfr; anl|orl a,#m; mov sfr,a`, never `anl|orl direct,#imm`.
   TI SWRU191F (sha256 in `E/abi/swru191f.sha256`; text lines 4331–4336) says
   that P0/P1/P2 reads return **pin** levels except under RMW instructions. LLVM
   therefore writes pin levels back into unrelated port latch bits, where SDCC
   operates on the latch. The production site is `boards/lg_esl29_rev03.c`
   `board_early_off` (P0/P1 clears). This is a **hazard, not verified on hardware**.
   There is no ISR race (no ISRs).
4. **Generic pointers** — §9.
5. **SDCC_VOLATILE** — 326 sites: 244 pointer-object parameters, 70 scalar
   objects, 12 static objects (ccm_star, ed_wire, mac_join `time/remaining/admitted`,
   mac_poll `step_now/step_profile`, mac_tx `input`, radio_autoack `attempt`).
   **0 sites qualify a pointee** (no MMIO/DMA access is de-volatilised). There
   are no ISRs and the static objects are not DMA buffers. All 326 are
   classified as genuine SDCC spill/codegen steering (`E/audit/volatile_sites.json`).
6. **Structure layout** — §7: clang short alignment differs (defect). Fixed via
   `-fpack-struct=1` plus a compile-time guard.
7. **JOIN_FAR / SECURITY_FAR** — removal is not banking. Banking is proven
   independently in §10.

### 3.3 Additional study compat commits (firmware)

| Commit | Reason |
| --- | --- |
| `fe95e6b` | `LINK_FAR` empty under clang; naked join_smoke hooks; aes.c tagged-generic ownership (tag≠0 cannot be in arena); zcl_sensor CODE→XDATA copy (memcpy takes default pointers) |
| `3777356` | `join_smoke_status` via `--defsym 0x1e00`; call external-startup hook |
| `f123c76` | flash_exec RAM template forced into common CODE (auto-bank put it in another bank; 16-bit MOVC copy read the caller's bank) |
| `310a54c` | `MCU_ESCAPED_LOCAL` = `static` under clang for 33 escaping locals (backend IDATA-escape defect, §9) |
| `4e42693` | `-fpack-struct=1` + layout guard (clang short alignment defect) |
| `5feb899` | clang fault hook looped on `sjmp $` at hook+1; SDCC hook cycles through the entry (simulator terminal-loop check) |
| `4e44d42` | direct `sdcc_volatile.h` include (310a54c relied on a transitive include; `llvm-zigbee-image` failed) |
| `55a5552` | banked_join_fixture clang staged offset 654 → 645 (= SDCC 0x285) under packing; `mac_join_staged` defsym |

## 4. SDCC feature-off comparison

SDCC 4.2 builds of `wt-base` vs `wt-patched` (diff applied, same flags), per-
module `.rel` areas (`E/sdcc-feature-off/sdcc-rel-size-delta.txt`):

| Area | Base | Patched | Δ |
| --- | ---: | ---: | ---: |
| CODE (all code areas) | 252737 | 254315 | **+1578** |
| XSEG | 7650 | 7669 | +19 (zdo_runtime) |
| BSEG bits | 88 | 87 | −1 |

Largest contributor: zdo_runtime `JS_zdo_runtime` +125, `JD_zdo_runtime` +9.
Small deltas (+2…+12) appear across about 20 modules. **The patch is not
SDCC-neutral**; the SDCC path is changed. Base join-smoke IHX sha256 is recorded
in `base-ihx-sha256.txt`. The study's own commits 310a54c/4e44d42 leave SDCC
`.rel` T/R records unchanged (macro-only).

## 5. Bootstrap reproduction

| Target | Result |
| --- | --- |
| `llvm-bringup` generic / lg_esl29_rev03 | builds (`E/bootstrap/llvm-bringup-*.log`) |
| `llvm-timebase_fixture` generic / lg_esl29_rev03 | builds |
| `llvm-zigbee-modules` (Makefile −Os) | **fails**: `radio_rx.c` killed after 55 min (MCS51PostRAPeephole hang) |
| `llvm-zigbee-image` (fixture image, −Oz) | failed at study start; **builds** after `4e44d42` + `55a5552` (`llvm-zigbee-image-rerun.log`) |

## 6. Zigbee TU compile matrix

All 45 production TUs (exact link order in `E/link/link.sh`):

| Profile | Result |
| --- | --- |
| `-Oz -fpack-struct=1 -ffunction-sections -fdata-sections` | **45/45 rc=0**; max 0.85 s, 72 MB per TU (`E/matrix/results-pack.txt`) |
| `-Os` packed | radio_rx **times out (600 s)** in MCS51PostRAPeephole; same for −O2 and −O1 (`E/bootstrap/radio_rx-variants.txt`) |
| `-Os` with radio_rx at −Oz (measure only) | links only with bank-7 overflow into NV pages; CODE 236812 |

The uniform optimization profile is **−Oz only**. The compat Makefile's per-file
−O2/−O1 overrides were not used.

## 7. ABI / layout differences

* **Defect: clang `ShortAlign`.** `MCS51.h` TargetInfo never sets `ShortAlign`,
  so `short`/`uint16_t` are 2-aligned (datalayout says `i16:8`, SDCC uses 1).
  In the join-smoke admission layout 140 of 150 bytes moved
  (`E/sim/admission_layout_check.log`). Firmware workaround: `-fpack-struct=1` +
  guard (packed check passes). Toolchain fix `2f70dd019` sets `ShortAlign = 8`.
  Its lit test `clang/test/MCS51/short-alignment.c` was run with the rebuilt
  clang (§17).
* The compat patch's clang-only fixture asserts (654) had encoded the
  misaligned layout. With packing the clang layout equals SDCC's (645 / 0x285).
* Externally observable layouts:
  * join_smoke admission/phase/mailbox/status. Fixed offsets are used by the
    simulator; proven by the packed check and by the replay prologue.
  * banked_join_fixture staged alias (fixed).
  * AES DMA descriptors and NV journal bytes: covered by the replay harness,
    but **not reached** before the stack overflow (§13), so not dynamically
    proven.
* mac_tx.c control-block copy was rewritten as field copies by the patch
  (category E). Not separately proven.

## 8. Fixed-address audit

`MCU_AT()` is empty under clang.

* **Production image:** exactly one fixed object, `join_smoke_status` @0x1E00,
  placed with `--defsym join_smoke_status=0x1e00`. `nm` shows `A 0x1e00`, and it
  lies outside `.bss` (which ends at 0x12EA). **Proven.**
* `m0_status` (status.c) is not linked into this image.
* About 50 test/fixture files use `MCU_AT(0x1e00)`. They are not in the
  production image and are **not proven**. Under clang they would silently become
  normal `.bss` unless each gets a defsym or section.

## 9. Generic-pointer audit (`E/abi/generic-pointer.json`)

* Representation: 4 bytes `[lo, hi, tag, 0]`. XDATA tag 0x00, CODE tag 0x80,
  NULL all-zero. Access via `__mcs51_gptrget8/put`; 4-byte compare; 16-bit
  arithmetic.
* **Defect:** `sizeof(T __generic *) == 2` in the frontend, and `offsetof` after
  a generic member is 2, while the IR stores 4 bytes. Frontend/backend layout
  mismatch.
* **Defect:** DATA→generic cast is a fatal ISel crash
  (`Cannot select GlobalAddress addrspace(1)`, `E/abi/gptr_data.log`).
* **Defect (miscompile class):** an IDATA local whose address is passed as a
  default (XDATA) pointer keeps its 8-bit address, so the callee accesses
  XDATA 0x00xx. Found in 33 call sites / 20 functions / 12 TUs
  (`E/escape/escapes.json`). After `310a54c`: 0 (`escapes-after.json`).
* Firmware exposure of the first two: none found. Generic pointers appear only as
  parameters, in explicit casts and in the aes.c union; there are no DATA globals.
  aes.c ownership checks were adapted to the tag (`fe95e6b`).

## 10. Banking implementation

* 373 thunks in the production image.
* **Defect (shipped lld): local-symbol thunks encode bank 0.** The shipped
  image (`Oz-packfix`) has **170 incorrect thunks**: every banked call to an
  `STB_LOCAL` function maps bank 0. Fix `ac98e2734` with a new lit test
  `mcs51-auto-bank-local-thunk.s`, which fails on shipped lld and passes on the fix.
  8/8 `*mcs51*` lld tests pass.
* Fixed-lld image `Oz-fix` (`E/banking/thunks-Oz-fix.json`): 373 thunks;
  0 incorrect. The 3 flagged are `STB_WEAK` thunks into common code with bank 0,
  which is benign.
* Cross-bank non-thunk references: 0 (`xbank-Oz-fix.json`). XDATA order proof:
  0 violations, 14 LLVM-only objects. Fence proof: 0 (`E/xdata/*-Oz-fix.json`).
* Earlier "banking proven" statements made with the shipped lld are
  **withdrawn**. The audit then validated thunk *presence*, not the bank operand.
* Banking defect worked around in the firmware: auto-banking split the
  flash_exec RAM template from its copier (`f123c76`).
* Interrupt interaction: **not exercised** (no ISRs). The thunk is not
  interrupt-safe by design: FMAP is changed before the lcall.
* Auto-banking fills banks to 0–1 B free and leaves common 61% empty (§11).

## 11. Real firmware link (`Oz-fix`)

```
STARTUP=…/startup/cc2530_startup_fixed.o MDIR=…/matrix/Oz-pack \
PROJECT_LD=…/link/project-ordered-pack.ld link.sh Oz fix \
  --ld-path=…/build-llvm-fix/bin/ld.lld -Wl,--no-check-sections -Wl,--gc-sections
```

* **Defect: shipped `cc2530_startup.s` clears 64 KiB of XDATA** (including
  XREG/SFR mirror/flash-mapped space). Fix `257eb1499`: clear 0x0000–0x1EFF plus
  IRAM.
* `--no-check-sections` is required: the CC2530 XDATA/IRAM alias overlap
  otherwise fails.
* `--gc-sections` saves 0 B (Oz-gc = Oz-nogc = 222610 before fixes).

| Section | Size | Notes |
| --- | ---: | --- |
| `.vectors` | 3 | |
| `.text` (common, from 0x94) | 12386 | extent to 0x30F6 |
| `.bank1`…`.bank6` | 32767, 32767, 32767, 32768, 32767, 32768 | |
| `.bank7` | 13097 | usable 26624 (NV from 0x3E800) |
| `.bss` (XDATA) | 4842 | 0x0000–0x12E9 |
| `.data`, PDATA, DATA/IDATA/BIT globals | 0 | |
| `join_smoke_status` | absolute | 0x1E00 |

## 12. Internal RAM and stack proof

IRAM map (`E/resources/iram-Oz-packfix.json`): R0–R7 of bank 0 (R1 unused),
pseudo-registers 0x30–0x47 (24 B), no BIT, stack 0x80–0xFF (128 B).

**Static bound** (`E/stack/stack_analysis.py`, a fail-closed SP analyzer over the
linked image, 775 functions; `stack-Oz-fix.json`):

* bounded; main peak 527; max SP 656; **demand 529 B vs capacity 128 B
  (headroom −401)**.
* Worst path: 19 nested banked calls, main → join_smoke_poll →
  mac_link_driver_step → observe → bdb_join_step → nwk_aps_step →
  nwk_aps_transmit → security_keys_send → seal → ed_wire_crypt →
  ccm_star_crypt → feed → flush → encrypt → aes128_encrypt_block → sample →
  timebase_expired → link_work_io → child_work_address → edge.
* This bound is context-insensitive; path feasibility is not proven for every
  frame. Cost drivers: 5 B per banked level, callee-saved IM8–IM23 pushes, IDATA
  frames. SDCC A/TM reset-relative demand: 45 B.
* `-fstack-usage` (`.su`) underestimates: it excludes thunks and save helpers.

**Dynamic proof (simulator, §13):** in every join case the **hardware stack
wraps 0xFF→0x00**. ucsim stops in `__mcs51_save_im4` with SP=0x00 after pushing
IM 0x38–0x3B into 0xFD..0x00, overwriting R0 (`E/sim/runs-fix/overflow-iram.json`).
Heuristic return-address backtrace (`overflow-backtrace.txt`): main →
join_smoke_poll → security_keys_open → security_counter_open → nv_record_load
→ select_record → scan_page → flash_nv_read → link_work_io →
`__mcs51_save_im4`. This is the **first NV read at join start**.

**Stack proof: FAIL. Stop condition reached.** Even with SP moved to 0x47 the
capacity would be 184 B, far below 529 B.

## 13. Simulator results (`E/sim/llvm_join_replay.py`, image `Oz-fix`)

The replay driver reuses the repository harness. It overrides the 15 s
`simulate` timeout (`SIM_TIMEOUT`) and adds an IRAM dump on stack wrap
(`DIAG_IRAM`). Harness fix: zero-event steps no longer loop.

| Case | Steps / events | Result |
| --- | --- | --- |
| 2 (bad admission packet) | 1 / 0 | **complete**, terminal fault JS_PACKET as expected, peak SP 0x93 |
| 3 | 1 / 0 | **complete**, terminal fault, peak SP 0x9E |
| 1 (admission expiry) | 256 / 0 | **complete** 256/256 steps, final phase 6 as expected, peak SP 0x93, 1139 s (`runs-fix/case1.json`) |
| 0 (full join, serving) | 1142 / 2,303,966 | **fail** step 2 event 3: stack wrap (SP 0x00) |
| 4, 5, 6, 8, 9, 10 | 3–406 steps | **fail**, identical point |

The harness defines cases 0–6 and 8–10 (there is no case 7), so all ten were
run.
The `7a1b9` "unexpected stop" in the logs is the second adjacency attempt after
the wrap. The first attempt reports `stop 1498 SP 00`. **Simulator full join:
FAIL.**

## 14. SDCC 4.2 vs SDCC TM vs LLVM

| Metric | SDCC 4.2 (A) | SDCC TM | LLVM plain |
| --- | ---: | ---: | ---: |
| Compiler identity | SDCC 4.2.0 | SDCC trunk TM (frozen) | clang/lld 23.1.2 `44a8e3000` + study lld fix |
| Optimization profile | production | production | −Oz, `-fpack-struct=1`, function/data sections, uniform |
| LTO | OFF | OFF | OFF |
| Linker GC | n/a (SDCC) | n/a | ON (saves 0 B) |
| Compiler overlay | SDCC OSEG (A raw; D 553 saving) | SDCC OSEG | OFF (no-op if ON, §15) |
| Populated CODE | 253445 | 247449 | **222090** |
| Common CODE | 32714 | 31922 | **12389** |
| Physical flash extent (bank 7 end) | 0x3E70F | 0x3E729 | 0x3B329 |
| Common used/free | 32714 / 54 | 31922 / 846 | 12389 / 20234 |
| Bank 1 used/free | 32083 / 685 | 31131 / 1637 | 32767 / 1 |
| Bank 2 | 32755 / 13 | 32424 / 344 | 32767 / 1 |
| Bank 3 | 32734 / 34 | 32678 / 90 | 32767 / 1 |
| Bank 4 | 31350 / 1418 | 32463 / 305 | 32768 / 0 |
| Bank 5 | 32685 / 83 | 31260 / 1508 | 32767 / 1 |
| Bank 6 | 32741 / 27 | 29162 / 3606 | 32768 / 0 |
| Bank 7 (usable 26624) | 26383 / 241 | 26409 / 215 | 13097 / 13527 |
| Worst-bank free | 13 | 90 | **0** |
| Raw XDATA | 7676 | 7676 | **4842** + status @0x1E00 |
| DATA | 51 (+OSEG 10) | 51 (+OSEG 10) | 0 globals; 8 regs + 24 pseudo-regs |
| IDATA | stack only | stack only | 0 globals |
| PDATA | 0 | 0 | 0 |
| BIT | 88 bits | 80 bits | 0 |
| Stack demand | 45 | 45 | **529 (static); overflow (dynamic)** |
| Stack capacity | 45 | 46 | 128 |
| Peak SP | 0x7C | 0x7B | wraps past 0xFF |
| Bank thunk count | SDCC banker | SDCC banker | 373 |
| Bank depth | 8 | 8 | 19 (static worst path) |
| Full simulator join | pass (reference) | pass (reference) | **FAIL** |

LLVM's CODE is 12% below A, but these CODE numbers are **not** a fair density
win:

* the image does not run;
* LLVM's lower XDATA is mostly frame state moved onto the IRAM stack, which
  SDCC `--model-large` allocates statically in XDATA;
* the bank fill is not controllable.

SDCC numbers come from the TM study document, not re-measured here.

## 15. LLVM overlay experiment

`-mllvm -mcs51-overlay` and `…-overlay-space=xdata` on all 45 TUs: rc=0,
**0 objects changed**.

* Root cause: all 294 allocas are `alloca addrspace(2)` + `addrspacecast`.
  `MCS51Overlay::isAddressUse` rejects AddrSpaceCast, so 0/294 are eligible
  (`E/overlay/overlay-result.json`).
* The upstream lit test passes only on hand-written addrspace(0) IR.
* Soundness note: the module-local interference walk skips declarations
  (static A → extern X → public B → static C would not interfere A/C). Not
  exercised.

**Overlay saving: 0 B.**

## 16. LTO feasibility

* `-flto` bitcode for 45/45 TUs.
* The shipped lld links it (`Oz-lto`, 7.7 s): CODE 211039 (−11051 vs non-LTO),
  `.bss` 4820.

**Not usable:**

* The map attributes everything to `Oz-lto.elf.lto.o`. XDATA order/fence proofs
  become vacuous; the fence tool cannot run.
* The shipped lld writes 238 incorrect thunks of 243.
* With the assertion-enabled fixed lld, LTO codegen aborted on the
  `setTargetDAGCombine` OOB (`002530b7b`, §17). After that fix it aborted
  again in `MCS-51 constant byte grouping` (`child_work_address`), which
  dereferenced `MBB.end()`; the minimal trigger is
  `unsigned long f(void){return 0;}` at any -O level (fix `4c0e1ff15`).
* With both fixes, the fixed lld links the same bitcode (`Oz-lto-fix`, rc 0):
  CODE 211045 (common `.text` 12992, bank7 1456), `.bss` 4820. Thunks:
  243, **0 incorrect** (1 targets common code with bank 0, which is harmless;
  `banking/thunks-Oz-lto-fix.json`).
* It remains **unusable**:
  * the per-object order/fence/xbank proofs are vacuous (a single `lto.o`);
  * the bitcode matrix predates packing and the fault-hook fix, so the layout
    guard and the simulator were not run on it;
  * LTO does not change the frame model, so the stack blocker (§12) stands.

## 17. Compiler blockers (ordered)

1. **Frame/stack model (architectural, blocking).** Several costs all land on the
   128 B 8051 hardware stack:
   * IDATA frames for every addressable local;
   * spills;
   * callee-saved IM8–IM23 pushes;
   * 5 B per banked call.

   The result is 529 B static demand and a dynamic overflow at the first NV read.
   SDCC's model (static XDATA frames + OSEG + 45 B stack) has no counterpart.
2. **Overlay ineffective** on real Clang IR (addrspacecast). This is the main
   in-backend mitigation and it is a no-op.
3. **MCS51PostRAPeephole hang** at −Os/−O2/−O1 (radio_rx); −Os also does not fit.
4. **lld local-thunk bank 0** (fixed in study, `ac98e2734`).
5. **Startup clears 64 KiB** (fixed, `257eb1499`).
6. **clang ShortAlign = 16** (fixed, `2f70dd019`; firmware uses packing).
7. **IDATA-local escape** through default pointers (miscompile class;
   source-level workaround).
8. **Generic pointers:** frontend sizeof 2 vs IR 4; DATA→generic ISel crash.
9. **SFR RMW** on ports: pin-vs-latch hazard.
10. **`setTargetDAGCombine` with target opcodes:** out-of-bounds write in every
    compilation; assertion abort in +Asserts builds (fixed, `002530b7b`).
11. **Auto-banking:** no placement control, banks filled to 0 B, common unused;
    a cross-bank template/copier split needed a source workaround.
12. **Constant byte grouping** dereferences `MBB.end()` whenever a group of
    constant stores runs to the end of a block (e.g. returning a constant
    `long`): UB in release builds, abort in +Asserts builds. Fixed in
    `4c0e1ff15`; generated instructions are identical to the shipped clang.
    These two latent defects show that upstream CI does not exercise an
    assertion-enabled build on real C.
13. `--no-check-sections` required; `-fstack-usage` underestimates.
14. **Stack arguments through bank thunks** (found in §19.8; shipped and fixed
    toolchains). The callee reads incoming stack arguments 3 bytes too low
    when it is entered via the cc2530 bank thunk. This affects 45 callees and
    86 call sites. Reduced test: `E/banking/stackarg-thunk/run.sh`. The
    "0 incorrect thunks" result in §10–§11 covers bank/target encoding only.

Validation of the study fixes: the assertion-enabled `build-llvm-fix` (clang +
lld at `4c0e1ff15`) passes 11/11 tests: `clang/test/MCS51` (3, including
the new `short-alignment.c` and `constant-byte-grouping.c`) and the 8
`lld/test/ELF/*mcs51*` tests (`bootstrap/fix-lit.log`). `Oz-fix` was linked
with the fixed lld (`ac98e2734`; later commits do not change lld's thunk code); its 373
thunks have 0 incorrect.

## 18. Recommendation

**Classification: UNSUITABLE** (for this firmware, in the pinned release).

The toolchain compiles and links the real firmware at uniform −Oz, and once two
toolchain defects are fixed the banking and fixed-address proofs pass. However:

* the stack demand is 4× the physical stack;
* the join overflows in simulation before any radio work;
* the overlay mechanism that should address this is a no-op on Clang's IR;
* only −Oz completes, so there is no fallback optimization level.

Per the stop conditions, the study stops here rather than redesigning.

A second phase is justified only as **backend work**, not firmware work:

* static (non-reentrant) XDATA frames or an overlay that handles
  `addrspacecast`;
* a cheaper callee-saved policy;
* bank-aware stack accounting.

The promising signal is code density (−12% vs A). Re-measure it only after the
image runs.

### Answers to the study questions

1. Bootstrap fixtures: yes (bringup, timebase_fixture; zigbee-image after compat fixes).
2. Every Zigbee TU: yes at −Oz (45/45); not at −Os/−O2/−O1.
3. One optimization level: yes, −Oz only.
4. Why not −Os: radio_rx PostRAPeephole hang; −Os also overflows bank 7 into NV.
5. Real firmware link: yes (`Oz-fix`, fixed lld + startup).
6. CC2530 banking correct: no with shipped lld (170 bad thunks); yes with fix.
7. Cross-bank calls proven: statically yes for `Oz-fix`; dynamically only up to the stack overflow.
8. Interrupts across bank switches: not applicable / not proven (no ISRs; thunk not IRQ-safe).
9. Fixed-address objects: production yes (1 object); test images no.
10. Generic pointers: no in general (two defects + escape miscompile); firmware exposure avoided.
11. Clang changes alter SDCC: yes (+1578 CODE, +19 XSEG, −1 BSEG). *Superseded:*
    byte-identical after `5b5687b`/`b3542de` (§19.1).
12. LLVM populated CODE: 222090. *Corrected:* 196172 on the identical source (§19.1).
13. Common CODE: 12389.
14. Raw XDATA: 4842 + absolute status.
15. Internal DATA/IDATA: 8 register + 24 pseudo-register bytes; 0 globals; 128 B stack.
16. Stack demand/capacity: 529 / 128 (fail). *Corrected:* 476 (plain), 162 with the
    static-frame study flags (§19.8).
17. Minimum bank headroom: 0 B.
18. Full simulator join: no.
19. `--gc-sections` saving: 0 B.
20. Overlay saving: 0 B.
21. Full LTO usable: no.
22. UNSUITABLE (not production-ready).

## 19. Backend-architecture study (static machine frames)

This section follows the classification in §18 and adds a backend-architecture
study. Its evidence lives under `E/` = `build/llvm-study/`.

* LLVM: `study/fixes` at `a52aea89d`, which sits on `4c0e1ff15`. It adds the
  following study-only options (`-mllvm`):
  * `-mcs51-frame-census`
  * `-mcs51-static-frames`
  * `-mcs51-csr=default|r|none`
  * `-mcs51-overlay-census`

  Default codegen is unchanged: there are 0 disassembly differences over the 45
  firmware objects.
* Firmware: branch `llvm-study/compat-v2`, commits `5b5687b` and `b3542de`.

The banking, the fixed lld and the fixed startup are frozen, as described in
§10–§11. No hardware was flashed.

### 19.1 Baseline integrity

**`-fpack-struct=1`.** With the fixed clang (`2f70dd019`, ShortAlign = 8), the
option is a no-op:

* `-Oz` and `-Oz -fpack-struct=1` produce identical objects in all 45 TUs
  (`E/layout/fix-{plain,pack}`).
* The shipped clang needs the option only to work around the ShortAlign = 16
  bug, which is what reproduces SDCC's 1-byte alignment.

A probe of 140 firmware records against SDCC `sizeof`/`offsetof`
(`E/layout/probe/sdcc-vs-fix.txt`) found:

* 120 records identical;
* 20 records differing only because of pointer width. A pointer is 3 bytes in
  SDCC (generic) and 2 bytes in clang. For example, `zcl_basic_t` is 152/145 B,
  `bdb_join_t` 1313/1312, `mac_link_driver_t` 305/302 and `nwk_aps_t` 583/582.

All 20 are internal-only C state. None of them is a wire, NV, DMA, assembly,
fixed-offset or aliased layout; those layouts use byte arrays and explicit
serialisation and match SDCC.

**SDCC source identity.** The +1578 CODE / +19 XSEG / −1 BSEG regression in §3
was attributed per symbol (`E/audit/`):

| Cause | Effect |
|---|---|
| `mac_tx` save/load_control rewritten from `memcpy` to per-field copies | +649 / +616 |
| `zdo_runtime` helper refactor | +358 ×2, plus 13 B of parameters |
| `-Itoolchain/clang/include` in the SDCC defines, which swapped the `memcpy`/`memset` prototypes | +2/+4/+6 at many call sites |
| `aes` `owned()` rewrite | BSEG −1 |

All four were redesigned in `5b5687b`. `b3542de` then routes the SDCC
spill-hint `volatile` locals through `SDCC_VOLATILE`. These are 288 of the 294
IR allocas, used only via `addrspacecast` + `volatile`, so on clang they were a
pure pessimisation.

The SDCC image of the compatibility source is **byte-identical** to the base:
ihx `10155c95…`. All LLVM numbers below come from this same source.

**Corrected LLVM baseline (LLVM-PLAIN, `Oz-v4-plain`).**
Configuration: `-Oz`, no pack, no LTO, overlay off, fixed lld/startup/banking.

| Metric | Value |
|---|---:|
| CODE | 196172 |
| Common CODE | 16179 |
| Raw XDATA | 4841 `.bss`, plus the absolute status block at 0x1E00 |
| Register bytes | 8 |
| Pseudo-register bytes | 24 |
| Ordinary globals | 0 |
| Stack | 476 / 128 (fail) |
| Thunks | 373, 0 encoding errors |

* Banks 1–5 are full (0–2 B free).
* Bank 6 holds 16156 B and bank 7 holds 0 B.
* Aggregate bank slack is 43239 B.

The earlier `Oz-fix` figures (222090 CODE / 12389 common / 4842 XDATA /
529 stack) are superseded.

Why the numbers changed:

* About 25 KB of the CODE drop is the clang-only `SDCC_VOLATILE` change.
* Common CODE grew because `zdo_runtime` code is now inlined into
  `MCU_COMMON_CODE` functions.

### 19.2 Stack decomposition

The census tool (`-mcs51-frame-census` plus `E/stack/stack_analysis.py`)
attributes every byte on the worst path (`E/stack/census-Oz-*.json`). The
analyzer fails closed on recursion and on any `jmp @a+dptr` other than the save
helpers and the RAM-exec entry. It found neither.

| Category (worst path) | v2 Oz-plain | **v4-plain** |
|---|---:|---:|
| `main` return | 2 | 2 |
| Nested return addresses (19 calls) | 38 | 38 |
| Bank thunk: return + FMAP (19 banked calls) | 38 + 19 | 38 + 19 |
| Callee-saved R2–R7 | 96 | 89 |
| Callee-saved IM8–IM23 (pseudo-registers) | 138 | 170 |
| Outgoing arguments | 40 | 40 |
| RA spills | 66 | 55 |
| Fixed locals / allocas | 90 | 25 |
| Interrupts | 0 (no ISRs; vectors 3 B) | 0 |
| **Total** | **527** | **476** |

Worst path for v4-plain, 476 B:

`main → join_smoke_poll → mac_link_driver_step → observe → bdb_join_step →
nwk_aps_step → nwk_aps_transmit → security_keys_send → seal → ed_wire_crypt →
ccm_star_crypt → feed → flush → encrypt → aes128_encrypt_block → sample →
timebase_expired → link_work_io → child_work_address → edge`.

All of the top 20 paths (476…) run through the same crypto chain.

Top functions by hardware-stack bytes (frame, CSR, of which IM):

| Function | Total | Frame | CSR | CSR IM |
|---|---:|---:|---:|---:|
| `aes128_encrypt_block` | 54 | 26 | 22 | 16 |
| `ed_wire_crypt` | 53 | 15 | 22 | 16 |
| `ccm_star_crypt` | 44 | 20 | 22 | 16 |
| `mac_join_step` | 36 | 4 | 22 | 16 |
| `zigbee_key_hash` | 36 | 8 | 20 | 14 |
| `zdo_runtime_step` | 33 | 3 | 22 | 16 |
| `mac_poll` `step` | 32 | 2 | 20 | 14 |
| `mac_scan_step` | 32 | 0 | 22 | 16 |
| `mac_tx_interval_submit` | 32 | 6 | 16 | 10 |
| `security_keys_receive` | 32 | 4 | 22 | 16 |

The JSON holds the top 50 functions and top 20 paths.

Static totals over 380 functions:

| Object | v4 | v2 |
|---|---:|---:|
| Allocas | 52 B | 548 B |
| Spills | 279 B | 393 B |
| CSR R | 963 B | 988 B |
| CSR IM | 1826 B | 1498 B |
| Outgoing arguments (max) | 245 B | – |

**Conclusion.** About 54% of the demand (259 of 476 B) is callee-saved register
traffic. Spills add 12% and argument/local slots 14%. The irreducible part
(return addresses and bank thunks) is 97 B.

### 19.3 Why the existing overlay saves 0 B

The `-mcs51-overlay-census` run (`E/overlay/`) shows that `MCS51Overlay` sees
only IR allocas: **294 allocas, 597 B, 0 eligible**.

* 219 are rejected because their function has external linkage.
* 75 are rejected as escaping.
* 288 of the 294 (586 B) are used only through `addrspacecast` to the IDATA
  space plus `volatile`. These are the SDCC spill-hint idioms, and the pass
  treats `addrspacecast` as an escape.

The pass runs before instruction selection. The objects created later are
invisible to it:

| Created after the pass (v2) | Bytes |
|---|---:|
| RA spills | 393 |
| CSR saves | 2486 |
| Outgoing arguments | 245 |
| **Total** | **3124** |

That is about 85% of all frame bytes. The answer is **yes**: most problematic
stack bytes are created after the overlay pass. Even a perfect IR-level overlay
could not address CSR or spills.

### 19.4 Frame-object classification

The analyzer proves for this firmware that there is:

* no recursion;
* no ISR (vectors are 3 B);
* no unresolved indirect call.

The prototype still fails closed per function:

* recursive, `interrupt` or variadic functions keep stack frames;
* functions with dynamic allocas keep stack frames.

| Object class | Bytes (v4, static) | Classification |
|---|---:|---|
| RA spill slots | 279 | **Static-overlay eligible** (not addressable, function-private) |
| CSR save slots | 2789 | Eligible as static save areas, or removable by CSR policy |
| Outgoing stack arguments | 245 (max) | Eligible only with static argument frames, an ABI change. Currently **must stay on the stack** |
| Address-taken locals (FRAMEADDR8 IDATA pointers) | 52 | **Must stay on the hardware stack** in this prototype. An 8-bit IDATA pointer cannot address PDATA; moving them needs generic or XDATA pointers |
| Return addresses, bank thunk FMAP | 97 on the worst path | **Must stay on the hardware stack** |
| Variable-sized objects | 0 | – |
| Interrupt-sensitive state | 0 | – |
| Non-overlayable static | 0 | – |

### 19.5 Architecture comparison

| | A. Hardware-stack frames (current) | B. XDATA/PDATA software stack | C. Static frames + overlay |
|---|---|---|---|
| Stack/RAM | 476 B demand, does not fit | Frames off the 128 B stack; RAM equals the dynamic peak | 97–162 B hardware stack; the PDATA pool is the overlay peak (55–222 B measured) |
| Per-access cost | `mov a,sp; add; mov r0,a; mov a,@r0` | Pointer arithmetic + `movx`, about 6–10 B | `mov r1,#sym; movx`: 3 B, plus A save if live |
| CODE (measured/estimate) | baseline | Estimated well above C (not prototyped) | spills: −1443; spills + CSR none: +23.6 KB |
| Reentrancy | full | full | none; fails closed |
| ISR safety | yes | needs an atomic SP update | needs separate ISR frames or exclusion |
| Complexity | existing | new frame lowering + runtime | frame lowering + link-time allocator |
| LTO needed | no | no | **no**: a per-function metadata section plus a prelink allocator |

B was not prototyped. C was chosen because the firmware is provably
nonrecursive and ISR-free, and because SDCC's overlay already succeeds on the
same program.

### 19.6 Static-frame design without LTO

Compiler side:

* Each eligible function gets a section `.mcs51.pdata.bss.sframe.<fn>`,
  holding the symbol `__sframe.<fn>`.
* Eligible frame indices are rewritten by
  `MCS51RegisterInfo::eliminateStaticFrameIndex` to `MOV R1,#__sframe.<fn>+off`
  followed by `MOVX`.

Link side (`E/sframe/sframe_link.py`):

* **Pass 1** links the program with the frames packed. The code size is
  independent of the frame addresses, so layout is final.
* It builds the linked call graph with the fail-closed analyzer.
* It assigns `offset(f) = max over callers (offset(c) + size(c))`, so the
  frames of simultaneously active functions never overlap.
* **Pass 2** relinks with the fixed PDATA addresses and verifies the result.

This is the SDCC overlay model, driven by linked-image facts instead of LTO.
Production would want:

* compiler-emitted call-edge and frame metadata, rather than disassembly;
* an lld or prelink pass instead of the Python script.

### 19.7 Spills, CSR and pseudo-registers

**Spills.** Generic RA creates ordinary spill frame indices
(`MFI.isSpillSlotObjectIndex`), and they can be redirected without touching the
register allocator. `-mcs51-static-frames` does exactly that.

**CSR (Phase 10).** The default callee-saved set is R2–R7 plus IM8–IM23, which
is 22 bytes:

* 250 of 380 functions save IM8/IM9;
* 51 functions save 8 IMs and 14 functions save 16.

The bank thunk adds no preservation beyond FMAP.

The shipped backend attaches **no call-preserved regmask** to calls. Changing
the CSR list alone therefore miscompiles: callers assume preservation. The
study flag adds the regmask in `LowerCall` and in the `ICALL_W` inserter.

| Option | Stack | CODE |
|---|---:|---:|
| `-mcs51-csr=r` | 253 | +23.5 KB |
| `-mcs51-csr=none` | 162 | +25.1 KB |

The CODE grows because values now live across calls in static spill slots
(each access costs 3–5 B). The CSR set is therefore not "too broad" in a
CODE sense: it trades stack for code density.

The better design is to keep the CSR policy and move the **save area** into the
static frame. This could be done with a shared save/restore helper operating
through `@R1`. Expected result: about v4-sfn stack at about v4-sf CODE. This is
an estimate and is not prototyped.

**Pseudo-registers (Phase 11).** All 24 bytes are used, and IM0/IM1 are the
hottest (about 6.7k and 6.3k references in v4-plain). IM20–IM23 are saved by
14–35 functions.

A larger IM file would reduce spills (at most 55 B on the worst path) but would
add CSR bytes unless the IMs are caller-saved. With `csr=r` the IMs become
scratch space and the stack is still 253 B. Enlarging the IM file is therefore
not the main lever: the save policy is.

### 19.8 Prototype (`-mcs51-static-frames`, ± CSR study)

| Metric | SDCC 4.2 | SDCC TM | LLVM plain (v4) | LLVM frame (v4-sf) | Frame + `csr=none` (v4-sfn) |
|---|---:|---:|---:|---:|---:|
| CODE | 253445 | 247449 | 196172 | 194729 | 219805 |
| Common CODE | 32714 | 31922 | 16179 | 16155 | 17560 |
| Raw XDATA | 7676 | 7676 | 4841 | 4841 + 55 pool | 4841 + 222 pool |
| DATA/IDATA | – | – | 8 reg + 24 IM | same | same |
| Static frames (in image) | – | – | 0 | 100 frames, 265 B → overlaid 55 B | 219 frames, 1873 B → overlaid 222 B |
| Hardware stack demand | 45 | 45/46 | 476 | 421 | **162** |
| Stack capacity | 128 | 128 | 128 | 128 | 128 |
| Peak SP (sim, cases 1–3) | – | – | 0x93–0x9E | 0x93 | 0x8B (case 4: 0xB5) |
| Worst bank free | 13 | 90 | 0 | 0 | 0 |
| Aggregate bank slack | – | – | 43239 | 44658 | 20987 |
| Simulator cases passed (0–4) | ref | ref | 1, 2, 3 | 1, 2, 3 | 1, 2, 3, 4 |

* **Per-bank deltas vs plain.** v4-sf: bank 6 −1418 B, the other banks
  unchanged. v4-sfn: banks 1–6 full and bank 7 +5641 B used.
* "Worst bank free 0" comes from the greedy fill-first auto-banking in every
  LLVM image. The bank-7 tail is the real margin. There was no rebalancing.
* The compiler emitted 109 frames / 331 B for v4-sf; 100 frames / 265 B are in
  the linked image. The cause of the difference was not verified.

**Simulator (patched ucsim; `E/ucsim-fix`).** Upstream ucsim had a
use-after-free in `~cl_banker`, which this study fixed. The harness also missed
the high-water mark of the reset stage; this is now fixed (`reset_peak_sp`).

* plain and sf, cases 0 and 4: stack overflow at step 2, event 3.
* sfn, case 4: passes, peak 54 B.
* sfn, case 0: passes event 3 and reaches step 2, event 259, where it fails on
  the new banking defect below. This is not a stack failure.

**New generic defect: stack arguments through bank thunks** (all LLVM images,
shipped and fixed toolchains).

`LowerFormalArguments` addresses incoming stack arguments at
`-(LocMemOffset+2)`, which assumes a bare LCALL. The cc2530 bank thunk
(`push FMAP; mov FMAP,#b; lcall f; pop FMAP; ret`) inserts 3 more bytes, so the
callee reads the saved FMAP and the caller's return address as its arguments.

* Observed: `mac_radio_init → clock_select_init` read `&clock_status` as 0xDA05
  and wrote XDATA 0xDA17.
* Scale: 45 stack-argument callees are reached through 86 thunked call sites
  (`E/banking/stackarg-*.json`). `thunk_audit.py` verified only the
  bank/target encoding of the thunks; it did not check stack-argument offsets.
  Simulator cases 1–3 do not check these calls.
* Reduced test: `E/banking/stackarg-thunk/run.sh`. The banked call returns
  0x0100 instead of 0x1234; the direct call is correct.

It is not fixed because banking is frozen. The fix options are:

* callee offset +3, with every banked entry going through the thunk;
* a layout-preserving thunk;
* static argument frames, which also removes 38–40 B from the stack.

**Generic pointers.** The two defects from §9 are still not reached:
`-mcs51-static-frames` uses only `MOVX @R1` and no generic pointers. Their
reduced tests are kept.

**Full LTO** remains deferred (§16).

**Economics.** On the same source, LLVM is smaller than SDCC TM:

| LLVM image | CODE vs TM | Raw XDATA vs TM |
|---|---:|---:|
| plain (v4) | −51.3 KB (−20.7%) | about −2.8 KB |
| frame + `csr=none` (v4-sfn) | −27.6 KB (−11.2%) | about −2.6 KB |

The advantage became larger after the baseline correction, not smaller.

### 19.9 Answers (backend-architecture study)

**Baseline integrity**

1. `-fpack-struct` required: **no** with the fixed clang (no-op in 45/45 TUs);
   only as a workaround for the ShortAlign bug in the shipped clang.
2. Layout semantic equivalence: yes for every externally observable layout. 20
   internal records differ only by the 2-byte versus 3-byte pointer width.
3. SDCC compatibility-source identity: **byte-identical** (ihx `10155c95…`,
   `5b5687b` + `b3542de`).
4. Corrected LLVM CODE: 196172 (common 16179).
5. Corrected LLVM XDATA: 4841, plus the absolute status block.

**Stack decomposition (v4-plain)**

6. Total demand: 476 B (v2: 527; earlier: 529).
7. Locals: 25 B.
8. Spills: 55 B.
9. Saved registers: 259 B (89 R2–R7 + 170 IM).
10. Arguments: 40 B.
11. Return addresses: 40 B (38 nested + 2 `main`).
12. Banking: 57 B (19 thunks × 3).
13. Interrupt contribution: 0 B (no ISRs).

**Existing overlay failure**

14. IR allocas seen: 294 (597 B); 0 eligible.
15. Machine spill slots missed: all of them, 393 B (v2) / 279 B (v4). CSR
    (2486 B) and arguments (245 B) were missed as well: about 85% of frame
    bytes are created after the pass.
16. Why the overlay saves 0: `addrspacecast` is treated as an escape; the SDCC
    volatile idioms; external linkage; and the pass runs before ISel/RA.

**Static frame design**

17. Eligible frame bytes: 279 (spills) + 2789 (CSR save areas) = 3068 static
    bytes.
18. Non-overlayable bytes: 0.
19. Must-stack bytes: return addresses + bank (97 on the worst path); 52
    address-taken bytes; arguments (245 max) unless static argument frames are
    added.
20. Recursion/reentrancy blockers: none in this firmware (proven). The compiler
    fails closed for others.
21. Proposed storage: an overlaid PDATA page (`MOVX @R1`, at most 256 B) for
    frames; DATA/IDATA only for IMs; XDATA via DPTR as overflow.

**Prototype**

22. Functions converted: 100 (spills only) / 219 (with `csr=none`).
23. Stack before: 476.
24. Stack after: 421 (sf) / 162 (sfn).
25. CODE delta: −1443 / +23633.
26. XDATA delta: +55 / +222 (overlaid PDATA pool).
27. Bank-headroom delta: worst bank 0 → 0; aggregate slack +1419 / −22252.
28. Simulator result: sfn passes cases 1–4. Case 0 is blocked by the bank-thunk
    stack-argument defect, not by the stack.

**Recommendation**

29. Continue LLVM backend work: **YES**. The CODE/XDATA advantage holds on the
    same source, and the static-frame mechanism scales.
30. Likely path to a stack of at most 128 B: **PLAUSIBLE**. The v4-sfn worst
    path is 162 B. Static argument frames (−38) and IDATA→static allocas (−25)
    leave about 99 B, but this is not yet proven, and moving the CSR save area
    must also avoid the +25 KB CODE cost.

The classification remains **UNSUITABLE** for production for now.

## 20. Backend development: ABI fixes, stack ≤128 B, simulator commissioning

Toolchain: `devbis/llvm-mcs51` `23.1.2-mcs51-20261004` (`44a8e3000`) plus the
study branch `study/fixes`. All fixes are generic, have lit tests, and contain
no firmware-specific code. Firmware: the same compat source, `lg_esl29_rev03`,
`JOIN_SMOKE_KEY_MODE=default-tc`. No LTO. Banking is frozen: fixed lld, fixed
startup, fill-first auto-banking.

### 20.1 Generic compiler defects fixed in this phase

| Commit | Defect | Effect in the firmware | Test |
|---|---|---|---|
| `6204dbdcf` | Odd-width (i24) generic-pointer access crashed ISel | not reached (§9) | `generic-pointer-odd-width.ll` |
| `70695a362` | `ICALL_W` inserter dropped the implicit argument-register uses, so every register argument of an indirect call was dead | 0 indirect calls in the firmware | `icall-arg-registers.ll` |
| `f0e7a932b` | Banked callees read stack arguments 3 B too high (20.2) | 45 callees / 86 thunked sites | `bank-stack-arg-bias.ll`, `mcs51-external-bank-thunk-common.s` |
| `01683fe51` | PEI ignored stack-argument pushes for frame indices inside a call sequence (`SPAdj`) | wrong locals between pushes (`mac_tx_submit`, `seal`, …); case 0 step 2 ev508 | `frame-index-inside-call-sequence.mir` |
| `55cf933ea` | Escaping CODE constants were read with `MOVX` from XDATA addresses | wrong rodata reads; now an XDATA copy (35 B `.data`) | 3 lit tests |
| `d137a4e3e` | DPTR was reused across pseudos that were not expanded yet | `mac_scan.c:369` stored to the wrong offset; case 0 step 21 | `dptr-reuse-pending-pseudo.ll` |
| `59e068541` | Constant i8 call arguments were selected into `A`, so all but one went through the hardware stack | `f(1,2,3,4)` built a 4-byte SP frame | `call-const-byte-args.ll`, `call-i8.c` |
| `b5fafbbe2` | New opt-in ABI `-mcs51-wide-args`: words 5–8 in IP4–IP7, bytes 5–12 in IM16–IM23. Refused with the default CSR set. | removes outgoing-argument pushes | `wide-args.ll` |
| `dc1ca97a3` | The ALU-pseudo expander's "operand is still in A" scans checked only physical `A`. A def of a virtual register in `MCS51AReg` in between (later allocated to `A`) was missed, so `0 - (x & 1)` computed `0 - 0`. Also present in the pinned upstream toolchain. | the READY CSMA backoff LFSR in `join_smoke.c` (`(s>>1) ^ (s&1 ? 0xB8 : 0)`) never applied the tap; case 0 step 808 drew backoff 4 instead of 0, failing at step 811 | `sub-from-const-acc-clobber.ll` |
| `709c6d3c0` | Under `minsize`, i8 `udiv`/`urem` select `DIV AB`. The `urem` expansion read the remainder with a generic direct load of 0xF0 that carries no use of `B`, so `DIV AB` (both results apparently dead) and its `mov a,lhs` were erased by dead-instruction elimination: `x % 41` returned 41. Now uses `MOV_A_B` (`Uses=[B]`). | `zcl_sensor.c` `ZCL_SENSOR_PHASE` (`sample % 41`) was always 41, so the initial report baselines were 2510/5525/28/159 and no value ever changed; case 0 queued no ZCL report at step 908 and missed the step-909 APS seal | `urem-i8-div-ab.ll` |

Lit gate (`CodeGen/8051` + `lld/test/ELF` mcs51): **153/154**. The single
failure is the pre-existing assertions-only `cc2530-pdata-overflow.c`.

The `dc1ca97a3` fix changes the code of one firmware object
(`join_smoke.o`, three instructions, same size). The runtime library
disassembly is unchanged. CODE, banks, thunks and the stack census of
`Oz-v9wc` are unchanged.

The `709c6d3c0` fix changes only `zcl_sensor.o` (the only firmware object with
`DIV AB`): CODE 213093 → **213132** (+39, bank 6). The runtime (built at
`-O2`, magic-multiply division) disassembles identically. Thunks 367/0 bad,
0 entry violations, census 106 are unchanged.

How it was found: case 0 then failed at step 909 event 153. The reference
does an AES operation there; the image did not. Backtraces in the instrumented
host model showed the AES was `nwk_aps_step` → `security_keys_send` → `seal`
for a ZCL report queued one step earlier by `join_smoke.c` `serve()`, when
`zdo.last` crossed 0x400000 and the synthetic sensor sample went 0 → 1. The
image's XDATA (`ZCL_SENSOR_STATE`) held `last` = 2510 = 2100 + 41·10, an
impossible phase for `% 41`; a 3-line reduction reproduced it.

How it was found: case 0 failed at step 811 inside `mac_adapter_step`
(SCHEDULED, `reached(live, at)` false). An instrumented copy of the host
reference model showed `requested.at` = 0x2F9685 against 0x2F96D5 in the
image, a difference of four 20-symbol backoffs. Per-step XDATA dumps from the
simulator then showed the LFSR state 0x28 where the host had 0x90
(0x51 → 0x28 is the shift without the tap).

**Harness and tool defects found (not compiler defects).**

* `llvm_join_replay.py` loaded `.data` from the VMA instead of the LMA, so every
  earlier run of an image with `.data` started with wrong initial values.
* Every earlier LLVM link used the **shipped** `mcs51-runtime.o` (sha
  `d1062d34…`), which the unfixed compiler built with the default ABI.
  `rt/mk_rt.sh` now builds ABI-matched runtimes with the fixed compiler for each
  CSR/argument policy, and `link.sh` takes `RESOURCE_DIR`. This is mandatory for
  `-mcs51-wide-args` because the 5th byte argument of `__mcs51_gptrput*` moves
  to IM16.
* The ucsim `~cl_banker` use-after-free is fixed (§19.8).
* The host reference vectors encode the **SDCC** addresses of the AES DMA
  descriptors (`TARGET_aes_dma0/1` = 0x07FE/0x0806 in
  `ref/join_smoke_layout.h`). They appear as DMA0CFG/DMA1CFG writes and reads.
  The LLVM image correctly programs its own `_aes_dma0/1` (0x0798/0x07A0).
  Cases 0 and 10 failed at step 395 until the harness relocated those event
  values at load time (`relocate_dma_cfg`). The relocation first requires the
  reference byte to equal the SDCC layout byte; descriptor contents were
  already checked against the image symbols.
* `next_pc` returned "unknown" for sites inside the RAM copy of the flash
  template. So the adjacency check could not see that, after `w FCTL`, the
  check read `movx a,@dptr` is the very next instruction. A `run` then
  executed that read without an event, using the stale written value. This
  shifted every later poll by one event. The symptoms were:
  * case 10 reached `ram_stop` one poll early;
  * cases 0 and 8 hung after the erase completed (the B.7 mismatch path).

  The SDCC harness hard-codes this adjacency. `next_pc` now decodes template
  sites through their ROM copy. No earlier passing case ran a flash command.
* **Placement-contract defect in the LLVM link profile (fixed in compat
  source).** `flash_exec.h` requires `flash_exec_command` to be in common CODE
  below 0x8000, and the SDCC join-smoke build places it in `CSEG`
  (`JOIN_SMOKE_COMMON`). The LLVM auto-banker had put it in bank 4 (0xB95C).
  The RAM flash template runs with XMAP set and returns with `RET` while XMAP
  is still set, so its caller must not sit in the XMAP-hidden 0x8000–0x9FFF
  window of a bank. In this image the banked address (0xB95C) happened to be
  outside that window. The cases 0/8 hang was later traced to the template
  defect below, not to this placement. The fix is kept because it restores the
  documented contract.
  * The fix is `MCU_COMMON_CODE` on the definition. It is the same mechanism
    `idle` already used, and it expands to nothing under SDCC: the SDCC ihx is
    still `10155c95…`.
  * `flash_exec_command` is the only function that writes MEMCTR.XMAP.
    `flash_nv_read` writes only XBANK, which does not change code fetch, so it
    may stay banked.
  * This is a link-profile/source-contract issue, not a compiler defect.
* **Flash RAM-template result-offset defect (compat source, fixed).** The
  hand-written LLVM template `src/flash_exec_mcs51.s` (from the original
  `clang_compat.diff`) stored the result and the last FCTL value at
  `flash_exec_work[0..1]`. The SDCC template stores them through the pointer
  left after its seven argument reads, which is `flash_exec_work[7..8]`.
  `flash_exec_command` presets `work[7] = 0xFF` and returns `work[7]`, so every
  LLVM flash command reported failure. After the first completed erase, the NV
  layer took its error/recovery path (a long fill loop) instead of the
  reference `flash_nv_read` verify. Cases 0 and 8 timed out there.
  * Fix: both stores use `#(flash_exec_work + 7)`. The template size is
    unchanged.
  * The link now takes the template from the compat tree (`wt-sdcc`,
    `TEMPLATE_SRC` overridable) instead of `wt-llvm`.
  * SDCC does not assemble this file, so the SDCC ihx is unaffected.
  * This is a source-port defect, not a compiler defect. Cases without a
    completed flash command (1–6, 9) could not see it. Case 10 ends at the
    poll-limit `ram_stop`, so it could not see it either.
* The harness can snapshot and resume (`SNAP_DIR`, `SNAP_EVENTS`, `RESUME`).
  A snapshot stores the ELF sha and the full captured state.

**Compat source.** `1649965` (compat-v4) routes the remaining SDCC
cast-reload spill hints (`CW_READ_HOME`, `*(T volatile *)&x`) through
`SDCC_VOLATILE`. Under LLVM they produced 52 B of address-taken stack objects
(25 B on the worst path). The SDCC ihx is unchanged (`10155c95…`).

### 20.2 Bank-thunk stack argument fix

1. **Exact three-byte cause.** The bank thunk is
   `push FMAP; mov FMAP,#b; lcall F; pop FMAP; ret`. A banked callee therefore
   sees `[args][caller ret 2][FMAP 1][thunk ret 2]`.
   `LowerFormalArguments` assumed a bare `LCALL` and used
   `-(LocMemOffset+2)`, so it read the saved FMAP byte and the caller's
   return address as arguments. Observed:
   `mac_radio_init → clock_select_init` read `&clock_status` as 0xDA05.
2. **Backend layer fixed.** The ABI is fixed in three places:
   * **ISel.** `MCS51Banking.h` adds `isMCS51BankedFunction` and
     `getMCS51IncomingStackArgBias` (2 + 3 for banked functions).
     `LowerFormalArguments` and the varargs fixed object use this bias.
     `LowerCall` routes an explicit-bank callee through the thunk when it has
     stack arguments, even from the same bank.
   * **AsmPrinter.** The thunk is unchanged.
   * **lld.** A weak thunk whose target is common is redirected to the direct
     target.
3. **Direct-call regression.** Common and near callees keep their offset
   (`#252` in `bank-stack-arg-bias.ll`). A same-bank call with only register
   arguments stays direct (`cc2530-banked-call.c`). Runtime ABI matrix
   (`banking/abi-matrix`, 14 scenarios): shipped 0/14, fixed **14/14**.
4. **Banked-call regression.** In `bank-stack-arg-bias.ll` a banked callee
   reads at `#249` (auto-bank and explicit). A same-bank call with stack
   arguments goes through `__mcs51_bankcall_*`. lld test
   `mcs51-external-bank-thunk-common.s`. Reduced runtime proof
   `banking/stackarg-thunk/run.sh`: shipped `0x0100`, fixed `0x1234`.
5. **Firmware affected call sites.** Before the fix: 45 stack-argument callees
   through 86 thunked call sites (v4-sfn).
   * After the fix (v5-sfn), `stackarg_entry_audit.py` found 48 stack-argument
     functions: 85 thunk entries, 1 direct entry to a common function, and **0
     violations**.
   * Final image (v9w/v9wc, wide args): 2 stack-argument functions, 3 thunk entries,
     0 violations.
6. **Simulator result after fix.** Case 0 passed the old step 2 / ev259 failure.
   It then exposed independent defects in turn: PEI `SPAdj` (ev508), the
   harness LMA bug (step 6), and DPTR reuse (step 21). After each fix the
   replay moved further. The final result is in 20.5.

### 20.3 Final hardware-stack architecture

The bound is static and whole-program (`stack/stack_census.py`, from
`-mcs51-frame-census` plus the linked call graph).
* There is no recursion.
* There are no indirect calls.
* Interrupt vectors are unpopulated, and nothing writes `IEN0`/`EA` (`idle`
  only reads `IEN0`).

**Callee-save / argument policy matrix.** Every policy is built with
`-mcs51-static-frames -mcs51-frame-census` and an ABI-matched runtime. Every
policy has 367 thunks, 0 bad, and 0 stack-argument entry violations.

| Policy | CODE | Common | PDATA frame pool | Stack | Worst-path composition | Stack-arg fns |
|---|---:|---:|---:|---:|---|---:|
| v4-plain (no static frames, default CSR) | 196172 | 16179 | 0 | 476 | ret 40, bank 57, csr 259, args 40, locals 25, spills 55 | – |
| v9-def (static frames, default CSR) | 191300 | 16159 | 48 | 398 | ret 40, bank 57, csr 260, push 40, frame 1 | 48 |
| v9-r (`-mcs51-csr=r`) | 214241 | 17576 | 180 | 232 | … csr 94, push 40 | 48 |
| v9-rw (`csr=r` + wide args) | 212141 | 17584 | 173 | 200 | … csr 94, push 8 | 2 |
| v9 (`csr=none`) | 215954 | 17529 | 212 | 138 | … csr 2, push 38 | 48 |
| **v9w (`csr=none` + wide args)** | **213005** | **17506** | **203** | **106** | ret 40, bank 57, push 8, frame 1 | **2** |
| **v9wc (v9w + `flash_exec_command` common, 20.1)** | **213132** | **18711** | **203** | **106** | same as v9w | **2** |

`csr=none` + wide args is selected. It is the only policy at or below 128 B.
It is also smaller than `csr=none` alone (−2949 B CODE) because outgoing
argument pushes disappear. The measured image is **v9wc**: v9w with the
placement contract fix of 20.1. It has the same flags, the same stack census
and the same audits (367 thunks, 0 bad, 0 entry violations).

7. Starting stack: **476**.
8. No-callee-saved prototype: **162** (v4-sfn/v7d). Then 138 with compat-v4
   (−24 address-taken reload objects).
9. Final selected callee-save policy: **none** (`-mcs51-csr=none`), plus
   `-mcs51-wide-args`. Values live across calls go to the per-function static
   frame instead of being pushed.
10. Static machine-frame bytes moved: **1684 B** in 215 linked frames, overlaid
    by the call graph into a **203 B** PDATA pool (`MOVX @R1`).
11. Unavoidable dynamic bytes (worst path):
    * 40 return addresses (19 nested + `main`);
    * 57 banking (19 thunks × [thunk ret 2 + FMAP 1]).

    That is **97 B** with the current thunk protocol. A further 9 B are
    avoidable in principle: 8 B of argument pushes beyond the wide-argument
    registers (`ed_wire_crypt`, `encrypt`) and 1 B for an address-taken local
    (`sample`).
12. Final whole-program stack: **106 B** (`main` → … → `child_work_address` →
    `edge`, 19 banked hops; the top 20 paths are all 106).
13. Interrupt-inclusive bound: **106 B**. There are no ISRs, so the interrupt
    contribution is 0. Any future ISR must fit in the remaining 22 B.
14. Physical capacity: **128**.

Further levers, not implemented:
* **bank-ret thunk protocol.** Projected −2 B per hop, about −38 on the worst
  path, but every banked entry would have to go through a thunk.
* **Static argument frames** for the remaining 8 B.

### 20.4 Cost

| Metric | LLVM baseline (v4-plain) | Final LLVM (v9wc) | SDCC 4.2 | SDCC TM |
|---|---:|---:|---:|---:|
| CODE | 196172 | **213132** | 253445 | 247449 |
| Common CODE | 16179 | 18711 | 32714 | 31922 |
| Raw XDATA | 4841 | **5079** (4841 bss + 35 data + 203 frame pool) | 7676 | 7676 |
| Internal DATA | 8 regs + 24 IM, 0 globals | 8 regs + 24 IM, 0 globals | 51 + OSEG 10, 88 bits | 51 + OSEG 10, 80 bits |
| Hardware stack | 476 | **106** | 45 | 45/46 |
| Stack capacity | 128 | 128 | 45 | 46 |
| Minimum bank headroom | 0 | 0 | 13 | 90 |
| Aggregate bank slack | 43239 | 28811 | – | – |
| Bank thunks (named) | 367 | 367 | SDCC banker | SDCC banker |
| Incorrect thunks | 0 | 0 | – | – |

The SDCC columns come from §14, measured on the same benchmark source; they
were not re-measured. The absolute status block at 0x1E00 is excluded from raw
XDATA in all columns.

15. Final CODE: **213132**.
16. CODE delta vs LLVM baseline: **+16960** (+8.6%). This is mostly the
    `csr=none` static-frame save/restore (`MOVX @R1`) that replaces push/pop.
17. CODE delta vs SDCC TM: **−34317** (−13.9%). Against SDCC 4.2: −40313.
18. Final XDATA: **5079** raw (+238 vs the baseline: the 203 B frame pool and
    35 B of CODE-constant copies).
19. XDATA delta vs SDCC TM: **−2597**.
20. Minimum bank headroom: **0** (banks 1, 3, 4, 5).
    * Bank 2 has 1 B free, bank 6 has 2186 B, and bank 7 has 26624 B.
    * Every LLVM image so far has a worst bank free of 0, because the auto-banker
      packs fill-first. The real margin is the 28811 B of aggregate slack.
    * No rebalancing was done.

Per-bank used bytes (v9wc): b1 32768, b2 32767, b3–b5 32768, b6 30582, b7 0.
Compared with v4-plain, bank 6 is +14426 and bank 7 is unchanged. Compared
with v9w, the common area grew by 1205 B and bank 6 shrank by 1117 B (the
banked copy and its internal thunk are gone).

### 20.5 Runtime (patched ucsim, `sim/llvm_join_replay.py`, image `Oz-v9wc`)

`complete` means that every step's status block (0x1E00–0x1E30) and NV journal
matched the host reference vectors.

| Case | Steps | Result | Terminal | Peak SP | Stack used |
|---|---:|---|---|---:|---:|
| 0 | 1142/1142 | complete | serving | 0xE4 | 101 B |
| 1 | 256/256 | complete | fault | 0x8B | 12 B |
| 2 | 1/1 | complete | fault | 0x8B | 12 B |
| 3 | 1/1 | complete | fault | 0x8B | 12 B |
| 4 | 3/3 | complete | fault | 0xB3 | 52 B |
| 5 | 130/130 | complete | fault | 0xC8 | 73 B |
| 6 | 405/405 | complete | fault | 0xC8 | 73 B |
| 8 | 406/406 | complete | fault | 0xDB | 92 B |
| 9 | 25/25 | complete | fault | 0xC8 | 73 B |
| 10 | 396/396 | complete | flash-ram-stop | 0xDA | 91 B |

Every terminal is the one the host reference vector ends in. Cases 1–6, 8 and 9
are fault-injection vectors that end in `fault`. Case 10 ends at the
flash poll-limit `ram_stop`. Case 0 is the nominal join. It ran 559709
peripheral stops in 4.8 h with 10 adjacency retries.

21. Simulator cases passed: **10/10** (every step's status block and NV journal match).
22. Complete join: **YES** (case 0, 1142/1142 steps: scan, association, key transport, NV commit, device announce).
23. Final READY/serving: **YES** (terminal `serving`; ZCL reports sealed and sent).

### 20.6 Classification

24. Stack ≤128: **YES** (106 static and whole-program; measured peak SP 0xE4 = **101 B** in case 0, 27 B below capacity).
25. Banking ABI proven: **YES**.
    * 367 thunks, 0 incorrect.
    * 0 stack-argument entry violations.
    * ABI matrix 14/14.
    * Lit tests for the bias, the thunk routing and the lld redirect.
26. Continue backend work: **YES**. The stack blocker is resolved and the
    advantage holds: −34317 B CODE and −2597 B raw XDATA vs SDCC TM. The
    remaining work is bank headroom (0 B fill-first), the two generic-pointer
    defects of §9, ISR support within the 22 B margin, and hardware
    validation.
27. LLVM profile classification: **SIMULATOR-PROVEN RESEARCH PROFILE**.
    Stop condition (A) is met: a reproducible ≤128 B profile completes the
    full simulator join to READY/serving. It is not production-ready:
    it has not run on hardware and has no bank headroom.

Not production-ready. Hardware remains a later phase.

Reproduce:
* Compiler flags: `-Oz -mllvm -mcs51-frame-census -mllvm -mcs51-static-frames
  -mllvm -mcs51-csr=none -mllvm -mcs51-wide-args`.
* Runtime: `rt/mk_rt.sh` variant `none-wide`.
* Link: fixed lld and `startup/cc2530_startup_fixed.o` via
  `sframe/sframe_link.py`.
* Audits: `banking/thunk_audit.py`, `banking/stackarg_entry_audit.py`,
  `stack/stack_census.py`.
* Toolchain: `study/fixes` at `709c6d3c0`, built with `ninja clang llc lld`.
* Simulator: patched ucsim (`ucsim-fix`), run from `sim/` as
  `llvm_join_replay.py --simulator S51 --elf ../link/Oz-v9wc.elf --case N`
  for N in 0–6 and 8–10. Reference vectors: `sim/ref/out/vN.json` from the
  host model (`sim/ref/cc.sh`).
* SDCC identity: `make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc
  prepare-join-smoke-image` gives `join_smoke_unverified.ihx` sha256
  `10155c95…8534`.
