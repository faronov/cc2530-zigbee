# Pinned XDATA ownership toolchain

1. SDCC fork URL: https://github.com/faronov/sdcc
2. Base SDCC version: 4.2.0 #13081, exact Debian DFSG source.
3. Fork release tag: `v4.2.0-xdata-ownership.1`.
4. Source commit: `3ae36e48f54f787c095d5ffc77a964e59cf85652`.
5. Linux release asset: `sdcc-4.2.0-xdata-ownership.1-linux-x86_64.tar.xz`.
6. Release archive SHA256:
   `1d326c551a94149be9904da5335f1f82552a43f10bfad33a3f54cb0c68c92794`.
7. sdcc executable SHA256:
   `48dac1fd659a36a74adfcb2166043ed33fc850deb7229e3684d414feb5c988fd`.
8. XDATA ownership schema: 1 only.
9. Compiler CI: PASS, [run 37132323644](https://github.com/faronov/sdcc/actions/runs/37132323644).
10. Packaged-toolchain self-test: PASS, including both extracted packages in
    [tag release run 37132681995](https://github.com/faronov/sdcc/actions/runs/37132681995).
11. cc2530-zigbee clean download/build: download/probe/offline reuse PASS;
    final release-backed overlay build pending.
12. Overlay final l_XSEG: pending; exact required value remains 7123.
13. Overlay full simulator release gate: NOT RUN for this integration yet.
14. Feature-off build unchanged: no source/flags/default dependency changes;
    new full-CI confirmation pending.
15. Source reproduction: PASS, two clean tag-source packages byte-identical
    in the declared Ubuntu 24.04 release environment.
16. GPL/source distribution: COMPLETE conventional source/binary distribution;
    component licensing is preserved, not a legal opinion.

## Fork provenance and patch history

The actual GitHub fork's parent is `swegener/sdcc`, an upstream SVN mirror.
SVN r13081 is commit `69e89aa1bab8b96494c6d4d351d0e2d601ec5bb8`.
The next base commit, `c324be6c05a410d87b03f64eb9e7ee5815226fc7`, exactly
reproduces all 5628 regular source files and modes in
`sdcc_4.2.0+dfsg.orig.tar.xz`, SHA256
`ebe7bfb0894380cd92798b57fb9de96e6c0b913a02b6854d0a01cd70328c1578`.
The 1533 upstream-only entries and 22 archive content/mode differences are
explicit normalization, not an unexplained compiler change.

| Original patch | Fork commit | Purpose |
| --- | --- | --- |
| `7bdae98` | `5d05f54cc` | Standalone representation tests |
| `4282c3d` | `169edb18e` | Corrected regression coverage |
| `e6f1148` | `43229f834` | Ownership-only compiler implementation |
| `02bf3da` | `7292c7128` | Schema documentation |

The four actual patch payloads, not coincidentally equal Git hashes, were
transferred. Additional release tests retain a real three-byte
`COMPILER_TEMP` using a volatile pointer-valued result, while preserving
the optimized-away RMW fixture. No compiler allocation or ABI rule changed.

## Build configuration and release contents

Only mcs51 and model-large libraries are packaged. The archive contains
`bin/{sdcc,sdcpp,sdas8051,sdld,sdar}`, generic/mcs51 headers, six runtime
libraries, BUILDINFO, a content/mode manifest and original license material.
Other backends, library models, non-free PIC files and uCsim are excluded.
GNU libc and libstdc++ remain OS dependencies; Ubuntu 24.04 x86_64 is the
tested platform. Other distributions/platforms are not claimed.

The fork's `release/version.json` and external `BUILDINFO.txt` record exact
configure flags, source, GCC/binutils/build-package versions and source
date epoch. The 1,270,452-byte binary archive and corresponding-source
archive are [release assets](https://github.com/faronov/sdcc/releases/tag/v4.2.0-xdata-ownership.1),
alongside SHA256SUMS and the self-test report. The external BUILDINFO
contains the archive digest; the archive cannot contain its own digest.

## CI architecture, self-containment and reproducibility

Compiler PR/push CI is read-only and independent of this firmware repository.
Tag CI exports exact tagged source, builds a pristine control and two clean
patched toolchains, runs standalone storage-class/ISR/reentrant/option-off
regressions, packages, extracts and relocates both packages. Successful
syscall traces prove actual packaged compiler/preprocessor/assembler/linker
execution and packaged header/library opens, including long-long division.
Failed lookups do not count. No host SDCC access is admitted.

Two manifests and complete archive digests matched. Source path mappings
and sorted, timestamp-normalized archives reduce nondeterminism; equality
across different OS/tool/dependency versions is not promised. The Linux
pilot and release also produced the same compiler executable digest, and
all six pilot runtime archives matched the stock 4.2.0 runtime byte-for-byte.

A separate publishing job alone receives `contents: write`; it validates
source/test/hash bindings and creates the release once with the provided
GitHub token. It does not replace an existing accepted asset.

This repository's ordinary 118 CI workers are retained. Its overlay worker
proves feature-off absence of metadata/toolchain preparation, restores an
exact-pin cache, revalidates/downloads the release, builds the overlay and
runs the unchanged complete simulator release gate. A separate weekly/manual
`xdata-source.yml` workflow retains source reconstruction and compares
source/release probe behavior with explicitly distinct identities. Full
manual/nightly CI also calls that workflow from the same checked-out
revision, so it can be exercised before the new standalone workflow reaches
the default branch.

## Normal firmware consumer

```sh
make prepare-xdata-toolchain
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc \
  BUILD=build/overlay prepare-join-smoke-overlay
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc \
  BUILD=build/overlay verify-join-smoke-overlay
```

No compiler path or other worktree is required. The deterministic compiler
path is:

```text
build/toolchains/sdcc-xdata/v4.2.0-xdata-ownership.1/toolchain/bin/sdcc
```

`make all` and ordinary feature-off targets remain independent of this
download. The full simulator command and separate uCsim preparation are
documented in [XDATA_OVERLAY.md](XDATA_OVERLAY.md).

## Hash/schema pinning and receipts

`tools/xdata_toolchain_pins.json` pins the tag, asset, archive, source,
executable, complete manifest and implicit runtime archives. No latest,
branch or automatic version selection exists. The manifest itself is
hash-pinned, so editing both a cached binary and its manifest cannot hide
corruption. All extracted files/modes, required tools/libraries and
BUILDINFO bindings are checked before execution.

A real option-off/on compile/link probe requires schema 1, actual GLOBAL
and LOCAL records, sidecar absence when disabled, and identical ordinary
outputs. The version string alone is insufficient. Receipt version 2 binds
release/development selection, capability evidence, actual supporting
executables and header/runtime contents, source inputs, sidecars and every
preliminary/final artifact. Old receipts cannot be reused.

Relocation changes only ASlink's absolute library-path text. For relocated
runtimes, the identity reader parses the exact six-member library table,
requires correct order/format and a common owner, then verifies the actual
`mcs51.lib` and `libsdcc.lib` archive digests before canonicalizing path
spelling. Historical catalogs use `/usr/share/sdcc/lib/large` for owned
preliminary images and `/usr/bin/../share/sdcc/lib/large` for final overlays.
Only that representation changes: names, members, all other map bytes,
CODE, CDB, listings and every physical/lifetime proof remain bound.
Actual raw maps/paths are retained and hashed in the receipt, and the
relocation verifier still reads the actual linked archives. Existing
production/experimental image catalogs are not replaced.

## Cache and offline behavior

The cache is locked during validation/download/publication. Downloads are
hash-checked before extraction; bounded extraction rejects escaping paths,
links, duplicate/nonregular entries and unexpected modes. A staging tree
must pass manifest and compiler probes before atomic publication.

```sh
make XDATA_TOOLCHAIN_OFFLINE=1 prepare-xdata-toolchain
make XDATA_TOOLCHAIN_OFFLINE=1 BOARD=lg_esl29_rev03 \
  JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/overlay prepare-join-smoke-overlay
```

A valid extracted cache needs no network. A valid cached archive can supply
a fresh extraction offline. Missing cache fails with an actionable message,
never a system-SDCC fallback. Corrupt archives or extracted files are
rejected by default. Deliberate recovery is:

```sh
make XDATA_TOOLCHAIN_FLAGS=--repair prepare-xdata-toolchain
```

Repair reuses a verified archive or explicitly redownloads it. Old corrupt
trees are retained as `rejected-*` for diagnosis; arbitrary directories and
system tools are not deleted or replaced.

## Local development override and source reproduction

```sh
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/local-overlay \
  XDATA_SDCC=/absolute/path/to/sdcc prepare-join-smoke-overlay
```

An explicit `SDCC=...` command-line/environment override is also retained for
compatibility. Even selecting the released executable explicitly creates
a **development**, not release, receipt. Version/schema/behavior and actual
tool/support hashes remain mandatory; no release binary hash is imposed on
the explicitly selected development compiler. A compiler-only development
build may use separately bound stock support tools, as before.

`tools/prepare_xdata_compiler.py` and the four pinned patches remain a
development/reproduction path, not a second normal dependency. It checks
the original archive and patch digests, rebuilds locally without installation,
and regresses against stock SDCC. Reproducing the complete public package
instead uses a full clone of `faronov/sdcc` at the exact release tag and:

```sh
python3 -B release/build.py --output build/reproduced --reproduce
```

Install the fork's declared build dependencies first. No firmware worktree
or SDK is needed.

## Negative tests and current validation

The targeted command is:

```sh
PYTHONPATH=tools:tests python3 -B -m unittest \
  tools/test_xdata_toolchain.py tools/test_xdata_build.py \
  tools/test_xdata_acceptance.py tools/test_ci_plan.py -q
```

Result: PASS, 52 tests. They cover wrong/truncated archives, correct filenames
with wrong contents, missing executables/runtime, wrong executable hashes,
manifest rewriting, unsupported schema, cache corruption and explicit
repair, offline cache/no-cache, no stock fallback, latest/unpinned metadata,
development identity separation, stale compiler receipts, unsafe extraction,
failed capability before publication and strict runtime-table mutations.
The archive/cache controls are synthetic host tests, not MCU evidence.

Real `make -s prepare-xdata-toolchain` and its offline repeat passed.
An unchanged-source pilot using the packaged suite reproduced the eight
non-path baseline identities, 7676 XDATA, 253445 CODE, DATA and static-stack
proofs; raw library paths were the only identity difference. The new default
overlay and full-CI/simulator result are pending, not inferred from that pilot.

## Upgrade procedure and remaining limitations

Publish a new compiler version rather than overwriting accepted assets.
Update all consumer pins explicitly, keep unsupported schemas rejected,
run package/cache negatives and the clean full firmware release gate.
Neither a new tag nor a fitting memory report alone admits an upgrade.

SDCC and its GPLv3 suite components, Boost/dbuf notices and runtime linking
exceptions retain their original terms. Exact modified source is public
at the immutable tag and accompanying source asset; the independently
developed firmware remains BSD-3-Clause.

No additional platform, physical RF trial, BDB/network-key timeout fix,
changed pool placement or hardware-readiness claim is part of this work.
