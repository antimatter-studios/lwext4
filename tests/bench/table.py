#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Pages and checks of the benchmark (fork issue #165).

The measurements are the lines ci/jobs/bench.sh writes per platform:

  BENCH <platform> <op> <count> <unit> reads <n> writes <n> heap <b> stack <b>
  SIZE <platform> text <b> data <b> bss <b>

The dataset (docs/performance/data/*.txt) is the committed set of them;
everything else is made from it:

  table.py docs DATA OUT [README]   write OUT/README.md (overview) and
                                    OUT/<platform>.md (details), and the
                                    summary in README
  table.py check DATA... -- NEW...  fail if a figure of NEW is more than
                                    5 % (and 2 units) above the dataset
  table.py verify DATA OUT README   fail if the pages in OUT or the summary
                                    in README (between the bench-table
                                    markers) are not the ones DATA makes
  table.py table NEW...             print the tables of measurements
"""
import sys

BEGIN = "<!-- bench-table: begin (tests/bench/table.py) -->"
END = "<!-- bench-table: end -->"
MARGIN = 0.05
SLACK = 2


def load(paths):
    """{platform: {"ops": {op: {...}}, "size": {...}}} in input order."""
    data = {}
    for path in paths:
        with open(path) as f:
            for line in f:
                w = line.split()
                if not w or w[0] not in ("BENCH", "SIZE"):
                    continue
                p = data.setdefault(w[1], {"ops": {}, "size": {}})
                if w[0] == "BENCH":
                    rec = {"count": int(w[3]), "unit": w[4]}
                    rec.update({w[i]: int(w[i + 1])
                                for i in range(5, len(w) - 1, 2)})
                    p["ops"][w[2]] = rec
                else:
                    p["size"] = {w[i]: int(w[i + 1])
                                 for i in range(2, len(w) - 1, 2)}
    return data


def kib(n):
    return "%.1f KiB" % (n / 1024.0)


def kinsns(n):
    return "%d k" % round(n / 1000.0) if n >= 1000 else str(n)


def table(data):
    plats = list(data)
    ops = list(data[plats[0]]["ops"]) if plats else []
    out = []
    out.append("| | " + " | ".join(plats) + " |")
    out.append("|---|" + "---|" * len(plats))
    out.append("| Flash (whole library) | " + " | ".join(
        kib(data[p]["size"].get("text", 0)) for p in plats) + " |")
    out.append("| Static RAM (data + bss) | " + " | ".join(
        kib(data[p]["size"].get("data", 0) + data[p]["size"].get("bss", 0))
        for p in plats) + " |")
    out.append("| Peak heap | " + " | ".join(
        kib(max((o["heap"] for o in data[p]["ops"].values()), default=0))
        for p in plats) + " |")
    out.append("| Peak stack | " + " | ".join(
        kib(max((o["stack"] for o in data[p]["ops"].values()), default=0))
        for p in plats) + " |")
    out.append("")
    def head(p):
        unit = next(iter(data[p]["ops"].values()), {}).get("unit")
        return "%s %s" % (p, "time" if unit == "ns" else "instructions")

    def cost(p, op):
        rec = data[p]["ops"].get(op, {})
        if rec.get("unit") == "ns":
            return "%d µs" % round(rec.get("count", 0) / 1000.0)
        return kinsns(rec.get("count", 0))

    out.append("| Operation | Block reads | Block writes | " +
               " | ".join(head(p) for p in plats) + " |")
    out.append("|---|---|---|" + "---|" * len(plats))
    for op in ops:
        first = data[plats[0]]["ops"][op]
        out.append("| %s | %d | %d | " % (op, first["reads"], first["writes"])
                   + " | ".join(cost(p, op) for p in plats) + " |")
    return "\n".join(out) + "\n"


def worse(new, old):
    return new > old * (1 + MARGIN) + SLACK


def check(base, new):
    bad = []
    for p, d in new.items():
        if p not in base:
            bad.append("%s: not in the baseline" % p)
            continue
        for k, v in d["size"].items():
            if worse(v, base[p]["size"].get(k, 0)):
                bad.append("%s size %s: %d, baseline %d" %
                           (p, k, v, base[p]["size"].get(k, 0)))
        for op, rec in d["ops"].items():
            old = base[p]["ops"].get(op)
            if not old:
                bad.append("%s %s: not in the baseline" % (p, op))
                continue
            for k in ("count", "reads", "writes", "heap", "stack"):
                if rec["unit"] == "ns" and k == "count":
                    continue  # time on a host is no reproducible figure
                if worse(rec[k], old[k]):
                    bad.append("%s %s %s: %d, baseline %d" %
                               (p, op, k, rec[k], old[k]))
    return bad


NOTE = """\
These figures come from emulators: nobody has every board, so the
benchmark (tests/bench) runs on QEMU's MPS2 boards in CI. Instruction
counts, code and data sizes, heap, stack and block I/O are exact for the
code as built (arm-none-eabi-gcc -O2, see the toolchain files); real
hardware adds what an emulator does not model (caches, flash wait states,
the storage). Read them as a ball-park guide: at a clock of f MHz, n
million instructions are roughly n / f seconds of CPU, and on an MCU with
an SD card the block writes usually cost more than the CPU.

Flash is the code of the whole library (liblwext4.a): an upper bound,
since the linker leaves out what an application does not use.

The workloads run on a 3 MiB RAM disk, ext4 with a 1 MiB journal and
1 KiB blocks. The library has all its features, without debug output and
assertions (CONFIG_DEBUG_PRINTF=0, CONFIG_DEBUG_ASSERT=0), as a product
would ship it. Regenerate with `ci/run.sh bench update` (see
tests/bench/README.md).
"""


def pages(data):
    """{file name: content} of the pages DATA makes."""
    out = {}
    plats = list(data)
    out["README.md"] = ("# Performance\n\n" + NOTE + "\n" + table(data) +
                        "\nDetails per MCU: " + ", ".join(
                            "[%s](%s.md)" % (p, p) for p in plats) + ".\n")
    for p in plats:
        rows = ["# Performance on %s\n" % p, NOTE,
                "| Operation | CPU | Block reads | Block writes | "
                "Peak heap | Peak stack |", "|---|---|---|---|---|---|"]
        for op, rec in data[p]["ops"].items():
            cpu = ("%d µs" % round(rec["count"] / 1000.0)
                   if rec["unit"] == "ns" else
                   "%s instructions" % "{:,}".format(rec["count"]))
            rows.append("| %s | %s | %d | %d | %s | %s |" % (
                op, cpu, rec["reads"], rec["writes"], kib(rec["heap"]),
                kib(rec["stack"])))
        size = data[p]["size"]
        rows.append("")
        rows.append("Library (liblwext4.a, all of it): %s of code, %s of "
                    "initialised data, %s of zeroed data." % (kib(size.get("text", 0)),
                                      kib(size.get("data", 0)),
                                      kib(size.get("bss", 0))))
        out["%s.md" % p] = "\n".join(rows) + "\n"
    return out


def dataset(path):
    import glob
    import os
    return load(sorted(glob.glob(os.path.join(path, "*.txt"))))


def main(argv):
    import os
    if len(argv) >= 2 and argv[0] == "table":
        sys.stdout.write(table(load(argv[1:])))
        return 0
    if len(argv) in (3, 4) and argv[0] == "docs":
        data = dataset(argv[1])
        os.makedirs(argv[2], exist_ok=True)
        for name, text in pages(data).items():
            with open(os.path.join(argv[2], name), "w") as f:
                f.write(text)
        if len(argv) == 4:
            with open(argv[3]) as f:
                readme = f.read()
            if BEGIN not in readme or END not in readme:
                print("%s: no bench table markers" % argv[3])
                return 1
            head, rest = readme.split(BEGIN, 1)
            tail = rest.split(END, 1)[1]
            with open(argv[3], "w") as f:
                f.write(head + BEGIN + "\n" + table(data) + END + tail)
        return 0
    if len(argv) >= 4 and argv[0] == "check" and "--" in argv:
        k = argv.index("--")
        bad = check(load(argv[1:k]), load(argv[k + 1:]))
        for b in bad:
            print("WORSE " + b)
        if bad:
            print("Faster or smaller is fine; worse needs a new dataset: "
                  "ci/run.sh bench update")
        return 1 if bad else 0
    if len(argv) == 4 and argv[0] == "verify":
        data = dataset(argv[1])
        bad = 0
        for name, text in pages(data).items():
            path = os.path.join(argv[2], name)
            have = open(path).read() if os.path.exists(path) else None
            if have != text:
                print("%s is not the page the dataset makes "
                      "(ci/run.sh bench update)" % path)
                bad = 1
        with open(argv[3]) as f:
            readme = f.read()
        if BEGIN not in readme or END not in readme:
            print("%s: no bench table markers" % argv[3])
            return 1
        have = readme.split(BEGIN, 1)[1].split(END, 1)[0].strip("\n") + "\n"
        if have != table(data):
            print("%s: the benchmark summary is not the dataset's; it is:\n"
                  % argv[3] + table(data))
            bad = 1
        return bad
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
