# Opt-in physical XDATA overlay

**Default OFF.** This build feature preserves the existing 553-byte experiment:
the primary `lg_esl29_rev03/default-tc` image uses **7123 ordinary XDATA bytes,
557 free**, instead of 7676 used and four free. It does not change production C,
instructions apart from relocation operands, ABI, protocol limits or memory
ceilings. It is not a Zigbee production-release or hardware-validation claim.
Other board/key profiles are rejected, not silently built without overlay.

## Normal firmware

Existing commands require only stock SDCC and do not invoke the allocator:

```sh
make BOARD=lg_esl29_rev03 all
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc \
  BUILD=build/ordinary prepare-join-smoke-stack
```

The first command builds the non-networking bootstrap. The second prepares
the ordinary experimental join image with DATA/stack checks; it does not
authorize flashing or change its existing admission requirements.

## Prepare the pinned toolchain

The normal overlay build uses the immutable public
[SDCC ownership release](XDATA_TOOLCHAIN.md), not an experimental worktree
or a reconstructed compiler. Linux x86_64, Python 3.12, GNU Make and a host
C compiler are required. The package contains its own SDCC, preprocessor,
assembler, linker, archiver, headers and model-large libraries.

```sh
make prepare-xdata-toolchain
```

The command verifies the pinned archive and complete extracted package,
then compiles/links a real option-off/on schema-1 probe. Repeat builds reuse
the cache; `XDATA_TOOLCHAIN_OFFLINE=1` prohibits downloads. No system compiler
is installed or replaced. Stock SDCC remains the separate feature-off
dependency. Explicit local compiler development and deliberate cache repair
are documented in [XDATA_TOOLCHAIN.md](XDATA_TOOLCHAIN.md).

The complete simulator gate still needs the isolated debugger-only uCsim
build. Its source-build dependencies on Ubuntu 24.04 are `build-essential`,
Python, `bison`, `flex`, `m4` and `zlib1g-dev`:
```sh
mkdir -p build
curl --fail --location --retry 3 \
  https://deb.debian.org/debian/pool/main/s/sdcc/sdcc_4.2.0+dfsg.orig.tar.xz \
  --output build/sdcc_4.2.0+dfsg.orig.tar.xz
python3 -B tools/prepare_join_simulator.py \
  --archive build/sdcc_4.2.0+dfsg.orig.tar.xz --output build/join-simulator
```

The simulator preparation verifies the exact archive SHA-256
`ebe7bfb0894380cd92798b57fb9de96e6c0b913a02b6854d0a01cd70328c1578`
before extraction. The simulator changes only two debugger null guards,
not CPU execution; the acceptance harness still checks alias/bank behavior.
An existing external simulator can instead be supplied explicitly.

## One build target

```sh
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/overlay \
  prepare-join-smoke-overlay
```

This compiles with `--xdata-ownership`, links the preliminary image, checks
the immutable preliminary/sidecar bindings, runs the unchanged region
allocator, relinks, independently proves the final image and publishes it.
The interface is an explicit target rather than a global Make flag, consistent
with the existing `prepare-join-smoke-*` resource/admission stages.

The publication is:

```text
build/overlay/xdata-overlay/
  .build.lock
  .generations/image-.../
    preliminary/                  compiler outputs and preliminary image
    input-identities.json         immutable input catalog copy
    join-smoke-layout/            final IHX/CDB/map/listings, manifest, header, proof
    receipt.json                  source/toolchain/output content bindings
    build.log
  join-smoke-layout -> .generations/image-.../join-smoke-layout
```

Only the symlink is atomically replaced. Generation paths cannot be renamed
after linking: the actual NoICE LOAD record binds their spelling. Failed or
interrupted generations remain unpublished for diagnosis. Do not use one by
guessing its filename. The historical `join_smoke_unverified.ihx` name is
retained inside a successfully verified generation; the complete receipt and
independent admission, not that filename, determine acceptance.

No build/CI target erases, programs, resets or enables RF on equipment.
Virtual banked IHX is **not** directly suitable for a hardware programmer.

## Rebuilds and existing-image verification

```sh
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/overlay \
  verify-join-smoke-overlay
```

Verification checks the current source/toolchain, every generated artifact,
the immutable final identities, and the independent lifetime/region,
DATA and stack proofs. It does not imply simulator or hardware execution.

