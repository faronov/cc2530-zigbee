# Source provenance and bounded contract

Original application, portable control/transport, codecs, checks and tests
are BSD-3-Clause under the repository license. No upstream sample command
processor was copied. The only imported code text is the small, reviewed
patch context in `phyend-observer.patch`; that file and its upstream target
retain Nordic's complete per-file BSD-3-Clause notice.

The pinned external trees are not redistributed in this repository.
`dependencies.json` fixes all five revisions and the verified compiler
archive digests; `requirements-build.txt` fixes the isolated Python tools
and their transitive versions. Source-only means **radio and service-layer
objects are compiled from reviewed public source**, not that every SDK
file has the repository's license or that GCC itself was rebuilt.

`audit-tools.json` records named executable/runtime digests independently
checked against the locked Zephyr Arm release archive, plus native
Ninja 1.11.1.1 and CMake 3.28.3 binaries checked against their recorded PyPI
wheels. The audit compares installed bytes before executing them and binds
the selected `libgcc.a` by exact path and digest, not basename/version.
This is a trusted-local-build consistency audit, not a sandbox or a
supply-chain certificate for arbitrary downloaded metadata/objects.
The trusted host, SDK/build-script/header closure and absence of concurrent
modification remain explicit preconditions; see the README trust boundary.

| External input | Applicable license/evidence |
|---|---|
| sdk-nrfxlib radio core, direct notification/request, open SL | Per-file BSD-3-Clause notices |
| sdk-zephyr kernel, platform, UART, board/build support | Per-file Apache-2.0 notices |
| sdk-nrf integration/build glue | LicenseRef-Nordic-5-Clause, not relicensed |
| hal_nordic nrfx/MDK | BSD-3-Clause wrapper/register/errata files; included `system_nrf52.c` and `system_nrf52_approtect.h` are Arm Apache-2.0 with Nordic modification notices |
| CMSIS | Arm per-file Apache-2.0 |
| Zephyr GCC SDK and libgcc | Upstream GCC/toolchain licenses and runtime exception; external only |

The external evidence manifest records source and object digests, per-file
SPDX where present, compiler-command digests and archive members. The two
empty linker-placeholder compilations and generated ISR table have no
per-file SPDX text; they are identified as such, not silently assigned a
license. No opaque Nordic radio, SL, MPSL or CryptoCell archive is linked.
Source SL's local `mpsl_fem_*` compatibility functions are not a linked
MPSL binary. Compiled but unused encryption code is garbage-collected;
its blocking ECB operation is explicitly absent from linked symbols.

## Pinned source contracts

These are exact vendor-authored source/API citations, **not observations
of a physical DK and not a substitute for silicon timing qualification**.

