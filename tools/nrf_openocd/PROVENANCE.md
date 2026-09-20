# OpenOCD preservation provenance

This is external programmer-tool preparation for #51, **not BSD firmware
source adoption**. No vendor tree, executable, SDK object, capture, backup or
identity belongs in this directory or CI artifacts.

| Material | Identity and license |
| --- | --- |
| `preserve-reset.patch` | OpenOCD `9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c`, GPL-2.0-or-later; original 2026-09-20 changes to three files |
| `reset_probe.c` | Original GPL-2.0-or-later test, linked only against external OpenOCD objects |
| Python tools, synthetic API, syscall confinement | Original BSD-3-Clause, under the repository license |
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
