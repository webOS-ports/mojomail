mojomail test and hardening harness
===================================

Three layers: the unit tests of email-common and the three services, the
sanitizers and valgrind over them, and static analysis and a warning gate
over the sources.

mojomail is four CMake projects (`common/`, `imap/`, `pop/`, `smtp/`), each
built by its own recipe. Every script here takes a Yocto recipe workdir
(`tmp/work/<arch>/mojomail-imap/<ver>`) for the toolchain and the sysroot:
mojomail-imap's sysroot has what all four need, googletest included. They
build email-common first and the services against it
(`-DEMAILCOMMON_BUILD_DIR`), so a change to email-common is tested in the
services too, not against the email-common installed in the sysroot.

Unit tests (`*/test/`)
----------------------

The gtest suites from the original Palm tree, which had not been built
since Palm's own desktop build (the `gtest.sh` runners that pointed at
`/opt/Palm/...` are gone), revived and extended. Each component has an
`option(BUILD_TESTING ...)`, off by default, that adds its `test/`
directory:

| Binary | What it covers |
|---|---|
| `email-common-tests` | activities and activity sets, the command manager and its priority queue, email/MIME parsing and writing, RFC 2047, preview text, the stream filters (base64, quoted-printable, UTF-8, deflate/inflate, line reader), string utilities, the folder adapter, curl |
| `mojomail-imap-tests` | the IMAP response parsers, the request manager, UID maps, folder list diffs, special-folder picking, the sync engine, local-change sync, the IDLE refresh limit |
| `mojomail-pop-tests` | the old-emails cache |
| `mojomail-smtp-tests` | CRLF termination, account validation |

The services' tests are linked from the service's own objects (the
`mojomail-<service>-core` object library, everything but `main()`), with the
mocks in `common/test-shared/`.

Tests that pin a fixed bug:

- `ActivitySetTest.TestEnd`, `TestEndActivityHeldOnlyBySet`: ending an
  activity the set holds the last reference to (the use-after-free that
  crashed mojomail-imap during sync)
- `CommandManagerTest.TestCompletedCommandKeptUntilCleanup`: a completed
  command stays alive until the idle cleanup, not freed inside its own
  completion
- `CommandManagerTest.TestAbortPendingKeepsOrder`: removing a pending command
  keeps the priority queue a heap
- `IdleLimitTrackerTest.*`: learning a server's idle drop (TransIP's 30 s)
  and re-issuing IDLE inside it
- `AsyncEmailWriterTest.TestSymlinkPartRefused`, `TestFifoPartRefused`: an
  attachment path that is a symlink or not a regular file is refused, and a
  FIFO no longer blocks the writer in open()
- `DeflateInflateTest`: the deflater's default window, which zlib 1.2.9 and
  later refused
- `UIDMapTest.TestDuplicatesRemoved`: a UID the server lists twice is kept
  once
- `Rfc2047DecoderTest.TestNoEncodingSeparator`: "=?=" is refused on 64-bit
  targets as on 32-bit ones
- `GIOChannelWrapperTest.TestIsClosedAfterShutdown`: the wrapper no longer
  hides AsyncIOChannel's state behind members of the same name
- `SmtpValidatorTest.TestLoginFailureRepliesBeforeQuit`: a rejected login
  is answered before QUIT, which can end the validator when the server has
  already closed the connection (the crash on a wrong Gmail app password)

Fixed without a test, as nothing here can drive them yet (there is no SMTP
or IMAP session mock): SMTP's RCPT TO reply codes, the SMTP reply fields
read before the first reply, SmtpAccountEnableCommand's bus reply when its
first step throws, and ImapSyncSessionCommand skipping its parent's
Cleanup().

Build and run them on the host (the corei7-64 build runs against its
sysroot):

    cmake -S common -B build/common -DBUILD_TESTING=ON ...   # recipe's toolchain.cmake
    cmake -S imap -B build/imap -DBUILD_TESTING=ON -DEMAILCOMMON_BUILD_DIR=$PWD/build/common ...
    <sysroot>/usr/lib/ld-linux-x86-64.so.2 --library-path build/common:<sysroot>/usr/lib \
        build/imap/test/mojomail-imap-tests

The tests need no bus, database or network; `AsyncEmailWriterTest` writes
its part files under `/tmp`, where the writer's sandbox check lets them be.
On a device, copy the armv7 or aarch64 binaries (gtest is linked
statically) and the matching `libemail-common.so.2`.

Hardening (`hardening/`)
------------------------

- `run-sanitizers.sh <corei7-64 workdir>`: all four suites under ASan+UBSan
  (with LeakSanitizer) and TSan, each built with gcc and with clang, and
  under valgrind memcheck on the image toolchain's own build. clang also
  gets its integer and nullability checks, with boost's headers left out
  (`sanitizer-ignorelist.txt`). The image toolchain ships no sanitizer
  runtimes, so the host's gcc and clang build against the sysroot
  (`host-sysroot.cmake`); the host's glibc is the sysroot's.
- `static-analysis.sh <corei7-64 workdir>`: gcc `-fanalyzer` at -O0 and -O2,
  the clang analyzer with the alpha checkers, clang-tidy, cppcheck
  (exhaustive, inconclusive), flawfinder and Coccinelle (`cocci/`), over
  `*/src`. It fails on any finding not in `baseline.txt`, where each entry
  that was looked at and left has its reason. cppcheck's style notes are
  listed but do not gate. sparse is not run: it parses C only.
  gcc's analyzer runs at its default budget. Where it gives up on a path it
  says so; those notes go to `analyzer-coverage.txt` and do not gate,
  because which files they hit changes from run to run. A file any tool has
  not finished within `MOJ_ANALYSIS_TIMEOUT` seconds (600) is a gating
  finding. A run took about four minutes with 64 parallel jobs, and two
  runs gave the same findings.
- `build-all.py <workdir>...`: the warning gate. All four components and
  their tests, for each architecture given, with the image's gcc and with
  the host's clang, at `-Wall -Wextra` and with assertions on. A warning in
  mojomail's own files that is not in `warnings-baseline.txt` fails it. Run
  it with the armv7, aarch64 and corei7-64 workdirs.
- `mutation-check.py <corei7-64 workdir>`: shows the tests can fail. Each
  mutant puts back one of the bugs above, or breaks a behaviour a revived
  legacy suite pins, and has to be caught by a test, by ASan, or by a
  timeout.

The Coccinelle rule `ref-then-erase.cocci` looks for the bug class behind
the sync crash: a reference to a container element used after `erase()`.
It is checked against `cocci/selftest.cpp`, so a rule that silently matches
nothing fails the gate instead of passing it.

Clang and db8
-------------

db8's `MojOsLinuxInternal.h` defines `MojAtomicCAS()` only for GCC 4.4 and
later and `#error`s otherwise; clang reports itself as GCC 4.2. Claiming a
later GCC (`-fgnuc-version=`) breaks glibc's fortified headers instead, so
the clang builds here put a copy of that one header, made from the
sysroot's on every run with clang accepted next to GCC, ahead of it on the
quoted-include path (`moj_clang_db8_shim` in `lib.sh`). The fix belongs in
db8; once it is there the shim can go.
