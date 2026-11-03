#!/usr/bin/env python3
"""Per-function code size of the OpenMMT-8 core image, largest first.

Reads the relocated listings (build/core/*.rst) after tools/build_openmmt8_core.sh: each
function's size is the distance from its label to the next function label in the same
code area. Usage: rom_size_report.py [build_dir] [top_n]
"""
import glob
import os
import re
import sys

build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "build", "core")
top = int(sys.argv[2]) if len(sys.argv) > 2 else 60

label = re.compile(r"^\s+([0-9A-F]{6})\s+\d+\s+(_[A-Za-z_0-9]+)::?\s*$")
area = re.compile(r"\.area\s+(\w+)")
code_areas = {"CSEG", "CONST", "HOME", "GSINIT", "GSFINAL"}
rows = []
for rst in sorted(glob.glob(os.path.join(build, "*.rst"))):
    mod = os.path.basename(rst)[:-4]
    cur = None
    marks, area_end = [], {}
    for line in open(rst, errors="replace"):
        m = area.search(line)
        if m and ";" not in line.split(".area")[0]:
            cur = m.group(1)
            continue
        if cur not in code_areas:
            continue
        m = label.match(line)
        if m:
            marks.append((int(m.group(1), 16), m.group(2), cur))
        m2 = re.match(r"^\s+([0-9A-F]{6})\s+([0-9A-F]{2}(?: [0-9A-F]{2})*)", line)
        if m2:
            n = len(m2.group(2).split())
            area_end[cur] = max(area_end.get(cur, 0), int(m2.group(1), 16) + n)
    marks.sort()
    for i, (addr, name, a) in enumerate(marks):
        nxt = marks[i + 1] if i + 1 < len(marks) else None
        end = nxt[0] if nxt and nxt[2] == a else area_end.get(a, addr)
        rows.append((end - addr, mod, a, name, addr))

rows.sort(reverse=True)
total = sum(r[0] for r in rows)
for size, mod, a, name, addr in rows[:top]:
    print(f"{size:6d}  {mod:5s} {a:6s} 0x{addr:04X}  {name}")
print(f"{total:6d}  total over {len(rows)} labels")
