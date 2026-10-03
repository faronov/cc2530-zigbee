# SDCC/mcs51 study of the complete join image

## Summary

1. **Bottleneck:** XDATA has only 4 bytes free; all 88 BSEG bits, the
   45-byte stack bound and bank-call nesting limit of 8 are occupied.
   CODE is also tight: common has 54 bytes free and bank 2 has 13.
2. **Immediate configuration win:** a restricted `--peep-file`, retaining
   `--debug` and `--opt-code-size`, saves **1774 populated CODE bytes**,
   including **543 common bytes**. It does not free RAM.
   `--fomit-frame-pointer` saves nothing in this firmware.
   Unrestricted `--peep-return` saves 2339 bytes but produces tail transfers
   rejected by the existing proof. It is not an accepted configuration.
3. **Small improvement:** independently enable jump-to-local-RET rules without
   enabling CALL/RET-to-tail-JMP rules. The working prototype is
   `experiments/sdcc/local-returns.peep`.
4. **Medium improvement:** function/data ownership sections and relocation
   reachability, including unused parameter/local homes. Symbolic analysis
   finds 29 apparently unreachable functions, 6194 CODE bytes and 169 bytes
   of their function-scoped XDATA homes. These are opportunities, **not
   proven removable storage or an implemented firmware GC saving**.
5. **Full LTO: no, not first.** A narrow return optimization already helps
   CODE. XDATA ownership/lifetime and section retention are more direct next
   targets than a new whole-program optimizer.

## Scope and evidence

Firmware source baseline:
`6ba00392d1a5ebd297fd0de809f2ccaa92571682`.
Board: `lg_esl29_rev03`; join profile: `default-tc`.
The experiment started in a separate detached worktree and fresh, disjoint
`build/sdcc-study/<variant>` directories. No production C/header, banked ABI,
memory limit, stack limit, resource-proof logic or radio setting was changed.
No equipment was accessed.
The resulting report/prototype is retained on branch `sdcc-toolchain-study`;
the active firmware branch and its pending work were not modified.

Toolchain: packaged SDCC 4.2.0 #13081, mcs51 large model, C99, debug,
size optimization. A separately source-built 4.2.0 control produces identical
code/resources. All 46 compiler objects match after removing only the
`;!FILE <build-directory>/...` provenance line. This normalization is an
explanatory comparison, **not** a change to immutable image admission.

The exact baseline commit contains stale join artifact pins. A clean baseline
initially fails `Complete immutable join artifacts differ`, before simulation.
Its fresh IHX SHA256 is
`10155c95fceb837c68ac3ed5d6177a87d1d5601afb9f84acb775a6f191ab8534`;
the committed pin is
`83a109978c2b1cb30f606c78020a09233b368ba154b5f5d37082dc996602d860`.
Independent unchanged XDATA ownership, linked call graph, DATA liveness and
stack checks pass. Reviewed exact identities were temporarily substituted in
the isolated worktree for admission/replay; this is distinct from passing
the original committed pin catalog.

There is also a pre-existing host trace build failure: with the trace's short
sampling interval, two lower-bound constants become zero and unsigned
`>= 0` comparisons trigger `-Werror=type-limits`. The accompanying two-line
test-only fix widens the left sides to `int64_t`, preserving both bounds
and all production code.

After those prerequisites, the old replay fails its reset signature because
it expects status version 1 while `src/join_smoke.c:140` at the requested
commit explicitly writes version 2. The accompanying replay correction checks
the exact same eight-byte DISARMED signature and zero mailbox, with version
**2**. No condition was removed, no deadline was extended and no mismatch
was ignored. These test repairs are separate from compiler optimization.

Full CI, all simulator scenarios and hardware acceptance are not claimed.
See the final validation results below.

## Measurements

All sizes are bytes unless marked as bits. `DATA/OSEG` means physically
backed allocations, not ASlink's DSEG address envelope.

