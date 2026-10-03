# Compiler-owned MCS-51 XDATA representation

1. **Selected design:** B: unchanged ordinary areas plus the opt-in,
   compiler-owned `--xdata-ownership` JSON sidecar.
2. **ABI changed: NO.**
3. **CODE changed: NO.** Complete production IHX is byte-identical.
4. **cc2530-zigbee builds: YES**, unchanged production inputs from
   `6ba00392d1a5ebd297fd0de809f2ccaa92571682`, LG/default-TC.
5. **Existing verification passes: YES, scoped** to the linked image,
   DATA/static-stack/relocation proofs and the checks listed below.
   This is not full repository CI or full upstream SDCC regression acceptance.
6. **Function-scoped XDATA identified by compiler: 2849 bytes / 1316 objects**,
   owned by 407 functions.
7. **External inventory: 2849 bytes / 1316 objects.**
8. **Ownership mismatches: 0.** Fourteen one-byte inline-return homes receive
   a more precise class than the old compiler-temporary name heuristic.
9. **Patch size: 9 upstream files, +466/-2 lines** including tests/docs;
   the compiler representation itself changes five files, +137/-2.
10. **Recommended next step:** a generic, region-aware external prelink
    allocator consuming compiler ownership, with independent final-image
    verification. Do not add an allocator or physical overlay to this patch.

The original [lifetime study](XDATA_LIFETIME_STUDY.md) was read in full before
this work. Its production and v1 artifacts remain separate and unchanged.
The later v2 experiment is also separate: this compiler feature claims
**zero RAM saving**, not the physical-overlay study's delta.

## Evidence and scope

