# Explicit external build

This is a manual build recipe, never an ordinary test action. Set `REVIEW`
to an absolute **new external public-source workspace** and `REPO` to this
repository. The recorded build used the root given in README. No global
package installation, dependency maintainer scripts, OpenOCD install target,
programmer invocation, SDK installer or hardware discovery is needed.

`dependencies.json` locks all fetched packages/source. The existing host
supplies GCC, binutils, make, Perl, Python, Git, curl, gh, tar and dpkg-deb.
Record their local identities; package names or version strings are not
authentication. The Ubuntu archive SHA-256 metadata was retrieved over HTTPS;
the libjaylink `.dsc` checksum was checked, but its OpenPGP signature was not
independently verified.

## Source and tool preparation

Use GitHub (`gh`) to retrieve OpenOCD at the locked commit and Jim's exact
gitlink revision. The original execution used the parent's already pristine
OpenOCD checkout, then:

```sh
gh api repos/msteveb/jimtcl/tarball/a77ef1a6218fad4c928ddbdc03c1aedc41007e70 \
  > "$REVIEW/jimtcl.tar.gz"
```

Verify `jimtcl.tar.gz` against the manifest before extracting it into
`$REVIEW/openocd/jimtcl` with `--strip-components=1`.
For each `packages` entry, fetch `package_base + path` into `downloads/`,
verify its complete SHA-256, then run only:

```sh
dpkg-deb -x "$REVIEW/downloads/<locked-package>.deb" "$REVIEW/deps"
```

Fetch the locked libjaylink original tarball, verify its digest and extract
into `$REVIEW/libjaylink-0.3.1`. No Debian maintainer/post-install scripts are
run. Every regular libjaylink archive member is independently compared with
the archive, except the sole hash-bound patched file. Jim retains its
source/header/template comparison. Both explicit proofs validate the
libjaylink archive, patch and complete pre/postimage before compilation.

The package tools need user-prefix relocation. Create `bin/`, then these
symlinks (only in this new workspace):

```sh
ln -s ../deps/usr/bin/automake-1.16 "$REVIEW/bin/automake"
ln -s ../deps/usr/bin/pkgconf "$REVIEW/bin/pkg-config"
ln -s ../aclocal "$REVIEW/deps/usr/share/libtool/m4"
```

Create executable `$REVIEW/bin/aclocal` with:

```sh
#!/bin/sh
exec "$REVIEW/deps/usr/bin/aclocal-1.16" --automake-acdir="$REVIEW/deps/usr/share/aclocal-1.16" --system-acdir="$REVIEW/deps/usr/share/aclocal" "$@"
```

Use the reviewed environment and relocate only the packaged macro-data path:

```sh
. "$REPO/tools/nrf_openocd/build_env.sh"
sed "s|/usr/share/autoconf|$REVIEW/deps/usr/share/autoconf|g" \
  "$REVIEW/deps/usr/share/autoconf/autom4te.cfg" > "$REVIEW/autom4te.cfg"
```

The recorded external `environment.sh` has the same exports plus its literal
workspace assignment. None of the tool executables was patched.

Apply the repository patch to the pristine pinned OpenOCD checkout:

```sh
git -C "$REVIEW/openocd" apply --check "$REPO/tools/nrf_openocd/preserve-reset.patch"
git -C "$REVIEW/openocd" apply "$REPO/tools/nrf_openocd/preserve-reset.patch"
```

## Actual selected build

The reviewed libjaylink `autogen.sh`, configure and normal make targets only
generate/build/install host files; its install prefix is this private user
workspace. OpenOCD bootstrap must use `nosubmodule` to avoid extra fetches.

```sh
cd "$REVIEW/libjaylink-0.3.1"
./autogen.sh
./configure --prefix="$REVIEW/library" --enable-shared --disable-static
make -j4
make install

# Preserve the genuinely built, unmodified discovery control before correction.
mkdir "$REVIEW/pre-usb-1025"
install -m 644 "$REVIEW/library/lib/libjaylink.so.0" \
  "$REVIEW/pre-usb-1025/libjaylink.so.0"
git apply --check "$REPO/tools/nrf_openocd/libjaylink-usb-1025.patch"
git apply "$REPO/tools/nrf_openocd/libjaylink-usb-1025.patch"
make -j4 > "$REVIEW/libjaylink-usb-1025-build.log" 2>&1
make install > "$REVIEW/libjaylink-usb-1025-install.log" 2>&1

cd "$REVIEW/openocd"
./bootstrap nosubmodule

unset PKG_CONFIG_SYSROOT_DIR
export PKG_CONFIG_PATH="$REVIEW/library/lib/pkgconfig"
export LIBUSB1_CFLAGS="-I$REVIEW/deps/usr/include/libusb-1.0"
export LIBUSB1_LIBS="-L$REVIEW/deps/usr/lib/x86_64-linux-gnu -lusb-1.0"
mkdir "$REVIEW/build"
cd "$REVIEW/build"
"$REVIEW/openocd/configure" \
  --disable-internal-libjaylink --enable-jlink \
  --disable-doxygen-html --disable-doxygen-pdf --enable-werror --without-capstone \
  --disable-ftdi --disable-stlink --disable-ti-icdi --disable-ulink \
  --disable-usb-blaster-2 --disable-ft232r --disable-vsllink --disable-xds110 \
  --disable-cmsis-dap-v2 --disable-osbdm --disable-opendous --disable-armjtagew \
  --disable-rlink --disable-usbprog --disable-esp-usb-jtag --disable-aice \
  --disable-cmsis-dap --disable-nulink --disable-kitprog --disable-usb-blaster \
  --disable-presto --disable-openjtag --disable-linuxgpiod \
  --disable-xlnx-pcie-xvc --disable-buspirate
make -j4
```

