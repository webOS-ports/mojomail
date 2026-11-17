#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# static-analysis.sh <corei7-64 recipe workdir> [work dir]
#
# Static-analysis gate for the sources of email-common and the three
# services (*/src). Runs:
#
#   gcc -fanalyzer -O0, -O2  use after free, double free, leaks, NULL
#                            dereference, uninitialised reads; twice, as
#                            inlining at -O2 hides some paths and shows others.
#                            At gcc's default budget: raised, a few of the
#                            larger files take well over half an hour each.
#                            Where it gives up, -Wanalyzer-too-complex says
#                            so, and is a finding like any other.
#   clang analyzer           the same from a second engine, with the alpha
#                            unix/security/core checkers
#   clang-tidy               clang-analyzer-*, bugprone-*, cert-*, ...
#   cppcheck                 all checks, exhaustive, inconclusive
#   flawfinder               race-prone calls (TOCTOU), unsafe string calls
#   Coccinelle               tests/hardening/cocci/*.cocci: a reference to a
#                            container element used after erase()
#
# sparse is not run: it parses C only, and mojomail is C++.
#
# Findings are compared with tests/hardening/baseline.txt, the triaged ones
# that are false positives or not mojomail's to fix (each with its reason).
# Anything else fails the gate. A tool that is not installed is reported and
# skipped, not passed.
set -u
export LC_ALL=C

W=${1:?usage: static-analysis.sh <corei7-64 recipe workdir> [work dir]}
WORK=${2:-$HOME/claude-scratch/mojomail/analysis}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$SRC/tests/hardening
. "$HERE/lib.sh"
moj_env "$W"
B=$WORK/build
J=$(nproc)
mkdir -p "$WORK"
skipped=""
fail=0

# Configured trees for the include paths and defines.
MOJ_CONFIGURE_ONLY=1 moj_build "$B" gcc - - "$WORK/hostrt" >"$WORK/configure.log" 2>&1 \
	|| { echo "configure failed, see $WORK/configure.log"; exit 2; }
python3 - "$B" $COMPONENTS >"$WORK/compile_commands.json" <<'PY'
import json, sys
out = []
for c in sys.argv[2:]:
    out += json.load(open("%s/%s/compile_commands.json" % (sys.argv[1], c)))
json.dump(out, sys.stdout)
PY
mkdir -p "$WORK/db" && cp "$WORK/compile_commands.json" "$WORK/db/"

gccdir=$(ls -d "$S"/usr/lib/x86_64-*-linux/* | head -n 1)
triple=$(basename "$(dirname "$gccdir")")
cxxinc=$S/usr/include/c++/$(basename "$gccdir")
# db8's headers need a hand to get through clang: see moj_clang_db8_shim.
CLANG_EXTRA="--gcc-install-dir=$gccdir -isystem $cxxinc -isystem $cxxinc/$triple $(moj_clang_db8_shim "$S" "$WORK/db8-clang")"

# The flags the build uses for a source file, without the output options:
# flags.sh <file> prints them, one shell word each, quoted.
cat >"$WORK/flags.py" <<'PY'
import json, shlex, sys
db, src = sys.argv[1], sys.argv[2]
for e in json.load(open(db)):
    if e["file"] == src:
        args = shlex.split(e["command"])[1:]
        out, skip = [], False
        for a in args:
            if skip: skip = False; continue
            if a in ("-o", "-c", "-MF", "-MT", "-MQ"): skip = True; continue
            if a == src or a.startswith("-M"): continue
            out.append(a)
        print(" ".join(shlex.quote(a) for a in out))
        break
PY
export WORK SRC CLANG_EXTRA

SOURCES=$(cd "$SRC" && ls common/src/*.cpp common/src/*/*.cpp imap/src/*.cpp imap/src/*/*.cpp \
	pop/src/*.cpp pop/src/*/*.cpp smtp/src/*.cpp smtp/src/*/*.cpp | grep -v "src/async/AsyncBuffer.cpp")
# Only files the build compiles (the CMake files leave a few out).
SOURCES=$(for f in $SOURCES; do grep -q "\"$SRC/$f\"" "$WORK/compile_commands.json" && echo "$f"; done)
SRC_RE="^$SRC/(common|imap|pop|smtp)/(src|inc|include)/[^:]+"
: >"$WORK/findings.txt"
: >"$WORK/analyzer-coverage.txt"

record() {
	# tool, then findings on stdin, one per line, as "file:line: text"
	sed -e "s|$SRC/||g" -e "s|^|$1: |" >>"$WORK/findings.txt"
}

have() {
	command -v "$1" >/dev/null 2>&1 && return 0
	skipped="$skipped $1"
	echo "== $1: not installed, skipped"
	return 1
}

