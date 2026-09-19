# Offline Nordic recovery-artifact agreement

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

## Remaining manual gates

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