| Contract | Source |
|---|---|
| Genuine PHYEND interrupt, not generic END or a software delay | [trx.c 2668-2688](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/nrf_802154_trx.c#L2668-L2688), [event dispatch 2944-2949](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/nrf_802154_trx.c#L2944-L2949) |
| Original AR-to-RX_ACK branch and patch location | [core.c 2179-2231](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/nrf_802154_core.c#L2179-L2231) |
| Normal RX transition and notification bookkeeping | [core.c 1210-1223](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/nrf_802154_core.c#L1210-L1223), [370-395](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/nrf_802154_core.c#L370-L395), [direct notification 89-110](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/nrf_802154_notification_direct.c#L89-L110) |
| Real ACK timer cancellation hook | [precise ACK timeout 141-150, 204-209](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/mac_features/nrf_802154_precise_ack_timeout.c#L141-L209) |
| Zero-copy TX, callback-before-return, scheduling versus completion, FCS placeholders | [public API 458-503](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/common/include/nrf_802154.h#L458-L503) |
| Promiscuous/auto-ACK controls, not a skip-ACK-on-TX API | [API 835-876](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/common/include/nrf_802154.h#L835-L876), [normal RX 1973-2074](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/nrf_802154_core.c#L1973-L2074) |
| Buffer-return API/context and selected direct implementation | [API 724-753](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/common/include/nrf_802154.h#L724-L753), [core 2909-2935](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/nrf_802154_core.c#L2909-L2935) |
| Actual raw RX callback ABI, verified CRC and potentially modified FCS | [callouts 103-134](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/common/include/nrf_802154_callouts.h#L103-L134) |
| Source-only direct backend and disabled captured timestamps/delayed/IFS/CSMA features | [driver CMake 83-117](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/CMakeLists.txt#L83-L117), [open SL CMake 35-56](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/sl/sl_opensource/CMakeLists.txt#L35-L56) |
| Actual internal RADIO IRQ management | [trx.c 565-574](https://github.com/nrfconnect/sdk-nrfxlib/blob/13cd978b22d192447537a60f7fae5fe092930dc4/nrf_802154/driver/src/nrf_802154_trx.c#L565-L574), [Zephyr priority/flags adapter 12-23](https://github.com/nrfconnect/sdk-zephyr/blob/d96769facecaba386b642d2c76c92c7694c81da0/modules/hal_nordic/nrf_802154/sl_opensource/platform/nrf_802154_irq_zephyr.c#L12-L23) |
| UART bounded FIFO copy, RX single-byte restart, TXSTOPPED readiness | [UART driver 1520-1610](https://github.com/nrfconnect/sdk-zephyr/blob/d96769facecaba386b642d2c76c92c7694c81da0/drivers/serial/uart_nrfx_uarte.c#L1520-L1610) |
| DK UART pins | [board pinctrl 6-16](https://github.com/nrfconnect/sdk-zephyr/blob/d96769facecaba386b642d2c76c92c7694c81da0/boards/arm/nrf52840dk_nrf52840/nrf52840dk_nrf52840-pinctrl.dtsi#L6-L16) |
| Board's otherwise unsafe default reset-pin property | [board DTS 142-144](https://github.com/nrfconnect/sdk-zephyr/blob/d96769facecaba386b642d2c76c92c7694c81da0/boards/arm/nrf52840dk_nrf52840/nrf52840dk_nrf52840.dts#L142-L144) |
| Property-to-startup macro translation | [nrfx CMake 130-146](https://github.com/nrfconnect/sdk-zephyr/blob/d96769facecaba386b642d2c76c92c7694c81da0/modules/hal_nordic/nrfx/CMakeLists.txt#L130-L146) |
| Startup NFC/reset UICR writers and emulation branches that must be excluded | [system_nrf52.c 314-367](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/system_nrf52.c#L314-L367) |
| Conditional volatile APPROTECT handling | [system_nrf52_approtect.h 41-58](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/system_nrf52_approtect.h#L41-L58) |

No claim is made that this small diagnostic patch solves Nordic MAC behavior
in general. It intentionally bypasses ACK-wait semantics for this one image,
with a separately named completion callback. Upstream RF timing, receive
turnaround, packet loss/error behavior, DK UART reliability, physical power,
debug protection and full restoration remain separate acceptance work.

## Offline startup and preservation prerequisites

This #55 review concerns the accepted #51 image, not a newly built image or a
writer. It used public sources and the existing external
`nrf-stimulus-sdk/build` artifacts only. No private #54 inputs, current
target values, equipment or programmer operations were accessed. Matching
private reads reported separately by the parent do not establish the
conditions below or authorize programming, reset, RF or restoration.

### Inspected image and primary references

The trusted-workspace `verify_build` command in README was rerun successfully.
The source/patch/tool pins are unchanged: Zephyr SDK 0.16.5, Arm GCC 12.2.0,
174 compiled objects, source radio and source SL. This is **linked-image and
preprocessed-source evidence**, not Cortex-M execution, simulation or a
physical reset/debug-access test.

| Artifact under `build/` | Bytes | SHA-256 |
|---|---:|---|
| `zephyr/zephyr.elf` | 1364020 | `909f5695d5f47e23524370fab20d29db0f116345a9c04d4e6a6b41b1b1c3192d` |
| `zephyr/zephyr.hex` | 158176 | `e19e78b103793fc7741f5d949172caf8ada9e0eb57d0b251af967034e72fc29d` |
| `zephyr/zephyr.map` | 562717 | `03555a86802877f1039132c2b7c7932ac2b551a1b17d36eca03ff44c6bacf7de` |
| `startup.i` | 187205 | `3156ae61a8c1f5ccdd0aabc340c232e2eedb688c03bb22e6c4375b7a0dc6a330` |
| `startup.macros` | 615323 | `29cafb8bc69ce8d7b769603316f233ec27472f28876bf54883e96f5536242bfb` |
| `linked.dis` | 847771 | `142dd3b90c43f5dd453be58049a25f9156ca9417e67e0c90c9fa64b1ff45e4e0` |

Actual flash extent/HEX coverage remains **56,204 bytes**; allocated SRAM is
**17,755 bytes**, extent **17,792 bytes**. The provisional 256 KiB flash /
64 KiB allocated-SRAM guards have not changed. The reset vector is
`0x0000175D`, initial MSP `0x20003D00`.

References used for this review:

| Reference | Exact scope |
|---|---|
| Nordic *nRF52840 Product Specification*, **4413_417 v1.1, 2019-02-28** ([Nordic URL](https://infocenter.nordicsemi.com/pdf/nRF52840_PS_v1.1.pdf), [retrieved Arduino mirror](https://content.arduino.cc/assets/Nano_BLE_MCU-nRF52840_PS_v1.1.pdf)) | Sections 4.2.2 (p.20), 4.3 (pp.24-30), 4.4 (pp.31-36), 4.5 (pp.43-46), 4.8 (pp.50-53), 5.3.6 (pp.69-70), 5.3.7.11 (p.75), 6.3 (pp.107-110). This old revision does **not** establish the later hardware/software APPROTECT variant mapping. |
| Pinned [MDK version](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf.h#L39-L41), **8.60.3** | Actual included startup, register definitions and generated errata predicates, not a separate silicon acceptance report. |
| [MDK configuration 249](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf52_erratas.h#L13133-L13212) and [selected protection handler](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/system_nrf52_approtect.h#L35-L60) | Exact chip-selector predicate and volatile UICR-to-DISABLE copy. |
| [Variant enumerations](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf52840_bitfields.h#L1630-L1649), [UICR encodings](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf52840_bitfields.h#L15853-L15861), [software protection fields](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf52840_bitfields.h#L214-L229) | Different `0xFF` and `0x5A` disable encodings; no inferred marketing-revision mapping. |
| Pinned Nordic [AP-Protect implementation overview](https://github.com/nrfconnect/sdk-nrf/blob/3190fa573ff67bfb745028f203e9d0ea4144a1ce/doc/nrf/security/ap_protect.rst#L18-L80) | Hardware-only versus hardware/software mechanisms, reset/wake re-protection, both disable conditions, and destructive ERASEALL. Its generic configuration wording is not proof that this particular preserved UICR is open. |

The retrieved PDF is 18,277,247 bytes, SHA-256
`c619e336b9c0610663273041f057f2537a65fd408ce0c5b8214a26de2aa88422`.
It and its extracted text remain external under
`nrf-stimulus-sdk/startup-contract-review/`. Extraction used the isolated
`pypdf-5.4.0-py3-none-any.whl`, SHA-256
`db994ab47cadc81057ea1591b90e5b543e2b7ef2d0e31ef41a9bfe763c119dab`;
the parser was loaded from that wheel without installing packages into
the accepted build environment.

**Primary-document gap:** newer Nordic product/errata endpoints returned
HTTP 403; another distributor retrieval timed out. The versioned newer
specification and applicable complete silicon errata were not reviewed.
The inspected MDK predicates and Nordic SDK documentation establish the
conditional behavior below, but do not establish an approved
`INFO.VARIANT`-to-silicon-revision allowlist. Do not substitute alphabetical
variant comparisons, another HAL's implementation, or the MDK's
future-revision default for that missing evidence.

### Actual linked startup and runtime effects

The linked reset entry at `0x175C` calls `z_arm_platform_init` at `0x1E7C`,
which branches to `SystemInit` at `0x6EC8`, **before** Zephyr C initialization
and the application's RF-disarmed control/serial handshake. See the pinned
[reset entry](https://github.com/nrfconnect/sdk-zephyr/blob/d96769facecaba386b642d2c76c92c7694c81da0/arch/arm/core/cortex_m/reset.S#L62-L117).
RF disarming therefore does not disarm startup's debug/power effects.

`NRF52840_XXAA` and `CONFIG_NRF_APPROTECT_USE_UICR` are selected.
`CONFIG_GPIO_AS_PINRESET`, `CONFIG_NFCT_PINS_AS_GPIOS`,
`CONFIG_NRF_APPROTECT_LOCK`, `ENABLE_APPROTECT` and emulation `DEVELOP_IN`
macros are **undefined**, not defined to zero. Actual preprocessed
`SystemInit` contains no NFC/PSELRESET/NRFMDK UICR writer or reset call.
The NRFMDK configuration-249 NVM branch in the shared source is for
nRF52805/52810/52811, not this compilation.

Review of the actual source closure, preprocessed startup, map and linked
instructions found **no retained flash/UICR programming or erase path**:
the bare `nvmc_wait` and `nvmc_config` functions occur in preprocessed text
but are in the map's discarded sections, as is `sys_arch_reboot`.
They are not linked callable routines. No flash/NVMC driver, bootloader
writer or reboot/fatal-reset path is selected. The linked default fatal
handler calls `arch_system_halt` (`0xBFBC`), which loops; it does not reboot
into recovery. This conclusion uses the actual code and discarded-section
evidence, not just the existing audit's named-writer symbol deny-list.

There are important **volatile** effects:

| Actual effect | Consequence |
|---|---|
| At `0x6FFA`, load `UICR.APPROTECT` (`0x10001208`); at `0x7002`, store the full word to `APPROTECT.DISABLE` (`0x40000558`) when configuration 249 is true | Not an NV write, not a FORCEPROTECT write, and not an unconditional debug unlock. No conversion of erased `0xFF` to `0x5A` occurs. |
| `nordicsemi_nrf52_init` at `0x1E24` writes `NVMC.ICACHECNF=1`, `POWER.DCDCEN=1`, `POWER.DCDCEN0=1`, with the selected errata-197 workaround at `0x40000638` | Cache and regulator control, not flash programming. Actual DK power/inductor configuration and preserved REGOUT0 must be compatible; the image is not electrically inert. |
| SystemInit's conditional CLOCK/TEMP/NFCT/CCM/RAM/QSPI errata workarounds remain (36, 66, 98, 103, 115, 120) | Volatile initialization occurs even though application use of some peripherals is disabled. No claim of zero peripheral side effects. |
| Errata 136 checks RESETPIN and writes `~RESETPIN` to write-one-to-clear `POWER.RESETREAS` | Clears other set reset-reason flags while retaining RESETPIN; post-startup RESETREAS is not untouched historical evidence. |

Sources for those effects are the pinned
[SystemInit](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/system_nrf52.c),
[Zephyr SoC init](https://github.com/nrfconnect/sdk-zephyr/blob/d96769facecaba386b642d2c76c92c7694c81da0/soc/arm/nordic_nrf/nrf52/soc.c#L39-L54),
and [DCDC HAL](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/hal/nrf_power.h#L1464-L1509).
The linked NVMC-base reference is the cache-control store, not WEN/ERASE.

### Nonunique silicon/protection decision tables

These are **offline prerequisite classifications**, not decisions about
the privately acquired board. A source-compatible row is not programming
or hardware approval. Values must not be repaired or normalized by writing
UICR to make a row pass.

| Nonunique input | Classification |
|---|---|
| `INFO.PART != 0x00052840`, absent/contradictory geometry, or not the separately established DK/package/power configuration | Reject for this image. Expected geometry is 4096-byte CODEPAGESIZE, 256-page CODESIZE, INFO.FLASH=1024 KiB and INFO.RAM=256 KiB. |
| PART=`0x00052840`; MDK-listed VARIANT `AAAA`, `AAAB`, `AABA`, `AABB`, `AAC0`, `AACA`, `AAD0`, `AAD1`, `AADA`, `AAEA`, `AAF0`, `AAFA`, `BAAA` or `CAAA` | Recognized enumeration only. This review has no authoritative complete revision/errata mapping for these values; **none is an unconditional allowlist entry**. |
| VARIANT=`0xFFFFFFFF`, unlisted, or inconsistent with the independently established silicon class | Unknown: reject pending primary evidence. Do not interpret a numerically later ASCII value as a supported later revision. |

The *actual* configuration-249 selector is not INFO.VARIANT. It reads
nonunique internal FICR words at `0x10000130` and `0x10000134`:

| Selector words | Emitted behavior / review boundary |
|---|---|
| First=`8`, second=`0..4` | No APPROTECT.DISABLE copy. |
| First=`8`, second=`5` | Performs the copy. The linked six-byte table at `0xCEE9` is `00 00 00 00 00 01`. |
| First=`8`, second=`>5` | MDK default performs the copy; **unknown future silicon is not approved**. |
| First not `8` | MDK returns false; not an acceptable fallback for an expected nRF52840. |

Even separately obtaining those nonunique words would prove only which
branch runs, not replace the missing documented silicon/errata mapping.
No such current-target values were obtained in this review.

| Independently established class and relevant field | Source-supported disposition |
|---|---|
| Hardware-only protection; selector skips the copy; UICR.APPROTECT.PALL=`0xFF`; current CTRL-AP.APPROTECTSTATUS=`1` | Compatible with the documented legacy disabled encoding, conditional on the missing revision/errata and board gates. No firmware software-disable action is required for this class. |
| Hardware/software protection; supported selector performs the copy; preserved PALL=`0x5A`; current status=`1` | Compatible with the hardware-disable encoding and this firmware's software-disable write. The core must actually reach SystemInit after a re-protecting reset/wake; debug availability before that point is not guaranteed. |
| Hardware/software protection; PALL=`0xFF`, including all-erased UICR | **No-go for preserved debug access.** This startup copies `0xFF`, not SwDisable=`0x5A`, and never programs HwDisabled into UICR. A presently open AP does not fix this. |
| Any class with PALL=`0x00`, current status=`0`, unreadable/inconsistent status, or an undocumented PALL encoding for that class | Reject; do not invoke recover/ERASEALL or change UICR. `0x5A` is not inferred to be a legacy disable value. |
| Any missing/unknown class or unsupported selector | Unknown/no-go, regardless of successful reads or an apparently favorable PALL byte. |

PALL is bits 7:0. The actual handler copies the **whole** UICR word, so
reserved bits must also be retained and reviewed, not silently masked in
an image or private snapshot. Nordic's hardware/software description
requires both hardware and software disable; its generic
"USE_UICR makes debugging available" prose is not a substitute for those
conditions. A boot failure or interrupted image replacement before the
software-disable write can therefore defeat a reset-based debug recovery
assumption. No non-destructive recovery guarantee follows from an open AP
in the preceding session.

Other preserved/protection fields are independent gates (PS v1.1):

| Field / state | Required interpretation, not a write request |
|---|---|
| UICR.PSELRESET[0/1], offsets `0x200/0x204` | Equal, valid mappings with CONNECT bit 31=`0` and the actual board wiring are needed to rely on pin reset. Disconnected (`1`)/mismatched values do not provide that recovery path. Deleting the DTS property preserves the existing mapping; it does not make reset work. |
| UICR.NFCPINS, offset `0x20C` | Preserve PROTECT bit 0: `1` selects NFC protection, `0` GPIO. Startup does not switch it to GPIO. |
| UICR.DEBUGCTRL, offset `0x210` | CPUNIDEN bits 7:0 and CPUFPBEN bits 15:8 control trace and flash-patch/breakpoint facilities: `0xFF` enabled, `0x00` disabled. Disabled is not the same as global AP protection, but a recovery procedure cannot assume those facilities work. Preserve them; unknown encodings need review. |
| UICR.REGOUT0, offset `0x304` | Preserve VOUT bits 2:0 and reconcile with actual normal/high-voltage board power and this image's DCDC/DCDC_HV enablement. Defined codes `0..5` select 1.8/2.1/2.4/2.7/3.0/3.3 V; `7` defaults to 1.8 V in high-voltage mode, not 3.3 V. Code `6` is not a reviewed defined setting. |
| All other UICR bytes, including NRFFW/NRFHW/CUSTOMER/reserved bytes | Preserve exactly; do not infer unused, bootloader-safe or erasable from this image not referencing them. |
| All eight ACL ADDR/SIZE/PERM triplets (`0x4001E800 + 0x10*n`, offsets 0/4/8) | PS 6.3 specifies debugger read-as-zero on read denial and write-ignored on write denial. Global APPROTECTSTATUS=`1` and matching full reads alone do **not** rule out masked backup bytes or establish flash writeability. Establish applicable ACL state separately before accepting recovery material or a writer. ACL registers are write-once until reset; reset clearing is itself a separately authorized operation. |

The new image does not configure flash ACL permissions. This says nothing
about ACL configured by the original running image, or private protection
snapshot coverage. This audit did not inspect those snapshots.

### Main-flash preservation contract for a future offline plan

PS v1.1 sections 4.2.2 and 4.3 establish 1 MiB main flash at
`0x00000000..0x000FFFFF`, **256 pages of 4096 bytes**, full word-aligned
**32-bit programming**, erased bits equal to one, and programming only
one-to-zero transitions. Byte/halfword writes are not substitutes.
The specified maximum is two writes per word between erases; a conservative
future writer can require a completed erase and at most one submission per
word, never retrying an uncertain attempt or inferring write history from
all-FF readback. Actual controller completion, applicable errata, clock,
voltage and bounded failure handling still require a separate review.

The product specification explicitly requires the **CPU to be halted
before an NVMC operation initiated from the debug system** (4.3, p.24).
The existing read-only, no-halt acquisition contract is therefore not a
writer contract. Replacing low flash and resuming the original PC/context
is not a valid helper startup/recovery method either; reset/entry and
debug-access behavior need an independently authorized design.

For this exact accepted HEX, the parser confirms contiguous, explicitly
present bytes `[0, 0xDB8C)`. A page-preserving overlay would cover page
indices **0..13**, erase envelope `[0, 0xE000)`, and must carry forward
**1,140 original bytes** at `[0xDB8C, 0xE000)` from trustworthy private
recovery material. Pages 14..255 must remain unchanged. This is an
arithmetic/layout result, **not authorization to erase those pages**.

Use the exact HEX byte coverage as the intended overlay. There are **15
alignment-padding bytes** where the raw ELF LOAD contents are zero while
the accepted HEX explicitly contains `0xFF` from the reviewed objcopy
`--gap-fill 0xff` rule; `artifact.compare` checks that distinction.
Do not substitute literal ELF padding, or treat arbitrary sparse-image
holes as an instruction to erase. For any future sparse/unaligned image,
every byte outside explicit approved image coverage within an affected
page, including holes and partial-word bytes, must be merged from the
original page before any erase. Verify the entire reconstructed page,
not only the image prefix, and independently verify the untouched regions.

NVMC "partial page erase" is **time slicing of erasing the whole page**,
not subrange preservation (4.3.7). An incomplete partial erase leaves
page bits undefined; it cannot preserve a tail. Main-page ERASEPAGE is
distinct from ERASEUICR and ERASEALL. Exclude the entire
`0x10001000..0x10001FFF` UICR region from both installation and ordinary
restoration operations; FICR and all non-main-flash load destinations
remain excluded. CTRL-AP ERASEALL destroys flash, UICR and RAM (4.8.2);
it is not a permitted fallback to a failed page-preserving attempt.

**Decision:** static layout and conditional startup behavior are established;
silicon-specific installation/recovery approval is **blocked**. Required
remaining evidence includes authoritative variant/revision and applicable
errata classification, class-correct preserved UICR/protection/ACL state,
actual board power/reset conditions, trustworthy complete private recovery
material, and a separately reviewed/authorized halt, programming,
interruption and restoration scope. This review supplies no live writer,
no reset/protection experiment, no private-data assessment, and no physical
installation, RF or restoration acceptance.
