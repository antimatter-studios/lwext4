#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Flash/RAM use per component, from a GNU ld map file.

  footprint.py firmware.map

Only what survived --gc-sections is counted. .text/.rodata are flash,
.data is flash (initial values) and RAM, .bss is RAM. Heap and stack are
not in the map file: the firmware reports their high water marks at run
time ("MEM:" lines on the console).
"""
import re
import sys
from collections import defaultdict


def component(path):
    m = re.search(r"([^/\\]+\.a)\(([^)]+)\)", path)
    if m:
        lib, obj = m.groups()
        if lib == "liblwext4.a":
            return "lwext4 (%s)" % re.sub(r"\.c\.obj$|\.o$", "", obj)
        if lib.startswith(("libc", "libg", "libm", "libnosys")):
            return "newlib-nano"
        if lib.startswith("libgcc"):
            return "libgcc"
        return lib
    base = re.sub(r"\.c\.obj$|\.o$", "", path.split("/")[-1])
    if base == "board":  # platforms/<board>/board.c
        return "app: board support"
    if base in ("crti", "crtn", "crtbegin", "crtend", "crt0"):
        return "libgcc"
    return "app: " + base


def main():
    sizes = defaultdict(lambda: [0, 0, 0])  # text+rodata, data, bss
    out_size = defaultdict(int)  # output section sizes
    in_size = defaultdict(int)   # sum of their input sections
    out_sec = None
    in_map = False
    pending = None
    for line in open(sys.argv[1], errors="replace"):
        if line.startswith("Linker script and memory map"):
            in_map = True
            continue
        if not in_map:
            continue
        m = re.match(r"^(\.[\w.]+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)", line)
        if m:
            out_sec = m.group(1)
            out_size[out_sec] += int(m.group(2), 16)
            continue
        m = re.match(r"^(\.[\w.]+)\s", line)
        if m:
            out_sec = m.group(1)
            continue
        if out_sec not in (".text", ".data", ".bss", ".ARM.exidx"):
            continue
        # " .text.foo  0xADDR  0xSIZE  file" or name alone on one line
        m = re.match(r"^ (\.\S+|COMMON)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+)$",
                     line.rstrip())
        if not m and pending:
            m2 = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+)$",
                          line.rstrip())
            if m2:
                m = (pending,) + m2.groups()
            pending = None
        elif not m:
            m1 = re.match(r"^ (\.\S+|COMMON)\s*$", line)
            pending = m1.group(1) if m1 else None
            continue
        if isinstance(m, tuple):
            _, _, size, path = m
        else:
            _, _, size, path = m.groups()
        size = int(size, 16)
        if not size or path.startswith("*"):
            continue
        idx = {".text": 0, ".ARM.exidx": 0, ".data": 1, ".bss": 2}[out_sec]
        sizes[component(path)][idx] += size
        in_size[out_sec] += size

    # Identical string constants (e.g. __FILE__ of the asserts) of different
    # objects are merged by the linker; the map lists them per object.
    merged = [0, 0, 0]
    for sec, idx in ((".text", 0), (".ARM.exidx", 0), (".data", 1), (".bss", 2)):
        merged[idx] += out_size[sec] - in_size[sec]
    if any(merged):
        sizes["(merged strings, padding)"] = merged

    lw = [0, 0, 0]
    rows = []
    for name, (t, d, b) in sizes.items():
        if name.startswith("lwext4"):
            lw = [lw[0] + t, lw[1] + d, lw[2] + b]
        rows.append((name, t, d, b))
    rows.sort(key=lambda r: -(r[1] + r[2]))
    total = [sum(r[i] for r in rows) for i in (1, 2, 3)]
    fmt = "%-32s %9s %7s %7s %9s %8s"
    print(fmt % ("component", "text+ro", "data", "bss", "flash", "ram"))
    for name, t, d, b in [("lwext4 (total)",) + tuple(lw)] + rows + [
            ("TOTAL",) + tuple(total)]:
        print(fmt % (name, t, d, b, t + d, d + b))


if __name__ == "__main__":
    main()
