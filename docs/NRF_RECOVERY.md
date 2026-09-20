# Nordic recovery preparation

**Controlled active test-node preparation may continue**, alongside passive
sniffing. Preparation and artifact reports do not authorize installation or
RF activity and do not bypass the separate preservation/recovery gates.
This work is not a prerequisite for unrelated CC2530 development. The
existing sniffer firmware has not been replaced.

[`tools/nrf_recovery.py`](../tools/nrf_recovery.py) is an original, hardware-free
preparation tool for #51. It compares two sets of private raw files, **not two
devices or authenticated acquisition transcripts**. It neither collects a backup
nor flashes, resets, unlocks, resumes or restores a device. No Nordic SDK,
OpenOCD, USB backend or serial package is imported.

## Inputs and exact scope

Each supplied directory must already be user-owned mode `0700`, outside the
repository, with no symlink path components. It must contain:

| Fixed name | Assumed nRF52840 region | Exact bytes |
| --- | --- | ---: |
| `main-flash.bin` | `00000000..000FFFFF` | 1,048,576 |
| `uicr.bin` | `10001000..10001FFF` | 4,096 |

Files must be regular, user-owned, single-link, mode `0400` or `0600`.
Truncation, extra bytes, different contents, aliases, unsafe permissions and
changes detected while opening/reading reject the comparison. Input files are
not modified. The operator must exclusively own the artifact directories.
FICR, factory identity and the onboard debugger firmware are not restore images
and are not inputs to this tool.

The geometry is an **assumption for an nRF52840**, not device detection.
Agreement is byte-for-byte across each complete region. Distinct files do not
prove independent hardware reads: copied or fabricated files can also agree.
Even matching all-FF files establish neither usable firmware nor recovery.

## Usage and private result

With existing private directories, replace these placeholder paths:

```sh
python3 -B tools/nrf_recovery.py \
  --first /path/to/private/nrf/read-01 \
  --second /path/to/private/nrf/read-02 \
  --report /path/to/private/nrf/agreement-01.json
```

The new report is created exclusively as a single-link `0600` ASCII JSON file,
outside Git; an existing destination is never overwritten. It contains region
sizes/addresses and private artifact hashes, not dump contents or absolute
paths. Console output contains neither private hashes nor paths. A report-write
or durability failure returns an error, not a success message.

Schema `nrf52840-artifact-agreement-v1` explicitly sets physical origin,
independent acquisition, firmware execution, debug access, restoration and
programming authorization to **false**. No CLI option can promote those claims.
Do not upload the report, source files or captures to Git/CI.

## Offline page-overlay report

[`tools/nrf_overlay.py`](../tools/nrf_overlay.py) completes the artifact-only
step in #58. It takes the same two full capture directories plus privately
staged ELF/HEX files. All six input files must be distinct and satisfy the
private-path rules above. ELF and HEX input limits are 16 MiB and 4 MiB;
the new report is limited to 128 KiB.

```sh
python3 -B tools/nrf_overlay.py \
  --first /path/to/private/nrf/read-01 \
  --second /path/to/private/nrf/read-02 \
  --elf /path/to/private/nrf/image/ns51.elf \
  --hex /path/to/private/nrf/image/ns51.hex \
  --report /path/to/private/nrf/overlay-01.json
```

The existing strict [artifact comparison](../tools/nrf_stimulus/artifact.py)
checks ELF/HEX agreement, vectors and the helper's 256-KiB flash/64-KiB SRAM
profile. Canonical explicit HEX bytes, including its checked FF gap padding,
take precedence in an **in-memory** copy of the baseline; ELF LOAD padding
does not substitute for those bytes. Every byte outside image coverage must
remain unchanged, including partial-page tails and unaffected pages. UICR
is compared but excluded from the overlay.

Schema `nrf52840-artifact-overlay-v1` records per-page addresses, covered/
preserved/changed counts and before/overlay hashes. Covered pages are not
erase requests, and need not have changed bytes. No binary payload, command,
programmer process, SDK build or device operation is produced. Geometry is
assumed, not detected. Physical origin, independent acquisition, ACL
readability, atomicity, usable recovery material, silicon compatibility,
startup, execution, debug access, restoration and programming authorization
remain **false** even when actual captured files are supplied.

Inputs and directory bindings are rechecked through report completion.
The exclusive `0600` report is flushed, fsynced and read back. **Accept only
an exit-zero invocation**, not the mere presence of a file: failed/interrupted
attempts confer no validity, and uncertain cleanup/durability is an explicit
error. A replaced output is not deleted as though it were the created report.
Reports and their hashes remain private.

