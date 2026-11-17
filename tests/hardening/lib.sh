# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# Sourced by the hardening scripts: the environment for a recipe workdir, and
# a build of all four components (email-common first, then the services
# against it) with the build host's gcc or clang.
#
# mojomail is four CMake projects, not one: each service finds email-common
# through pkg-config, in the sysroot. Here the services are built against
# the email-common of this tree instead (EMAILCOMMON_BUILD_DIR, and its
# library first on the run path), so a change to email-common is tested in
# the services too.

COMPONENTS="common imap pop smtp"

# moj_env <recipe workdir>
moj_env() {
	W=$(cd "$1" && pwd)
	S=$W/recipe-sysroot
	N=$W/recipe-sysroot-native
	export PKG_CONFIG_SYSROOT_DIR="$S"
	export PKG_CONFIG_LIBDIR="$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig"
	export PKG_CONFIG_PATH="$S/usr/lib/pkgconfig"
	# The symbolizer otherwise reaches for debuginfod and can crash on its cache
	export DEBUGINFOD_URLS=
}

# The test binary of a component, relative to its build directory
moj_test_binary() {
	case $1 in
	common) echo test/email-common-tests ;;
	*)      echo test/mojomail-$1-tests ;;
	esac
}

# moj_host_runtimes <dir>: the host gcc's sanitizer runtimes and libstdc++
# (newer than the sysroot's, so backward compatible), for the rpath.
moj_host_runtimes() {
	mkdir -p "$1"
	for l in libasan.so.8 libubsan.so.1 libtsan.so.2 libstdc++.so.6 libgcc_s.so.1; do
		f=$(gcc -print-file-name="$l")
		[ -e "$f" ] && ln -sf "$(readlink -f "$f")" "$1/$l"
	done
}

# moj_clang_db8_shim <sysroot> <dir>: prints the flag that lets clang through
# db8's headers. db8's MojOsLinuxInternal.h defines MojAtomicCAS() (and with
# it everything atomic) only for __GNUC__ 4.4 and later, and #errors
# otherwise; clang calls itself GCC 4.2. Claiming a later GCC with
# -fgnuc-version= is no way out: glibc's fortified headers then use GCC-only
# builtins (__builtin_va_arg_pack). So a copy of that one header, with clang
# accepted next to GCC (it has the same __sync builtins), is put ahead of the
# sysroot's on the quoted-include path. It is made from the sysroot's own
# header on every build, so it follows db8. The fix belongs in db8.
moj_clang_db8_shim() {
	h=core/internal/MojOsLinuxInternal.h
	mkdir -p "$2/$(dirname $h)"
	sed 's/^#if __GNUC__ > 4 || (__GNUC__ == 4 \&\& __GNUC_MINOR__ > 3)/#if defined(__clang__) || __GNUC__ > 4 || (__GNUC__ == 4 \&\& __GNUC_MINOR__ > 3)/' \
		"$1/usr/include/mojodb/$h" >"$2/$h"
	grep -q "defined(__clang__)" "$2/$h" || { echo "db8 shim: pattern not found in $1/usr/include/mojodb/$h" >&2; return 1; }
	echo "-iquote $2"
}

