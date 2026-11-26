#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# mutation-check.py [--work DIR] <corei7-64 recipe workdir>
#
# Shows the tests can fail: each mutant below breaks one behaviour they are
# meant to pin, in a copy of the tree, and the ASan+UBSan build (host gcc on
# the recipe's sysroot, as run-sanitizers.sh builds it) has to fail on it -
# through a test, through ASan, or by hanging past the timeout. The
# unmutated copy has to pass first, or nothing below means anything.
#
# One copy and one build serve all mutants: each is applied, built
# incrementally, run, and taken out again.

import argparse, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.expanduser("~/claude-scratch/mojomail/mutants"))
ap.add_argument("workdir")
opts = ap.parse_args()

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, "..", ".."))
COMPONENTS = ("common", "imap", "pop", "smtp")
BINARY = {"common": "test/email-common-tests", "imap": "test/mojomail-imap-tests",
          "pop": "test/mojomail-pop-tests", "smtp": "test/mojomail-smtp-tests"}

AS = "common/src/activity/ActivitySet.cpp"
CM = "common/src/client/CommandManager.cpp"
CMH = "common/include/public/email-common/client/CommandManager.h"
AEW = "common/src/email/AsyncEmailWriter.cpp"

# name, file, text, replacement, what should catch it
MUTANTS = [
    # The use-after-free that crashed mojomail-imap on the phone
    ("activity-ref-then-erase", AS,
     "const ActivityPtr activity = *it;", "const ActivityPtr& activity = *it;",
     "AddressSanitizer: heap-use-after-free"),
    ("active-command-ref-then-erase", CM,
     "\t\t\tfound = true;\n\t\t\t// Take a copy: erase() shifts the next command into this slot.\n"
     "\t\t\tconst CommandPtr completed = *it;",
     "\t\t\tfound = true;\n\t\t\t// Take a copy: erase() shifts the next command into this slot.\n"
     "\t\t\tconst CommandPtr& completed = *it;",
     "CommandManagerTest.TestCompletedCommandKeptUntilCleanup"),
    ("pending-queue-not-reheaped", CMH,
     "c.erase(it); std::make_heap(c.begin(), c.end(), comp);", "c.erase(it);",
     "CommandManagerTest.TestAbortPendingKeepsOrder"),
    # The IDLE refresh learns the shortest drop, never a longer one
    ("idle-limit-takes-longer-drop", "imap/inc/client/IdleLimitTracker.h",
     "int limit = std::min(seconds, m_lastShortDrop);", "int limit = std::max(seconds, m_lastShortDrop);",
     "IdleLimitTrackerTest"),
    # Attachment parts: what the writer must refuse, and must not hang on
    ("part-follows-symlinks", AEW,
     "O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK", "O_RDONLY | O_CLOEXEC | O_NONBLOCK",
     "AsyncEmailWriterTest.TestSymlinkPartRefused"),
    ("part-open-blocks-on-fifo", AEW,
     "O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK", "O_RDONLY | O_NOFOLLOW | O_CLOEXEC",
     "timeout"),
    ("part-not-regular-accepted", AEW,
     "if(!S_ISREG(st.st_mode)) {", "if(false) {",
     "AsyncEmailWriterTest.TestFifoPartRefused"),
    # zlib refuses a raw-deflate window of 2^8
    ("deflate-window-256", "common/include/public/email-common/stream/DeflaterOutputStream.h",
     "int windowBits = -9,", "int windowBits = -8,",
     "DeflateInflateTest.TestDeflateInflate"),
    # Legacy suites, revived: each still pins what it was written for
    ("qp-soft-line-break-kept", "common/src/stream/QuotePrintableDecoderOutputStream.cpp",
     "if (m_hi=='\\r' && lo=='\\n')", "if (false && m_hi=='\\r' && lo=='\\n')",
     "QuotePrintableDecoderOutputStreamTest"),
    ("uidvalidity-overflow-not-truncated", "imap/src/protocol/ExamineResponseParser.cpp",
     "m_uidValidity = (UID) uidValidity;",
     "m_uidValidity = uidValidity > MojUInt32Max ? 0 : (UID) uidValidity;",
     "ExamineResponseParserTest.TestUIDValidityOverflow"),
    ("uidmap-keeps-duplicates", "imap/src/sync/UIDMap.cpp",
     "m_uids.erase(std::unique(m_uids.begin(), m_uids.end()), m_uids.end());",
     "std::unique(m_uids.begin(), m_uids.end());",
     "UIDMapTest.TestDuplicatesRemoved"),
    ("gio-channel-hides-closed", "common/include/public/email-common/async/GIOChannelWrapper.h",
     "	guint			m_watchWriteId;\n};",
     "	guint			m_watchWriteId;\n	bool			m_closed = false;\n};",
     "GIOChannelWrapperTest.TestIsClosedAfterShutdown"),
    ("smtp-validator-quits-before-reply", "smtp/src/SmtpValidator.cpp",
     "	// Reply before QUIT, as LoginSuccess() does.",
     "	RunState(State_SendQuitCommand);\n	// Reply before QUIT, as LoginSuccess() does.",
     "SmtpValidatorTest.TestLoginFailureRepliesBeforeQuit"),
    ("pop-cache-keeps-older-than-oldest", "pop/src/data/OldEmailsCache.cpp",
     "if (timestamp < m_oldestEmailTimestamp) {", "if (false) {",
     "OldEmailsCache"),
    # Wi-Fi whose online check failed must not carry the push connection
    ("wifi-onInternet-no-ignored", "common/src/network/NetworkStatus.cpp",
     'return value == "no";', "return false;",
     "NetworkStatusTest.WifiWithoutInternetLeavesPushToCellular"),
    ("onInternet-false-ignored", "common/src/network/NetworkStatus.cpp",
     "return !onInternet.boolValue();", "return false;",
     "NetworkStatusTest.BooleanOnInternet"),
    ("onInternet-unknown-taken-as-offline", "common/src/network/NetworkStatus.cpp",
     'return value == "no";', 'return value != "yes";',
     "NetworkStatusTest.UnrecognisedOnInternetIsIgnored"),
    # webOS OSE's cellular: "connected" and its data context's address
    ("wan-connected-flag-ignored", "common/src/network/NetworkStatus.cpp",
     'if(!hasState && status.get("connected", connectedFlag) && connectedFlag) {', "if(false) {",
     "NetworkStatusTest.CellularAloneCarriesPush"),
    ("wan-context-address-ignored", "common/src/network/NetworkStatus.cpp",
     "\t\tm_ipAddress = ContextIpAddress(status);", "",
     "NetworkStatusTest.CellularAddressFromConnectedContext"),
    ("wan-disconnected-context-taken", "common/src/network/NetworkStatus.cpp",
     'if(it->get("connected", connected) && !connected) {', "if(false) {",
     "NetworkStatusTest.CellularAddressFromConnectedContext"),
    # The status followed from the connection manager, not frozen at the
    # activity's start
    ("status-not-subscribed", "common/src/network/NetworkStatusMonitor.cpp",
     "void NetworkStatusMonitor::StartMonitoring()\n{\n\tSubscribe();",
     "void NetworkStatusMonitor::StartMonitoring()\n{",
     "NetworkStatusMonitorTest.FollowsWifiGoing"),
    ("activity-overrides-newer-status", "common/src/network/NetworkStatusMonitor.cpp",
     "if(m_hasDirectStatus) {", "if(false) {",
     "NetworkStatusMonitorTest.ActivityDoesNotOverrideNewerStatus"),
    ("status-not-asked-again", "common/src/network/NetworkStatusMonitor.cpp",
     "\t\tm_statusSlot.cancel();\n\t\tm_statusRequested = false;", "\t\tm_statusSlot.cancel();",
     "NetworkStatusMonitorTest.FallsBackToTheActivity"),
]

