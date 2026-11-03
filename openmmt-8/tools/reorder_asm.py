#!/usr/bin/env python3
"""Reorder the functions of modules so more calls share their target's 2 KB page.

Usage: reorder_asm.py build_dir module...

relax_calls.py turns a call or jump into the 2-byte acall/ajmp only when the target sits in
the caller's 2 KB page; the order of the functions decides how many do. For each module this
rewrites <module>.asm from <module>.asm.pre (its code before relax_calls.py) with the CSEG
functions in a new order, chosen by simulated annealing on the layout of the last link
(<module>.rst and the map): sizes and call sites move with their function. acall/ajmp and
lcall/ljmp take the same cycles, so the order changes no timing. A function that can run
into the next one stays glued to it. The rewritten .pre is kept for the next round; the
order is only changed when the estimate improves.
"""
import glob
import math
import os
import random
import re
import sys

ROW = re.compile(r"^\s*([0-9A-F]{6})\s((?:[0-9A-F]{2} )*)")
SRC = re.compile(r"^.{34}\s*(\d+) ?(.*)$")
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9]*)::?\s*(;.*)?$")
LOCAL = re.compile(r"^(\d+\$):")
AREA = re.compile(r"^\s*\.area\s+(\w+)")
CALL = re.compile(r"^\s*(lcall|ljmp|acall|ajmp)\s+([A-Za-z_0-9$]+)\s*(;.*)?$")
END_OPS = {"ret", "reti", "ljmp", "ajmp", "sjmp", "jmp"}


def op_of(line):
    s = line.split(";", 1)[0].strip()
    if not s or s.endswith(":") or s.startswith("."):
        return None
    return s.split()[0].lower()


def parse_asm(path):
    """Lines, and the CSEG blocks: (label, [line indexes]) in file order."""
    lines = open(path).read().split("\n")
    blocks, area, cur = [], None, None
    for i, raw in enumerate(lines):
        m = AREA.match(raw)
        if m:
            area = m.group(1)
            cur = None
            continue
        if area != "CSEG":
            continue
        m = LABEL.match(raw.strip())
        if m and not LOCAL.match(raw.strip()):
            # The comments heading a function (relax_calls.py reads its name there) go
            # with it.
            head = []
            while cur and cur[1] and lines[cur[1][-1]].strip()[:1] in ("", ";"):
                head.insert(0, cur[1].pop())
            cur = [m.group(1), head]
            blocks.append(cur)
        if cur is None:
            cur = ["", []]
            blocks.append(cur)
        cur[1].append(i)
    return lines, blocks


def groups_of(lines, blocks):
    """Consecutive blocks that must stay together: a block not ending in a jump or return
    runs into the next."""
    out, cur = [], None
    for name, idx in blocks:
        if cur is None:
            cur = [[name], list(idx)]
        else:
            cur[0].append(name)
            cur[1].extend(idx)
        last = None
        for i in idx:
            o = op_of(lines[i])
            if o:
                last = o
        if name == "" or last not in END_OPS:
            continue
        out.append(cur)
        cur = None
    if cur:
        out.append(cur)
    return out


def parse_rst(path):
    """Normal CSEG labels with addresses in order, local label addresses by (scope, name),
    call sites (address, scope, target) and the CSEG end."""
    labels, locals_, sites = [], {}, []
    area, scope, end, table = None, None, 0, False
    for raw in open(path, errors="replace"):
        m = SRC.match(raw)
        if not m:
            continue
        text = m.group(2)
        a = AREA.match(text)
        if a:
            area = a.group(1)
            continue
        if area != "CSEG":
            continue
        r = ROW.match(raw)
        addr = int(r.group(1), 16) if r else None
        nb = len(r.group(2).split()) if r else 0
        t = text.strip()
        lm = LABEL.match(t)
        if lm and addr is not None and not LOCAL.match(t):
            scope = lm.group(1)
            labels.append((scope, addr))
            continue
        lo = LOCAL.match(t)
        if lo and addr is not None:
            locals_[(scope, lo.group(1))] = addr
            continue
        if addr is None:
            continue
        end = max(end, addr + nb)
        o = op_of(text)
        if o == "jmp" and "@a+dptr" in text:
            table = True
            continue
        c = CALL.match(text)
        if c and not (table and c.group(1) in ("ljmp", "ajmp")):
            sites.append((addr, scope, c.group(2)))
        if not (c and c.group(1) in ("ljmp", "ajmp")):
            table = False
    return labels, locals_, sites, end


def map_symbols(path):
    out = {}
    for line in open(path, errors="replace"):
        m = re.match(r"^C:\s+([0-9A-F]{8})\s+(\S+)", line)
        if m:
            out[m.group(2)] = int(m.group(1), 16)
    return out


