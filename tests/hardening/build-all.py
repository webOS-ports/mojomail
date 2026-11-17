#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# build-all.py [--work DIR] <recipe workdir>...
#
# The warning gate: email-common, the three services and all their tests
# built for every architecture given (one mojomail-imap recipe workdir each,
# e.g. the armv7, aarch64 and corei7-64 ones under tmp/work), with both
# compilers:
#
#   gcc    the image toolchain itself, through the recipe's toolchain.cmake
#   clang  the build host's clang++, --target=<the toolchain's triple>, on
#          the same compile commands against the same sysroot
#
# Both with -Wall -Wextra under the project's flags. A warning in mojomail's
# own files that is not in warnings-baseline.txt fails the gate; one from a
# header in the sysroot is listed (it is not ours to fix) but does not.
#
# armv7 is in the list for a reason: it is the 32-bit target, where size_t,
# long and the like are narrower than on the other two.

import argparse, collections, glob, json, os, re, shlex, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.expanduser("~/claude-scratch/mojomail/builds"))
ap.add_argument("workdirs", nargs="+")
opts = ap.parse_args()

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, "..", ".."))
COMPONENTS = ("common", "imap", "pop", "smtp")
WARN = re.compile(r"^(?P<file>[^\s:]+):(?P<line>\d+):\d+: warning: (?P<msg>.*)$", re.M)
# Flags only gcc knows; clang would warn about each, which is noise.
GCC_ONLY = ("-fcanon-prefix-map", "-mthumb-interwork", "-fno-tree-vrp", "-fmacro-prefix-map",
            "-fdebug-prefix-map", "-ffile-prefix-map", "-Wno-error=strict-aliasing")
EXTRA = ["-Wall", "-Wextra"]


def baseline():
    keys = set()
    path = os.path.join(HERE, "warnings-baseline.txt")
    if os.path.exists(path):
        for l in open(path):
            l = l.split("#", 1)[0].strip()
            if l:
                keys.add(l)
    return keys


def key(f, msg):
    # Line numbers move; a warning is its file, its message and its flag.
    return "%s: %s" % (f, msg)


def classify(text):
    ours, theirs = collections.Counter(), collections.Counter()
    for m in WARN.finditer(text):
        f = os.path.realpath(m["file"]) if os.path.exists(m["file"]) else m["file"]
        if f.startswith(SRC + "/"):
            ours[(f.replace(SRC + "/", ""), m["line"], m["msg"])] += 1
        else:
            theirs["%s:%s: %s" % (f, m["line"], m["msg"])] += 1
    return ours, theirs


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def native_path(w):
    n = os.path.join(w, "recipe-sysroot-native")
    t = sorted(glob.glob(os.path.join(n, "usr/bin/*-webos-linux*")))
    return os.pathsep.join(t[:1] + [os.path.join(n, "usr/bin"), os.path.join(n, "bin"), os.environ["PATH"]])