# each <tool> <command template>: runs it once per source, $J at a time;
# the template gets the file as $f and its flags as $FLAGS. A file the tool
# has not finished within $TMO seconds is reported as a finding of its own
# (tagged [-Wanalyzer-harness-timeout], which every tool's filter keeps),
# not passed over. Each run writes its own file, read back in order once all
# are done: parallel writers sharing one pipe interleave long diagnostics
# and lose lines, which made the findings differ from run to run.
TMO=${MOJ_ANALYSIS_TIMEOUT:-600}
export TMO
each() {
	out="$WORK/out/$1"
	rm -rf "$out"; mkdir -p "$out"
	export out
	printf '%s\n' $SOURCES | xargs -P "$J" -I{} sh -c \
		'f={}; log="$out/$(echo "$f" | tr / _).log"; FLAGS=$(python3 "$WORK/flags.py" "$WORK/compile_commands.json" "$SRC/$f"); eval "timeout $TMO '"$2"'" >"$log" 2>&1; [ $? -eq 124 ] && echo "$SRC/$f:1:1: warning: '"$1"' did not finish within $TMO s [-Wanalyzer-harness-timeout]" >>"$log"; true'
	cat "$out"/*.log
}

if have gcc; then
	for opt in -O0 -O2; do
		echo "== gcc -fanalyzer $opt"
		each "gcc$opt" "gcc -x c++ -c -o /dev/null $opt -fanalyzer -Wanalyzer-too-complex \
			\$FLAGS \$SRC/\$f" \
			| grep -E "$SRC_RE:[0-9]+:[0-9]+: warning: .*\[-Wanalyzer" \
			| sed -E "s/leak of '([A-Za-z_][A-Za-z0-9_]*)\\.[^']*'/leak of '\\1.…'/" \
			| sort -u >"$WORK/gcc$opt.txt"
		# Leaks of a member path are keyed by the variable: the path gcc
		# prints into a container's internals changes with where it stopped.
		# Where gcc gave up on a path is a coverage note, not a finding, and
		# not stable: the files and node counts change from run to run.
		grep -E -- '-Wanalyzer-too-complex' "$WORK/gcc$opt.txt" \
			| sed -e "s|$SRC/||g" -e "s|^|gcc-fanalyzer$opt: |" >>"$WORK/analyzer-coverage.txt"
		grep -vE -- '-Wanalyzer-too-complex' "$WORK/gcc$opt.txt" | record "gcc-fanalyzer$opt"
	done
fi

if have clang; then
	echo "== clang analyzer"
	each clang-analyzer "clang++ --analyze -Xanalyzer -analyzer-output=text \
		-Xanalyzer -analyzer-checker=core,cplusplus,unix,deadcode,security,nullability,optin.portability,optin.core \
		-Xanalyzer -analyzer-checker=alpha.unix,alpha.security,alpha.core.PointerArithm,alpha.core.CastSize,alpha.core.Conversion,alpha.cplusplus \
		\$CLANG_EXTRA \$(echo \$FLAGS | sed 's/-fcanon-prefix-map//') -o /dev/null \$SRC/\$f" \
		| grep -E "$SRC_RE:[0-9]+:[0-9]+: warning:" \
		| sed -E 's/:[0-9]+: warning:/: warning:/' | sort -u | record clang-analyzer
fi

if have clang-tidy; then
	echo "== clang-tidy"
	CHECKS='clang-analyzer-*,bugprone-*,cert-*,concurrency-*,misc-*,performance-*,portability-*,readability-misleading-indentation,readability-suspicious-call-argument,-bugprone-easily-swappable-parameters,-misc-include-cleaner,-misc-unused-parameters,-misc-non-private-member-variables-in-classes,-misc-use-anonymous-namespace,-misc-use-internal-linkage,-cert-err33-c,-bugprone-assignment-in-if-condition,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-bugprone-reserved-identifier,-cert-dcl37-c,-cert-dcl51-cpp,-misc-const-correctness,-cert-err58-cpp'
	export CHECKS
	each clang-tidy "clang-tidy --quiet -p \$WORK/db \$SRC/\$f --checks=\"\$CHECKS\" \
		\$(for a in \$CLANG_EXTRA; do printf -- '--extra-arg=%s ' \"\$a\"; done) \
		--extra-arg=-Wno-unknown-warning-option 2>/dev/null" \
		| grep -E "$SRC_RE:[0-9]+:[0-9]+: (warning|error):" \
		| sed -E 's/:[0-9]+: (warning|error):/: \1:/' | sort -u | record clang-tidy
fi

if have cppcheck; then
	echo "== cppcheck"
	incs=""
	for c in $COMPONENTS; do
		for d in "$SRC/$c/inc" "$SRC/$c/include/public/email-common" "$SRC/$c/include"; do
			[ -d "$d" ] && incs="$incs -I $d"
		done
	done
	# shellcheck disable=SC2046,SC2086  # one argument per source file
	cppcheck --enable=all --check-level=exhaustive --inconclusive --std=c++17 --language=c++ \
		--library=gnu --library=posix --library=boost --force --inline-suppr --quiet -j "$J" \
		--suppress=missingIncludeSystem --suppress=missingInclude --suppress=unmatchedSuppression \
		--suppress=checkersReport --suppress=unusedFunction \
		--template='{file}:{line}: {severity}: {message} [{id}]' \
		$incs -DMOJ_LINUX -D__linux__ -D__GNUC__=15 \
		$(printf "$SRC/%s " $SOURCES) >"$WORK/cppcheck.log" 2>&1
	# Style findings (naming, const-ness, C casts) are listed for reading but
	# do not gate; the bug classes do.
	grep -E "$SRC_RE:[0-9]+: style:" "$WORK/cppcheck.log" | sed "s|$SRC/||" | sort -u >"$WORK/cppcheck-style.txt"
	grep -E "$SRC_RE:[0-9]+: (error|warning|performance|portability):" "$WORK/cppcheck.log" \
		| sort -u | record cppcheck
	# ...except the style checks that point at logic errors.
	grep -E "\[(knownConditionTrueFalse|redundantAssignment|duplicateCondition|identicalConditionAfterEarlyExit|oppositeInnerCondition|duplicateBranch|uninitvar|nullPointer.*)\]$" \
		"$WORK/cppcheck-style.txt" | sed "s|^|$SRC/|" | record cppcheck
fi

if have flawfinder; then
	echo "== flawfinder"
	# shellcheck disable=SC2046
	flawfinder --minlevel=1 --falsepositive --dataonly --quiet \
		$(for c in $COMPONENTS; do printf '%s ' "$SRC/$c/src"; done) 2>&1 \
		| grep -E "$SRC_RE:[0-9]+: +\[[1-5]\]" | sort -u | record flawfinder
fi

# The diff spatch prints, as "file:line: rule: text" for each line a rule
# marked.
cocci() {
	spatch --very-quiet --c++ --sp-file "$1" "$2" 2>/dev/null \
		| awk -v rule="$(basename "$1" .cocci)" -v file="$3" '
			/^@@ / { split($2, a, ","); line = -a[1]; n = 0; next }
			/^-/ && !/^---/ { print file ":" line + n ": " rule ": " substr($0, 2); n++; next }
			/^ / { n++ }'
}

if have spatch; then
	echo "== Coccinelle"
	# A rule that finds nothing in its own self-test is broken, not clean.
	for c in "$HERE"/cocci/*.cocci; do
		if [ -z "$(cocci "$c" "$HERE/cocci/selftest.cpp" selftest.cpp)" ]; then
			echo "   $(basename "$c") matches nothing in cocci/selftest.cpp"
			fail=1
		fi
	done
	for c in "$HERE"/cocci/*.cocci; do
		for f in $SOURCES; do
			cocci "$c" "$SRC/$f" "$f"
		done
	done | sort -u | sed "s|^|$SRC/|" | record coccinelle
fi

# Line numbers move; a baseline entry is tool, file and message.
key() { sed -E 's/^([^:]+): ([^:]+):[0-9]+(:[0-9]+)?:/\1: \2:/'; }
# A comment after an entry needs at least two spaces before its '#': messages
# themselves contain '#' ("parameter #1").
grep -v '^#' "$HERE/baseline.txt" 2>/dev/null | sed -E 's/ {2,}#.*//' | grep . | key | sort -u >"$WORK/baseline.keys"
key <"$WORK/findings.txt" | sort -u >"$WORK/findings.keys"
comm -23 "$WORK/findings.keys" "$WORK/baseline.keys" >"$WORK/new.keys"
comm -13 "$WORK/findings.keys" "$WORK/baseline.keys" >"$WORK/gone.keys"

echo
[ -s "$WORK/cppcheck-style.txt" ] && echo "cppcheck style notes (not gated): $(wc -l <"$WORK/cppcheck-style.txt"), in $WORK/cppcheck-style.txt"
[ -s "$WORK/analyzer-coverage.txt" ] && echo "gcc analyzer gave up on some paths (not gated): $(wc -l <"$WORK/analyzer-coverage.txt"), in $WORK/analyzer-coverage.txt"
echo "findings: $(wc -l <"$WORK/findings.keys"), triaged in baseline.txt: $(wc -l <"$WORK/baseline.keys")"
if [ -s "$WORK/gone.keys" ]; then
	echo "no longer reported (drop from baseline.txt):"
	sed 's/^/  /' "$WORK/gone.keys"
fi
[ -n "$skipped" ] && echo "skipped:$skipped"
if [ -s "$WORK/new.keys" ]; then
	echo "NEW findings: $(wc -l <"$WORK/new.keys"), in $WORK/new.keys"
	sed 's/^/  /' "$WORK/new.keys" | head -n 200
	exit 1
fi
[ $fail -ne 0 ] && { echo "a Coccinelle rule is broken"; exit 1; }
echo "clean"
exit 0
