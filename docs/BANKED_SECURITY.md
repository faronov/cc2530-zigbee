# Banked key, cryptography and NV composition

This is the first **real service placement** on the banked ABI, not the
complete Zigbee stack or a board firmware release. It contains the actual
key owner, extended NWK/APS wire protection, AES/CCM/MMO/keyed hash, outgoing
counter owner, journal and flash services. No authentication, cryptographic
result, counter allocation or NV-success return is supplied by the simulator.
All identities, keys and peripheral events are public synthetic fixtures.
**Never flash this image.** There is no GPIO, USB or RF activity.

```sh
make BOARD=generic test-banked-security
make BOARD=lg_esl29_rev03 test-banked-security
```

The default bootstrap and all older component/board images retain their
limits and suites. Two separate CI jobs run this composition without uploads;
each retains the 15-minute job and 15-second simulator-invocation deadlines.
Full acceptance belongs to the completed Actions run, not merely compilation.

## Placement and ABI

| Region | Placement | Populated bytes |
| --- | --- | ---: |
| Common CODE | Startup, runtime, real crypto/NV, codecs, caller and libc | 25017 |
| Bank1 | `security_keys`, virtual `18000..1C28E` | 17039 |
| Bank2 | `ed_wire`, virtual `28000..29ADF` | 6880 |
| Complete executable | Sparse canonical bank identities | 48936 |
| Ordinary XDATA | All production/caller/compiler/libc storage | 3886 |
| Status | Separately reserved at `1E00` | 64 |

The new composition budgets are 51200 populated CODE bytes and 4096 total
XDATA bytes including status. These do not enlarge an older profile's budget.
The packer emits `banked_security.hex` with physical addresses and
`banked_security-layout.json` with the distinct
`offline-banked-security-fixture` capability. The linked `.ihx` uses virtual
addresses and is **not a flashing HEX**. NV pages125/126 and the complete
lock/configuration page remain absent from the executable.

`CC2530_BANKED_SECURITY` opts only this profile into the fifteen public
`__banked` key/wire entry points. Ordinary and native callers retain the
existing generic-pointer ABI. Lower services stay common and byte-identical
at the relocatable-object level except for reviewed DATA-area names.
No foreign-bank ordinary CODE pointer is introduced; the journal constants
remain common. Static calls use the original common trampoline, including
calls between public functions in the same bank.

The stack start is explicitly `52`, initial SP `51`, allocation through `7C`.
`BANKED_STACK_FIRST=0x52` adjusts only the trampoline's two minimum-SP
comparisons. Maximum-SP, depth, state and mapping guards are unchanged.
The default banking fixture still has exactly its original machine CODE,
map, raw CDB, memory report and normalized objects; only the listing's
symbolic-expression spelling changes.

## Physical IRAM ownership

Top-level volatile parameter/local copies shorten SDCC register lifetimes;
they do not make caller memory volatile or change pointer memory spaces.
The ordinary key object uses 16835 CODE / 841 XDATA / 4 DATA bytes, compared
with the earlier 14755 / 783 / 46. The wire object uses 6837 CODE / 797
XDATA / 9 DATA bytes, instead of its earlier57-byte DATA requirement.
Both still use4 OSEG bytes; the linked libc requires10.

The key owner copies the install input and keyed-hash input into its existing
wipe-owned work area, with a new16-byte hash-input field. Local capacity,
offset and slot copies avoid register saves across deep calls. Inlining the
private `take` and `seal` helpers trades CODE for stack without changing
algorithms, persistence schemas or output/error semantics. A real preliminary
Request-Key run exceeded SP7C; the accepted layout must not be justified
solely by a successful link or an EMPTY/open path.

Two ordinary DATA arrays physically reserve `08..1D` and `23..47`.
Runtime depth/fault own `1E..1F`, bits own `20..22`, and OSEG owns `48..51`.
XDATA `1F00..1FFF` remains the IRAM alias, never additional memory.

