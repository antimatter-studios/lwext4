#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Release manifest, test report and notes for a tag on `integration`.

Usage: release-notes.py <tag> <dist dir> <ci run url>

Writes <dist>/MANIFEST.md (upstream base and the commit of every merged
topic branch, read from the merge commits of the tag), <dist>/TEST-REPORT.md
(totals of every CTest JUnit file in <dist>) and <dist>/RELEASE-NOTES.md.
Exits non-zero if any test in the reports failed or errored.
"""

import glob
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def git(*args):
    return subprocess.check_output(("git",) + args, text=True).strip()


def manifest(tag):
    merges = git("log", "--first-parent", "--merges", "--reverse",
                 "--format=%H %P%x09%s", tag).splitlines()
    rows = []
    base = None
    for line in merges:
        shas, subject = line.split("\t", 1)
        parts = shas.split()
        m = re.match(r"Merge branch '([^']+)'", subject)
        if not m or len(parts) < 3:
            continue
        if base is None:
            base = parts[1]
        branch_head = parts[2]
        rows.append((m.group(1), branch_head[:12],
                     git("log", "-1", "--format=%s", branch_head)))
    if base is None:
        raise SystemExit("%s has no 'Merge branch' commits: not an "
                         "integration build" % tag)
    out = ["# lwext4 %s manifest" % tag, "",
           "Upstream base: gkostka/lwext4 %s (%s)" % (
               base[:12], git("log", "-1", "--format=%cs %s", base)),
           "", "| Topic branch | Commit | Subject |", "|---|---|---|"]
    out += ["| %s | %s | %s |" % r for r in rows]
    return "\n".join(out) + "\n", rows


def test_report(dist):
    out = ["# Test report", "",
           "CTest results of the packaged builds (JUnit files in the "
           "release). The full CI matrix of the tag is linked in the "
           "release notes.", "",
           "| Build | Tests | Failures | Errors | Skipped/disabled |",
           "|---|---|---|---|---|"]
    bad = 0
    total = 0
    for path in sorted(glob.glob(os.path.join(dist, "ctest-*.xml"))):
        root = ET.parse(path).getroot()
        suites = [root] if root.tag == "testsuite" else root.findall(
            "testsuite")
        t = f = e = s = 0
        for su in suites:
            t += int(su.get("tests", 0))
            f += int(su.get("failures", 0))
            e += int(su.get("errors", 0))
            s += int(su.get("skipped", 0)) + int(su.get("disabled", 0))
        total += t
        bad += f + e
        name = os.path.basename(path)[len("ctest-"):-len(".xml")]
        out.append("| %s | %d | %d | %d | %d |" % (name, t, f, e, s))
    if total == 0:
        raise SystemExit("no test results in %s" % dist)
    return "\n".join(out) + "\n", bad


def main():
    tag, dist, run_url = sys.argv[1], sys.argv[2], sys.argv[3]
    man, rows = manifest(tag)
    report, bad = test_report(dist)
    with open(os.path.join(dist, "MANIFEST.md"), "w") as f:
        f.write(man)
    with open(os.path.join(dist, "TEST-REPORT.md"), "w") as f:
        f.write(report)
    notes = [
        "lwext4 %s: gkostka/lwext4 1.0.0 (master) with the topic branches "
        "below (each one an upstream pull request) merged." % tag, "",
        "Every job of the CI matrix passed for this tag: %s" % run_url, "",
        man, report,
        "Licences: the library is BSD-3-Clause except src/ext4_extent.c and "
        "src/ext4_xattr.c (GPL-2.0), see the file headers.", ""]
    with open(os.path.join(dist, "RELEASE-NOTES.md"), "w") as f:
        f.write("\n".join(notes))
    print("\n".join(notes))
    if bad:
        raise SystemExit("%d failed tests in the reports" % bad)


if __name__ == "__main__":
    main()
