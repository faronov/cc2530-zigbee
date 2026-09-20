# J-Link reset-preservation preparation

**Offline only.** This directory supplies a small opt-in OpenOCD patch and
execution proof for #51, plus the bounded USB PID correction for #53.
It does not supply an acquisition/programming runner,
authorize adapter access or establish physical preservation/restoration.
The executable and all third-party trees/build products stay outside Git/CI.
No CC2530 or NS51 firmware, ABI, allocation budget or board image changes.

## Command contract

The usable interface is **`jlink preserve_reset on`**, a CONFIG-only command,
default **off**. The intended configuration fragment is:

```tcl
adapter driver jlink
adapter serial <decimal>
transport select swd
adapter speed 1000
reset_config none
jlink preserve_reset on
```

This fragment is **not a safe standalone command to run now**: OpenOCD normally
initializes after parsing configuration. For a no-device parser check, prepend
`noinit` and append `shutdown`, as shown below. Never add `init` to a real-backend
check without a separately approved hardware task.

At initialization, enabled mode requires SWD, an explicit nonempty ASCII
decimal uint32 serial, effective `RESET_NONE` (no leftover reset flags), and
USB-capable libjaylink, **before `jaylink_init`/USB discovery**. Missing selected
USB devices fail without TCP fallback. Initialization skips both SRST and TRST
commands. Reset-hook calls reject before either queue flush. A default-false
adapter flag also rejects core reset requests before cached deassertion can
silently succeed or the SWD dispatcher can discard the rejection.
The internal `adapter_driver` layout changes; use the coherently rebuilt
program, not a replacement object in an older build.

`off` retains upstream behavior, including initialization's reset deassertions
and serial-selected USB-to-TCP fallback. Standard OpenOCD boolean spellings
are accepted by the existing parser; the operator should emit exactly `on`.
The setting cannot be changed after `init`; later `reset_config` changes do
not bypass the retained flag.

The mode is **not an arbitrary-command/write firewall**. The separate
operator must use only its reviewed MEM-AP configuration, without a core
target, flash bank, reset script, target-power command or arbitrary Tcl.
SWD protocol line-reset sequences are not SRST/TRST pin operations and are
not prohibited. GPIO/electrical state, physical device uniqueness and
restoration remain independent contracts.

## Genuine build and offline evidence

Set `REVIEW` to the external workspace used for the reviewed build, for example:

```text
/path/to/external/openocd-preservation-review
```

| Item beneath that root | Meaning |
| --- | --- |
| `build/src/openocd` | Genuine Linux x86-64 executable; only `BUILD_JLINK=1` |
| `library/lib/libjaylink.so.0.2.0` | Genuinely source-built libjaylink 0.3.1 plus the one-line PID `1025` patch (ABI filename is not the package version) |
| `deps/usr/lib/x86_64-linux-gnu/libusb-1.0.so.0.4.0` | Hash-locked Ubuntu libusb runtime |
| `build-evidence.json` | Sources, compiled objects, executable/tool/runtime digests, configure command and log identities |
| `offline-tests/evidence.json` | 58 real-driver synthetic process sequences, outputs, durations, and linked proof identities |
| `usb-discovery-tests/evidence.json` | 131,097 genuine-library cases in 34 confined processes, including the unmodified-library control |
| `operator-libs/` | Regular SONAME copies of the selected real libjaylink/libusb for a single pinned `library_dir` |
| `pre-usb-1025/` | Preserved public pre-correction library and evidence; never private readback data |
| `no-device-checks.json` | Real-backend `--version`, inert config, and invalid-mode parser checks; no `init` |

Final recorded SHA-256 identities:

| Artifact | SHA-256 |
| --- | --- |
| `build/src/openocd` | `1c845481ed064ef54ddd4eb1c3788b39b3e9241ac4d1653052f0e8b50885b704` |
| `library/lib/libjaylink.so.0.2.0` | `4817b000da468bab42eac25b22696650e43cd5b969a25748caddfa6d9b6c9cfc` |
| `deps/usr/lib/x86_64-linux-gnu/libusb-1.0.so.0.4.0` | `751349e3def1808981dc26bbeb746e2d6ff4ca97fc2c8bc8faaa8f46c879057f` |
| Repository `preserve-reset.patch` | `7e58a16de3963b4bebf3930e684188920256c19a67f173da5d724e88091db0a4` |
| Repository `libjaylink-usb-1025.patch` | `5705ecd7d29004f2535f39757a986f0257681c2cadcf67a30d691d95bb0990a4` |

The unstripped executable is 14,778,736 bytes; this is a host tool, not a
firmware resource comparison. The initial accepted 58-sequence run's slowest process
was 0.00813 seconds against its 5-second deadline. The evidence inventory
binds the source files and actual loader-resolved runtime closure,
including host libc, libudev, libcap and loader; host/runtime replacement
requires a new reviewed identity set.

