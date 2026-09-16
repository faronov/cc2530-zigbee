# Contributing

This is an experimental bootstrap project. Read the
[plan](docs/PLAN.md), [architecture](docs/ARCHITECTURE.md) and
[source policy](docs/PROVENANCE.md) before adding a subsystem.

## A useful change

- Addresses a bounded milestone requirement.
- States what is implemented and what remains unsupported.
- Includes the smallest relevant host/image/simulator tests.
- Records hardware evidence separately when hardware behavior changes.
- Preserves per-file licenses and contains no private data.

Do not add a success-returning radio, security, persistence or join stub to
make an example look functional. Add an explicit unsupported boundary only
when it has a real caller and a tested failure contract.

## Development checks

With SDCC 4.2.0, `s51`, Python 3.9 or newer, GNU Make and a host C compiler on `PATH`:

```sh
make BOARD=generic all test
make BOARD=lg_esl29_rev03 all test
make BOARD=generic IMAGE=debug_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=debug_fixture all test
python3 -m unittest discover -s tools -p 'test_*.py' -v
python3 tools/check_repository.py
git diff --check
```

The build writes to `build/`. Generated firmware, captures and logs must not
be committed. Do not add a dependency solely to avoid a small standard-library
check; new dependencies need a purpose and license review.

The M1 transport tests use synthetic USB backends and must never enumerate
hardware. Ordinary tests need no PyUSB; optional PyUSB resource-manager tests
are explicitly skipped when it is absent. To include those tests without any
USB device access, use a virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-debug.txt
.venv/bin/python -m unittest discover -s tools -p 'test_m1_*.py' -v
```

On Ubuntu, creating this environment may require `python3-venv`. Actual
device access is a separate manual activity described in
[DEBUGGING.md](docs/DEBUGGING.md#implemented-host-transport).

During offline M1 work, do not run even USB enumeration or adapter-state
commands against physical devices. `tools/debug_image.py` and all tests above
operate without an adapter. New control commands need explicit permissions,
pre/post-state checks, one bounded operation deadline, and tests proving that
an error cannot trigger a retry or an implicit resume/reset.
`reset-halt` and `attach-reset` are hardware commands despite their host-tested
implementations: never invoke them in these checks. Reset permission is
independent of ordinary CPU-control permission. Initial attach needs its
distinct explicit access policy and must not be substituted for ordinary
open or an existing-session read. Offline source-location tests must not open
paths named by CDB data.

## Code conventions

- C99, fixed-width integers and explicit bounds.
- Four-space C indentation; tabs for Make recipes.
- Keep compiler-specific SFR/pointer/interrupt details in platform code.
- Avoid heap allocation, packed-struct wire serialization and unbounded waits.
- Keep ISR work small and audit SDCC reentrancy/stack implications.
- Errors must carry a reason and a defined recovery/state transition.
- Comment hardware/ABI subtleties, not obvious assignments.

## Hardware and reviews

No CI job or ordinary build target may flash hardware or transmit RF.
Manual hardware work requires a confirmed board/image, recovery backups and
an appropriate test setup. A software-only change is not permission to erase
a connected device.

For protocol work, identify the specification revision and relevant sections.
For a footprint claim, identify code/XDATA/IRAM separately. For a compatibility
claim, provide the actual scenario and coordinator versions.
