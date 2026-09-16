# Validation and evidence

## Evidence levels

Use these terms consistently in documentation and pull requests:

| Level | Meaning |
| --- | --- |
| Host-tested | C/Python behavior checked on the development computer |
| Image-checked | Actual linked firmware parsed for checksums, bounds, symbols and memory layout |
| Simulated | Linked 8051 instructions executed in a specified simulator/model |
| Hardware-observed | A specified board/firmware produced an observed physical result |
| Interoperability-tested | A documented coordinator configuration passed a defined protocol scenario |

None of these silently implies the next level. A screenshot is not a network
test, and an interview is not proof of reliable SED behavior.

## M0 coverage

The current build must cover:

- Strict host compilation and status/board tests.
- Intel HEX integrity, image equivalence and memory bounds.
- Ordinary/status/IRAM-alias allocation checks, including negative cases.
- Bounded execution of the linked image with the CC2530 IRAM alias modeled.
- Explicit reporting of the simulator's limitations.

The generic simulator does not emulate the RF subsystem, analog behavior,
physical supply rails or a display. M0 CI is not a silicon test.

## Partial M1 fixture coverage

`IMAGE=debug_fixture` adds a separate target image for both boards, covered by
the same HEX/BIN, memory/alias and board-startup checks as M0. It also checks:

- The versioned 16-byte state within ordinary XDATA, without expanding status.
- All 256 seed values, counter wrap, reinitialization and no extra host MMIO.
- Exact probe instruction bytes and distinct linked code locations, including
  rejection of missing symbols, changed operands and a stop label on RET.
- Actual nested calls, known registers and a NOP moving PC exactly one byte
  without changing the captured memory/registers.
- 257 linked-image cycles through real returns, without stack leakage,
  writes outside allocated XDATA or changes to immutable M0 status fields.

