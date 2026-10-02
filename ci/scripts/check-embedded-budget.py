#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check the embedded budget of the Cortex-M0 library build.

Usage: check-embedded-budget.py <budget> <nm -u> <nm --defined-only>
                                <stack usage> [summary.md]

<budget> (ci/embedded-budget.txt) lists, one per line ("#" starts a
comment):
  symbol <name>       an external symbol the library may use
  stack-frame <bytes> the largest stack frame of any library function

The external symbols are those the objects of the library leave undefined
and no other object of it defines. Every one of them must be listed, and
every listed one must still be used, so the list says exactly what the
library needs from the C library and the compiler runtime. Every stack
frame must be static (no VLA, no alloca) and no larger than the ceiling,
which may only be lowered. Exits 1 on any difference.
"""

import re
import sys


def read_budget(path):
    symbols, frame = set(), None
    for line in open(path):
        line = line.split("#", 1)[0].split()
        if not line:
            continue
        if line[0] == "symbol" and len(line) == 2:
            symbols.add(line[1])
        elif line[0] == "stack-frame" and len(line) == 2:
            frame = int(line[1])
        else:
            raise SystemExit("%s: bad line: %s" % (path, " ".join(line)))
    if frame is None:
        raise SystemExit("%s: no stack-frame line" % path)
    return symbols, frame


def nm_symbols(path, defined):
    out = set()
    for line in open(path):
        f = line.split()
        if not f or f[-1].endswith(":"):
            continue
        if defined and len(f) == 3 and f[1] not in "Uw":
            out.add(f[2])
        elif not defined and len(f) == 2 and f[0] in "Uw":
            out.add(f[1])
    return out


def main():
    budget, undef, defined, stack = sys.argv[1:5]
    summary = sys.argv[5] if len(sys.argv) > 5 else None
    allowed, ceiling = read_budget(budget)
    external = nm_symbols(undef, False) - nm_symbols(defined, True)
    bad = 0
    lines = ["## Embedded budget (Cortex-M0)", ""]

    new = sorted(external - allowed)
    gone = sorted(allowed - external)
    for s in new:
        print("FAIL: the library now uses %s (not in %s)" % (s, budget))
    for s in gone:
        print("FAIL: %s is listed in %s but no longer used: remove it"
              % (s, budget))
    bad += len(new) + len(gone)
    print("external symbols (%d): %s" % (len(external),
                                         " ".join(sorted(external))))
    lines += ["External symbols (%d): `%s`" % (
        len(external), "`, `".join(sorted(external))), ""]

    frames = []
    for line in open(stack):
        m = re.match(r"(.*):\d+:\d+:(\S+)\s+(\d+)\s+(\S+)", line.strip())
        if not m:
            continue
        where, func, size, kind = m.group(1), m.group(2), int(m.group(3)), \
            m.group(4)
        frames.append((size, func, where.rsplit("/", 1)[-1], kind))
        if kind != "static":
            print("FAIL: %s (%s) has a %s stack frame" % (func, where, kind))
            bad += 1
    frames.sort(reverse=True)
    if not frames:
        raise SystemExit("no stack usage records in %s" % stack)
    top = frames[0][0]
    if top > ceiling:
        print("FAIL: %s has a %d byte stack frame, above the ceiling %d"
              % (frames[0][1], top, ceiling))
        bad += 1
    elif top < ceiling:
        print("NOTE: the largest frame is %d bytes: the ceiling %d in %s "
              "can be lowered" % (top, ceiling, budget))
    print("largest stack frames:")
    lines += ["| Stack frame (bytes) | Function | File |", "|---|---|---|"]
    for size, func, where, kind in frames[:10]:
        print("  %5d %s (%s)" % (size, func, where))
        lines.append("| %d | %s | %s |" % (size, func, where))
    if summary:
        with open(summary, "w") as f:
            f.write("\n".join(lines) + "\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