The parent separately ran this offline CLI on the retained #57 capture
pairs and accepted public image, then independently recomputed every page's
counts/hashes and all non-image preservation. It measured **14 pages (0..13),
56,204 image bytes, 1,140 preserved final-page bytes and 242 unaffected pages**.
UICR remained excluded and unchanged. The report and durable acceptance
record stayed outside Git; no device was accessed. This is artifact arithmetic,
not new hardware observation or completion of #55.

## Remaining manual gates

The [SRAM-only helper profile](../tools/nrf_stimulus/README.md#optional-sram-only-profile)
is an alternative to this nonvolatile overlay/programming route. It does
not consume an overlay report or authorize flash writes. Its accepted
artifact has only SRAM loads, but loading it would overwrite volatile
sniffer state: quiescent CPU/peripheral handoff and controlled return to
the original firmware still need review. The separate
[checked handoff preparation](../tools/nrf_stimulus/README.md#checked-volatile-handoff-preparation)
is host-tested and can end at the halted original reset vector in its
synthetic backend. It is not a live loader, unchanged-NV proof or restored
sniffer observation. No such hardware activity is implemented by these
recovery tools; their read-only acquisition API is unchanged.

Before any future programming, a separately reviewed private operation still
needs fresh target/geometry/protection binding, genuinely independent complete
recovery reads, exact modified/excluded regions, and a usable restoration path.
Current access protection, bootloader/settings regions and UICR cannot be
inferred from a historical official-HEX match. Do not use unlock/recover or
mass erase to bypass inability to preserve the baseline.

Restoration requires new physical readback against that baseline and separately
authorized sniffer startup/channel verification. Running this same comparison
on files labelled "restored" does not establish their physical origin.
The temporary application's absent UICR load records also cannot, by itself,
exclude runtime UICR writes or guarantee debug access.

**Host-tested only:** synthetic complete-size files cover extent boundaries,
content disagreement, private paths/permissions, aliases, replacement/mutation,
short reads, report creation/errors and the deliberately limited result shape.
The shared private-file helper preserves the existing passive-RX runner's
public `private_capture()` entry point. No actual backup or hardware restoration
was performed by these tests.

## Explicit read-only acquisition operator

[`tools/nrf_acquire.py`](../tools/nrf_acquire.py) prepares the next #51
preservation step. Its default invocation is **offline**: it validates a
private probe/tool selection and an empty operation directory, without
starting OpenOCD or consuming an attempt. Only `--execute-read` starts the
selected external tool. Builds, tests and CI never use that option against
a real tool/device.

This requires the separately reviewed
[OpenOCD preservation mode](../tools/nrf_openocd/README.md), not stock OpenOCD.
The pinned upstream J-Link driver otherwise unconditionally deasserts reset
during initialization and can fall back from USB to TCP discovery. The
generated configuration explicitly requests `jlink preserve_reset on`, USB
serial selection, SWD and `reset_config none`; an unmodified tool rejects
the unknown mode **before `init`**. The mode is not a firewall for arbitrary
commands. Use only the reviewed operator and tool/runtime combination.

The operator creates only a deferred-examination `mem_ap` target, no
`cortex_m` target, flash bank, upstream target script, work area or server.
It issues no halt, resume, CPU reset, memory write, erase, unlock or recover
command. The `mem_ap` model's state is not evidence of the actual CPU state.
SWD line initialization, debug power requests, AP CSW/TAR changes and the
upstream SWD-to-JTAG shutdown sequence still occur; this is **not** a promise
of unchanged debug registers, power, RF or firmware behavior. The firmware
can continue running and changing memory. Ordinary OpenOCD wire-level
WAIT/error handling and reconnect behavior remain; there is no operator-level
retry, new process, target-state restoration or fallback after failure.

Before MEM-AP examination, CTRL-AP identity and disabled access protection
must be readable. Three fresh snapshots surround two full read passes.
They bind CTRL-AP/MEM-AP values, FICR page geometry, DEVICEID and INFO words,
plus all eight ACL ADDR/SIZE/PERM triplets. Only PERM `0` or `2` is accepted:
read denial or unknown permission bits fail even for an apparently inactive
region. Write-only protection is recorded, not interpreted as permission
to write. No reserved ACL word is read and no ACL register is written.
The supported geometry is exactly nRF52840, 4096-byte pages, 256 pages,
1024-KiB flash and 256-KiB RAM; unspecified variant/identity values fail.
The raw variant/package are recorded, not treated as a reviewed programming
or protection policy. Each pass invokes real `dump_image` separately for
the whole main flash and UICR. The second pass is not a copied file or a
cached CPU-register view. Both passes use one selected debug connection,
not independent reconnects or power cycles.

The four fixed output files must pass the original exact private-file
comparison. Changed identity/protection, short data, disagreement, tool
errors/warnings, malformed or incomplete observations, timeout or output
overflow prevent a successful result. All outputs are `0600` in `0700`
directories outside Git. The child receives already-open directory/script
descriptors through Linux `/proc/self/fd`, not an interpolated private path.
A durable exclusive `consumed.json` precedes device access; a failed attempt
is retained and cannot be rerun in that directory. The process has one
180-second deadline and a 1-MiB captured-output cap.

The user-owned private selection JSON has exactly these fields; replace the
placeholder path/digest and use the explicitly selected probe's actual serial
only in this private file, never in Git, a command line or a public report:

```json
{
  "schema": "nrf52840-read-selection-v1",
  "probe_serial": "123456789",
  "openocd": "/path/to/reviewed/external/openocd",
  "openocd_sha256": "<reviewed executable SHA256>",
  "library_dir": null,
  "libraries": {}
}
```

If the reviewed build requires private runtime libraries, `library_dir` is
their absolute directory and `libraries` maps the selected `lib*.so` SONAME
filenames to their vetted SHA256 values. Both must be supplied together.
Executable and named library bytes are checked; inherited loader/Tcl/OpenOCD
overrides are refused. The child receives only fixed `PATH`/`LC_ALL`/`HOME` and the
selected library directory, not the rest of the caller's environment.
These user-supplied pins are **not authentication or a certificate for an
untrusted executable**. The operator requires a trusted,
reviewed local build, host/runtime dependency closure, exclusively owned
directories and no concurrent replacement of tools/files. It does not load
arbitrary downloaded build metadata safely.

Default, no-device preparation:

```sh
python3 -B tools/nrf_acquire.py \
  --selection /path/to/private/nrf/selection.json \
  --operation /path/to/private/nrf/new-read-operation
```

Only during the separately selected manual device activity, the same command
with `--execute-read` performs the single read attempt. It produces two
capture directories, private tool log/script/observations and `readback.json`.
The `nrf52840-readback-v2` report records **trusted-local-tool-reported reads**, not authenticated
physical origin. Its nested artifact agreement retains the original false
physical-origin claims. Successful readback does not verify firmware
execution, restoration, future debug access or authorize programming.
Matching live reads are not an atomic snapshot of a running device, nor
proof that its flash cannot change afterward.
The `NS51_READ_V2` transcript requires exactly 36 words per snapshot; v1
evidence without ACL state is rejected, not silently upgraded. The report
sets `acl_read_checks_passed=true`, but `atomic_snapshot_verified=false` and
`recovery_material_verified=false`. Sampled ACL agreement does not exclude
intervening reset/reconfiguration or certify future restoration.

The ordinary focused check is:

```sh
PYTHONPATH=tools python3 -B -m unittest test_nrf_acquire test_nrf_recovery -v
```

Tcl 8.6 executes the actual generated script against an original synthetic
backend with no hardware path. It checks all 51 operation/finalization
failure positions, protection/geometry/identity changes, malformed responses,
complete four-region flow, private artifacts, process/output limits and
non-retry behavior. All ACL slots, known read/write permissions, unknown
permission bits and changes to each triplet field are covered. This is
host evidence, not SWD or restoration acceptance.

The same 16 acquisition tests also passed manually with the genuine pinned
Jim interpreter built alongside OpenOCD, still using only the synthetic
backend. The real OpenOCD executable accepted the generated MEM-AP
configuration/procedure prefix, ending **before** the explicit `init` and
followed by `shutdown`. `noinit` alone does not make the complete script safe:
it still contains an explicit later `init`. Neither compatibility check
opened a device or substitutes for the separately scoped #52 readback.

### Read-path source contracts

The target/read-path review pins OpenOCD 0.12.0,
`9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c`; source stays external.
These are implementation facts, not observations of a physical board:

| Boundary | Reviewed source |
| --- | --- |
| Initial target examination failure is logged, not sufficient to fail `init`; deferred examination and explicit read checks are required | [`openocd.c:109-181`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/openocd.c#L109-L181) |
| MEM-AP examination/read path, dummy CPU-control state and no real register view | [`mem_ap.c:59-155`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/target/mem_ap.c#L59-L155), [`mem_ap.c:248-278`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/target/mem_ap.c#L248-L278) |
| Debug power/error and AP configuration writes, not a passive electrical attachment | [`arm_adi_v5.c:675-846`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/target/arm_adi_v5.c#L675-L846) |
| SRST-on-connect only with the separately excluded reset flag; SWD shutdown mode sequence | [`adi_v5_swd.c:397-407`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/target/adi_v5_swd.c#L397-L407), [`606-635`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/target/adi_v5_swd.c#L606-L635) |
| Separate bounded memory-read calls per dump; host files still require exact-size/agreement checking | [`target.c:3743-3802`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/target/target.c#L3743-L3802), [`4601-4745`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/target/target.c#L4601-L4745) |
| CTRL-AP identity/status predicate; the separate destructive recovery procedure is deliberately not imported | [`nrf52.cfg:53-81`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/tcl/target/nrf52.cfg#L53-L81) |
| Vendor FICR offsets, part/variant and memory-size definitions | [`nrf52840.h:893-904`](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf52840.h#L893-L904), [`nrf52840_bitfields.h:1567-1684`](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf52840_bitfields.h#L1567-L1684) |
| Eight ACL triplets, READ bit 2 and WRITE bit 1; debugger read-as-zero on read denial | [`nrf52840.h:604-613`](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf52840.h#L604-L613), [`2357-2360`](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf52840.h#L2357-L2360), [`nrf52840_bitfields.h:199-209`](https://github.com/zephyrproject-rtos/hal_nordic/blob/5470822384781624efb2fda28cbc6a895a227677/nrfx/mdk/nrf52840_bitfields.h#L199-L209), [Nordic PS 4413_417 v1.1 section 6.3, pp.107-110](../tools/nrf_stimulus/PROVENANCE.md#offline-startup-and-preservation-prerequisites) |

Acquiring and comparing these files is still **not an executable restoration
plan or a programming scope**. A later writer must bind fresh actual
silicon/protection state, preserve UICR and every non-image flash byte, define
the exact erased pages and partial-page tail restoration, and verify complete
physical readback before any separately authorized startup. No mass erase or
unlock may replace that review. No actual device was read by the offline
work recorded here.

## 2026-09-20 manual discovery failure

The separate #52 attempt used the published `a073f9c` operator and the
then-recorded programmer/runtime identities against the selected J2 debug
connection. It failed with `Error: No J-Link device found`. Both main-flash
files, both UICR files and the observation file remained empty. The private
consumed marker and failure log were retained; no successful `readback.json`
was produced. There was no process retry, TCP fallback, unlock or recovery.

Subsequent inspection used only existing private artifacts, cached Linux USB
metadata and public source, not another programmer connection. The cached
nonunique descriptor class is SEGGER `1366:1025`, product `J-Link`.
The unpatched, hash-locked libjaylink 0.3.1 `libjaylink/discovery_usb.c` PID table does not
contain `1025`; its `probe_device()` rejects unsupported PIDs before opening
them. That source limitation explains why cached host visibility and adequate
USB-node permissions did not produce a selected programmer device.

This is a **host/programmer-observed discovery failure**, not a fresh
nRF52840 part, geometry, protection or memory observation. No physical
backup, restored firmware, electrical reset preservation or programming
permission was established. A separately reviewed narrow programmer
compatibility correction is required before any new manual operation;
the consumed attempt must not be cleared or rerun. Private selectors,
raw files and logs remain outside Git/CI.

### Offline compatibility correction

The separate #53 correction adds only PID `1025` to that exact library.
Its [primary upstream basis and retained interface assumptions](../tools/nrf_openocd/PROVENANCE.md#narrow-usb-pid-correction-for-53)
are explicit; there is no vendor wildcard, broader library upgrade, changed
serial filter or new reset/transport operation. The corrected runtime has a
new library hash, so an old private selection must reject rather than silently
use different bytes.

Independent integration passed 39 combined tests, retaining all 58 confined
real OpenOCD driver sequences and adding 131,097 genuine-library discovery
cases in 34 confined processes. The original library remains the rejecting
control. Source/archive/patch/object/runtime checks and the actual generated
configuration prefix passed with no target initialization. This removes the
known software filter only; physical interface compatibility, target identity,
protection and full recovery reads remain unverified. The correction neither
reuses the consumed operation nor authorizes a new device action by itself.

## 2026-09-20 corrected-runtime manual readback

The new #54 scope used published `a3d2064`, the unchanged reviewed OpenOCD
executable and the corrected, pinned libjaylink runtime. Before access,
cached host metadata confirmed the unchanged unique selector, active
configuration/alternate setting, interface-number/index agreement, matching
vendor interface, two bulk IN/OUT endpoints and adequate permissions without
a bound kernel driver. This preflight opened no USB handle and was not
treated as target identity or physical compatibility proof.

A new private selection and empty operation directory were used. The original
selection, failed operation and consumed marker remain retained. The new
single programmer process completed within the unchanged 180-second bound,
without reported errors/warnings, process retry or TCP fallback. Its three
CTRL-AP/FICR snapshots matched and accepted nRF52840 with 4096-byte pages,
256 pages, 1024-KiB flash, 256-KiB RAM and the required global access-protection
status. Raw device identity, variant and configuration words remain private.

| Region | Each complete read | Separate reads | Result |
| --- | ---: | ---: | --- |
| Main flash | 1,048,576 bytes | 2 | Full byte agreement |
| UICR | 4,096 bytes | 2 | Full byte agreement |

The parent independently rechecked complete file agreement, all three
snapshots, script digest, selected runtime and the report's explicit false
authorization/restoration claims after acquisition. The private report and
separate acceptance record were durably retained alongside all four files,
with `0600` files and `0700` directories outside Git/CI.

This is **manual hardware readback reported by the trusted local tool**, not
synthetic test output, authenticated physical origin or an atomic snapshot.
No halt/resume/reset, target-memory write, flash erase, unlock, recovery,
sniffer replacement or RF stimulus command was requested. Debug power,
AP configuration and ordinary wire/USB cleanup remain the reviewed side
effects; electrical reset preservation was not measured. Firmware execution,
future debug access and actual restoration remain unverified.

**Qualification from #55:** these full-extent matching files exist, but v1
did not capture ACL state. Nordic PS section 6.3 permits debugger
read-as-zero for ACL-protected flash even with global APPROTECT open.
Usable, unmasked recovery material is therefore **not yet verified**.
No actual ACL denial or damaged data is inferred. The old files/report
remain unchanged, with a separate private qualification record appended.

The #56 v2 correction now adds ACL samples and strict permission checks;
it cannot retroactively qualify the v1 operation. Its independent 41-test
integration, all 16 tests under genuine Jim and the real noinit-only
configuration prefix pass **offline**, with no new hardware attempt.
Installing the temporary image still requires actual silicon/UICR/ACL/
protection policy review, trustworthy recovery material, an exact
page-preserving write/restoration scope and fresh physical checks.
Neither version authorizes programming or closes #40/#50.

## 2026-09-20 ACL-qualified manual readback

The new #57 scope used published `73d409b`, the same reviewed programmer/
runtime pins and a new private selection/operation. Cached selector,
descriptor, active interface and permissions were rechecked without opening
a USB handle. Neither earlier consumed operation nor its evidence was reused
or modified.

The single v2 process completed both full flash/UICR passes. All 36 words
agreed across the three snapshots, including the eight ACL triplets; every
sampled PERM passed the conservative read-access predicate. The parent
independently revalidated the v2 schema/flags, private script/selection,
observations and complete byte agreement. The original 12-word binding
and **all bytes of the previous v1 read files also matched**.
The new report and separate acceptance record are durable private files,
not repository or CI artifacts.

This is **manual hardware readback with sampled ACL checks**, not a
retroactive observation of v1's ACL state. It provides the missing sampled
read-access qualification, but does not prove atomicity or exclude
intervening reset/reconfiguration. `recovery_material_verified`,
`atomic_snapshot_verified`, `restoration_verified` and
`authorizes_programming` remain false. No protection change, reset,
halt/resume, target-memory write, erase, unlock, recovery or RF stimulus
command was requested. Silicon-specific startup/debug-access policy and
fresh quiescent recovery checks remain separate prerequisites to any writer.

## Bounded protection-class source evidence

The [#55 source ledger](../tools/nrf_stimulus/PROVENANCE.md#nonunique-siliconprotection-decision-tables)
now includes Nordic PS 4413_417 v1.11 (2024-10-01). Its explicit build-code
encoding/applicability supports hardware-only protection for `AAC0`, `AAD0`,
`AAD1`, and hardware/software protection for `AAF0`. Other engineering,
unlisted or uncovered variants are not inferred to be supported.

This is public-source classification, not a decision about a captured device.
It does not establish named silicon revision/applicable errata, mapping to
the actual MDK selector, board power/reset wiring or usable restoration.
For the hardware/software class, legacy erased `PALL=0xFF` is not the
documented `0x5A` hardware-plus-software disable sequence. Reset behavior
also depends on the documented reset/debug-mode conditions, not simply
whether any reset occurred. No UICR/protection modification is authorized.