These are **host-tested, image-checked and simulated** results only. They do
not test the four physical breakpoint comparators, USB/debug commands or a
host frontend's register-preserving reads. The
[fixture contract](DEBUGGING.md#implemented-target-fixture) provides the ABI,
golden state and resource accounting. All M1 hardware gates remain open.

## Partial M1 host transport coverage

Synthetic backends cover explicit USB selection and claiming, permission
denial before I/O, exact diagnostic requests, malformed/short replies, partial
writes, disconnect/errors at each phase, one shared operation deadline and
terminal failure/cleanup states. CLI errors must emit no success JSON.
Unknown targets, unsupported descriptor layouts and active kernel drivers are
refused rather than reset, reconfigured or silently accepted.

`tools/test_m1_transport.py` and the facade tests in `tools/test_m1_usb.py`
require only the standard library. With the pinned optional PyUSB installed,
additional tests exercise its real resource manager with a synthetic driver,
including failed/interrupted claiming and release-error propagation. No test
calls real USB discovery or opens libusb hardware. CI runs both the
dependency-free and optional test paths. This is **host-tested** evidence only,
not USB/CC2530 simulation or
hardware-observed debugger behavior. It adds no firmware memory consumption.

The CPU-control tests additionally cover separate permissions, locked/erasing,
unstable-clock and power-mode rejection, halted preconditions, invalid
postconditions, undefined HALT replies, breakpoint races, immediate
breakpoints after RESUME, and all 256 STEP accumulator / GET_BM raw values.
Precheck/command/postcheck share one deadline and never retry a possibly
executed command. Config reads check lock state rather than issuing disabled
commands on a locked target.

Reset tests separately cover denied permissions before I/O, exact vendor-OUT
reset-into-debug fields, integer zero-length completion, refreshed adapter
family and halted/awake postconditions. Every transfer boundary, timeout,
interruption and cleanup error is covered without a retry, fallback HALT or
reset into normal execution. Initial attach tests cover the distinct access
policy, blocked target operations before readiness, exact `C5`/`C8`/`C9`
ordering and metadata layout, invalid descriptor revisions, partial writes
and failures at every preparation boundary. Successful attach does not grant
CPU-control permission or trigger resume on close. This does not prove reset
timing or initial attach on physical hardware.

Offline image tests cover board/image/compiler/hash mismatches, supported
CODE/XDATA/SFR symbol classification, conflicting map/CDB spaces/addresses, missing
symbols and non-executable breakpoint targets. Synthetic parameter vectors
cover all four breakpoint slots and eight bank-bit values, not real
comparators. M0/M1 decoders reject invalid ABI/phase/board/guard/checkpoint
records and oversized snapshots. The alias-aware image test also exercises
symbol lookup and decodes actual simulated status records; this links the
offline reader to the compiled fixture without creating USB hardware evidence.
Source-location tests preserve multiple mappings, reject malformed/conflicting
records and addresses outside the image, and reject missing exact matches
rather than guessing a nearby line. Both board/image builds check main's
source records; the fixture also checks the actual declaration lines and
linked addresses of all four stage functions. These are offline source
metadata checks, not source stepping through a hardware debugger.

## Future protocol tests

The [standalone legacy MAC codec](MAC.md) is already covered by original
golden/layout/negative vectors on host and an isolated SDCC/uCsim image,
plus all 65,536 FCF values and exact-sized buffer cases on the host. This is
syntax/serialization evidence, not CRC, authentication, radio or MAC operation.
Its test executable is not a board firmware image or CI upload artifact.

| Area | Required cases before the corresponding milestone closes |
| --- | --- |
| Encoders/parsers | Golden wire bytes, every boundary length, invalid/truncated/reserved fields, explicit byte order |
| MAC | ACK/no-ACK, retry/backoff, filtering, RX overflow, association failure, queue pressure |
| Security | Public vectors, wrong key/MIC/nonce, replay, key sequence changes, authenticated error handling |
| Persistence | Reset at every write boundary, corrupt records, generation wrap, counter reservation and wear |
| NWK/APS | Join failure/success, rejoin, changed parent, leave, transaction timeout, duplicate handling |
| ZDO/ZCL | Interview, mandatory responses, unsupported-service fallback, full Mgmt Leave/owned-source Mgmt Bind, omitted Mgmt LQI, bind/unbind, read/report/configuration |
| Sleep | Pending downlink, fast polling, expired parent state, restart, key update and wake deadlines |
| Application | Missing sensor, stuck I2C, stuck BUSY, display timeout, concurrent radio/reporting |

The M6 application uses explicitly labeled, deterministic synthetic
measurements to test transport/reporting before a physical sensor is selected.
That evidence must not be described as sensor accuracy or battery measurement.
Physical sources and their errors/calibration enter at M8.

Persisted-resume cases include known and unknown parent information, immediate
keepalive selection/renegotiation, an absent parent and bounded recovery.

Test vectors committed to the repository are synthetic or publicly licensed.
Do not use real network keys, install codes, device factory records or raw
personal captures as convenient fixtures.

## Hardware progression

1. Non-RF debugger fixture, using the exact board profile.
2. Isolated radio/MAC experiments with an independent sniffer.
3. Authenticated receiver-on ED with a test coordinator.
4. Interview/reporting/restart/leave and parent-loss scenarios.
5. SED behavior and measurements with debugger influence separated.
6. Concurrent sensor/display operation.

Before flashing, preserve and verify recovery backups and confirm the exact
target/image. Never reconnect a display flex under power. Firmware must not
guess voltage/LUT settings for an unidentified panel.

A hardware record should name the board revision, panel/sensor if relevant,
firmware commit/hash, toolchain, test scenario, expected/observed result and
limitations. Keep unique identifiers, keys, private captures and raw dumps
outside public Git and Actions artifacts.

## Interoperability matrix

The following are **planned test slots, not claims of support**:

| Environment | Awake ED | SED | Restart/rejoin | Bind/report/leave |
| --- | --- | --- | --- | --- |
| Zigbee2MQTT with a documented adapter/firmware | Not tested | Not tested | Not tested | Not tested |
| ZHA with a documented adapter/firmware | Not tested | Not tested | Not tested | Not tested |
| Additional independent coordinator stack | Not tested | Not tested | Not tested | Not tested |

Track software and adapter versions in actual test records. Testing two user
interfaces with the same coordinator stack is useful, but is not the same as
testing two independent radio/stack implementations.

For M7, record at least 12-hour and 72-hour continuous runs and 100 controlled
restart/rejoin cycles. Record polling/reporting rates, current limits, allowed
latencies and timing margins before the run; do not invent pass limits after
seeing measurements.

## CI and release boundary

Hosted CI builds/tests without physical devices or repository secrets.
The CI matrix covers both non-RF images (`bringup`, `debug_fixture`) on both
boards. Artifacts contain only their generated firmware, symbols and build
metadata. Pull requests must not use privileged `pull_request_target` execution
to build untrusted source.

CI success means the declared automated checks passed. Experimental releases
must separately list their completed milestones, known limitations, exact
build inputs and hardware/interoperability evidence.