TREE = os.path.join(opts.work, "src")
BUILD = os.path.join(opts.work, "build")


def sh(script, *args, **kw):
    # lib.sh from the copy, so the build is of the copy
    return subprocess.run(["sh", "-c", '. "$0/tests/hardening/lib.sh"; ' + script, TREE] + list(args),
                          capture_output=True, text=True, **kw)


def build():
    r = sh('moj_env "$1"; SRC=$0; moj_host_runtimes "$2"; '
           'moj_build "$3" gcc address,undefined - "$2"',
           opts.workdir, os.path.join(opts.work, "hostrt"), BUILD)
    return r.returncode == 0, r.stdout + r.stderr


def run_tests():
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:fast_unwind_on_malloc=0",
               UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1", G_SLICE="always-malloc",
               DEBUGINFOD_URLS="")
    out, failed = "", False
    for c in COMPONENTS:
        try:
            r = subprocess.run([os.path.join(BUILD, c, BINARY[c])], cwd=os.path.join(BUILD, c),
                               capture_output=True, text=True, env=env, timeout=120)
            out += r.stdout + r.stderr
            failed = failed or r.returncode != 0
        except subprocess.TimeoutExpired:
            out += "timeout: %s did not finish\n" % c
            failed = True
    return failed, out


shutil.rmtree(TREE, ignore_errors=True)
shutil.copytree(SRC, TREE, ignore=shutil.ignore_patterns(".git"))
ok, out = build()
if not ok:
    print("FAIL the unmutated tree does not build:\n" + out[-3000:])
    sys.exit(1)
failed, out = run_tests()
if failed:
    print("FAIL the unmutated tree does not pass:\n" + out[-3000:])
    sys.exit(1)
print("ok   unmutated tree passes")

fail = False
for name, path, old, new, catcher in MUTANTS:
    p = os.path.join(TREE, path)
    original = open(p).read()
    if original.count(old) != 1:
        print("FAIL %-36s mutation does not apply (%d matches)" % (name, original.count(old)))
        fail = True
        continue
    open(p, "w").write(original.replace(old, new))
    try:
        ok, out = build()
        if not ok:
            print("FAIL %-36s does not build:\n%s" % (name, out[-2000:]))
            fail = True
            continue
        failed, out = run_tests()
        if not failed:
            print("FAIL %-36s survived: no test failed" % name)
            fail = True
        elif catcher not in out:
            print("ok   %-36s killed, but not by %s" % (name, catcher))
        else:
            print("ok   %-36s killed by %s" % (name, catcher))
    finally:
        open(p, "w").write(original)

# The last mutant's build has to go before the tree is used again
ok, _ = build()
sys.exit(1 if fail or not ok else 0)
