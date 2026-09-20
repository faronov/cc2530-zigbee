# OpenOCD preservation provenance

This is external programmer-tool preparation for #51, **not BSD firmware
source adoption**. No vendor tree, executable, SDK object, capture, backup or
identity belongs in this directory or CI artifacts.

| Material | Identity and license |
| --- | --- |
| `preserve-reset.patch` | OpenOCD `9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c`, GPL-2.0-or-later; original 2026-09-20 changes to three files |
| `libjaylink-usb-1025.patch` | GPL-2.0-or-later one-line correction to pinned 0.3.1; functional PID/address fact confirmed by upstream `7e94e423c6a2ce5b412b1db8a9fbc749b1b29d24` |
| `reset_probe.c` | Original GPL-2.0-or-later test, linked only against external OpenOCD objects |
| Python tools, synthetic API, syscall confinement | Original BSD-3-Clause, under the repository license |
| `discovery_probe.c`, `fake_libusb.c`, `fake_libusb.h` | Original BSD-3-Clause tests; the proof links the genuine external GPL library, not a copied source fixture |
| `COPYING` | Verbatim GNU GPL v2 license text from the pinned OpenOCD `LICENSES/preferred/GPL-2.0`, excluding its SPDX usage-guide prefix |
| External libjaylink | 0.3.1 original source archive; SHA-256 `dd211d3f4e8fcd53c97e9d8b7967061e4bdad6c0823d92ce99406577aaf7f254`; GPL-2.0-or-later |
| External Jim | `a77ef1a6218fad4c928ddbdc03c1aedc41007e70` (0.81), matching OpenOCD's gitlink; BSD-2-Clause default in `LICENSE`, retain per-file notices |
| External libusb | Ubuntu `2:1.0.27-1`, LGPL-2.1-or-later library, locked package digest |

The patch leaves the original source notices intact. OpenOCD `jlink.c`
credits Juergen Stuber, Dominic Rath, Benedikt Sauter, Spencer Oliver,
Jean-Christophe PLAGNIOL-VILLARD, Marc Schink and Paul Fertser. `core.c`
credits Zachary T Welch, Oyvind Harboe, SoftPLC Corporation and Dominic Rath;
`interface.h` credits Dominic Rath, Oyvind Harboe and Zachary T Welch.
The original spelling and notices remain in the upstream files.

libjaylink `core.c`, `device.c`, `target.c`, `transport_usb.c`,
`discovery_usb.c`, `strutil.c`, `util.c`, `error.c`, `version.c` and its public
header carry GPL-2.0-or-later notices. The synthetic shared library compiles
the four **unchanged, CPU-only** `strutil.c`, `util.c`, `error.c`, `version.c`
helpers, plus the original synthetic API and confinement code. That linked
test library is GPL-covered; no upstream implementation is copied into a
BSD firmware source file. Unexpected APIs terminate the synthetic process,
not return invented success. The full upstream license and corresponding
sources must accompany any separate binary redistribution; this repository
does not distribute the built program.

## Source trace

OpenOCD citations are to the **unmodified pinned revision**:

