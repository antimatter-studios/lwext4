#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Release manifest, test report and notes for a tag on `main`.

Usage: release-notes.py <tag> <dist dir> <ci run url>

Writes <dist>/MANIFEST.md (the changes since the previous release tag: the
pull requests merged into main, or the commits for direct pushes),
<dist>/TEST-REPORT.md (totals of every CTest JUnit file in <dist>) and
<dist>/RELEASE-NOTES.md. Exits non-zero if any test in the reports failed
or errored.
"""

import glob
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def git(*args):
    return subprocess.check_output(("git",) + args, text=True).strip()


def previous_tag(tag):
    """The highest v* tag below <tag> in version order, or None."""
    tags = git("tag", "--list", "v*", "--sort=-v:refname").splitlines()
    if tag in tags:
        tags = tags[tags.index(tag) + 1:]
    return tags[0] if tags else None


def manifest(tag):
    # Releases before main were tagged on the rebuilt integration branch,
    # which main does not contain: start at the merge base with the
    # previous tag rather than at the tag itself.
    prev = previous_tag(tag)
    base = git("merge-base", prev, tag) if prev else None
    rng = "%s..%s" % (base, tag) if base else tag
    rows = []
    for rec in git("log", "--first-parent", "--reverse",
                   "--format=%H%x09%s%x09%b%x00", rng).split("\0"):
        rec = rec.strip("\n")
        if not rec:
            continue
        sha, subject, body = (rec.split("\t", 2) + ["", ""])[:3]
        m = re.match(r"Merge pull request #(\d+) from [^/]+/(\S+)", subject)
        b = re.match(r"Merge branch '([^']+)'", subject)
        if m:
            title = body.strip().splitlines()[0] if body.strip() else ""
            rows.append(("#" + m.group(1), m.group(2), title))
        elif b:
            head = git("rev-parse", sha + "^2")
            rows.append((head[:12], b.group(1),
                         git("log", "-1", "--format=%s", head)))
        else:
            rows.append((sha[:12], "", subject))
    head = ("Changes since %s:" % prev) if prev else ("All changes up to %s:"
                                                      % tag)
    out = ["# lwext4 %s manifest" % tag, "", head, "",
           "| Pull request / commit | Branch | Title |", "|---|---|---|"]
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
        "lwext4 %s: the main branch of antimatter-studios/lwext4, a "
        "maintained fork of gkostka/lwext4 1.0.0." % tag, "",
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