| Reusable module frame | Base, hex | Bytes |
| --- | ---: | ---: |
| Flash executor / reader / writer |08 /10 /14|8 /4 /5|
| Journal / counter owner |23 /3A|23 /14|
| AES / NWK codec / APS codec |23|32 /12 /8|
| MMO / keyed hash |08 /43|18 /3|
| Wire / key owner |08 /1A|9 /4|
| Timebase / CCM / fixture caller |08 /08 /1A|0 /0 /0|

Only compiler `sloc` temporaries occupy reused frames. Complete raw CDB
records distinguish them from retained DATA; real reservation objects and
runtime state cannot overlap them. The whole emitted transfer graph, including
far-call triples and ordinary tails, must match the reviewed acyclic module
graph. Every simultaneously active ancestor/descendant frame is disjoint.
Libc instructions cannot access these frames or call back into services.
All source and libc scratch allocations are accounted for, including the
complete20-byte libc XDATA suffix, not just `__gptrput_PARM_2`.

This is a **serialized foreground-only** composition. The IRQ demonstration
in the separate ABI fixture does not confer ISR headroom on this image.

## Executed contract

The native and nonrecovering sanitizer references agree on the complete
ordered public trace. The target then executes22 operations:
explicit EMPTY/open and provisioning, association notification, MIC rejection and initial
network-key Transport, protected Request/Transport/Verify/Confirm, protected
application sending, replay rejection, persisted reopen at intermediate and
verified phases, and durable Leave. Persisted VERIFIED remains **key phase
only**, not BDB READY or successful secure rejoin.

Every AES block uses the actual driver, DMA descriptors, MMIO stores and
nine-clock arm path. An independent existing AES reference supplies only
peripheral cipher output after actual key/IV/plaintext transfers are checked.
Flash commands execute the actual123-byte copied RAM core. FCTL/FADDR,
word operands, fresh erased words, idle return and common-CODE MEMCTR
restoration are observed before synthetic media/mapping changes. The CPU's
FMAP and independent XBANK use the native banker; XMAP aliases actual SRAM
and physical IRAM. No protocol/function return or instruction is replaced.

Long operations are split only at confirmed-idle common-code boundaries,
not given longer deadlines. Continuations contain complete ordinary XDATA,
IRAM, SFRs, extended peripheral memory, physical256-KiB flash and logical PC.
Exact restoration is checked before the next instruction. Synthetic timers
and UART are stopped; SBUF is not written during restoration. Both active
bank identity and genuine stack return frames survive the continuation.

The checks bind every executable byte/address, map, raw CDB, memory report,
normalized object and all18 listings captured immediately after this link.
Only an optional NoICE `;!FILE` path comment is excluded; DATA-only objects
have no such comment, so their `XH3` format header must remain in the identity.
Complete source/libc instruction coverage includes SDCC listings with
five-digit source-line numbers touching the cycle bracket. Caller outputs
and unchanged inputs, private key/crypto staging wipes, unowned memory,
guarded peripherals, physical flash neighbors, mapping/depth/stack unwind
and the SP7C ceiling are checked. Per-byte artifact and outcome corruption
must fail. A separate public provisioning attempt exhausts a busy flash
command in the real RAM fail-stop and stays there after later synthetic idle,
without successful publication or media replacement.

The regular lifecycle executes128 actual AES calls and532 flash-RAM commands.
Its maximum SP is `7B` under the unchanged `7C` cap; the corpus requires
244699 artifact and9941 outcome negatives, without sampling either count.

Wipe checks cover the complete named key/crypto work areas, not durable
counter/journal payloads or retained lower AES/DMA/compiler copies.
Artifact comparisons cache only immutable bytes already authenticated
against the fixed expected digest; every address/byte mutation still runs.
Static CDB location caching never caches a simulated observation.

Evidence is **host-tested, image-checked and simulated**, not
hardware-observed. This is not a claim of electrical interruption/endurance,
entropy quality, general TC/router-parent support, complete MCU join,
application interoperability or conformance. The next placement step is the
real MAC/NWK/APS/ZDO/BDB composition; the radio adapter, secure restart and
separate hardware gates remain necessary.