def main():
    build, mods = sys.argv[1], sys.argv[2:]
    syms = map_symbols(glob.glob(os.path.join(build, "*.map"))[0])
    info = {}
    for mod in mods:
        lines, blocks = parse_asm(os.path.join(build, mod + ".asm.pre"))
        labels, locals_, sites, end = parse_rst(os.path.join(build, mod + ".rst"))
        info[mod] = (lines, blocks, labels, locals_, sites, end)
    # Fixed symbol addresses (other modules, library), then every module's own labels.
    addr_of = dict(syms)
    for mod in mods:
        for name, a in info[mod][2]:
            addr_of[name] = a

    units = []  # per module: list of [group_names, size]
    allsites = []  # (unit index, offset, target kind, target)
    unit_mod, unit_old = [], []
    bases = {}
    label_unit = {}
    for mod in mods:
        lines, blocks, labels, locals_, sites, end = info[mod]
        order = [n for n, _ in labels]
        pos = {n: a for n, a in labels}
        nxt = {order[i]: (labels[i + 1][1] if i + 1 < len(labels) else end) for i in range(len(order))}
        groups = groups_of(lines, blocks)
        bases[mod] = min(a for _, a in labels)
        for g in groups:
            names = [n for n in g[0] if n in pos]
            if not names:
                continue
            start = min(pos[n] for n in names)
            size = sum(nxt[n] - pos[n] for n in names)
            u = len(units)
            units.append((mod, g, start, size))
            for n in g[0]:
                label_unit[n] = u
        # A label of the .rst missing from the .pre (added by relax) belongs to the unit
        # whose range holds it.
    ustart = [u[2] for u in units]
    def unit_at(mod, a):
        best = None
        for i, u in enumerate(units):
            if u[0] == mod and u[2] <= a < u[2] + u[3]:
                best = i
        return best
    for mod in mods:
        lines, blocks, labels, locals_, sites, end = info[mod]
        for a, scope, tgt in sites:
            su = label_unit.get(scope)
            if su is None:
                su = unit_at(mod, a)
            if su is None:
                continue
            off = a - ustart[su]
            if tgt.endswith("$"):
                ta = locals_.get((scope, tgt))
                if ta is None:
                    continue
                allsites.append((su, off, su, ta - ustart[su]))
            elif tgt in label_unit and units[label_unit[tgt]][0] in mods:
                tu = label_unit[tgt]
                allsites.append((su, off, tu, addr_of[tgt] - ustart[tu]))
            elif tgt in addr_of:
                allsites.append((su, off, -1, addr_of[tgt]))

    per_mod = {mod: [i for i, u in enumerate(units) if u[0] == mod] for mod in mods}
    sizes = [u[3] for u in units]

    def layout(orders):
        st = [0] * len(units)
        for mod in mods:
            a = bases[mod]
            for i in orders[mod]:
                st[i] = a
                a += sizes[i]
        return st

    def cost(st):
        c = 0
        for su, off, tu, toff in allsites:
            s = st[su] + off
            t = toff if tu < 0 else st[tu] + toff
            if (s ^ t) & ~0x7FF:
                c += 1
        return c

    orders = {mod: sorted(per_mod[mod], key=lambda i: ustart[i]) for mod in mods}
    st = layout(orders)
    touch = [[] for _ in units]
    for k, (su, off, tu, toff) in enumerate(allsites):
        touch[su].append(k)
        if tu >= 0 and tu != su:
            touch[tu].append(k)

    def far(k):
        su, off, tu, toff = allsites[k]
        t = toff if tu < 0 else st[tu] + toff
        return ((st[su] + off) ^ t) & ~0x7FF != 0

    def local_cost(us):
        ks = set()
        for u in us:
            ks.update(touch[u])
        return sum(far(k) for k in ks)

    def place(o, lo, hi):
        a = st[o[lo]] if lo else bases[units[o[0]][0]]
        for x in range(lo, hi + 1):
            st[o[x]] = a
            a += sizes[o[x]]

    best = cost(st)
    start_cost = cur = best
    best_orders = {m: list(o) for m, o in orders.items()}
    rnd = random.Random(int(os.environ.get("REORDER_SEED", "1")))
    iters = int(os.environ.get("REORDER_ITERS", "400000"))
    temp0 = 1.5
    for it in range(iters):
        temp = temp0 * (1 - it / iters) + 0.02
        mod = rnd.choice(mods)
        o = orders[mod]
        n = len(o)
        if n < 3:
            continue
        i = rnd.randrange(n)
        j = min(n - 1, max(0, i + rnd.randint(-12, 12))) if rnd.random() < 0.7 else rnd.randrange(n)
        if i == j:
            continue
        lo, hi = min(i, j), max(i, j)
        us = o[lo:hi + 1]
        before = local_cost(us)
        lo_start = st[o[lo]]
        u = o.pop(i)
        o.insert(j, u)
        a = lo_start
        for x in range(lo, hi + 1):
            st[o[x]] = a
            a += sizes[o[x]]
        d = local_cost(us) - before
        if d <= 0 or rnd.random() < math.exp(-d / temp):
            cur += d
            if cur < best:
                best = cur
                best_orders = {m: list(x) for m, x in orders.items()}
        else:
            o.pop(j)
            o.insert(i, u)
            a = lo_start
            for x in range(lo, hi + 1):
                st[o[x]] = a
                a += sizes[o[x]]
    print(f"reorder: cross-page sites {start_cost} -> {best}")
    if best >= start_cost:
        for mod in mods:
            write(build, mod, info[mod][0], None, None)
        return
    for mod in mods:
        lines = info[mod][0]
        write(build, mod, lines, [units[i][1] for i in best_orders[mod]], info[mod][1])


def write(build, mod, lines, groups, blocks):
    pre = os.path.join(build, mod + ".asm.pre")
    if groups is None:
        text = "\n".join(lines)
    else:
        cseg = set()
        for _, idx in blocks:
            cseg.update(idx)
        moved = set()
        for g in groups:
            moved.update(g[1])
        # Blocks without a linked label (empty) stay where they were, ahead of the rest.
        keep = [i for _, idx in blocks for i in idx if i not in moved]
        out, placed = [], False
        for i, raw in enumerate(lines):
            if i in cseg or (AREA.match(raw) and AREA.match(raw).group(1) == "CSEG"):
                if not placed:
                    placed = True
                    out.append("\t.area CSEG    (CODE)")
                    out.extend(lines[k] for k in keep)
                    for g in groups:
                        out.extend(lines[k] for k in g[1])
                continue
            out.append(raw)
        text = "\n".join(out)
    open(pre, "w").write(text)
    open(os.path.join(build, mod + ".asm"), "w").write(text)


if __name__ == "__main__":
    main()
