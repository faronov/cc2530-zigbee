# SPDX-License-Identifier: BSD-3-Clause
# Source only for the explicitly selected external build, never ordinary tests.
: "${REVIEW:?Set REVIEW to the absolute external workspace first}"
case "$REVIEW" in
	/*) ;;
	*) echo "REVIEW must be absolute" >&2; return 1 ;;
esac
export REVIEW
export PATH="$REVIEW/bin:$REVIEW/deps/usr/bin:/usr/bin:/bin"
export LD_LIBRARY_PATH="$REVIEW/deps/usr/lib/x86_64-linux-gnu"
export PERL5LIB="$REVIEW/deps/usr/share/automake-1.16"
export AUTOMAKE_LIBDIR="$REVIEW/deps/usr/share/automake-1.16"
export AC_MACRODIR="$REVIEW/deps/usr/share/autoconf"
export trailer_m4="$AC_MACRODIR/autoconf/trailer.m4"
export autom4te_perllibdir="$AC_MACRODIR"
export M4="$REVIEW/deps/usr/bin/m4"
export AUTOM4TE="$REVIEW/deps/usr/bin/autom4te"
export AUTOM4TE_CFG="$REVIEW/autom4te.cfg"
export AUTOCONF="$REVIEW/deps/usr/bin/autoconf"
export AUTOHEADER="$REVIEW/deps/usr/bin/autoheader"
export AUTOMAKE="$REVIEW/deps/usr/bin/automake-1.16"
export ACLOCAL_PATH="$REVIEW/deps/usr/share/aclocal"
export _lt_pkgdatadir="$REVIEW/deps/usr/share/libtool"
export PKG_CONFIG_SYSROOT_DIR="$REVIEW/deps"
export PKG_CONFIG_LIBDIR="$REVIEW/deps/usr/lib/x86_64-linux-gnu/pkgconfig:$REVIEW/deps/usr/share/pkgconfig"
