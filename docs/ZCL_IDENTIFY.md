# Bounded offline Identify procedure

`zcl_identify` implements an actual caller-owned Identify countdown and
**unicast server** command handling over the existing ZCL codecs and foundation
dispatcher. It is not a static IdentifyTime placeholder. Start, restart, stop,
fractional-second progress, expiry and active/inactive Query behavior execute
in native and genuine SDCC tests.

This is preparatory ZCL work, not an authenticated endpoint or full Identify
cluster. There is no physical indicator, GPIO policy, hardware clock, radio,
network send, persistence, factory reset, reporting, client-side discovery or
binding procedure. The separate MAC/radio-before-security order is unchanged.
The subsequent [foundation write family](ZCL_WRITE.md) also implements actual
IdentifyTime updates; this is no longer a command-only/read-only preparation.

## Primary requirements and wire decisions

The selected primary is [ZCL Revision 8, 07-5123-08, December 2019](https://csa-iot.org/wp-content/uploads/2022/01/07-5123-08-Zigbee-Cluster-Library-1.pdf),
1,213 PDF pages, SHA-256
`ad536e1d95a40ca27532e360b124cd76a1b18d96398c1c19fd32fbf7dcdd1aa0`.
The relevant chapters are Foundation **14-0126-17** and General **14-0127-21**.
The following base-text clauses were read directly; printed and PDF page
numbers are distinguished.

| Primary location, printed / PDF pages | Decision |
| --- | --- |
| 3.5.1, 3.5.1.1, 3.5.1.3; 3-30..3-31 / 140-141 | Standard cluster **0003**, Utility, **ClusterRevision=2**. Identify is not disabled by Basic DeviceEnabled; this component has no Basic dependency. |
| 3.5.2.2, Table 3-31; 3-31 / 141 | Mandatory **0000 IdentifyTime**, uint16, **RW**, default **0**, full range **0000..FFFF**. Nonzero starts identification; decrement each second; zero terminates it. FFFF is a valid duration here, not an unknown-value sentinel. |
| 3.5.2.3, Table 3-32, 3.5.2.3.1/Figure 3-7; 3-31..3-32 / 141-142 | Mandatory client-to-server **00 Identify**, exactly two defined payload octets: **LE16 Identify Time**. Set the attribute to that value. Standard appended extension octets are ignored on RX. |
| 3.5.2.3.2; 3-32 / 142 | Mandatory client-to-server **01 Identify Query**, **no defined payload**. When identifying, generate Query Response; otherwise **take no further action**. Idle Query is silent even when Disable Default Response is clear. |
| Table 3-35, 3.5.2.4.1/Figure 3-9; 3-33..3-34 / 143-144 | Mandatory server-to-client **00 Identify Query Response**, payload **LE16 current IdentifyTime**, unicast to the requester, only while identifying. This module constructs bytes; it never sends them. |
| 3.5.2.3.3/Figure 3-8, Tables 3-33/34; 3-32..3-33 / 142-143 | Optional **40 Trigger Effect**: enum8 effect, enum8 variant. Explicitly unsupported here; no successful effect stub. |
| 2.3.4.5/Table 2-1; 2-6..2-7 / 58-59 | Mandatory global **FFFD ClusterRevision**, uint16 read; value **2**. Optional AttributeReportingStatus is absent. No client instance is advertised. |
| 2.4, 2.4.1/Figures 2-2..4; 2-7..2-9 / 59-61 | LE multibyte values, standard three-byte header, direction bit, TSN echo. Immediate response frames set Disable Default Response. |
| 2.3.1-2; 2-3..2-4 / 55-56 | Standard RX reserved bits and appended octets are ignored. TX reserved bits are zero. Manufacturer extensions must not execute standard Identify. |
| 2.5.12/Figure 2-25; 2-28..2-29 / 80-81 | Unicast Identify success produces Default Response SUCCESS unless disabled. Errors still produce Default Response when disabled. A specific receipt rule overrides the general default rule: idle Query is silent. Never reply to a received Default Response. |
| 2.5, 2.5.3-6; 2-10,2-14..18 / 62,66–70 | The shared write family now changes IdentifyTime, with atomic Undivided and silent No Response; [precise scope and type limits](ZCL_WRITE.md). |
| 2.5.1-2, 2.5.13-14; 2-11..2-14,2-29..2-31 / 63-66,81-83 | Actual Read/Discover handlers read the current staged IdentifyTime and ClusterRevision; no duplicated foundation parser. |
| Table 2-11, Table 2-12; 2-46..2-47,2-55..2-57 / 98-99,107-109 | uint16 wire type **21**; SUCCESS **00**, MALFORMED_COMMAND **80**, UNSUP_COMMAND **81**, UNSUPPORTED_ATTRIBUTE **86**, INSUFFICIENT_SPACE **89**. Existing deprecated Default Response status normalization is reused. |

Figure 3-7's caption incorrectly says Query Response inside the Identify
subclause. Its field and receipt rule specify Identify Time. The actual Query
clause explicitly has no payload; do not infer a Query payload from the caption.
Standard RX extension handling is also not permission to send those extensions.

The prior [pinned secondary lookup](ZCL_LAB.md#primary-sources-and-selection)
at `faronov/zigbee-docs` revision
`6e575bdc8c1a68880ef7552d6190a0c6bc80b3a6` was an index/selected-file aid only:
its Identify Query payload and IdentifyTime optionality are wrong, and its
IdentifyType is Matter-only. This implementation imports none of those fields,
catalog contents, scripts or implementation code.

### Independent wire examples

All TSNs and request manufacturer fields in tests are synthetic. Hex octets:

| Input / condition | Output |
| --- | --- |
| `01 5A 00 03 00`, start/restart 3 seconds | `18 5A 0B 00 00` |
| `11 5A 00 03 00`, default disabled | no response; same real state change |
| `01 5A 00 00 00`, stop | `18 5A 0B 00 00` |
| `01 5A 01`, current time 2 seconds | `19 5A 00 02 00` |
| `01 5A 01` or `11 5A 01`, idle | no response, **not** a Default Response |
| `11 5A 00 03`, truncated Identify payload | `18 5A 0B 00 80`, no Identify action |
| `01 5A 40 00 00`, unsupported effect | `18 5A 0B 40 81` |
| `05 34 12 5A 00 FF FF`, unsupported manufacturer namespace | `1C 34 12 5A 0B 00 81`, no Identify action |
| `00 5A 00 00 00 FD FF`, current time 2 | `18 5A 01 00 00 00 21 02 00 FD FF 00 21 02 00` |
| `00 5A 0C 00 00 FF` | `18 5A 0D 01 00 00 21 FD FF 21` |

The response type is significant: Query Response is cluster-specific (`19`);
Default Response and Read/Discover responses are global (`18`).
Server-to-client input, including a Query Response intended for an Identify
client, is rejected as `UNSUPPORTED_CONTEXT`. This server cannot consume it as
an Identify request just because both command IDs are zero.

## API and exact time semantics

Public header: `include/zcl_identify.h`.

```c
zcl_codec_result_t zcl_id_init(zcl_id_t *ctx, uint32_t now);
zcl_codec_result_t zcl_id_tick(zcl_id_t *ctx, uint32_t now);
zcl_codec_result_t zcl_id_rx(zcl_id_t *ctx, uint32_t now,
    const uint8_t *request, uint16_t length,
    uint8_t *response, uint16_t capacity, zcl_dispatch_info_t *info);
```

`now` is **abstract caller-supplied monotonic milliseconds modulo 2^32**.
It is not a Sleep Timer/MAC Timer reading, calibrated time, hidden timer or
claim that a hardware adapter exists. Caller must maintain the same clock
epoch and ensure true forward elapsed time between successful calls is
**less than 2^31 ms**, even while idle. Equal times are accepted. A delta
`>=80000000` is rejected; this detects stale/half-range input within the
contract, but cannot detect all resets, epoch changes or multiple wraps.
After broken continuity, explicitly reinitialize and abandon the old procedure;
do not pretend that modular arithmetic recovered elapsed real time.

The caller owns an eight-byte SDCC context:

| Field | Meaning |
| --- | --- |
| `stamp`, uint32 | Last successfully committed observation time |
| `remaining`, uint16 | Current IdentifyTime, seconds |
| `phase`, uint16 | Elapsed milliseconds of the current second, 0..999; zero when idle |

Initialization starts idle at `now`. It is also explicit volatile local
cancellation, not Basic/BDB factory reset. Context contains **no pointers**:
relocation/copying as an independent procedure is valid. Fields may be observed
but must not be edited by applications. Invalid phase (`>=1000`, or nonzero
while idle) is rejected without mutation; this is not a general corruption
recovery mechanism.

For a live countdown, add the admitted elapsed milliseconds to `phase`.
Subtract all completed seconds from `remaining`, saturating at zero; retain
the fractional remainder only while active. Work is bounded, not a loop per
elapsed second. Maximum addition is `7FFFFFFF + 999`, within uint32.
Zero-duration Identify stops immediately. Maximum duration is **65,535,000 ms**.

Accepted Identify always sets the supplied duration and resets its second phase
**at that call's now**, even for the same TSN/value. This is an explicit local
phase policy: R8 requires one-second decrementing but does not prescribe
subsecond restart phase. Duplicate transport delivery must be filtered by the
caller/APS layer; this component has no transaction cache or replay protection.
Query, Read, Discover and tick advance time but **do not restart phase**.

Example: Identify(3) at time100 leaves3 at1099,2 at1100,1 at2100, and expires
at3100. Restart(3) at1099 changes the next decrement to2099. Delayed calls catch
up exactly; they do not retroactively produce physical flashes. The application
can observe `remaining != 0` and future start/stop transitions for an eventual
indicator, but must call tick frequently enough for its observation deadline.
No notification queue or physical indication is implied.

### Atomicity, ownership and outcomes

All calls are serialized foreground-only, not ISR-reentrant. Inputs, context
and outputs must not overlap; their extents must be truthful. Response storage
must be nonnull even at capacity0. No input/output pointer is retained.

RX validates pointers, clock/state, frame and server direction in that order.
The existing 100-byte ZCL body bound is retained; it is not a claim about a
secured APS service's available payload size or support for fragmentation.
It stages only the eight-byte next timer state, metadata/header and two-byte
cluster payload. The foundation path builds a transient two-attribute view
(IdentifyTime and ClusterRevision) and calls the **real** existing dispatcher.
There is no copied 100-byte input frame or persistent/self-referencing model.

Serialization must succeed before the timer/action and metadata are committed.
**Every local failure preserves the entire context, response storage and info**:
including stale time, malformed header, wrong direction, unsupported layout,
insufficient capacity and unsupported/malformed No Response input. A later retry computes
elapsed time from the last successful stamp; failures do not freeze the
abstract clock or secretly accept an Identify action.

`ZCL_CODEC_OK` requires inspecting `info.kind`:

- `ZCL_DISPATCH_RESPONSE`: bytes were constructed, including protocol-error
  replies. `info.command_id` distinguishes Query Response, foundation response
  and Default Response. SUCCESS acknowledges the actual in-memory procedure,
  not a physical indicator or send.
- `ZCL_DISPATCH_DEFAULT_RECEIVED`: existing received-default metadata;
  response buffer untouched, length0.
- `ZCL_ID_SILENT=2`: no response; only kind and sequence are populated, all
  other fields zero. Response buffer is untouched, regardless of capacity.
  This is used for idle Query and successful default-disabled Identify.

A constructed MALFORMED/UNSUP_COMMAND response is a successful local processing
result: it commits elapsed time, **not** the rejected Identify/effect/write.
For example a malformed Identify can coincide with expiration of an already
running countdown. An error response capacity failure commits neither.

## Explicit unsupported boundaries and conformance gaps

- **IdentifyTime is RW and writable through the shared foundation path.**
  Ordinary/No Response writes apply valid records; Undivided applies none
  if any record fails. Accepted writes reset the phase at `now`, including
  repeated TSNs/values, and zero stops the timer. ClusterRevision stays
  read-only. [Unsupported formats and exact atomicity](ZCL_WRITE.md) remain explicit.
- Trigger Effect is optional and unsupported; no visual-effect success.
  No Matter IdentifyType, reportable flag, physical reading or factory-reset
  function is invented. Only IDs0000 and FFFD are discoverable/readable.
- The API requires a caller-selected, admitted **unicast Identify server**
  context. It has no delivery-mode argument and must not be used for
  broadcast/group traffic. R8's broadcast Query finding/binding use and
  response routing are not implemented by this unicast component.
- No Identify client, profile/device/endpoint advertisement, manufacturer
  assignment, registry, authentication, MIC/replay checking, send or durable
  state. Synthetic manufacturer namespace `1234` is negative input only.
- A physical identification procedure remains unimplemented. R8's recommended
  half-second light indication is not a board-specific policy selected here.

Core R22 **05-3474-22** and PRO BDB3.0.1 **16-02828-012** remain selected.
BDB6.5-7 finding/binding, Identify client, binding capacity, applicable awake
target group memberships and report requirements are not waived by one endpoint
or by this procedure. Full join/rejoin/leave, ED Timeout, parent keepalive,
endpoint-0 and durable counters remain in the [conformance ledger](CONFORMANCE.md).
Centralized-only ED operation is not full BDB conformance.

R8 errata **19-2019** remains a follow-up risk permitting base-text work, not a
claim of errata-aware conformance. BDB errata **21-65431**, Test Plan **16-02826**
and applicable primary application/device/profile selection gates stay open.
The component is not linked into board firmware or the full protocol budget
image and does not establish complete-stack fit.

## Tests, image proof and resources

The numerical measurements/pins below record the original #71 (`bc25c8d`)
six-module baseline. The current write-enabled seven-module composition,
expanded cases, resources and CI evidence are recorded in [ZCL_WRITE.md](ZCL_WRITE.md).

Focused canonical target: **`test-zcl-identify`**. Native executables:
**`host-zcl-identify-tests`**, **`host-zcl-identify-tests-sanitize`**.
Full acceptance runs in GitHub Actions under the
[CI-first policy](../CONTRIBUTING.md#development-checks); the commands below
are available for targeted reproduction, not a required duplicate before push.

```sh
make BOARD=generic BUILD=build/zcl-identify-agent/generic test-zcl-identify
make BOARD=lg_esl29_rev03 BUILD=build/zcl-identify-agent/lg_esl29_rev03 test-zcl-identify
```

The composition uses repository SDCC4.2.0 flags:
`-mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror`,
`-Iinclude -DCC2530_BOARD=0` or `=1`; link flags
`--iram-size 0x100 --xram-loc 0 --xram-size 0x1e00 --code-size 0x8000`.
Native flags are `-std=c99 -O2 -Wall -Wextra -Werror -pedantic
-DCC2530_HOST_TEST -Iinclude -DCC2530_BOARD=... -Itests`.
Sanitizer adds `-fsanitize=address,undefined -fno-sanitize-recover=all
-fno-omit-frame-pointer -fno-pie -no-pie`.

Exact link order and output stem:

```text
zcl_identify.rel zcl_dispatch.rel zcl_attributes.rel
zcl_frame.rel zcl_value.rel zcl_identify_test.rel
=> zcl_identify_test.ihx / .map / .cdb / .mem
```

All six project `.rst` files are snapshotted **immediately after the link** to
`zcl_identify_test.MODULE.rst`, before any later link can relocate shared
listings. `boot_zcl_identify.py` reads only those composition-specific snapshots.
It never accepts later unsnapshotted `.rst` files or guessed pins. Direct proof:

```sh
python3 -B tests/boot_zcl_identify.py --output build/zcl-identify-agent/generic --simulator s51
python3 -B tests/boot_zcl_identify.py --output build/zcl-identify-agent/lg_esl29_rev03 --simulator s51
```

Both board definitions passed equivalent explicit native/sanitizer/SDCC/link/
proof invocations on 2026-09-21. No full local matrix or hardware operation
was run. The common SDCC corpus contains **73 counted scenarios/groups**,
including independent exact wire bytes, actual CODE and XDATA inputs, every
boundary of a small countdown, maximum duration/expiry, wrap, repeated and
stale time, restart/stop/cancellation, Query silence, default behavior,
malformed/trailing/reserved input, manufacturer namespace and atomic failures.

Native tests additionally exercise **196,608 ticks** at offsets0/1/999 of every
second0..65535, and **20,000 mixed operations** against a separate uint64
absolute-deadline oracle spanning many uint32 wraps. Exact-sized native
context/metadata/request/response allocations cover lengths0..102 and response
capacity boundaries for Identify, active/idle Query, default-disabled Identify,
Discover and Read. No decoder/dispatcher successes are stubbed.

| Both-board measured evidence | Result |
| --- | --- |
| Native strict + nonrecovering ASan/UBSan (leak detection) | PASS |
| Complete lower CODE / separate composition cap | **21,792 / 24,576 bytes** |
| Ordinary XDATA + complete status reservation / cap | **836 + 64 / 1,536 bytes** |
| Initial stack / checkpoint SP | **54 / 53** |
| Independent uninterrupted reset-to-checkpoint peak / cap | **70 / 7C** |
| Artifact rejection controls | **114** |
| Result/guard, peak and missing-alias negative controls | **9 + 3 + 1** |
| Alias-aware genuine target execution, 15-second per-process deadline | PASS |
| Hardware observation / calibrated time / physical indication | None |

The initial exploratory build exceeded the SP cap (`81`). The correction
placed pointer temporaries and long-lived corpus loop variables in ordinary
SDCC model-large storage using volatile pointer/local copies; it did not alter
the countdown, weaken guards, add an IRAM pool or raise any budget. Final
production protocol C has no board/MMIO includes.

| Module | CODE, including constants/startup | XSEG | DSEG | OSEG | BSEG bits |
| --- | ---: | ---: | ---: | ---: | ---: |
| zcl_identify | 2,487 | 141 | 17 | 0 | 0 |
| zcl_dispatch | 2,864 | 135 | 15 | 3 | 1 |
| zcl_attributes | 2,086 | 148 | 6 | 12 | 1 |
| zcl_frame | 1,173 | 35 | 6 | 15 | 0 |
| zcl_value | 1,710 | 46 | 15 | 0 | 0 |
| zcl_identify_test | 10,420 | 285 | 0 | 0 | 0 |
| Linked compiler/runtime remainder | 1,052 | 46 | — | — | — |

Compiler-private XDATA is0..504; caller objects505..759; caller locals760..789;
libc/compiler scratch790..835, including non-public runtime locals. Ordinary
XDATA ends at0343, below1E00. Only result bytes1E00..1E07 may change; the rest
of reserved status through1E3F and every unallocated XDATA byte are guarded.
IRAM7D..FF is guarded, and XDATA1F00..1FFF is its real alias, **not extra RAM**.
DSEG/overlays are not additional free storage. No IRQ nesting headroom is claimed.

The proof pins complete CODE (including constants/runtime), **raw CDB bytes
before any decoding**, complete parsed-map identity, all F/S/L/T/private/helper
records, complete ordered area allocations and all six complete immediate raw/
instruction snapshots. It checks field offsets, generic pointers, byte result
ABI, distinct short parameter names, actual calls through countdown/dispatcher/
read/frame/value functions, and private/caller/runtime boundaries. Implicit
SDCC library members have no per-link project `.rst`; full linked identities
cover them rather than fabricated snapshots. Checkpoint SP is not used as
full-run peak evidence.

Final shared two-board identities:

- CODE SHA-256:
  `d9b2bf29d2edf7c7994c28bf952b0707bbdda6bbd25d8ef2e0ff9dd22e3539fe`
- Raw CDB SHA-256:
  `7b74474d63e1a0bd6786b0cb5fdbbc850e9e05398eb1bb7d4700411b305e6f2e`
- Complete parsed-map SHA-256:
  `c60ca2fcd564c0db1c36155d1bd2a79210883c6c1ce3edbb5ca92754f1552552`
- Intel HEX file SHA-256:
  `06f35b3994f80306b92fdf04c9ff645324cf53a91afba6b3c9d6a73b21443642`

Implementation, synthetic corpus and evidence are original BSD-3-Clause work;
generic proof helpers from this repository are reused. The public PDF/reader
remain ignored research artifacts, never a firmware/build dependency.
No private captures, identities, SDK binaries or equipment were used.
**Never flash `zcl_identify_test.ihx`.**
