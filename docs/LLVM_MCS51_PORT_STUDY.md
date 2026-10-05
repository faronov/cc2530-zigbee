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
11. Clang changes alter SDCC: yes (+1578 CODE, +19 XSEG, −1 BSEG).
12. LLVM populated CODE: 222090.
13. Common CODE: 12389.
14. Raw XDATA: 4842 + absolute status.
15. Internal DATA/IDATA: 8 register + 24 pseudo-register bytes; 0 globals; 128 B stack.
16. Stack demand/capacity: 529 / 128 (fail).
17. Minimum bank headroom: 0 B.
18. Full simulator join: no.
19. `--gc-sections` saving: 0 B.
20. Overlay saving: 0 B.
21. Full LTO usable: no.
22. UNSUITABLE (not production-ready).
