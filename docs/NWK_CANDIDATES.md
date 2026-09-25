# Offline ED Beacon candidate collector

This is one bounded preparation step for #14, **not a scan or association
implementation**. Original BSD-3-Clause protocol C composes the existing
`mac_frame_decode` -> `mac_beacon_decode` -> `nwk_beacon_decode` path and retains
at most four copied, preliminary candidates. There is no radio model or
success-returning radio stub.

## Primary sources and selected policy

The actual primary documents were read, not generated extracts or vendor
implementations:

* **IEEE Std 802.15.4-2006**, public
  [UBC mirror](https://people.ece.ubc.ca/~edc/7860/data/802.15.4-2006.pdf).
  SHA256:
  `d245c8bb208f6cdb585fb753cefa67e367d30eebd55b9ed9957c8fd152d6a055`.
* **Zigbee Specification, Revision 22, document 05-3474-22,
  April 19, 2017**, at the
  [pinned primary-document mirror](https://github.com/pvginkel/ZigBeeHomeAutomation/blob/fc30145012eacd3a5af170b8ae8e0d4c848c2525/Documents/docs-05-3474-22-0csg-zigbee-specification.pdf).
  SHA256:
  `991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`;
  Git blob `c8123d63e30995e4a66941cbdf2529332480a0ff`, matching
  [provenance](PROVENANCE.md).

PDFs/research tools are temporary ignored build material, not vendored files
or CI artifacts. No third-party implementation, SDK, capture, private identity
or key was imported. The secondary specialist catalog was not needed for this
lookup. R23 and conflicting BDB labels were not substituted.

| Decision | Primary reference and exact boundary |
| --- | --- |
| Explicit page-0 channel subset 11..26 | IEEE section 6.1.2.1, printed p.29: legacy 2450-MHz channels and channel-number bitmap. The API accepts only a nonempty subset of `0x07fff800`, not page bits or an all-channel default. |
| Source identity and pending-list offsets | IEEE sections 7.2, 7.2.2.1.1-8, pp.137-138,143-146, Figures 44-51: little-endian multibyte fields, source-only Beacon addressing, short-before-extended pending lists, at most seven combined addresses. Reused [MAC codec](MAC.md) validates the selected no-GTS layout. |
| MAC Association Permit | IEEE section 7.2.2.1.2, Figure 47, pp.143-144: bit15 of the little-endian two-byte Superframe Specification says association requests are being accepted. This is an advertisement, not admission or authorization. |
| Beaconless filter | IEEE section 7.5.1.1, pp.167-168: BO15 means requested Beacons rather than a periodic superframe. **SO is ignored when BO15.** Transmitters in a nonbeacon-enabled PAN set both BO/SO15; that TX rule is not incorrectly imposed as an RX filter. |
| Selected PRO profile 2 | R22 Annex D, Figure D-3 and following required Enhanced Beacon layout, pp.520-521, explicitly identifies the embedded **standard ZigBee PRO Beacon Payload**, with stack-profile nibble `2`. This supplies the identifier only; Enhanced Beacons/IEs are not implemented. R22 Annex D, "Stack Size Issues," p.512, describes PRO beaconless operation. |
| ED capacity | R22 section 3.6.7, Table 3-71, pp.389-390, Figure 3-54 p.391: bit7 of NWK payload byte2 advertises capacity for joining end devices. Router Capacity is not the ED filter. |
| Preliminary, not complete parent selection | R22 sections 3.6.1.3, 3.6.1.4.1, pp.335-336. Discovery uses MAC scans and their confirms. Suitable-parent selection additionally needs the selected Extended PAN, link cost at most3, potential-parent state where present and wrap-aware most-recent Update ID. None is supplied by this collector. |

The **project's preliminary conjunction** is:

```
stack_profile == 2
AND Beacon Order == 15
AND MAC Association Permit == 1
AND End Device Capacity == 1
```

This deliberately does not implement a complete normative parent-admission
algorithm. There is no target-network selection, link-quality calibration,
link-cost calculation, ranking, freshness, sequence ordering, update-ID wrap
comparison or potential-parent/neighbor table. R22 section 3.6.1.4.1 excludes depth
selection for profiles other than1; its later depth-selection sentence is
not used to invent a profile2 ranking rule.

The separate [parent-choice function](NWK_PARENT.md) now consumes a stable
snapshot of this table. It adds explicit target/link-cost/eligibility inputs
and conservative Update ID ordering without changing collection behavior.
It does not derive link quality or turn this table into authenticated state.

### Byte layouts and inherited exclusions

An input is one complete **FCS-free legacy MAC body**, without PHR, RSSI/LQI,
radio status, timestamps or other trailers. The existing MAC subset admits
at most125 body bytes. With source mode short/extended, the MHR is respectively
7/13 bytes: two FCF bytes, sequence, two source-PAN bytes, then 2/8 source
address octets. The no-GTS MAC payload starts with two Superframe Specification
bytes, one GTS Specification byte and one Pending Address Specification byte.
Pending counts are bits0..2 and4..6; lists consume `2*short + 8*extended` bytes.
The collector uses returned decoder offsets, not its own alternative parser.

The actual NWK decoder requires exactly15 remaining bytes:

| Offset | Meaning |
| --- | --- |
| 0 | Protocol ID `00` |
| 1 | low nibble stack profile; high nibble protocol version `2` |
| 2 | reserved bits0..1; Router Capacity bit2; depth bits3..6; ED Capacity bit7 |
| 3..10 | Extended PAN ID, copied in wire order; range1 through `FFFFFFFFFFFFFFFE` |
| 11..13 | unsigned little-endian 24-bit TxOffset |
| 14 | Update ID |

These fields are from R22 section 3.6.7/Table 3-71/Figure 3-54; protocol version2 is
also specified by Table 3-57, p.322. **Protocol version and stack profile are
different fields.** Raw TxOffset is copied into a `uint32_t`; `FFFFFF` is the
beaconless default. Nondefault offsets remain raw metadata here, not evidence
of full beaconless consistency or calibrated time.

The unchanged codecs' strict syntax subset still applies: no secured,
Enhanced/version1 Beacon, GTS descriptor layout or unsupported address/length
combination; strict reserved-field and source/pending sentinel rules remain
as documented in [MAC.md](MAC.md) and [NWK.md](NWK.md). These restrictions are
not a claim of implementing every IEEE receiver-ignore requirement. In
particular, no reserved-bit normalization has been added by this collector.
Malformed/unsupported-protocol input is not an eligible observation.

## API, ownership and outcomes

Public API: [nwk_candidates.h](../include/nwk_candidates.h).

* `nwk_candidates_init(table, channel_mask)` returns `OK` and clears the
  complete caller table on valid initialization/reinitialization. NULL or an
  empty/out-of-range mask returns `INVALID_ARGUMENT` without modifying it.
  Changing the mask deliberately starts an empty collection. Initialization
  is only memory initialization, not scan cancellation or radio recovery.
* `nwk_candidates_consider(table, channel, crc_valid, body, length)` uses all
  three real decoders. `crc_valid` must be exactly0 or1; a truthful1 is an
  explicit caller/adapter precondition. Zero returns `BAD_CRC` without parsing
  or changing candidates. The function does **not** calculate or fake PHY CRC.
* `nwk_candidates_get(table, index, result)` returns `OK` and copies an entry;
  there are no borrowed spans. All errors leave the output unchanged.

Admission outcomes:

| Result | Table effect |
| --- | --- |
| `ADDED` | Append a new eligible identity. |
| `UPDATED` | Replace all copied metadata for an eligible duplicate, keeping its index. Identical bytes still return `UPDATED`. |
| `WITHDRAWN` | A syntactically valid duplicate now fails a selected filter: remove it, compact subsequent entries left and zero the vacated tail. |
| `NOT_ELIGIBLE` | No existing identity matched; no change. |
| `FULL` | A new eligible identity cannot fit; no eviction or other mutation. |
| `BAD_CRC`, `OUTSIDE_MASK` | No change. |
| `MAC_REJECTED`, `NOT_BEACON`, `NWK_REJECTED` | No change. Syntax success at one decoder is not success at the next. |
| `INVALID_ARGUMENT`, `INVALID_TABLE`, `BAD_INDEX` | No mutation of the relevant table/output. |

Validation order is argument shape (including channel11..26/boolean CRC),
table header, CRC indication, selected channel mask, MAC decode/type, MAC
Beacon decode, NWK decode, then identity/filter processing. Thus malformed or
CRC-failed input cannot withdraw an existing candidate. A well-formed changed
permit/capacity advertisement **does** withdraw it, including while full.
The same applies to valid profile/BO changes. Duplicates and withdrawals are
processed before capacity rejection.

Identity is exactly:

```
channel + PAN ID + Extended PAN ID + coordinator source mode + source wire octets
```

Source short addresses have six canonical zero tail bytes from the MAC decoder;
the mode remains part of identity even if all eight canonical octets match.
Sequence, permit/capacity, profile, depth, update, offset, MAC flags, GTS permit
and pending counts are metadata, not identity. Pending addresses themselves
are validated/skipped by the real codec but are not retained. Last valid
eligible observation wins even with a lower sequence or Update ID. This is
**not** freshness or replay protection.

This is a NWK candidate key, not a complete IEEE MAC PAN-descriptor list.
A different Extended PAN ID is a new identity; it does not withdraw an older
identity sharing the same channel/PAN/source. There is no automatic aging.
The caller must start a new collection explicitly and cannot treat retained
records as proof of current availability.

Entries `[0,count)` retain insertion order. Indices are not stable handles;
withdrawal changes subsequent indices. Every unused entry is zeroed. Copies
do not retain pointers into CODE or XDATA input; reuse of a long-address slot
for a short address cannot retain the old six-byte tail. Four entries are a
bounded project resource policy, not a required complete network-discovery
table size.

All pointer arguments must denote actual, accessible, disjoint C objects for
the stated lengths. Writable objects must be ordinary caller-owned storage,
not MMIO, compiler-private storage, CODE, reserved status or the IRAM alias.
NULL checks do not validate invented numeric pointers. Header checks
(`version`, valid mask, `count<=4`) bound traversal, not arbitrary corruption
detection or peer authentication; callers must not edit table fields after
initialization. No invalid header authorizes out-of-bounds traversal.

Production C has no board headers, registers, heap, clocks or hardware calls.
Table loops are bounded by4; all parser loops/length bounds remain inherited.
SDCC large-model arguments, temporary records and generic-pointer runtime
scratch make the composition **foreground-only and non-reentrant**, serialized
with all three codecs. `volatile` pointer locals preserve the reviewed SDCC
storage strategy, not concurrency safety.

## Evidence and measured resources

The detailed figures/hashes below describe the original collector delivery.
The #63 shared-code refresh leaves its source and corpus unchanged:
**18476 CODE,988+64 XDATA, SP4E**. Legacy Beacon selection is not broadened
by an explicit Association Response receive profile. The
[current shared summary](VALIDATION.md#r22-response-profile-and-shared-proof-refresh-63)
supersedes historical addresses/hashes below; all original budgets remain.
The later complete-join decoder lowering reduces CODE to18433; XDATA988+64,
stack start2F and peak4E remain unchanged on both boards. The complete
original corpus and37 artifact plus1 alias negatives pass with refreshed
identities, without broadening Beacon admission.

Both `generic` (`CC2530_BOARD=0`) and `lg_esl29_rev03` (`=1`) passed:

* Strict C99 native tests and ASan/UBSan, including 381 exact-sized length
  cases (`0..126` for 26-,44-,88-byte source/pending layouts), an exact-sized
  output, 2,304 control-byte cases and 256 cases each for channel/CRC flags.
* The common compiled C corpus: CODE and XDATA golden inputs; both source
  modes and all72 legal pending-count layouts; counts0..4/full rejection;
  duplicate replacement while full; valid permit/capacity/profile/BO
  withdrawal; compaction at every index; draining/reuse; output copies and
  zeroed tails; mask replacement; raw depth15/update regression/nondefault
  offset; all SO values with BO15; truncation/trailers, unsupported security,
  version/GTS/reserved/Extended-PAN values and unchanged error outputs.
* Genuine SDCC linked execution, without patched returns, using the shared
  **15-second per-process** simulator deadline. The simulator transcript is
  indexed once. XDATA `1F00..1FFF` maps to IRAM; both alias directions are
  tested, and omission of the alias actually fails its negative control.
* Whole CODE, exact public/main/checkpoint addresses, public return types,
  complete private/caller/field ABI, actual three decoder calls and all four
  relocated instruction snapshots. There are36 damaged-artifact negatives
  plus one missing-alias negative. Each module independently rejects one
  dropped, duplicated or reordered instruction record.
* Upper-IRAM, unallocated-XDATA and status-tail guards, stack unwind and
  interrupt-enable register checks. Only8 of the64 reserved status bytes are
  writable by this harness.
* Unchanged directly coupled MAC and NWK Beacon native codec regressions,
  repository guardrails and whitespace checks.

Measured with **SDCC 4.2.0 #13081**, `--model-large --opt-code-size`:

| Allocation | Bytes / addresses |
| --- | --- |
| Whole linked CODE | **18,349**, contiguous `0000..47AC` |
| Ordinary XDATA | **979**, `0000..03D2` |
| Accounted XDATA including entire status reservation | **1,043** =979+64 |
| `nwk_candidate_t` / `nwk_candidates_t` | **36 /150** bytes on SDCC; not a host/wire ABI promise |
| MAC compiler-private XDATA |207, `0000..00CE` |
| NWK Beacon compiler-private XDATA |27, `00CF..00E9` |
| Collector compiler-private XDATA |111, `00EA..0158` |
| Caller objects |607, `0159..03B7`, including two150-byte tables and two36-byte copies |
| Caller helper private storage |5, `03B8..03BC` |
| Linked runtime XDATA |22, `03BD..03D2`; `__gptrput_PARM_2` at `03C8` |
| DSEG / shared OSEG |28 /10; bit-addressable backing occupies one byte |
| Stack start / final SP / observed peak SP |`2F` / `2E` / **`4C`** |
| Observed stack use / remaining lower-IRAM margin |30 /51 bytes; upper `80..FF` remains guarded |

These are composed-corpus measurements, not full-stack fit or ISR-nesting
evidence. A production caller needs its own150-byte context in addition to
compiler-private storage. Linked runtime is shared and depends on composition.

Separate proof budgets are **20,480 CODE /1,280 XDATA including status**.
The test image includes both decoders, collector, substantial caller corpus,
two tables, two result copies and126/75-byte work buffers. Against the measured
18,349/1,043 this leaves2,131/237 bytes, while remaining far below lower CODE
`8000` and ordinary XDATA `1E00`. This is an independently justified
composition budget: no existing component budget, `verify_component_layout`
default or alias/stack guard was changed.

Object contributions and canonical instruction coverage:

| Module | CODE including data/startup | XSEG | DSEG/OSEG | Instruction records / bytes |
| --- | ---: | ---: | ---: | ---: |
| `mac_frame` |7,009|207|15/10|4,168 /7,009|
| `nwk_beacon` |601|27|9/0|353 /601|
| `nwk_candidates` |2,334|111|4/0|1,519 /2,334|
| `nwk_candidates_test` |7,846|612|0/0|4,173 /7,776|

Remaining559 CODE bytes are CRT/library contributions. Test CODE includes70
constant Beacon bytes. The proof pins all10,213 ordered instruction records
(17,720 bytes), their image bytes and per-module SHA256; normalization is
`six-hex-address:lowercase-byte-hex\n`, not a selected subset of records.
Constants/CRT/library bytes are also covered by the whole-image digest.

Both board definitions produced identical hashes:

```
CODE      cf9126b3ddfe31199be6c5e0a6c56749f6a3e82779b25194e1a32d4988e4119d
IHX       a0606331d5f58da6c960d390956cce4b7d3990fc69141283c7a09f1e9bf93a9b
private   99dc41a3676e93f58a0c69d5fd7cc0c1a02a8d44c4483504ccdab5b06a04ae59
caller    1cadde1df220feeeb2fd5cd428d5cdd0c70e11060d45f3ffe5feda9f881d2415
fields    ff3aa08d250d6bd3d54c9a021d1a81dfe64fbea00541d7cebae177a3cdda8014
```

Private/caller/field digests cover267/112/19 complete sorted debug records,
respectively, retaining duplicates and a trailing newline. The private prefix
is also reconstructed from addressed declarations and required to cover
exactly `0000..0158`. The proof pins network/candidate/table fields and
three-byte generic pointer declarations, not only total struct sizes.

Public CODE addresses: MAC command decode/encode `022A/0425`, MAC Beacon decode
`08E0`, MAC frame decode/encode `1214/195C`, NWK Beacon decode `1BC3`, collector
init/consider/get `1EFA/213A/266F`, main `4555`, exact done NOP `458D`.
Map/CDB/listing/instruction boundaries are cross-checked; an arbitrary
NOP-valued address cannot substitute for the done checkpoint.

## Reproduction and integration

Canonical focused checks, run serially from the repository root:

```sh
make -j1 BOARD=generic BUILD=build/nwk-candidates-check test-nwk-candidates
make -j1 BOARD=lg_esl29_rev03 BUILD=build/nwk-candidates-lg-check test-nwk-candidates
PYTHONPATH=tools python3 -B -m unittest test_local_checks test_m0_artifacts -q
python3 -B tools/check_repository.py
git diff --check
```

The target links `mac_frame`, `nwk_beacon`, `nwk_candidates`, then the caller.
It immediately snapshots all four listings as
`nwk_candidates_test.<module>.rst`, before another image can relocate the
shared objects. `test-common` includes the target for both board definitions.
The 49 focused Make/artifact regressions pass, enforcing inventory, link and
snapshot ordering, and source/symbol exclusion from every board image.
Canonical generic execution and a byte-identical second-board canonical build
retain the measurements above. No unchanged full local matrix was repeated.

Additional native ASan/UBSan checks after the canonical build:

```sh
# Repeat with number=1 and out=build/nwk-candidates-lg-check.
number=0
out=build/nwk-candidates-check
cc -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DCC2530_HOST_TEST -Iinclude -DCC2530_BOARD="$number" -Itests \
  tests/test_nwk_candidates.c src/mac_frame.c src/nwk_beacon.c src/nwk_candidates.c \
  -o "$out/host-nwk-candidates-sanitized" &&
  UBSAN_OPTIONS=halt_on_error=1 "$out/host-nwk-candidates-sanitized"
```

`nwk_candidates_test.ihx` is test-only: **never flash it or upload it as a
board artifact**. Existing board budgets and artifact paths are unchanged.

## Remaining gates

No channel walk/tune, scan-duration timer, Beacon Request scheduling, scan
completion, association transaction/capabilities, parent selection, membership,
join/rejoin/leave or radio adapter is implemented. Receiver-on ED is the
intended consumer, not a radio mode this collector configures. There is no
sleepy role, routing, child admission, network formation or Trust Center server.
Existing reset-exclusive radio TX and RX paths still cannot be combined in a
reset epoch merely by calling this collector.

The adapter must eventually deliver real complete bodies, channel identity
and confirmed CRC status with valid buffer ownership. Physical receive/TX,
timing, RF, CRC hardware behavior and bidirectional ownership remain separate
gates. Host inputs and simulator execution use **synthetic public bytes**;
no device was enumerated, attached, flashed, tuned or observed.

No BDB procedure was implemented and the PRO BDB v3.0.1 errata/test-plan
conformance follow-ups #16/#17 remain open. Candidate retention proves neither network compatibility,
authentication, authorization, secure join nor application interoperability.
Security, durable counters, required endpoint0 services, ED Timeout/parent
keepalive and other ledger obligations are not waived by this small collector.