| Variant | CODE | XDATA | DATA/OSEG | BSEG bits | Stack | Biggest free bank | Tests |
|---|---:|---:|---:|---:|---:|---:|---|
| Baseline | 253445 | 7676 | 51 / 10 | 88 | 45 | 1418 | H, P, S2, prefix |
| Source-built 4.2 control | 253445 | 7676 | 51 / 10 | 88 | 45 | 1418 | P |
| `--fomit-frame-pointer` | 253445 | 7676 | 51 / 10 | 88 | 45 | 1418 | H, P, identical baseline artifacts |
| `--peep-return` | 251106 | 7676 | 51 / 10 | 88 | unproved | 1702 | H; tailcall proof rejects |
| Both flags | 251106 | 7676 | 51 / 10 | 88 | unproved | 1702 | H; tailcall proof rejects |
| Local-return prototype | 251671 | 7676 | 51 / 10 | 88 | 45 | 1641 | H, P, S2, prefix |
| SDCC patch 466 backport experiment | no linked image | - | - | - | - | - | Compile/assembly failure |

H: native/sanitized join and ZCL tests. P: resource/DATA/static-stack proof.
S2: complete MCU command-rejection case 2, not a successful join.
Prefix: first three steps of case 0, explicitly a partial diagnostic.
Baseline and prototype also pass complete structural image admission after
their isolated exact-identity refreshes. Omit-frame matches **all nine**
baseline artifact identities, not only the footprint.

| Resource | Baseline | Local-return prototype | Delta |
|---|---:|---:|---:|
| Populated CODE | 253445 | 251671 | -1774 |
| Common CODE | 32714 | 32171 | -543 |
| XDATA | 7676 | 7676 | 0 |
| Backed DATA | 51 | 51 | 0 |
| OSEG | 10 | 10 | 0 |
| BSEG backing | 11 | 11 | 0 |
| Static stack | 45 | 45 | 0 |
| Maximum bank depth | 8 | 8 | 0 |
| JF frames | 106 | 106 | 0 |
| Summed JF frame bytes | 589 | 589 | 0 |
| Largest JF frame | 18 | 18 | 0 |

The 51 backed DATA bytes are 22 low reservation bytes, 27 high reservation
bytes, and 2 banker-state bytes. The spill reservations total **49**, not
589: inter-function reuse is already implemented. ASlink's `l_DSEG=125`
is an address-space extent, not 125 independently allocated DATA bytes.
Register bank 0 occupies another 8 bytes. OSEG, BSEG backing and stack must
not be counted as free RAM or added to XDATA through the IRAM alias.

| Bank | Capacity | Baseline used / free | Local-return used / free | Extra free |
|---|---:|---:|---:|---:|
| Common / 0 | 32768 | 32714 / 54 | 32171 / 597 | 543 |
| 1 | 32768 | 32083 / 685 | 31895 / 873 | 188 |
| 2 | 32768 | 32755 / 13 | 32626 / 142 | 129 |
| 3 | 32768 | 32734 / 34 | 32411 / 357 | 323 |
| 4 | 32768 | 31350 / 1418 | 31127 / 1641 | 223 |
| 5 | 32768 | 32685 / 83 | 32485 / 283 | 200 |
| 6 | 32768 | 32741 / 27 | 32679 / 89 | 62 |
| 7 | 26624 | 26383 / 241 | 26277 / 347 | 106 |

Bank 7 is deliberately smaller because NV/configuration space is excluded.
Total available image capacity is 256000, not the full physical flash size.
Existing placement is retained; no claimed saving comes from repacking or
changing banking groups.

## Findings in generated mcs51 code

`tools/sdcc_study.py` inspects linked `.rst`, instructions, CDB identities and
the actual call graph. Counts below are from the baseline, not C-source guesses.