- [`jlink.c:539-859`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/jtag/drivers/jlink.c#L539):
  USB discovery/filter/open, TCP fallback, initialization, interface selection,
  unconditional `(0,0)` reset at 814, registration and quit.
- [`jlink.c:940-963`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/jtag/drivers/jlink.c#L940):
  active-low reset/TRST commands and the flush-before-reset wrapper.
- [`jlink.c:1942-1947`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/jtag/drivers/jlink.c#L1942)
  and [`adi_v5_swd.c:689-703`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/target/adi_v5_swd.c#L689):
  selecting SWD sets the driver's interface before adapter initialization.
  This citation does not review or approve target acquisition.
- [`core.c:614-647`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/jtag/core.c#L614)
  and [`1833-1913`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/jtag/core.c#L1833):
  cached deassertion can bypass the driver; `adapter_resets()` discards a
  SWD reset result. The new default-false flag rejects before either behavior,
  without changing unselected behavior.
- [`adapter.c:186-205`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/jtag/adapter.c#L186)
  and [`openocd.c:342-371`](https://github.com/openocd-org/openocd/blob/9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c/src/openocd.c#L342):
  config-only shutdown exits before automatic initialization; adapter quit is
  called only after successful initialization.

libjaylink citations identify the selected **0.3.1**, not OpenOCD's unused
bundled libjaylink gitlink. The locked original tarball is authoritative;
the Debian source browser provides corresponding line views:

- [`core.c:91-195`](https://sources.debian.org/src/libjaylink/0.3.1-1/libjaylink/core.c/#L91):
  init calls `libusb_init`; exit releases device references and `libusb_exit`.
  Therefore preservation preflight must precede `jaylink_init`, not just
  adapter opening.
- [`discovery_usb.c:135-281`](https://sources.debian.org/src/libjaylink/0.3.1-1/libjaylink/discovery_usb.c/#L135):
  USB discovery enumerates candidates and opens their USB handles to read
  serial string descriptors, then closes them. Exact numeric serial filtering
  is subsequently done by OpenOCD; it is not pre-enumeration physical binding.
- [`transport_usb.c:46-128`](https://sources.debian.org/src/libjaylink/0.3.1-1/libjaylink/transport_usb.c/#L46)
  and [`154-226`](https://sources.debian.org/src/libjaylink/0.3.1-1/libjaylink/transport_usb.c/#L154):
  reads the active configuration and first altsetting descriptor, selects a
  vendor interface/bulk endpoints, opens and claims that interface. Close
  releases the interface, closes the USB handle and frees buffers. No explicit
  USB reset, set-configuration, set-altsetting, kernel-driver detach or target
  reset command is made by these libjaylink paths. USB interface release is
  still a real USB operation, not a proof that adapter pins cannot change.
- [`device.c:607-659`](https://sources.debian.org/src/libjaylink/0.3.1-1/libjaylink/device.c/#L607):
  open/close dispatch into those transport functions. Failed claim closes the
  USB handle; successful close does not restore a previous target interface.
- [`target.c:203-261`](https://sources.debian.org/src/libjaylink/0.3.1-1/libjaylink/target.c/#L203):
  interface selection sends `CMD_SELECT_TIF` (`0xc7`) plus interface selector,
  then reads four bytes. The selected route uses SWD selector 1. No explicit
  SRST/TRST command is part of that function.
- [`device.c:1656-1710`](https://sources.debian.org/src/libjaylink/0.3.1-1/libjaylink/device.c/#L1656):
  unregister sends a connection-management command, not a reset command.
- [`strutil.c:48-65`](https://sources.debian.org/src/libjaylink/0.3.1-1/libjaylink/strutil.c/#L48):
  pure decimal `strtoull` conversion/range check. Preservation additionally
  rejects empty strings, signs, spaces and nondecimal spelling before init.

## Limits of the evidence

No reset-pin API is submitted on the selected, tested path, including error
cleanup. That is a **host/source/link observation**, not an electrical
guarantee about J-Link firmware, USB claim/release, interface switching,
power-up or cable attachment. USB serials are not authentication, and existing
iteration among matching devices is not a uniqueness guarantee.

Upstream quit logs unregister failure but ignores close/exit results; its
caller also does not propagate quit failure as process failure. The corpus
preserves and exposes the unregister case. Exit status alone is consequently
not a complete cleanup or restoration attestation.

The mode is not a firewall for target power, arbitrary memory accesses,
commands, reset through target registers, flash algorithms or scripts.
The separate operator must restrict configuration/commands, physical binding,
acquisition, deadlines and recovery. No MEM-AP/target read contract, physical
board identity, flash/UICR backup or restoration claim is supplied here.

## Narrow USB PID correction for #53

Primary upstream commit
[`7e94e423c6a2ce5b412b1db8a9fbc749b1b29d24`](https://gitlab.zapb.de/libjaylink/libjaylink/-/commit/7e94e423c6a2ce5b412b1db8a9fbc749b1b29d24),
committed 2025-09-26, is titled **"Add USB product ID (PID) 0x1025"** and says
the PID was encountered debugging an nRF52840 board from Nordic Semiconductor.
Its entire functional change is `{0x1025, JAYLINK_USB_ADDRESS_0}`.
The same revision's
[`discovery_usb.c:27-52`](https://gitlab.zapb.de/libjaylink/libjaylink/-/blob/7e94e423c6a2ce5b412b1db8a9fbc749b1b29d24/libjaylink/discovery_usb.c#L27-52)
binds that table to VID `0x1366`. This is precise public upstream evidence,
not a guessed vendor wildcard, a physical identity or a vendor electrical
compatibility specification.

The three relevant bodies, `initialize_handle`, `transport_usb_open` and
`transport_usb_close`, are **byte-identical** between 0.3.1 and
[`transport_usb.c:29-210` at that revision](https://gitlab.zapb.de/libjaylink/libjaylink/-/blob/7e94e423c6a2ce5b412b1db8a9fbc749b1b29d24/libjaylink/transport_usb.c#L29-210).
Thus the primary addition does not require a new transport implementation.
The existing assumptions remain: inspect active configuration/altsetting zero,
choose the first class/subclass `ff/ff` interface with at least two endpoints,
find IN/OUT directions and use bulk transfers. The code uses the interface
array index for claiming and does not independently validate endpoint transfer
type or `bInterfaceNumber` equality. No altsetting, kernel-driver detach, USB
reset, target reset or power operation is introduced by this correction.
Whether a particular physical adapter satisfies those assumptions remains
unobserved here.

Only the single PID entry is added to 0.3.1; its 2014-2016 Marc Schink notice
and GPL-2.0-or-later license remain intact. Later upstream changed its library
license to LGPL-2.1-or-later; that implementation/license change is **not**
imported. The source archive digest is unchanged. The manifest pins the patch
and complete pre/postimage digests of the one changed file; every other
regular archive member must remain identical. Extra edits, an unapplied patch
or a different patch are rejected before either explicit proof builds.

The new proof loads the genuinely rebuilt libjaylink DSO behind a strict
original fake **libusb**, with the existing seccomp confinement. The original
58-sequence proof replaces libjaylink and consequently never proved USB
discovery; these are separate evidence levels and both are retained.
The new suite covers all 65,536 PIDs with VID `1366` and all 65,536 VIDs with
PID `1025` (two axes, **not** all 2^32 pairs), all 20 old supported PIDs and
their old address mappings, the new entry, descriptor/open/serial errors,
interface rejection, claim/release errors, cached discovery and cleanup.
The retained genuine old DSO rejects `1025` without `libusb_open`; the patched
DSO admits it. The mock never supplies firmware, SWD, memory or bulk-transfer
responses; unexpected operations terminate rather than report success.

Unchanged serial semantics are tested honestly: descriptor-read failure can
leave a candidate without an available serial (OpenOCD's explicit selection
skips it); an empty USB string parses as numeric zero in 0.3.1, and strings
longer than ten digits use the last ten digits. Neither descriptor metadata,
enumeration success nor USB serial selection is authentication or a successful
acquisition. No private failed-attempt material was consulted, no marker was
cleared and no hardware retry was performed.
