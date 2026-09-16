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

## Specialist Copilot agents

Two repository-local [Copilot custom agent profiles](https://docs.github.com/en/copilot/reference/custom-agents-configuration)
are provided; no global installation or fixed model is required.

| Agent | Intended task |
| --- | --- |
| [zigbee-stack](.github/agents/zigbee-stack.agent.md) | Protocol codecs/state machines, specification lookup, end-device conformance and security/commissioning boundaries |
| [cc2530-platform](.github/agents/cc2530-platform.agent.md) | CC2530/SDCC, memory and linker ABI, debugger transport and isolated chip services |

In Copilot CLI opened at this repository, use `/agent` to select a profile,
or name it in a prompt, for example: "Use zigbee-stack to implement the
assigned offline MAC codec extension." A new CLI invocation can select it
with `copilot --agent=cc2530-platform`.

Give a delegated task a bounded objective, file ownership and required
evidence. Coordinate shared interfaces in the main task; do not run two
writers over the same files. The profiles allow read/search/edit/execute/web
tools, omit nested-agent tools, and inherit the configured model. Ordinary CLI
permissions still apply: profile instructions are not an OS sandbox, and
shell access does not constitute authorization to operate hardware.

The Zigbee profile consults the external
[reviewed reference index](docs/PROVENANCE.md#specialist-agent-reference)
on demand. It does not install that repository's agent/skill or copy its
catalog here. Reference retrieval needs network access, but firmware builds
and offline tests do not depend on that repository. Neither profile adds
working firmware features or changes the project's support claims.

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

Focused offline access/CLI and erase-boundary observer coverage:

```sh
PYTHONPATH=tools python3 -B -m unittest test_m1_access test_erase_boundary_fault -q
```

`test_m1_access` uses an original small 8051/backend model. On macOS,
`test_erase_boundary_fault` compiles an original synthetic USB shared library,
driver and the DYLD interposer in a temporary directory with strict compiler
flags. It never links real libusb or runs `cc-tool`; it clearly skips on other
platforms or without a host compiler. The full discovery command includes it.

On Ubuntu, creating this environment may require `python3-venv`. Actual
device access is a separate manual activity described in
[DEBUGGING.md](docs/DEBUGGING.md#implemented-host-transport).

During offline M1 work, do not run even USB enumeration or adapter-state
commands against physical devices. `tools/debug_image.py` and all tests above
operate without an adapter. New control commands need explicit permissions,
pre/post-state checks, one bounded operation deadline, and tests proving that
an error cannot trigger a retry or an implicit resume/reset.
PC/register/memory access and breakpoint programming are implemented, but their
presence is not permission to operate hardware during development checks.
Memory reads, writes and breakpoints need their own permissions; writes are
limited to nonaliased ordinary SRAM below `0x1E00`. Preserve and verify the
full register context, including DPS/DPTR1, on successful inspection. Failed or
late exchanges must stop immediately, without attempting register restoration.
`reset-halt` and `attach-reset` are hardware commands despite their host-tested
implementations: never invoke them in these checks. Reset permission is
independent of ordinary CPU-control permission. Initial attach needs its
distinct explicit access policy and must not be substituted for ordinary
open or an existing-session read. Offline source-location tests must not open
paths named by CDB data.

The same `make ... all test` commands also run the standalone MAC codec on
host and in `mac_frame_test.ihx` under alias-aware simulation. That executable
is test-only: do not flash it, add it to firmware support claims or upload it
as a board image. Protocol codecs must reject unsupported security/layouts
explicitly and must not equate syntactic decoding with authenticated input.

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

`tools/check_debug_hardware.py` is a separate manual, non-flashing runner.
Its `--confirm-fixture-test` flag explicitly authorizes destructive reset,
CPU control, temporary RAM/alias writes and all four breakpoint slots, after
checking the local fixture artifacts; it compares physical CODE before resume.
`tools/erase_boundary_fault.c` is an opt-in macOS observer for an external
programmer, not a flashing target or ordinary debugger cleanup path. Follow the
[manual procedure and dated evidence](docs/DEBUGGING.md#manual-hardware-acceptance-and-recovery),
not an exit code alone. Neither tool belongs in CI or automatic hardware hooks.

The [2026-09-16 LG record](docs/DEBUGGING.md#2026-09-16-lg-fixture-hardware-record)
establishes bounded LG/unbanked M1 completion: a full fixture check after the
first confirmed cable reconnection, followed by the final real unplug/failure
check. After the last replug, PyUSB enumeration and the full one-cycle fixture
check in an explicitly selected new session also passed, leaving the fixture
halted at `0x0173` at the end of that run. The record also includes 309 passing
pinned-dependency host tests (including 18 offline macOS observer tests).
Keep generic-board and standalone
`bringup` hardware claims unobserved; shared startup evidence is limited to
the LG fixture. Bank discrimination is conditional on future banked CODE.
Do not turn bounded M1 completion into a universal debugger or M2/RF/network
claim, or infer current hardware availability from finite acceptance evidence.

For protocol work, identify the specification revision and relevant sections.
For a footprint claim, identify code/XDATA/IRAM separately. For a compatibility
claim, provide the actual scenario and coordinator versions.
