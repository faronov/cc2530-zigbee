# NS51: offline-built nRF52840 PHYEND observer

**Controlled test-node preparation may continue.** This laboratory role is
distinct from passive sniffing; permission to prepare it is not permission
to install firmware or transmit. Preserve the separate recovery/RF gates and
continue unrelated CC2530 work independently.

**Not authorized for installation or RF use.** This is a separately scoped,
original diagnostic application for `nrf52840dk_nrf52840`, not a board IMAGE,
Zigbee stack, sniffer replacement, restoration tool or live host runner.
Ordinary tests never fetch an SDK, build target firmware, import a transport,
enumerate devices or run a programmer. Target artifacts stay outside Git/CI.
See [provenance and source contracts](PROVENANCE.md).

## Behavior and limits

Every reset starts disarmed. UART is the DK J2/debug-port UART, **115200 8N1,
no flow control**, not native USB. Board sources select P0.06 TX/P0.08 RX;
the actual board/revision/wiring must still be verified separately.
There are only STATUS, ARM and RUN commands. No reset, raw memory, channel,
carrier, arbitrary frame, join, identity or retry command exists.

The one public frame is a legacy data frame, FCF `0x8861` (AR set, short
addresses, PAN compression, no security), DSN `51`, PAN `CAFE`, destination
`1234`, source `5678`, payload `NS51-PUBLIC`. These are synthetic constants,
not a factory identity. Its 23-byte driver buffer is:

```text
16 61 88 51 FE CA 34 12 78 56 4E 53 35 31 2D 50 55 42 4C 49 43 00 00
```

PHR `16` includes the two FCS bytes; the last two bytes are ignored TX
placeholders, not the transmitted FCS. Channel is **26**, nominal power is
**-20 dBm**, CCA is disabled, and there is one attempt, not CSMA/retransmission.
The TX buffer remains allocated for the entire boot. The attempt is consumed
before submission; a scheduling rejection cannot rearm or retry it.
Callbacks before the submitting API returns are supported.

Startup has an explicit begin/complete boundary. A cold RADIO DISABLED/no
pending RADIO IRQ sample only permits calling SDK initialization; it is
**never carried through initialization as stopped-state evidence**. No radio
action is allowed while the SDK is uninitialized. A bad cold sample skips
SDK initialization and cleanup calls, reports PROFILE plus an unknown-state
terminal (`disabled observed=0`, `stop issued=0`), and invents no API result.
After permitted initialization returns, profile and exact core SLEEP/RADIO
DISABLED/no pending IRQ are sampled together under PRIMASK. Only then is a
fresh HELLO queued. A bad profile or active state faults; active state requires
the actual sleep request/result and a new disabled observation, or an
uncertain terminal. Early callbacks/faults are retained, not reinitialized
away, and may precede HELLO. A skipped-init HELLO explicitly reports FAULT.

The narrowly guarded BSD driver patch changes the real PHYEND completion
path, not a foreground timer guess. It keeps AR and frame bytes unchanged,
switches to normal promiscuous RX, cancels the existing ACK timer through its
real completion hook, reconciles the TX work buffer, and calls the distinct
`ns51_phy_tx_done`. It never substitutes a normal successful TX/ACK callback.
Nordic automatic ACK transmission is disabled. Read-only diagnostic helpers
verify the PIB profile and distinguish actual core SLEEP from FALLING_ASLEEP.
An unexpected Nordic ACK-start callback is a retained fault, not ignored.
With the guard absent, the **entire preprocessed core** matches unmodified
upstream, ignoring blank lines and outer line indentation only.

ARM expires at 5000 service milliseconds, equality included. RUN must repeat
the descriptor and nonzero eight-byte host nonce exactly. One ARM/RUN pair
is allowed per boot; no history clearing exists. RUN commits an authorized
attempt: a later fault cannot retract an already issued RF operation.
This requires a trusted, exclusive operator port. It is **not authentication
or replay protection across a full reset**.

