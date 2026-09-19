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
| hal_nordic nrfx/MDK startup | Per-file BSD-3-Clause |
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
