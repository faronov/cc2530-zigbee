# Project working guide

The current implementation is a non-networking CC2530 bootstrap. The intended
stack is C/SDCC with one end-device role, awake first and sleepy later.

Read `docs/PLAN.md`, `docs/ARCHITECTURE.md` and `docs/PROVENANCE.md` before
changing scope. Keep README support claims consistent with actual code.

Key constraints:

- XDATA `0x1F00..0x1FFF` aliases IRAM. Never allocate it as separate RAM.
- M0 ordinary allocation ends below the reserved status block at `0x1E00`.
- Preserve strict host, linked-image and alias-aware simulator checks.
- Do not replace unimplemented security, NV or networking with successful stubs.
- Keep board GPIO, hardware services and protocol code separate.
- No automatic flashing/RF tests; hardware evidence is a separate activity.
- Original code is BSD-3-Clause. Check provenance before importing anything.
- Private dumps, captures, identities, photographs and SDK binaries stay out
  of this repository and its generated CI artifacts.

Run the commands in `CONTRIBUTING.md`. Update directly related docs and state
whether a result is host-tested, image-checked, simulated or hardware-observed.

## Specialist agents

Repository profiles live in `.github/agents`:

- `zigbee-stack`: bounded MAC/NWK/APS/ZDO/ZCL and security/commissioning work,
  with specification-revision and conformance checks.
- `cc2530-platform`: chip services, SDCC/memory ABI, debugger and hardware
  evidence boundaries.

See [usage and scope](CONTRIBUTING.md#specialist-copilot-agents). Use a specialist
for a bounded task that benefits from its context, not for every small lookup.
Supply the objective, owned files, constraints and expected evidence. Do not
give concurrent agents overlapping edits or treat their conclusions as
hardware observations. These profiles do not change milestone gates or grant
permission to access equipment.