| Bound | Action at the bound |
|---|---|
| 100 ms after RUN | Request stop; missing PHYEND is a retained expiry fault |
| 20 ms after PHYEND | Request stop, also capped by the RUN deadline |
| Four received frames | Preserve the fourth, retain capture-limit fault, request stop |
| 20 ms / 256 service checks after stop request | Terminal stop uncertainty, no retry |
| 8192 ARM / 4096 active control checks | Retained work fault |
| 100 ms partial command / 1024 input bytes / eight commands per boot | Retained protocol/work fault |
| 16 queued records, plus reserved first-fault and terminal records | Never overwrite; queue loss is a retained fault |
| 32 regular-record sequence positions plus at most two reserved records | No sequence wrap or unbounded recording |
| 256 UART RX bytes; 32 bytes per ISR/foreground slice | RX overflow faults and disables input |
| 100 ms / 4096 checks per output line | Retained UART TX fault |

All deadlines are checked software/service-time deadlines, conditional on
CPU, clock, scheduler and driver progress, **not a hard RF-off guarantee**.
SDK initialization includes its upstream entropy acquisition loop; failure
there does not yield a ready handshake. Assertions remain enabled, but
reset-on-fatal is disabled. A driver/kernel halt or incomplete UART transcript
is unresolved failure, never successful silence or a completed trial.

The only stop request is `nrf_802154_sleep()`, issued once. It can abort a TX
or incoming frame; a resulting real TX/RX error is recorded. Completion also
requires exact core SLEEP, RADIO DISABLED and no pending RADIO IRQ. This is a
software-observed disabled state, **not MAC CLOSED, IFS, ordinary-TX permission
or a physical RF measurement**. No automatic fault recovery/reinitialization
occurs. A failed/unresolved stop retains ownership.

RX bodies are copied before exactly one driver-buffer-return call, including
invalid/unexpected receipts. In the selected direct backend that return
function sets the buffer free and returns true even if its optional critical
section is unavailable. The defensive false branch retains the pointer and
faults; it never retries. Copies exclude both potentially modified FCS bytes.
`CRC=1` means the driver's documented hardware CRC validation, not preserved
literal over-air FCS and not MAC/security acceptance.

RADIO priority is 0, flags 0, internal IRQ management and direct notifications
are selected; zero-latency IRQ configuration is off. Every shared control and
UART state access is serialized using saved/restored **PRIMASK**, including
callback-before-return handling. It does not assume `irq_lock()` masks every
priority. RX copies and record construction are bounded; serial encoding and
radio submission/sleep occur outside this critical section. The selected
UART FIFO implementation is nonblocking and copies each TX chunk into its
own DMA buffer. A line is retained until the subsequent TXSTOPPED indication.
RX-side faults still permit their fault report to be transmitted; TX failure
retains the current line and disables further publication. Neither proves
delivery to a host.

The two-node arrangement is blind while the nRF transmits and during its
interrupt/RX turnaround. Ordinary RX still has the upstream driver's frame
qualification and buffer limits. Missing data, an error, exhausted capture,
or timeout is **not evidence that the CC2530 emitted nothing**. OS milliseconds
are service observations, not captured PHY timestamps. Neither #40 nor #50,
arbitrary-input unicast-only ACK behavior, interoperability or timing
conformance is established.

## Serial ABI v1

There is no transport in `protocol.py`; it only constructs/validates bytes.
All lines are uppercase ASCII hex followed by exactly one LF, no CR, spaces
or NUL terminator. Decode the hex into:

```text
version:u8=1 | type:u8 | payload_length:u8 | payload | CRC16:little-endian
```

CRC16 is CCITT-FALSE (`poly=1021`, initial `FFFF`, no reflection/final xor)
over version, type, length and payload. It is unrelated to the RF FCS.
Maximum output payload/line is 145/301 bytes; maximum command is 40 payload
bytes, 45 decoded bytes, 91 serial bytes.

| Command type | Exact payload |
|---|---|
| `00` STATUS | Empty; produces a state/descriptor snapshot, not RF permission |
| `01` ARM | 32-byte descriptor digest followed by nonzero eight-byte nonce |
| `02` RUN | Exactly the same 40 bytes as the accepted ARM |

The descriptor is SHA256 of the following canonical bytes:
`b"NS51\x01\x1a\xec" + BODY + LE32(5000,100,20,20,4)`.
`BODY` is the 20 bytes between PHR and TX FCS placeholders above; version 1
also fixes no CCA, no automatic ACK, and promiscuous post-PHYEND observation.
The digest is:

```text
9dec858b7b61ebf360364b4b6596bec392d3f8d11371c6a58060d12fc13f09b9
```