The application study is on `sdcc-function-xdata-study`, initially `af762a7`;
`Makefile`, `src`, `include`, `examples` and `boards` are identical to the
specified production commit. No production pin catalog was changed.
The compiler is SDCC **4.2.0 #13081**, built from the public
[Debian source archive](https://deb.debian.org/debian/pool/main/s/sdcc/sdcc_4.2.0+dfsg.orig.tar.xz),
SHA256 `ebe7bfb0894380cd92798b57fb9de96e6c0b913a02b6854d0a01cd70328c1578`.
The pristine source-import commit is
`2f950e49be05b53572be4c422491a267d964e0bb`.
No installed compiler was replaced, and no equipment, RF or flashing was used.

The exported series is in
[`experiments/sdcc-function/patches`](../experiments/sdcc-function/patches).
The complete source/build remains under ignored `build/sdcc-function`.
Machine-readable measurements and all ordinary-object bindings are in
[`experiments/sdcc-function/evidence`](../experiments/sdcc-function/evidence).
The compiler patch follows SDCC's GPL-2.0-or-later licensing, not the
firmware's BSD license; see [provenance](PROVENANCE.md).

## Current SDCC architecture

These locations are in the exact 4.2.0 source, not inferred from emitted
symbol spellings. Line numbers refer to the pristine source unless marked
as patched.

| IR fact / operation | Source and function | Existing output |
| --- | --- | --- |
| Memory-map selection | `src/SDCCmem.c`, `initMem`, `allocGlobal`, `allocLocal` | XSEG/XISEG/XABS or another selected map |
| Local function ownership | `src/SDCCmem.c:809`, `allocLocal`; `src/SDCCsymt.h:304-407`, `symbol.localof` | Used for generated local names and CDB keys |
| Caller-written parameters | `src/SDCCmem.c:617`, `allocParms`; `_isparm`, `ismyparm`, `localof` | `_function_PARM_N`; may be written before callee activation |
| Register arguments | `src/SDCCsymt.c`, `processFuncArgs`; `src/mcs51/main.c`, `_mcs51_regparm` | `SPEC_REGPARM`/`SPEC_ARGREG`; a retained home is allocated as a local |
| Static local lifetime | `allocLocal` retains lexical owner and delegates static allocation to `allocGlobal` | Static storage, not an activation frame |
| Reentrant parameters/locals | `allocParms`, `allocLocal` and stack-auto handling | Stack locations, not invented static XDATA objects |
| Intermediate temporaries | `src/SDCCicode.c:625`, `newiTemp` | `isitmp`; no assumption that every temporary has a home |
| Register spills | `src/mcs51/ralloc.c:473-550`, spill-location creation | Explicit DATA allocation in this backend, not automatically XDATA |
| AST temporaries and inline returns | `src/SDCCast.c`, `replaceAstWithTemporary`, `inlineTempVar`, `expandInlineFuncs` | Ordinary symbol objects; old flags lost this provenance |
| Actual declarations | `src/SDCCglue.c`, `emitRegularMap`, called by `emitMaps` | Filters unused/extern/function declarations; emits labels/equates and `.ds` |
| CDB identity/location | `src/SDCCglue.c:116`, `emitDebugSym`; `src/cdbFile.c:351-444` | Module/function/scope key plus assembler/linker location |
| Existing internal overlay | `src/SDCCmem.c:1255`, `canOverlayLocals`, and `:1303`, `doOverlays`; `emitOverlay` | Internal DATA/OSEG mechanism, not a whole-program XDATA allocator |

`canOverlayLocals` rejects stack-auto, reentrant and interrupt functions,
and ordinary call/indirect-call cases. Extending this local test alone
cannot establish linked activation interference or private-region safety.

The patch adds two **provenance-only** symbol bits, `astGenerated` and
`inlineReturn`, at the actual AST creation sites. It does not reuse `cdef`,
change an object's type, or feed these bits into allocation/code generation.
Copies retain provenance. `localof` identifies the containing function after
inlining; an inlined local does not acquire a fictitious separate activation.

The sidecar is emitted at the existing declaration-emission point after
allocation filters. It is not reconstructed from C, CDB or symbol names.
The consumer still binds its compiler-supplied facts to actual assembly,
CDB sizes/locations, linked symbols and relocation references.

## Representation design

The A/B/C decision was made after the standalone area/CRT experiment and
before modifying the compiler.

| Concern | A: one area per function | B: unchanged areas + metadata | C: areas only for eligible objects |
| --- | --- | --- | --- |
| Compiler change | Group/reorder declarations; retain semantic classes separately | Annotate actual emitted declarations only | Grouping plus a premature eligibility decision |
| Assembler | Custom XDATA areas work | No new directives or symbols | Same as A |
| Linker | Allocates contiguous distinct areas; must preserve private order | Identical object/link inputs | Same as A, with less complete representation |
| CDB/public symbols | Relocations work, but addresses/order can change | Identical CDB and symbols | Same risks as A |
| Startup | **Custom areas are not cleared by stock CRT** | Unchanged XSEG clearing | Same startup defect as A |
| Future overlay | Useful movable units, but ownership alone is insufficient | Explicit owner/class; future tool can choose physical representation | Conflates ownership and safety; misses link-time facts |

**B is the smallest compatible step.** A remains a possible later physical
representation, after startup/accounting/fence semantics exist. C is
rejected for this task: a translation-unit compiler cannot decide all
whole-image escape, indirect-call, ISR and private-range constraints.

Before and after, the emitted assembly and object remain identical:

```asm
        .area XSEG (XDATA)
_foo_PARM_2:
        .ds 2
_foo_first_65536_3:
        .ds 1
```

With the option, a separate `<output-basename>.xdata.json` additionally
identifies the first declaration as owned by `foo`, class
`PARAM_CALLER_WRITTEN`, and the second as `FIRST_ARGUMENT_HOME`.
Neither becomes overlayable merely by having that owner. The schema includes
the assembler symbol, compiler name, owner and entry symbol, exact CDB key,
area, size, absolute/address fields, ISR/reentrant flags and address-taken
IR fact. Exact keys/block numbers are compiler outputs, not a proposed ABI.

The option works without `--debug`; the logical debug key is still emitted
in metadata, but absent debug records are not fabricated in the assembly.
No new option record is put in `.rel`. No physical address is promised for
a relocatable object. Absolute declarations describe an object width, not
extra ordinary XSEG bytes.

| Storage | Compiler representation | Lifetime/ABI boundary |
| --- | --- | --- |
| Global / file-static | `GLOBAL` / `FILE_STATIC`, no function owner | Retained; public symbol rules unchanged |
| Static local | `STATIC_LOCAL`, lexical function owner | Persists between activations |
| Automatic home | `LOCAL`, containing function owner | Ownership is not a nonescape proof |
| Non-register formal | `PARAM_CALLER_WRITTEN` | Callee-owned symbol, caller-written lifetime |
| First / other register formal | `FIRST_ARGUMENT_HOME` / `REGISTER_ARGUMENT_HOME` | Includes real register-bank-1 homes; no home invented for register-only values |
| Compiler-generated home | `COMPILER_TEMP` | Derived from IR/AST provenance, not a name prefix |
| Inlined return join | `INLINE_RETURN_HOME` | Compiler-created local in the containing function, not a new return-value ABI |
| Ordinary returned value | No invented XDATA record | Existing register/stack return ABI unchanged |
| Reentrant / ISR | Explicit owner flags on actual retained XDATA | Stack storage remains stack storage; ISR concurrency remains a separate proof |
| Runtime / assembly library | Ordinary records if compiled with the option; otherwise missing | Prebuilt runtime cannot be assigned invented owners |
| Unknown | `UNKNOWN` or absent owner | Consumer must remain conservative |

There is deliberately no `overlay_allowed` bit and no compiler call graph.

## ABI proof

The standalone regression compares stock, patched-option-off and
patched-option-on `.asm`, `.rel`, `.adb`, `.lst` and `.sym` byte for byte,
then compares linked IHX, CDB and memory reports. It also exercises
`--parms-in-bank1`, `--stack-auto`, debug-off, external-only symbols,
initialized/absolute XDATA, and an unwritable sidecar destination.

The full firmware comparison covers **48** assembly/object/ADB/listing/symbol
sets, **96** relocated listings, and the full IHX/CDB/memory report.
All are byte-identical between source-built control and final patched-on.
Production IHX/CDB/memory also match the preserved packaged-SDCC baseline.
Thus there is no function instruction diff, entry-label diff, XDATA-address
diff, parameter-symbol diff or banked-call diff to explain.

The standalone pointer fixtures retain a two-byte XDATA pointer and a
three-byte generic pointer. `_foo_PARM_2` remains the same symbol and width.
The linked application retains its real banked ABI and static stack proof.
No return convention, pointer tag, memory ceiling or startup range changed.

Source-built SDCC locates runtime libraries with the shorter
`/usr/share/sdcc/...` path; packaged SDCC uses `/usr/bin/../share/sdcc/...`.
ASlink consequently prints the member on the same line instead of wrapping
it. Eight of the nine existing baseline identity fields match; only the
map suffix differs. The relocation reader now accepts both whitespace
layouts while retaining the exact six-member library sequence check.
Three unit tests cover both layouts and rejection of a foreign member.
**Production pins were not updated or bypassed.**

## ASxxxx/aslink behavior

`experiments/sdcc-function/test_areas.py` creates separate hand-assembled
areas and references them from ordinary SDCC C. Bases are not assigned to
the custom areas. Every byte interval is contiguous and distinct.

| Custom areas | Custom bytes | Assembler seconds | Link seconds |
| --- | ---: | ---: | ---: |
| 3 | 9 | 0.00174 | 0.01413 |
| 300 | 306 | 0.01212 | 0.03082 |
| 1000 | 1006 | 0.06984 | 0.23247 |
| 2000 | 2006 | 0.37732 | 1.40080 |

These are single observed timings, not claimed asymptotic bounds or maximum
area capacity. The growth is appreciably super-linear in this sample.
There is no thousand-area increase in the selected design.

For the three-area probe, ordinary XSEG occupies `0x0100`; `XF_foo`,
`XF_bar` and `XF_baz` begin at `0x0101`, `0x0104`, `0x0108`.
Actual linked `MOV DPTR,#address` operands are checked against IHX bytes,
relocated listings, NoICE definitions and CDB locations. The named areas
are present in `.rel`, `.rst` and `.map`. The 8051 memory report counts all
ten bytes. It does **not** omit the custom areas from total external RAM.

However, `l_XSEG` remains **1**. Starting the genuine CRT from reset with
XRAM filled with `0xA5`, ordinary XSEG becomes zero while the custom area
remains `0xA5`. This is expected negative evidence, not a startup success.
`device/lib/mcs51/crtxclear.asm:54-59` uses `s_XSEG/l_XSEG`.
`sdas/linksrc/lkarea.c` and `lkmem.c` recognize XDATA area flags for placement
and accounting, but that does not extend the CRT clearing contract.
Other object/debug formats, including OMF51 export, were not validated.

## cc2530-zigbee results

| Resource | Stock / production | Patched, metadata enabled |
| --- | ---: | ---: |
| Populated CODE | 253445 | 253445 |
| Common CODE | 32714 | 32714 |
| Ordinary XDATA / `l_XSEG` | 7676 | 7676 |
| Free ordinary XDATA | 4 | 4 |
| Physical DATA | 51 | 51 |
| OSEG | 10 | 10 |
| BSEG bits | 88 | 88 |
| Static stack bytes | 45 | 45 |
| Maximum bank depth | 8 | 8 |
| Nonempty XDATA areas | 1 | 1 |
| Function-owned physical areas | 0 | 0 |
| Explicit function-owned bytes | Not separately emitted | 2849 |
| All linked areas | 200 | 200 |

Empty PSEG/XABS/XISEG remain empty. Application objects contribute 7650
XSEG bytes and the independently checked unchanged runtime prefix contributes
26. The absolute status object is separate and is not counted as ordinary
XSEG. Physical alias/status/stack reservations remain unchanged.

### External inventory reconciliation

Every one of the **1437 ordinary application objects** is listed in
`evidence/reconciliation.tsv`: external key, compiler owner, class, area,
size, actual address and match result. There are no ownership, size,
address or coverage mismatches. The compiler additionally describes the
existing absolute status object. Eleven runtime homes / 26 bytes remain
independently inventoried from the unchanged libraries, not guessed from
missing sidecars.

| Compiler class | Objects | Bytes |
| --- | ---: | ---: |
| `GLOBAL` | 58 | 4053 |
| `FILE_STATIC` | 63 | 748 |
| `PARAM_CALLER_WRITTEN` | 609 | 1471 |
| `FIRST_ARGUMENT_HOME` | 390 | 918 |
| `LOCAL` | 303 | 446 |
| `INLINE_RETURN_HOME` | 14 | 14 |

The fourteen former `__...` temporary classifications are all
`security_keys` inlined-return joins. Source inspection of `inlineTempVar`
and the return-symbol creation site established their origin. New
provenance bits make this precise without names as a correctness input.
The first metadata prototype reported them as LOCAL because existing
`isitmp/cdef` did not survive this creation path as useful provenance;
that incomplete classification was fixed, not accepted as the result.

`tools/xdata_lifetime.py --compiler-metadata <build-root>` now obtains
owners/classes directly from the compiler, with exact per-module allocation
coverage and independently matched CDB/assembly identities. It never invokes
`parameter_names()` in that mode. The regression makes that function raise
if called. Nonescape, initialized-before-read, actual linked call graph,
unknown-edge, recursion, relocation and resource proofs remain external.
Unknown/static/caller-written/reentrant/ISR classes cannot gain eligibility.
The compiler's address-taken bit does not substitute for the escape proof.

The eligible set is **exactly the same 492 homes / 1000 bytes**. No formerly
unknown object was newly admitted. The original theoretical packing models
remain lifetime-only; this metadata option does not grant region safety.

The optional real integration uses compiler ownership to select and relink
the original 15-home, ten-owner `nwk_aps` pool, **45 bytes -> 6 bytes**:
`l_XSEG=7637`, saving 39 bytes, physical range `0x1403..0x1408`.
Its generator and verifier can both run with source parsing prohibited.
The independent verifier retains complete physical-byte coverage and
instruction/relocation checks. **All nine original immutable v1 artifact
identities match**, including its IHX/CDB, objects, relocated listings and
map parts; existing experimental admission passes without replacing its
catalog. This is reuse of the existing external
allocator, not overlay implemented inside SDCC.

Sidecars are trusted compiler outputs, not self-authenticating certificates.
The consumer rejects missing, duplicate, mismatched and unsupported records.
The overlay manifest additionally binds all sidecar hashes and the original
linked-image identities. Reproducible trusted builds and immutable admission
remain necessary; a JSON owner claim alone is not proof.

## Regression tests and reproducibility

From the application study root, define:

```sh
R=$PWD
S=$R/build/sdcc-function/sdcc-4.2.0+dfsg
C=$R/build/sdcc-function/stock/bin/sdcc
P=$S/bin/sdcc
E=build/sdcc-function/evidence
L=build/sdcc-function/final-image/join-smoke-layout
M=build/sdcc-function/final-image
O=build/sdcc-function/metadata-overlay/join-smoke-layout
```

The pristine compiler was configured with:

```sh
./configure --prefix=/usr \
 --disable-z80-port --disable-z180-port --disable-r2k-port \
 --disable-r2ka-port --disable-r3ka-port --disable-sm83-port \
 --disable-tlcs90-port --disable-ez80_z80-port --disable-z80n-port \
 --disable-ds390-port --disable-ds400-port --disable-pic14-port \
 --disable-pic16-port --disable-hc08-port --disable-s08-port \
 --disable-stm8-port --disable-pdk13-port --disable-pdk14-port \
 --disable-pdk15-port --disable-mos6502-port \
 --disable-ucsim --disable-device-lib --disable-packihx \
 --disable-sdcpp --disable-sdcdb --disable-sdbinutils --disable-non-free
make -s -j2 -o sdcc-sdbinutils sdcc-cc
```

Keep the control binary as `stock/bin/sdcc`, not `stock-sdcc`: the renamed
executable attempted to invoke nonexistent `stock-sdcpp`. `/usr` is only
the configured lookup prefix for installed tools/runtime; **no `make install`**.
The compiler-only target avoids a top-level make dependency that still
invokes disabled sdbinutils. Missing bison/flex and Boost headers were
installed only after the corresponding configure failures.

The exported commits apply in filename order to the pristine archive.
An isolated Git-index replay of all four patches reproduced the exact
committed final source tree. Rebuild the compiler with the same make command.

Exact application commands, all **PASS**:

```sh
make -s -j2 BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc \
 BUILD=build/sdcc-function/stock-image SDCC="$C" prepare-join-smoke-stack

make -s -j2 BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc \
 BUILD=build/sdcc-function/final-image SDCC="$P" \
 SDCC_FLAGS='-mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror -Iinclude -DCC2530_BOARD=1 --xdata-ownership' \
 prepare-join-smoke-stack

python3 -B "$S/support/regression/test-xdata-ownership.py" \
 --sdcc "$P" --control "$C"

PYTHONPATH=tools:tests python3 -B -m unittest \
 tools.test_xdata_relocations tools.test_join_smoke_analysis tools.test_join_smoke_image

PYTHONPATH=tools:tests python3 -B tests/test_xdata_metadata.py \
 --layout "$L" --metadata "$M" --output "$E/metadata-regressions.json"

PYTHONPATH=tools:tests python3 -B tools/xdata_overlay.py \
 --baseline "$L" --output "$O" --compiler-metadata "$M"

PYTHONPATH=tools:tests python3 -B tests/test_xdata_lifetime.py \
 --baseline "$L" --candidate "$O"
```

The standalone compiler cases cover a leaf, callee/caller, siblings,
parameters, first/register-bank arguments, retained/static/global objects,
reentrant and ISR functions, address-taken locals, pointers, arrays, structs
and a reproducible inline-return temporary. A read-modify-write AST
temporary is optimized out of static XDATA in its tiny fixture: the test
does not invent a home to claim coverage. Invalid reentrant-qualified
automatic-storage fixtures were replaced with valid stack-auto input,
not by changing compiler diagnostics or ABI expectations.

The targeted verifier suite has **26 passing tests**. The compiler-metadata
regression includes **15 rejection cases**: wrong module/schema/size/owner/
entry/symbol/key/class/area/flags/address, false global/parameter attribution,
missing object and duplicate object. Four further mutations demonstrate
that UNKNOWN, STATIC_LOCAL, ISR and reentrant facts remove eligibility.
The original lifetime suite also passes its real 39-byte reduction and
**12 existing negative cases** against the metadata-generated candidate.

The immutable-admission/no-source-parsing check was run with this Python
body under `PYTHONPATH=tools:tests python3 -B -`, and **passed**:

```python
from pathlib import Path
from unittest.mock import patch
import json
from join_smoke_image import identities
from xdata_overlay import verify, verify_study

root = Path("build/sdcc-function/metadata-overlay/join-smoke-layout")
with patch("xdata_lifetime.parameter_names",
           side_effect=AssertionError("Source parsing forbidden")):
    result = verify(root)
    artifacts, admission = verify_study(root, "lg_esl29_rev03", "default-tc")
expected = json.loads(Path("experiments/xdata/identities.json").read_bytes())["overlay"]
assert identities(root) == expected
```

Area/CRT test, **PASS** (including the expected failure to clear custom areas):

```sh
python3 -B experiments/sdcc-function/test_areas.py \
 --output build/sdcc-function/areas-verified \
 --simulator "$S51"
```

`S51` is an explicit path to the separately prepared alias-aware simulator;
see `tools/prepare_join_simulator.py`. No machine-local path is required.

Full artifact/resource/timing comparison, **PASS**:

```sh
python3 -B experiments/sdcc-function/measure.py \
 --stock "$C" --patched "$P" \
 --stock-image build/sdcc-function/stock-image --image "$M" \
 --production ../xdata-lifetime-study/build/xdata-study/baseline/join-smoke-layout \
 --output "$E"
```

Fresh-output scripts intentionally reject an existing candidate directory.
Preserve earlier measured artifacts and choose another output for a rerun.
The original production and v1 artifacts/pin catalogs are not overwritten.

**Not run:** full SDCC regression matrix, non-MCS51 ports, all SDCC memory
models, full 118-worker application CI, other board/key profiles, or a new
full MCU replay for this metadata-only firmware. The firmware is byte-identical
to production, but equality is not relabelled as a new execution observation.
The earlier v2 complete replay is an independent outstanding run.
Native join/ZCL suites were not repeated for this compiler-only stage.

## Performance impact

| Artifact | Stock | Patched-on |
| --- | ---: | ---: |
| Application assembly bytes | 4236066 | 4236066 |
| Relocatable object bytes | 2738764 | 2738764 |
| Linked CDB bytes | 1658418 | 1658418 |
| Sidecar files / total bytes | 0 / 0 | 48 / 444912 |
| Additional linker areas | 0 | 0 |

The recorded `measurements.json` contains six timed samples after one
warmup for stock/off/on compilation and linking of the same standalone
fixture. Compiler timing uses `-S`; assembler time is excluded from it.
These are short microbenchmarks on a shared machine with a concurrent
replay, not proof of a universal overhead percentage.

| Median seconds | Stock | Patched-off | Patched-on |
| --- | ---: | ---: | ---: |
| Fixture compilation | 0.017683 | 0.020181 | 0.020653 |
| Fixture linking | 0.013032 | 0.011923 | 0.016023 |

Complete target build plus DATA/stack analysis took 401.03 s real /
610.17 s user for source-built stock and 369.53 s real / 556.01 s user for
the final patched build. Different concurrent load makes the apparent
speedup non-causal. An earlier metadata prototype took 405.28 s.
All raw timings are retained; no claim is made that metadata accelerates
compilation. It adds about 445 KB of text for this image and no linker work.

## Future static overlay architecture

The next pipeline should be:

```text
compiler-owned home/class/provenance + unchanged relocatable code
  -> closed object set and preliminary link
  -> bound owners/references + actual calls/tail calls/banked transitions
  -> initialization/escape/concurrency and region/fence constraints
  -> bounded external placement
  -> relink
  -> independently verify instructions, addresses, complete byte ledger,
     CRT ranges, private fences, DATA and stack
```

The compiler can emit owners, semantic storage classes, AST provenance,
ISR/reentrant flags, types and eventually defined access/call-site facts.
This patch emits the first five plus actual size/area/key facts. It does
not emit reliable escape, callgraph or final address assertions.

Only the linked composition establishes selected library members, actual
function entries, banked/helper/assembly edges, relocation targets and
physical private fences. Hardware aliases, status reservations and
pointer-admission regions also need explicit platform/linker contracts;
ordinary function ownership cannot infer them.

A separate prelink tool is the lowest-risk next allocator location.
`doOverlays()` remains useful for its existing local internal-RAM task,
but it is not the right place for cross-module linked XDATA interference.
A future linker implementation could consume the same facts after startup,
fragment/fence ordering and byte-accounting semantics are specified.

ISR/reentrant/escaped homes, caller-written arguments and owners affected by
unknown indirect calls require conservative conflict treatment or exclusion
until their distinct lifetimes/concurrency are proved. They are not
interchangeable categories; blanket callee-activation ownership is insufficient.
Static locals remain retained unless a separate proof permits otherwise.

**LTO is not required for this bounded static reuse architecture.**
Compiler-owned storage facts plus relocations, a closed linked call graph
and conservative local initialization/escape proofs already reproduce the
real v1 pool. This is not a claim that arbitrary indirect-call or
pointer-heavy programs can always be allocated optimally without more facts.

### Public IAR architectural comparison

The public [IAR 8051 Assembler Guide](https://updates.iar.com/FileStore/STANDARD/001/000/590/ew/doc/EW8051_AssemblerGuide.pdf),
printed pp.113-114, documents FUNCTION, FUNCALL, ARGFRAME and LOCFRAME.
The reviewed PDF SHA256 is
`accc81d065b4f3998d7d86425bec10f5ec0dc098e347909a277cde060671c815`.
Only public documented architectural facts were used; no proprietary
implementation, binary format or allocator was copied.

| Publicly documented IAR concept | This SDCC step |
| --- | --- |
| Function identity metadata | `owner` and `owner_symbol` on actual homes |
| Caller/callee relation, including unknown indirect relation | Not emitted; independently reconstructed downstream |
| Argument-frame memory/size/static-or-stack distinction | Explicit caller-written versus register-home class and actual area/size |
| Local-frame use at a call site | Not emitted; whole-activation conservative proof remains external |
| Compiler-to-linker static-overlay facts | Compiler-owned sidecar; no new object format or linker allocation |

The lesson is separation of ownership, caller argument preparation and
local frame use, not adoption of IAR's directives or object format.

## Upstreamability

The compiler logic contains no application, board or module-name whitelist.
It adds one MCS51 option, two read-only provenance bits and emission at
the existing allocation boundary. It changes no linker, CRT, SDCC allocator,
application C or memory limit. The exported commits are:

| Commit | Purpose |
| --- | --- |
| `7bdae98` | Initial standalone tests/infrastructure |
| `4282c3d` | Complete regression coverage and corrected fixture assumptions |
| `e6f1148` | Generic representation-only compiler change |
| `02bf3da` | Schema, invariance and lifetime documentation |

Before an upstream submission: agree the option/schema naming and stability
policy, integrate the standalone tests into the upstream harness, decide
sidecar installation/archive handling, review cross-port symbol-layout
effects and broaden target-model tests. A compiler-side content binding
for sidecars would improve stale-artifact detection; current consumers
must bind trusted build outputs and cannot treat arbitrary JSON as authority.

Function-scoped physical sections are **not** the first patch recommended by
this evidence: they would change CRT and placement behavior immediately.
Explicit compiler ownership is already enough to remove source-based owner
discovery and retain the existing independently verified physical prototype.