Keep configure/build outputs outside Git. For `record_build.py`, preserve
the literal configure argv as a JSON string array in `configure-command.json`
and capture these command outputs: `libjaylink-configure.log`,
`libjaylink-build.log`, `openocd-strict-configure.log` and
`openocd-strict-build.log`. Retain the environment exports as `environment.sh`;
the recorder intentionally fails if required evidence is missing. Its other
required JSON records are produced by the explicit proof and the three
parser-only checks described in README (version succeeds, the inert fragment
succeeds, `noinit; adapter driver jlink; jlink preserve_reset nonsense; shutdown`
fails).

The first successful link used
`--disable-werror`; final acceptance reconfigured with `--enable-werror`,
ran `make clean` inside this specific build directory and rebuilt. Only
the final strict-build executable is the handoff artifact. Neither build
ran a hardware target. No opaque SEGGER DLL was used.

The PID correction rebuild compiled genuine `libjaylink_la-discovery_usb.lo`
with `-Wall -Wextra -Werror`, relinked/installed the real library and ran
OpenOCD `make -j4` again (`openocd-usb-1025-build.log`). The relinked OpenOCD
is byte-identical; only the real libjaylink identity changes. Stage regular
copies named `libjaylink.so.0` and `libusb-1.0.so.0` in `operator-libs/`,
verify their bytes against the installed/extracted originals, and use only
that directory for the operator's `LD_LIBRARY_PATH`. Never stage either
synthetic test library there.

The retained pre-correction local control DSO has digest
`bd22b0246f79c15fe7fd067e33cd4f9506bdacb745a1fe698a4c6f75d581ec87`.
The explicit proof checks this identity before using it. Reproducing binaries
under a different absolute build path/toolchain can change debug information
and hashes; it requires a newly reviewed baseline identity, not deleting this
check or substituting a source fixture.

## Actual failures and resolutions

OpenOCD was initially absent (`--version` exited 127, parent observation).
Autotools/pkg-config were absent; only the manifest-locked user-prefix
packages were extracted.

The first libjaylink bootstrap failed because relocated libtool lacked its
`libltdl` data directory. The locked `libltdl-dev` package supplied the real
data; no empty stand-in directory was manufactured. Autotools next failed
to find `m4sugar/m4sugar.m4`, then the absolute
`/usr/share/autoconf/autoconf/trailer.m4`. Relocating `autom4te.cfg` and setting
the supported `trailer_m4` environment override resolved these lookup failures.
Logs are retained as `libjaylink-bootstrap.log`, `libjaylink-bootstrap2.log`,
`openocd-aclocal.log` and the successful `libjaylink-bootstrap3.log`.

The strict native reset probe initially treated `log_init()` as returning an
integer; its actual API is `void`, and the probe was corrected. The first
empty-device synthetic response set count zero but incorrectly retained a
nonnull first list element; libjaylink's list is NULL-terminated. The fixture
was corrected to return an actually empty list; the real driver was not
changed or weakened. All final assertions remain active.

The additional default cached-deassert regression initially expected one
pin-write call. Upstream starts `jtag_srst` at `-1`, so the first explicit
deassert writes again after initialization; only the second explicit deassert
is cached. The final regression checks two calls in total and keeps that
upstream behavior.

See README for explicit hardware-inaccessible proof commands. Its native
compilation selects only the fake API, syscall confinement, four genuine
CPU-only libjaylink helpers and actual OpenOCD/Jim archives. Generated fail-fast
stubs cover all remaining API declarations. A real backend is never used for
the proof's `init` cases.

The additional discovery command is:

```sh
python3 "$REPO/tools/nrf_openocd/discovery.py" --workspace "$REVIEW"
```

It builds only original strict fake libusb/test code, dynamically links the
genuine installed libjaylink, validates loader selection and runs 34 confined
processes with 5-second deadlines. No real USB implementation is linked into
the fake library; bulk operations terminate, without firmware-response
stubs. The public old DSO is used for one negative-control process; all other
cases use the corrected DSO. No hardware discovery or OpenOCD `init` is run by
this additional proof.

`record_build.py` requires both fresh proof records to match current binary,
library and patch hashes/counts, records the new probe/backend identities and
rejects stale operator copies. Preserve the old **public build** evidence if
needed before refreshing these records. This recipe never opens private
selection/readback material or alters a consumed hardware-attempt marker.