Every record payload begins with `sequence:LE16, state:u8, first_fault:u8,
service_ms:LE32, nonce:8 bytes` (16 bytes total). Sequence starts at 1;
milliseconds wrap modulo 2^32. No integer/struct layout is copied onto the
wire. Inactive record bytes are zeroed. Pre-ARM records have a zero nonce.

| Record type | Suffix after the common prefix |
|---|---|
| `80` HELLO/STATUS | 32-byte descriptor digest |
| `81` ARMED, `82` TX_REQUESTED, `84` PHY_TX_DONE, `87` STOP_REQUESTED | Empty |
| `83` SCHEDULED, `88` STOP_ACCEPTED | One Boolean: API return, **not physical completion** |
| `85` RX_FRAME | RSSI signed byte, LQI byte, CRC-valid byte `1`, body length byte, body |
| `86` RX_ERROR, `8B` TX_ERROR | One upstream error-enum byte; no fabricated body |
| `89` TERMINAL | consumed, PHYEND seen, receipt count, disabled state observed, TX retired, stop issued: six bytes |
| `8A` FAULT | Empty; first-fault field is nonzero |

States 0..6: DISARMED, ARMED, REQUESTED, CAPTURE, STOPPING, DONE, FAULT.
Faults 0..18: OK, PROFILE, PROTOCOL, EXPIRED, REPLAY, DESCRIPTOR, NONCE,
QUEUE_LOSS, CAPTURE_LIMIT, CALLBACK, REJECTED, TX_ERROR, RX_ERROR, RX_LENGTH,
CLOCK, WORK, STOP_ERROR, UART, BUFFER. Faults retain the first cause.
DONE means only the bounded diagnostic procedure completed without a
recorded fault. It does not mean an ACK arrived. Read the actual RX_FRAME
body; no ACK-success Boolean is provided. A terminal record with zero
receipts does not prove silence. Unexpected callbacks after a terminal
record are faults, not a new trial.

## Offline reproduction

Host-only checks, from the repository root:

```sh
python3 -B -m unittest tools.test_nrf_stimulus -v
```

This runs strict C99 native and real ASan/UBSan tests, the identical control
and UART corpus, C/Python wire parity, protocol mutations and synthetic
artifact/configuration negatives. It requires `cc`, not the SDK.

For the separately selected target build, set `SDK` to a **new absolute
external** workspace and `APP` to this directory. Nothing here downloads or
builds target firmware as part of normal tests. CMake and the audit reject
target build directories inside the repository.

Fetch the five repositories/commits in `dependencies.json` with
`git init`, `git remote add origin https://github.com/<repository>`,
`git fetch --depth=1 origin <exact-commit>` and detached
`git checkout FETCH_HEAD`. For NCS and nrfxlib use `--filter=blob:none` on
fetch and set sparse checkout **before checkout**:

```sh
git -C "$SDK/nrf" sparse-checkout init --cone
git -C "$SDK/nrf" sparse-checkout set boards cmake drivers dts ext include lib \
  modules samples scripts share subsys sysbuild tests zephyr
git -C "$SDK/nrfxlib" sparse-checkout init --no-cone
git -C "$SDK/nrfxlib" sparse-checkout set '/*' '!/*/' '/zephyr/' '/nrf_802154/' \
  '!/nrf_802154/sl/sl/' '**/Kconfig*' '**/CMakeLists.txt'
```

The other source directories are `zephyr`, `hal_nordic`, `cmsis`. Unselected
Kconfig/CMake metadata is necessary even though its libraries are not built.
No proprietary SL binary directory is selected.

Fetch the two Zephyr SDK 0.16.5 release assets using `gh api -H
'Accept: application/octet-stream' repos/zephyrproject-rtos/sdk-ng/releases/assets/<asset>`
into their manifest filenames. Asset IDs are 149035388 (minimal SDK) and
149035264 (Arm toolchain); the published checksum asset is 149035462.
**Verify both manifest SHA256 values before extraction.** Extract the
minimal archive into `$SDK`, then the Arm archive into
`$SDK/zephyr-sdk-0.16.5`. Do not execute `setup.sh`, its host-tools installer,
or register anything system-wide.