| Pattern / cost | Occurrences | Local byte opportunity | Interpretation |
|---|---:|---:|---|
| Jump to plain RET in same function | 939 | 1588 | Highest measured low-cost opportunity |
| Repeated immediate DPTR in tracked basic block | 76 | 228 | Candidate only; no MMIO/liveness proof |
| Adjacent near CALL/RET | 85 | 85 | Tailcall proof/ABI work required |
| Spill access instructions | 3494 | not inferred | Cost, not automatically redundant |
| Generic-pointer helper calls, including comparison | 1916 | not inferred | Pointer semantics must be preserved |
| Bank call sites | 200 | not inferred | 9-byte setup/call cost is not removable by fiat |

The local return count is a pattern estimate, not a guarantee that every site
meets SDCC's predicate. The measured final saving is larger than its direct
1588-byte substitution estimate because earlier substitutions enable subsequent
branch/peephole simplification. Do not sum overlapping pattern estimates.

Concrete generated example: `mac_link_workspace:held`, source line 75.

```asm
; baseline
    mov  a,r6
    cjne a,ar7,00102$
    mov  dpl,#0x01
    sjmp 00106$
    ; ... other live path ...
00106$:
    ret

; prototype
    mov  a,r6
    cjne a,ar7,00102$
    mov  dpl,#0x01
    ret
    ; ... same other live path ...
00106$:
    ret
```

No memory/register effect changes; the same return address is consumed once.
The skipped jump's execution time changes, as does source-level stepping.
This is not evidence for unchanged RF timing under real interrupts/equipment.
High-count modules include `security_keys` (94 sites), `radio_autoack` (83),
`mac_radio` (64), `mac_frame` (53), `security_counter` (45), `join_smoke` (44),
and `zcl_sensor` (36).

The assembly also shows why plain `volatile` removal is not a measured answer:
many homes and DPTR accesses belong to deliberate ownership/state boundaries.
The study does not remove qualifiers or rewrite application functions.

Another real example is `ccm_star:encrypt`, linked addresses `0x505c..0x5066`:

```asm
    mov  dptr,#(_child_work_arena + 0x021c)
    movx a,@dptr
    mov  r7,a
    inc  r7
    mov  dptr,#(_child_work_arena + 0x021c) ; counted repeat
    mov  a,r7
    movx @dptr,a
```

This motivates the secondary DPTR candidate count, not a blanket rule across
calls, control-flow merges, SFR writes or indirect aliasing.

## Flags and their actual scope

SDCC 4.2 `src/SDCCmain.c` explicitly disables return peepholes when `--debug`
is selected unless `--peep-return` has been explicitly requested.
`--opt-code-size` does not undo that decision.
An explicit `--peep-return` works before or after `--debug`.
The mcs51 `--fomit-frame-pointer` implementation affects reentrant frame
handling (`src/mcs51/main.c`, `src/mcs51/gen.c`), but not this firmware's
measured footprint. It is not a fictitious unsupported mcs51 flag.

The standalone reentrant testcase proves it can matter in another composition:

| Fixture variant | CODE | Oracle |
|---|---:|---|
| Debug baseline | 476 | all 1024 output bytes match |
| Restricted returns | 472 | match |
| Peep return | 468 | match |
| Omit frame | 466 | match |
| Both | 458 | match |
| Peep return, debug last | 468 | match |
| No debug | 468 | match |

Removing debug from the production benchmark is not an acceptable workaround:
the current proof intentionally consumes full CDB metadata and linked listings.

## Recommended first patch

The prototype copies only the behavior of mcs51 rules 251.a and 251.b:
`LJMP/SJMP label -> RET` when `labelIsReturnOnly(label)` succeeds.
It updates label reference counts and deliberately omits tailcall rules 400.*.
The original rule predicate rejects jump-table iCode, stops across non-local
function labels, and requires the target to be exactly a plain RET:
not an epilogue with POPs, not RETI, not a banker return jump.

