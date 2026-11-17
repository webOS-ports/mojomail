#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# run-sanitizers.sh <corei7-64 recipe workdir> [work dir]
#
# Builds email-common and the three services, each with its unit tests,
# with the build host's gcc and clang against the recipe's x86-64 sysroot,
# and runs every suite under:
#   ASan + UBSan (gcc, clang)   use after free, double free, overflows, leaks
#                               (LSan), undefined behaviour; clang adds its
#                               integer and nullability checks
#   TSan (gcc, clang)           data races
#   valgrind memcheck           on the image toolchain's own build, which is
#                               not instrumented: invalid accesses and
#                               definite leaks
#
# The image toolchain ships no sanitizer runtimes; the host's do, and the
# host's glibc is the sysroot's (check with <sysroot>/usr/lib/ld-linux-*.so.2
# --version). The binaries get the sysroot's loader as their interpreter and
# an rpath that puts the host's sanitizer runtimes and libstdc++ (newer, so
# backward compatible) ahead of the sysroot's libraries.
#
# The workdir is mojomail-imap's: its sysroot has what all four components
# need, googletest included (mojomail-pop's and mojomail-smtp's do too).
set -u

W=${1:?usage: run-sanitizers.sh <corei7-64 recipe workdir> [work dir]}
WORK=${2:-$HOME/claude-scratch/mojomail/sanitizers}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
. "$SRC/tests/hardening/lib.sh"
moj_env "$W"
RT=$WORK/hostrt
moj_host_runtimes "$RT"
fail=0

export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:detect_stack_use_after_return=1:fast_unwind_on_malloc=0:strict_string_checks=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1
export G_SLICE=always-malloc G_DEBUG=gc-friendly

# run <name> <build dir> [prefix...]: every component's suite
run() {
	name=$1; bd=$2; shift 2
	for c in $COMPONENTS; do
		log=$WORK/$name-$c-run.log
		if (cd "$bd/$c" && timeout 900 "$@" "./$(moj_test_binary "$c")") >"$log" 2>&1; then
			echo "PASS $name $c ($(grep -E '^\[  PASSED' "$log"))"
		else
			echo "FAIL $name $c ($log)"
			grep -E "ERROR: |runtime error|WARNING: ThreadSanitizer|SUMMARY|Failure|^\[  FAILED  \] [A-Z]" "$log" | head -n 20
			fail=1
		fi
	done
}

# Unsigned wrap-around is defined behaviour, and libstdc++ relies on it
# (basic_string's size arithmetic); the signed and conversion checks stay.
for cfg in "asan-gcc gcc address,undefined -" \
           "asan-clang clang address,undefined,integer,nullability unsigned-integer-overflow,unsigned-shift-base" \
           "tsan-gcc gcc thread -" "tsan-clang clang thread -"; do
	# shellcheck disable=SC2086
	set -- $cfg
	echo "== build $1"
	if moj_build "$WORK/$1" "$2" "$3" "$4" "$RT"; then
		run "$1" "$WORK/$1"
	else
		fail=1
	fi
done

# valgrind on the image toolchain's build, run through the sysroot's loader.
if command -v valgrind >/dev/null 2>&1; then
	echo "== build valgrind (image toolchain)"
	if moj_build_yocto "$WORK/yocto" "$W"; then
		run valgrind "$WORK/yocto" valgrind --error-exitcode=99 --leak-check=full \
			--errors-for-leak-kinds=definite --show-leak-kinds=definite --track-origins=yes \
			"$S/usr/lib/ld-linux-x86-64.so.2" --library-path "$WORK/yocto/common:$S/usr/lib"
	else
		fail=1
	fi
else
	echo "== valgrind: not installed, skipped"
fi

exit $fail