Python 3.12.3 in the build environment lacked `ensurepip`. The actual isolated
bootstrap was: fetch `https://pypi.org/pypi/pip/24.0/json`, download its
`pip-24.0-py3-none-any.whl` URL, verify the separately locked digest, create
`$SDK/venv` with `venv.create(..., with_pip=False)`, then:

```sh
PYTHONPATH="$SDK/pip-24.0-py3-none-any.whl" \
  PIP_CACHE_DIR="$SDK/pip-cache" PIP_DISABLE_PIP_VERSION_CHECK=1 \
  "$SDK/venv/bin/python" -m pip install -r "$APP/requirements-build.txt"
git -C "$SDK/nrfxlib" apply --check "$APP/phyend-observer.patch"
git -C "$SDK/nrfxlib" apply "$APP/phyend-observer.patch"
export PATH="$SDK/venv/bin:$PATH"
export ZEPHYR_BASE="$SDK/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$SDK/zephyr-sdk-0.16.5"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
cmake -S "$APP" -B "$SDK/build" -G Ninja -DBOARD=nrf52840dk_nrf52840 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DUSER_CACHE_DIR="$SDK/cache" \
  -DZEPHYR_MODULES="$SDK/nrf;$SDK/nrfxlib;$SDK/hal_nordic;$SDK/cmsis"
cmake --build "$SDK/build" --target zephyr_final -j 4
```

From the repository root, explicitly audit the already built artifacts:

```sh
python3 -B -m tools.nrf_stimulus.verify_build --sdk "$SDK" --build "$SDK/build"
```

The audit runs compiler preprocessing and read-only binutils/Git/Ninja
dry-run operations, not firmware. It emits `build/evidence.json`,
`startup.i`, `startup.macros`, `radio.macros`, and `linked.dis`.
**This executes tools against a trusted, locally built pinned workspace.
It is not a sandbox or a certifier for arbitrary downloaded build metadata.**
Python, Git, the host/shared libraries, SDK headers/build scripts, generated
build files and a non-concurrently modified workspace remain trusted.
Do not point it at an unknown downloaded build or an attacker-controlled tree.

Before tool execution the audit checks the named compiler, `cc1`, `collect2`,
assembler/linker paths, binutils, exact selected `libgcc`, and native
Ninja/CMake executable digests against `audit-tools.json`. These digests were
vetted against the checksum-verified SDK release archive and the exact
checksum-verified public Ninja/CMake wheels recorded there. Python entry-point
wrappers are not used by the audit. Paths and version strings are identifiers,
**not authentication of replaced tools or runtime archives**. This binds the
named files, not the complete host/SDK dependency closure, concurrent state
or the historical origin of arbitrary existing objects.

Compiler cwd must be the selected build directory; source, include, macro
header and output paths must stay in their reviewed roots. The audit checks
the entire compile inventory before preprocessing and repeats that check
at preprocessing entry. Only the reviewed flag vocabulary is accepted:
response files, plugins, wrappers, alternate executable prefixes/specs,
forwarded options, unknown defines/flags, alternate compiler aliases and
escaped paths are rejected. Compiler search/injection environment overrides
must be unset. These narrow guards do not turn an untrusted workspace into
a safe input.

It binds every source-built archive member and direct linked object to an
actual local compiler output; the selected `libgcc` is instead bound to its
vetted release digest. Default-off full-core equivalence, actual PHYEND call
order, selected macros and live startup reachability are checked. The genuine
main disassembly must branch around both SDK init and completion when begin
denies entry, and call the fresh stopped predicate after init. Twelve CMake
compile declarations were unbuilt and explicitly excluded as evidence.

The upstream build recipes contain explicitly invoked programmer runners;
**do not run `flash`, `debug`, `recover`, `west flash`, a runner or firmware**.
Only `zephyr_final` and read-only inspection were executed. No SDK host-tools
installer, USB/serial action or Cortex-M simulator was used.

## Measured evidence and actual build diagnostics

Completed on Linux with Python 3.12.3, CMake 3.28.3, Ninja 1.11.1, west 1.2.0,
Arm Zephyr GCC 12.2.0 / SDK 0.16.5, C99, `-Os`, Cortex-M4 Thumb, AAPCS,
minimal libc, no application heap. Native compiler: Ubuntu GCC 13.3.0.