Dependencies use **contents, not timestamps**. An unchanged second build,
or merely touching a source/header, checks inputs and outputs and reuses
the same generation. Changed source/header/compiler/allocator/verifier or
contract contents require a new fully verified generation. An incompatible
change fails immutable admission; update neither production catalogs nor
expected semantic results to work around it.

Missing, modified or mixed sidecars, objects, headers, manifests or images
cause a hard failure and invalidate publication. A subsequent explicit build
can create a fresh generation. Concurrent builds of the same output directory
serialize with a file lock; different output roots remain independent.
Changes during generation are rejected before publication.

The version-2 receipt includes release tag/archive/source/manifest identities,
compiler binary/version and actual behavior probe, invoked supporting tools,
headers/runtime libraries, source and tool hashes, all preliminary
and final artifacts, and algorithm/format versions. This prevents accidental
stale mixing; it is not a signed software-supply-chain attestation. Immutable
image/metadata admission and the structural verifier remain separate gates.

## Complete offline acceptance

```sh
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/overlay \
  S51="$PWD/build/join-simulator/sdcc-4.2.0+dfsg/sim/ucsim/s51.src/s51" \
  test-join-smoke-overlay
```

This runs the verified build, a fresh verified adapter observation header,
native/sanitized shallow/deep join and ZCL tests, fresh native/sanitized replay
vectors, repeated baseline/overlay directed calls, the BTR scenario, case2,
a bounded case0 prefix, and **complete successful case0**. There is no
skip-complete-replay option in this release target. The semantic gate requires
READY/serving and the existing stack bound, not a particular incidental step
or peripheral-stop count.

Each run has its own `acceptance-*` directory in the immutable generation.
Logs and `progress.json` can exist after failure; **only successful completion**
writes `acceptance.json`, bound to the image, receipt and simulator hash.
This is an expensive acceptance target, not the ordinary edit/build loop.
Use GitHub Actions for normal full acceptance rather than duplicating it.

The separate CI overlay job checks both feature-off and feature-on primary
builds. It supplements, rather than replaces, all118 existing workers.
Required offline acceptance rejects failures/cancellations/unexpected skips
from either group. Its longer job budget is separate from the unchanged
ordinary worker and per-simulator deadlines.

## Diagnostics and admission policy

| Failure | Action |
| --- | --- |
| Toolchain download/cache/probe failure | Follow [cache recovery](XDATA_TOOLCHAIN.md#cache-and-offline-behavior); no stock-compiler fallback is permitted. |
| `lacks --xdata-ownership` with an override | Fix the explicitly selected development compiler; stock SDCC remains valid only for feature-off builds. |
| Unsupported profile/version | Use the admitted primary profile and matching format4 / algorithm1 / platform1 artifacts. |
| Input or sidecar identity mismatch | Inspect the named expected/actual digest; rebuild from the declared source/toolchain, never copy a sidecar alone. |
| Generated artifact mismatch | Publication is invalidated; preserve the failed generation for diagnosis and request a fresh build. |
| Inputs changed during build/acceptance | Stop concurrent source/tool edits and rerun. A passing old report does not admit the new source. |
| Resource/private-fence/callgraph proof fails | Investigate the real linked change. Do not relax memory ceilings, insert an allowlist or bypass admission. |

Early adoption requires **exact** known-image identities and resources:
7123 XDATA, 36 pools, 553 bytes saved, 253445 populated CODE, 32714 common
CODE, 51 physical DATA, 10 OSEG, 88 BSEG bits, 45 stack bytes and bank depth8.
These immutable-artifact gates are stricter than the unchanged platform
ceilings. Merely fitting below `0x1e00` does not admit a regression.

The proof retains the closed foreground callgraph, no unexpected ISR/reentry
or computed edge, valid C-object accesses, compiler metadata trust root and
explicit CC2530 private-fence/region contracts. No IRAM alias bytes, NV pages,
dead-function removal or protocol-buffer reduction contribute to the saving.
See the [multi-pool study](XDATA_MULTIPOOL_STUDY.md) for the design evidence.
Current acceptance results and remaining gates are recorded in the
[productionization report](XDATA_MULTIPOOL_PRODUCTIONIZATION.md).