This leaves all existing function/call-boundary proof assumptions intact.
`--peep-return` does not: its inter-function jumps are rejected with
`Unreviewed inter-function tail transfer`. That failure was recorded, not
relaxed or reclassified as success.

Expected direct opportunity: 1588 bytes at the observed baseline sites.
Measured linked saving: **1774 CODE / 543 common / 0 RAM**.
Regression risks: debug stepping/unwind assumptions, instruction timing,
label reference maintenance and the compiler's jump-table classification.
The fixture exhausts 256 inputs through ordinary branches, nested reentrant
frames, calls and loops against an independent Python oracle under s51 with
the CC2530 IRAM alias. Full-image bank/stack/XDATA checks are separate evidence.

Upstream placement would be `src/mcs51/peeph.def` plus narrowly scoped option
handling in `src/SDCCmain.c` / `src/SDCCpeeph.c`, if a separate local-return
policy is accepted. Do not globally remove `optimizeReturn` from every rule
or enable tailcalls under the name of this smaller change.
There is no need to fork SDCC for the current proof of concept.

## Existing spill allocation versus compiler work

SDCC already reuses some temporary spill locations **within a function**.
In 4.2 `src/mcs51/ralloc.c`, `isFree`, `noOverLap` and `createStackSpil`
check the clash bitvectors, required size and bit/non-bit class. Proposing
that exact mechanism as a new optimization would be incorrect.

The repository goes further across function boundaries: it splits compiler
DATA into JF areas, reconstructs the actual linked/banked graph, proves byte
liveness and assigns physical DATA addresses inside real reservations.
Moving those area declarations into compiler output could simplify tooling,
but does not itself turn 589 into fewer than the existing 49 reservation bytes.
No measured frame/slot saving came from the tested flags or local-return rule.

CDB analysis finds **2849 bytes of function-scoped XDATA homes** in this
composition. This includes parameter/local homes, not just spills; it is not
2849 reclaimable bytes. A medium-sized compiler/link allocation project should
first characterize escape, volatile, persistent/static and interrupt lifetimes,
then emit owner/clash metadata. The existing post-link verifier remains an
independent checker. Ordinary local allocation improvements are tracked by
upstream request 958; they are not a finished option to switch on.

## Upstream investigation

Primary sources checked:

- [NGI0 SDCC work plan](https://sourceforge.net/p/sdcc/wiki/NGI0-Commons-SDCC/):
  A1 non-stack local allocation and A2 unused-function/object elimination.
- [Feature request 958](https://sourceforge.net/p/sdcc/feature-requests/958/):
  non-stack-local allocation work, open at inspection.
- [Patch 466](https://sourceforge.net/p/sdcc/patches/466/) and its
  [discussion](https://sourceforge.net/p/sdcc/patches/466/#f59a):
  mcs51 operand/register liveness, `notUsed` predicates and peepholes;
  partially merged work, not one ready-to-apply current-trunk patch.
- [Current SVN ChangeLog](https://svn.code.sf.net/p/sdcc/code/trunk/sdcc/ChangeLog)
  and `src/mcs51/{peeph.def,peep.c,ralloc.c}`. Inspection snapshot:
  2026-10-01. Github mirror repository update dates were not treated as
  proof of fresh compiler sources.

A source-built unmodified 4.2 control was measured on the complete firmware.
An isolated experimental backport used patches 1-15, the author's corrected
patch 1 and June-2025 complete peephole file. The April-2026 patch-7 update
was inspected: its additional alternative-register-name table entry concerns
a newer table not present in the backported register lookup.
Patch 17 and four rules referencing bank trampolines were deliberately
excluded: they introduce `__sdcc_banked_jmp`, changing the relevant ABI.

This was a **backport experiment**, not a build of current trunk. Two minor
source-context adaptations were required for the older iCode layout. The
compiler built, but firmware compilation first hit `scan4op -- got invalid
register name: 'c'`. A generic lowercase-carry alias allowed the next clean
trial to proceed to assembly, where generated `mac_radio.asm` and
`mac_attempt.asm` contain undefined local jump targets.
Neither attempt produced an accepted complete image; no author-reported
savings are substituted for absent measurements. This larger series is not
the recommended first patch.

Exact experimental source diff, compiler build logs and downloaded inputs are
retained under `build/sdcc-study/upstream/`; no system compiler was replaced.
The measured backport diff SHA256 is
`9051420e9d73e4327cbe6d428dc78aaaf878924339ef855316fcbb468c477643`.
The original source archive SHA256 is
`ebe7bfb0894380cd92798b57fb9de96e6c0b913a02b6854d0a01cd70328c1578`.

## Linker GC feasibility and bounded prototype

Current stock extraction granularity is an **archive object member**, not an
arbitrary function or relocatable area inside an explicitly linked object.
`experiments/sdcc/test_sections.py` measures that distinction:

| Same root and functions | Linked CODE |
|---|---:|
| Live/dead functions in distinct CODE areas of one explicit object | 13 |
| Live/dead functions in separate archive members | 9 |

The stock linker retains the unused 4-byte function in the first case and
does not extract its member in the second. Function areas alone are not GC.
This is a linker-semantics prototype, **not a 4-byte firmware improvement**.

A practical area-GC implementation would require:

1. Compiler emission of one owner-labelled CODE area per function, separate
   constant/data homes and associations for parameters, locals, initializers,
   switch tables and debug records. Static names need module-qualified
   identities. A non-reentrant function must retain its ABI parameter homes.
2. Assembler preservation of area/symbol ownership and local relocations.
   ASxxxx already supports multiple relocatable areas. Splitting functions
   requires care for short relative branches and module-local labels, not a
   new CPU assembler or an LLVM object format.
3. Linker reachability before final area sizing/placement. In `sdas/linksrc`,
   `lklib.c`/`lkrel.c` load members; `lkrloc3.c:relr3` decodes each R record's
   source area index and target area/symbol index. Those are graph edges.
   `lkarea.c` placement and `lklist.c`/debug output must omit discarded
   contributions consistently; filtering final IHX bytes is not GC.
4. Explicit roots for reset/startup/vector entries, interrupts, exported
   callbacks, flash-RAM execution entry and custom bank trampolines.
   Materialized function addresses in code **and data** retain their targets.
   Unknown indirect targets require conservative retention. Low/high/bank
   relocation pieces all represent references, not unrelated integers.
5. Existing bank groups, near calls, JF/DATA liveness, BSEG, private XDATA
   fences, aliases and stack proofs must be regenerated and rechecked.
   Interrupt concurrency cannot be inferred solely from the ordinary call
   graph. Keep special `s_*`/`l_*` symbols and startup initialization semantics.

The firmware's current **symbolic**, not relocation-complete, upper estimate is
6194 CODE bytes in 29 functions. Largest candidates include `security_keys:seal`
(918), `zigbee_mmo:install_code_derive` (734), `mac_frame:mac_command_encode`
(711), `mac_time:mac_time_attempt_read` (683), `zdo_node:zdo_node_req_encode`
(369), and `mac_poll:mac_poll_step` (337). Their counted function-scoped XDATA
homes occupy 169 bytes; other parameter/global homes are not fully classified.
No function was removed, and no production GC saving is claimed.

## Reproduction and validation

Use a separate clean directory per variant. The base command is:

```sh
make -s -j4 BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc \
  BUILD=build/sdcc-study/baseline prepare-join-smoke-stack
```

The flag trials append one of `--fomit-frame-pointer`, `--peep-return`, both,
or `--peep-file experiments/sdcc/local-returns.peep` through:

```sh
'JOIN_SMOKE_FLAGS=$(BANKED_JOIN_FLAGS) $(JOIN_SMOKE_DEFINES) -DCC2530_BANKED_LINK <flags>'
```

The source-built control uses the same command with `SDCC=<isolated>/bin/sdcc`
and `SDCC_HOME=/usr`; it does not substitute a newer ABI via version macros.

```sh
python3 -B tools/sdcc_study.py \
  build/sdcc-study/baseline/join-smoke-layout \
  --output build/sdcc-study/baseline.json --analyze
python3 -B tools/sdcc_study.py \
  build/sdcc-study/local-returns/join-smoke-layout \
  --output build/sdcc-study/local-returns.json \
  --baseline build/sdcc-study/baseline.json
python3 -B experiments/sdcc/test_returns.py
python3 -B experiments/sdcc/test_sections.py
PYTHONPATH=tools:tests python3 -B -m unittest tools.test_split_link_spills -q
```

Those regression commands pass: 7 exhaustive simulator fixture configurations,
the 13-versus-9-byte linker test, and 5 existing spill-splitter unit tests.
Resource/DATA/stack checks pass for baseline, source control, omit-frame and
restricted returns. Unrestricted return variants fail the unchanged tailcall
proof; their stack numbers are deliberately unreported.

`make -s -j4 BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc
BUILD=build/sdcc-study/<variant> test-join-smoke-host` passes for baseline,
omit-frame, peep-return, both and local-returns, with native/sanitized
shallow/deep transcripts and ZCL checks. Native host success does not prove
SDCC-generated machine code.

For each of `baseline` and `local-returns`, after the explicit temporary
exact-identity refresh and test-only prerequisites:

```sh
python3 -B tests/boot_join_smoke.py \
  --output build/sdcc-study/<variant> --board lg_esl29_rev03 \
  --key-mode default-tc --simulator <reviewed-s51> --case 2
python3 -B tests/boot_join_smoke.py \
  --output build/sdcc-study/<variant> --board lg_esl29_rev03 \
  --key-mode default-tc --simulator <reviewed-s51> --case 0 --limit 3
```

Both case-2 commands pass: complete terminal-retention checks, one caller
step, zero peripheral stops, peak SP `0x53`. Both prefix commands pass their
bounded comparisons: 3 steps, **1974 peripheral stops**, peak SP `0x71`
below the unchanged `0x7c` limit. Prefix checking includes actual instructions,
MMIO transcripts, complete ownership observations, NV and the IRAM alias.
The runner labels it **Partial diagnostic**, and does not write a complete
successful-join report.

Full successful-join case 0 was started, then stopped after measuring its
scope: **1111 steps / 2291167 peripheral events**. Its complete MCU replay
and the full CI matrix were not completed. No complete successful-join or
hardware validation is claimed for the prototype.

`experiments/sdcc/baseline.json` is the frozen baseline accepted by the
measurement tool's `--baseline` argument; `measurements.json` contains all
variant metrics, exact identities and focused validation evidence. The
production pin catalog was restored unchanged after these experiments.
The prototype is deliberately not enabled in the production Makefile.

## Next steps by expected return on effort

1. Review the local-return prototype, exact candidate identities and remaining
   replay evidence; run the unchanged CI matrix before enabling it by default.
2. Prototype owner/escape metadata for the 169-byte dead-home opportunity,
   using relocation-complete retention roots and the existing post-link
   checker. Measure CODE and XDATA together, not just discarded functions.
3. Evaluate allocation of ordinary function-scoped XDATA against actual
   lifetimes; retain the JF proof until a compiler-side alternative is independently
   proven equivalent. Do not promise physical DATA or BSEG gains from frame sums.
4. Isolate the undefined-target regression in the larger upstream peephole
   backport before considering its other rules. Pursue the 228-byte DPTR
   estimate only with register/MMIO/branch-aware proof.
5. Reconsider broader IPA/LTO only after these measured opportunities and the
   XDATA bottleneck have been addressed.