| Evidence | Result |
|---|---|
| Host-tested | 160256 C checks per native and ASan/UBSan run; 19 Python tests |
| Image/static-checked | Genuine ELF/HEX link; 174 compiled objects, 25 archives; no opaque radio/SL/MPSL |
| Flash load / extent | 56204 bytes / 56204 bytes, limit 262144 |
| Allocated SRAM / extent including alignment | 17755 / 17792 bytes, limit 65536 |
| Configured main / ISR / system-workqueue stacks | 4096 / 4096 / 2048 bytes, included in SRAM |
| Cortex-M simulation / hardware / restoration | **Not performed; not accepted** |

The ELF allocates 4160/4160/2112 bytes for those three stacks including
64-byte guards. No runtime stack high-water or full-stack fit is asserted. The allocation is
a provisional helper budget, unrelated to every unchanged CC2530 budget.

```text
ELF SHA256  909f5695d5f47e23524370fab20d29db0f116345a9c04d4e6a6b41b1b1c3192d
HEX SHA256  e19e78b103793fc7741f5d949172caf8ada9e0eb57d0b251af967034e72fc29d
patch       ec5ca35ddfff4884c1039d345bec845e73cdc56ac7d4392dbca09645ed915ab0
```

These identify this linked build, not path-independent debug-file
reproducibility. The ELF/HEX/map are `build/zephyr/zephyr.{elf,hex,map}`.
The Python native and sanitizer tests require exactly 160256 checks, not
merely a positive count. The original scenarios remain; added startup cases
cover cold refusal, pending initialization, every valid/invalid profile and
quiet/active combination with early callback/UART/RX faults, retained records,
fresh HELLO state, real stop completion/rejection, and invalid completion
ordering. Audit negatives run without an SDK or executing their synthetic
compiler fixtures. Ordinary discovery retains Python 3.9 support.
HEX includes upstream's `--gap-fill 0xff` alignment filling: the checker
derives the file-backed section/LMA mapping and permits FF only in proven
zero ELF alignment holes. All actual section contents must match.

Actual failed stages were retained externally: missing Python `ensurepip`;
incomplete sparse `sysbuild` metadata; missing NCS `share` version integration;
SDK defaults selecting CryptoCell (`ENTROPY_CC3XX`, independently `HW_CC3XX`)
and reset-on-fatal; initial app UART-name collision/nonexistent public
RxOnWhenIdle getter; and a link failure because chosen entropy still pointed
at CryptoCell after selecting the source RNG. The final configuration fixes
these explicitly, without stubs or binary libraries. `dtc` remained absent:
Zephyr's Python devicetree generation succeeded; optional `dtc` checking was
not performed. Early checker failures exposed upstream's legitimate type-03
HEX entry, FF gap filling, unbuilt CMake declarations, and the startup tail
branch; the final checker handles those exact cases without skipping checks.
The linker's four synthetic veneer inputs must all be zero-sized; they are
not accepted as externally supplied objects.

## UICR and restoration boundary

Both UICR DT properties are deleted, not merely disabled. Legacy NFCT config
is off. The **actual SystemInit compilation** has
`CONFIG_GPIO_AS_PINRESET` and `CONFIG_NFCT_PINS_AS_GPIOS` undefined, not zero.
The selected, reviewed preprocessed startup bodies and linked calls exclude
their runtime NVMC/UICR writes and emulation branches. There is no flash/NVMC
driver, bootloader, provision/S1 image or regtool-UICR output. The ELF and HEX
checks reject UICR `10001000..10001FFF`, FICR, SRAM load records, and any
other out-of-main-flash load range.

`APPROTECT_USE_UICR` is deliberately selected, never LOCK. On applicable
revisions startup **reads UICR.APPROTECT and writes volatile
APPROTECT.DISABLE**. It also reads factory calibration/errata information.
That is not a UICR write, but it is protection handling: this build does
**not** promise debug access or preservation of every volatile register.

Before any later installation, the parent must establish fresh board
identity/revision, readable **whole 1 MiB flash and whole 4 KiB UICR**
recovery material, independent reread/hash verification, protected-region
comparison, an executable restoration plan and separately reviewed
programming/reset/destructive scope. Historical sniffer-image matching is
not that proof. Preserve all non-image flash too; do not infer current UICR
or restorability from these source files or assume recover/unlock is safe.
No private recovery material was accessed in this task. Failure to establish
restoration/debug access remains a hardware-installation blocker.