known = baseline()
results = []
fail = False
for w in opts.workdirs:
    w = os.path.abspath(w)
    arch = os.path.basename(os.path.dirname(os.path.dirname(w)))
    sysroot = os.path.join(w, "recipe-sysroot")
    env = dict(os.environ, PATH=native_path(w), PKG_CONFIG_SYSROOT_DIR=sysroot,
               PKG_CONFIG_LIBDIR=sysroot + "/usr/lib/pkgconfig:" + sysroot + "/usr/share/pkgconfig",
               PKG_CONFIG_PATH=sysroot + "/usr/lib/pkgconfig")
    gcc_out, ok, db = "", True, []
    for c in COMPONENTS:
        bd = os.path.join(opts.work, arch, c)
        cfg = run(["cmake", "-S", os.path.join(SRC, c), "-B", bd, "-G", "Ninja",
                   "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(w, "toolchain.cmake"),
                   "-DWEBOS_INSTALL_ROOT=/", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DBUILD_TESTING=ON",
                   # The services build against the email-common of this tree.
                   "-DEMAILCOMMON_BUILD_DIR=" + os.path.join(opts.work, arch, "common"),
                   "-DCMAKE_CXX_FLAGS_RELEASE=" + " ".join(EXTRA), "-DCMAKE_BUILD_TYPE=Release"], env=env)
        if cfg.returncode:
            print("FAIL configure %s %s\n%s" % (arch, c, cfg.stdout[-3000:] + cfg.stderr[-3000:]))
            ok = False
            break
        run(["ninja", "-C", bd, "-t", "clean"], env=env)
        b = run(["ninja", "-C", bd], env=env)
        gcc_out += b.stdout + b.stderr
        if b.returncode:
            ok = False
            print("FAIL build %s %s gcc\n%s" % (arch, c, "\n".join(
                l for l in b.stdout.splitlines() if "error" in l)[:4000]))
            break
        db += json.load(open(os.path.join(bd, "compile_commands.json")))
    ours, theirs = classify(gcc_out)
    results.append((arch, "gcc", ok, ours, theirs))
    if not ok:
        continue

    # clang: the same compile commands with the host's clang++.
    cxx = shlex.split(db[0]["command"])[0]
    triple = os.path.basename(cxx)[: -len("-g++")]
    # clang does not take the "webos" vendor for a gcc layout it knows, so
    # it is shown where the sysroot's gcc and libstdc++ headers are.
    gccdir = sorted(glob.glob(os.path.join(sysroot, "usr/lib", triple, "*")))[-1]
    cxxinc = os.path.join(sysroot, "usr/include/c++", os.path.basename(gccdir))
    # db8's headers need a hand to get through clang: see moj_clang_db8_shim
    # in lib.sh, which makes the header copy this points at.
    shim = run(["sh", "-c", '. "$0"; moj_clang_db8_shim "$1" "$2"', os.path.join(HERE, "lib.sh"),
                sysroot, os.path.join(opts.work, arch, "db8-clang")])
    clang_extra = ["--gcc-install-dir=" + gccdir, "-isystem", cxxinc,
                   "-isystem", os.path.join(cxxinc, triple)] + shim.stdout.split()
    out, ok = "", True
    for e in db:
        args = shlex.split(e["command"])
        # -Wall -Wextra first, so the project's own -Wno-* win as they do
        # with gcc, where an explicit -Wno-* beats -Wextra wherever it stands.
        args = ["clang++", "--target=" + triple] + EXTRA + [
            a for a in args[1:] if not a.startswith(GCC_ONLY) and a not in EXTRA]
        i = args.index("-o")
        args[i + 1] = "/dev/null"
        args += clang_extra + ["-Wno-unknown-warning-option", "-Wno-unused-command-line-argument"]
        r = run(args, cwd=e["directory"], env=env)
        out += r.stderr
        if r.returncode:
            ok = False
            print("FAIL clang %s %s\n%s" % (arch, e["file"], r.stderr[-2000:]))
    ours, theirs = classify(out)
    results.append((arch, "clang", ok, ours, theirs))

print()
new_total = collections.Counter()
for arch, cc, ok, ours, theirs in results:
    new = {k for k in ours if key(k[0], k[2]) not in known}
    state = "ok" if ok else "BUILD FAILED"
    print("%-46s %-5s %s, our warnings: %d (%d not in baseline), sysroot headers: %d" % (
        arch, cc, state, len(ours), len(new), len(theirs)))
    for k in sorted(new):
        print("    %s:%s: %s" % k)
        new_total[key(k[0], k[2])] += 1
    for k in sorted(theirs)[:20]:
        print("    (header) " + k)
    if not ok or new:
        fail = True
seen = {key(k[0], k[2]) for r in results for k in r[3]}
gone = sorted(known - seen)
if gone and all(r[2] for r in results) and len(results) == 2 * len(opts.workdirs):
    print("\nno longer reported (drop from warnings-baseline.txt):")
    for k in gone:
        print("  " + k)
# All our warnings, one key per line, for writing the baseline
with open(os.path.join(opts.work, "warnings.keys"), "w") as f:
    f.write("".join(k + "\n" for k in sorted(seen)))
sys.exit(1 if fail else 0)