# moj_build <build dir> <gcc|clang> <sanitizers or -> <checks left out or -> <host runtime dir> [extra flags]
# Configures and builds the four components with their tests; logs go next
# to the build directories. Returns non-zero on the first failure. With
# MOJ_CONFIGURE_ONLY set it stops after configuring (for compile_commands.json).
moj_build() {
	bd=$1; cc=$2; san=$3; nosan=$4; rt=$5; extra=${6:-}
	[ "$san" = - ] && san=""
	[ "$nosan" = - ] && nosan=""
	gccdir=""; cxxflags="-g -O1 $extra"
	if [ "$cc" = clang ]; then
		# clang does not find the sysroot's gcc (crtbeginS.o, libstdc++'s
		# headers) in its Yocto triple directory on its own.
		gccdir=$(ls -d "$S"/usr/lib/x86_64-*-linux/* | head -n 1)
		triple=$(basename "$(dirname "$gccdir")")
		cxxinc=$S/usr/include/c++/$(basename "$gccdir")
		cxxflags="$cxxflags --gcc-install-dir=$gccdir -isystem $cxxinc -isystem $cxxinc/$triple $(moj_clang_db8_shim "$S" "$bd/db8-clang")"
		[ -n "$san" ] && cxxflags="$cxxflags -fsanitize-ignorelist=$SRC/tests/hardening/sanitizer-ignorelist.txt"
	fi
	mkdir -p "$bd"
	for c in $COMPONENTS; do
		flags=$cxxflags
		ldflags="${gccdir:+--gcc-install-dir=$gccdir} -Wl,--dynamic-linker=$S/usr/lib/ld-linux-x86-64.so.2 -Wl,--disable-new-dtags"
		# The email-common just built first, or the tests load the sysroot's
		ldflags="$ldflags -Wl,-rpath,$bd/common:$rt:$S/usr/lib"
		PATH=$N/usr/bin:$PATH cmake -S "$SRC/$c" -B "$bd/$c" -G Ninja \
			-DCMAKE_TOOLCHAIN_FILE="$SRC/tests/hardening/host-sysroot.cmake" \
			-DMOJ_SYSROOT="$S" -DMOJ_HOST_CC="$cc" \
			-DWEBOS_INSTALL_ROOT=/ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
			-DBUILD_TESTING=ON -DMOJ_SANITIZE="$san" -DMOJ_NO_SANITIZE="$nosan" \
			-DEMAILCOMMON_BUILD_DIR="$bd/common" \
			-DCMAKE_CXX_FLAGS="$flags" \
			-DCMAKE_EXE_LINKER_FLAGS="$ldflags" -DCMAKE_SHARED_LINKER_FLAGS="$ldflags" \
			>"$bd/$c-configure.log" 2>&1 \
			|| { echo "FAIL configure $c ($bd/$c-configure.log)"; tail -n 20 "$bd/$c-configure.log"; return 1; }
		[ -n "${MOJ_CONFIGURE_ONLY:-}" ] && continue
		PATH=$N/usr/bin:$PATH ninja -C "$bd/$c" >"$bd/$c-build.log" 2>&1 \
			|| { echo "FAIL build $c ($bd/$c-build.log)"; grep -E "error|FAILED|undefined reference" "$bd/$c-build.log" | head -n 20; return 1; }
	done
}

# moj_build_yocto <build dir> <recipe workdir> [extra cxxflags]
# The four components with the image toolchain itself, through the recipe's
# toolchain.cmake: what valgrind runs, and what build-all.py checks for
# warnings on each architecture.
moj_build_yocto() {
	bd=$1; w=$(cd "$2" && pwd); extra=${3:-}
	t=$(ls -d "$w"/recipe-sysroot-native/usr/bin/*-webos-linux* | head -n 1)
	mkdir -p "$bd"
	for c in $COMPONENTS; do
		flags=$extra
		PATH=$t:$w/recipe-sysroot-native/usr/bin:$PATH cmake -S "$SRC/$c" -B "$bd/$c" -G Ninja \
			-DCMAKE_TOOLCHAIN_FILE="$w/toolchain.cmake" \
			-DWEBOS_INSTALL_ROOT=/ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=ON \
			-DCMAKE_CXX_FLAGS="$flags" -DEMAILCOMMON_BUILD_DIR="$bd/common" \
			>"$bd/$c-configure.log" 2>&1 \
			|| { echo "FAIL configure $c ($bd/$c-configure.log)"; tail -n 20 "$bd/$c-configure.log"; return 1; }
		PATH=$t:$w/recipe-sysroot-native/usr/bin:$PATH ninja -C "$bd/$c" >"$bd/$c-build.log" 2>&1 \
			|| { echo "FAIL build $c ($bd/$c-build.log)"; grep -E "error|FAILED|undefined reference" "$bd/$c-build.log" | head -n 20; return 1; }
	done
}