OpenOCD source is `9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c`;
Jim is `a77ef1a6218fad4c928ddbdc03c1aedc41007e70`; libjaylink is the
locked 0.3.1 archive plus the exact PID patch, **not** the unused OpenOCD
libjaylink submodule. The OpenOCD relink remains byte-identical. Previous
selection files pinning the old libjaylink hash must fail closed; this update
does not authorize updating a private selection or retrying acquisition.
Actual compiler: GCC `13.3.0-6ubuntu2~24.04.1`; make 4.3; isolated
autoconf 2.71, automake 1.16.5, libtool 2.4.7, m4 1.4.19 and pkgconf 1.8.1.
The final OpenOCD build enables upstream `--enable-werror`. Jim's independent
build retains two upstream GCC warnings (`jim.c:665` allocation-size range,
`:12104` possibly uninitialized `boolean`); no changes or suppression were
introduced there. The probe/backend are separately compiled with
`-std=c11 -Wall -Wextra -Werror -Wstrict-prototypes`.

The corpus executes the **real executable and real archived driver/core
objects**, not copied policy functions or text markers. It covers default/off
and toggle-back behavior, a real default reset cycle, preserved init/quit,
late CONFIG rejection, transport/reset/serial/USB preflight, serial bounds,
no-match/absence, no TCP fallback, 16 injected initialization failures,
capability/resource failures, cleanup, all CLI assert/deassert combinations
and subsequent reset-config changes. The native probe rejects nine direct
reset-hook combinations with a genuine nonempty SWD queue, plus both core
assert/deassert entry points; switching off then flushes the still-present
64-bit queue, proving rejection did not flush or discard it.

Each of the original 58 synthetic processes replaces the sole adapter DSO
with the test library.
An x86-64 Linux seccomp constructor denies file opening, directory enumeration,
ioctl, sockets/connections, exec and io_uring before OpenOCD main. Fifteen
denied syscall families are independently executed in the ordinary test.
The test library links only libc, never libusb/libjaylink transport code;
unexpected APIs terminate with error. Processes receive only standard
streams and a fixed environment. Each sequence has an unchanged 5-second
deadline. This is **host-tested, linked/static evidence**, not target
simulation, USB compatibility or hardware-observed reset preservation.

The distinct discovery proof uses the **genuine rebuilt libjaylink DSO**,
replacing only libusb with a strict finite descriptor/lifetime model under
the same confinement. It executes two full 16-bit ID sweeps, partitioned
into 4,096-ID processes, 24 lifecycle cases and one genuine old-library
control: 131,097 cases in 34 processes, each with the same 5-second deadline.
The old library rejects `1366:1025` before opening; the corrected one admits
it without admitting any other new PID or vendor on the tested axes.
Open/close/reference/configuration/claim/release counts are checked;
unmodeled APIs and bulk transactions terminate. No device firmware response,
SWD exchange or target observation is supplied, and no hardware I/O runs.
See the [primary evidence and interface assumptions](PROVENANCE.md#narrow-usb-pid-correction-for-53).
The recorded correction run passed all eight explicit tests: worst driver
process 0.01095 seconds, worst discovery process 0.00584 seconds, both against
the unchanged 5-second deadline. The actual generated acquisition prefix was
also parsed with the real executable and corrected runtime, split before its
`init` line and followed by `shutdown`. Only the synthetic serial was supplied;
no selection file, operation directory or readback material was opened.

## Repeat the offline checks

Python 3.9+ ordinary discovery does not download, build or initialize a
programmer. It runs six tests and explicitly skips the two external proofs:

```sh
python3 -m unittest discover -s tools -p test_nrf_openocd.py -v
```

On the trusted Linux x86-64 build host, explicitly select the existing public
workspace to execute all eight tests, including the unchanged 58 driver
sequences and new genuine discovery proof:

```sh
NRF_OPENOCD_WORKSPACE="$REVIEW" \
  python3 -m unittest discover -s tools -p test_nrf_openocd.py -v
python3 tools/nrf_openocd/record_build.py --workspace "$REVIEW"
```

The proof validates the pinned OpenOCD revision, exact patch diff, sole
compiled adapter and ELF dependency shape. `record_build.py` additionally
compares dependency source files to the locked archives plus the one
hash-bound libjaylink exception, verifies downloaded
package digests, and records source/object/runtime identities without running
OpenOCD. The host loader's `--list` mode independently confirms actual library
resolution without entering the program or its constructors.
These are **trusted local build tools**, not a sandbox/certifier for
arbitrary downloaded workspaces, build metadata or hostile ELF files. A
recorded digest binds the vetted local bytes; neither pathname, version nor
self-recorded hash authenticates a replaced compiler/libgcc, dynamic loader,
host kernel or probe firmware. Stale execution reports and stale operator
library copies are rejected. Keep the reviewed environment and identities
bound to any later operator invocation.

The real-backend parser-only check is:

```sh
env -i PATH=/usr/bin:/bin LC_ALL=C HOME=/nonexistent \
  LD_LIBRARY_PATH="$REVIEW/operator-libs" \
  "$REVIEW/build/src/openocd" \
  -c 'noinit; adapter driver jlink; adapter serial 123456789; transport select swd; adapter speed 1000; reset_config none; jlink preserve_reset on; shutdown'
```

`123456789` here is a synthetic parser value, not a selected physical identity.
Do not use the synthetic library or its environment for a real operator.

Build reproduction, genuine setup failures and package extraction are in
[BUILD.md](BUILD.md). Exact source/cleanup citations, licenses and unresolved
physical/identity limits are in [PROVENANCE.md](PROVENANCE.md).
