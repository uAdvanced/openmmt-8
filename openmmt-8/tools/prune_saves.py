#!/usr/bin/env python3
"""Drop the register saves around calls that cannot change the saved registers.

Usage: prune_saves.py build_dir module...

SDCC saves every live register r0-r7 around a call (push arN ... pop arN), not knowing
what the callee uses. From all modules' code this finds the registers each label can write,
through everything it calls or jumps to and anything it runs on into (unknown code, such as
the library, and code touching SP count as writing all of them). In the modules named, a
push/pop pair of arN around straight-line code with calls (no labels or branches) is
removed when neither that code nor its callees write rN. Execution only gets shorter, so
the functions whose cycles matter (outline_asm.SKIP, SKIP_TREES) are left alone.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import outline_asm  # noqa: E402

ALL = frozenset(range(8))
REG = re.compile(r"^\(?(?:a?r|0x0?)([0-7])\)?$")
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9]*)::?$")
CALLS = {"lcall", "acall"}
JUMPS = {"ljmp", "ajmp"}
END_OPS = {"ret", "reti", "ljmp", "ajmp", "sjmp", "jmp"}
BRANCH = outline_asm.BRANCH | {"jmp"}
DEST_FIRST = {"mov", "add", "addc", "subb", "anl", "orl", "xrl", "inc", "dec", "djnz", "pop",
              "movx", "movc", "clr", "cpl", "setb", "rl", "rr", "rlc", "rrc", "swap", "da",
              "xchd"}


def code(line):
    return re.sub(r"\s+", " ", line.split(";", 1)[0].strip())


def split(c):
    op, _, rest = c.partition(" ")
    args = [a.strip() for a in rest.split(",")] if rest else []
    return op.lower(), args


def written(c):
    """Registers r0-r7 an instruction writes (None: it touches SP, so any)."""
    if outline_asm.SP_REF.search(c):
        return None
    op, args = split(c)
    regs = set()
    if op in DEST_FIRST and args:
        m = REG.match(args[0])
        if m:
            regs.add(int(m.group(1)))
    if op == "xch":
        for a in args:
            m = REG.match(a)
            if m:
                regs.add(int(m.group(1)))
    return regs


def parse(path):
    """label -> (writes or None, targets, next label if it can run on)."""
    out, order = {}, []
    area, cur = None, None
    for raw in open(path).read().split("\n"):
        c = code(raw)
        if not c:
            continue
        if c.startswith(".area"):
            area = c.split()[1]
            continue
        if area != "CSEG":
            continue
        m = LABEL.match(c)
        if m:
            if cur is not None and cur[3] not in END_OPS:
                cur[2].add(m.group(1))
            cur = [set(), set(), set(), None]
            out[m.group(1)] = cur
            order.append(m.group(1))
            continue
        if cur is None or c.endswith(":") or c.startswith("."):
            continue
        op, args = split(c)
        w = written(c)
        if w is None:
            cur[0] = None
        elif cur[0] is not None:
            cur[0] |= w
        if op in CALLS | JUMPS and args and not args[0].endswith("$"):
            cur[1].add(args[0])
        cur[3] = op
    return out


def clobbers(blocks):
    """Fixpoint of each label's own writes joined with those of everything it reaches."""
    clob = {n: ALL if b[0] is None else frozenset(b[0]) for n, b in blocks.items()}
    changed = True
    while changed:
        changed = False
        for n, (_, targets, nxt, _) in blocks.items():
            acc = set(clob[n])
            for t in targets | nxt:
                acc |= clob.get(t, ALL)
            if len(acc) != len(clob[n]):
                clob[n] = frozenset(acc)
                changed = True
    return clob


def prune(path, clob, skip):
    lines = open(path).read().split("\n")
    fn, removed = "", set()
    cs = [code(l) for l in lines]
    i, n = 0, len(lines)
    while i < n:
        m = re.search(r";\s*function (\S+)", lines[i])
        if m:
            fn = m.group(1)
        op, args = split(cs[i]) if cs[i] else ("", [])
        if op != "push" or fn in skip:
            i += 1
            continue
        pushes, j = [], i
        while j < n:
            o, a = split(cs[j]) if cs[j] else ("", [])
            if o == "push":
                pushes.append((j, a[0]))
            elif cs[j]:
                break
            j += 1
        # The straight-line code with its calls, then the pops.
        hit, body_w, called = False, set(), False
        while j < n:
            c = cs[j]
            if not c:
                j += 1
                continue
            o, a = split(c)
            if o in ("pop", "push") or c.endswith(":") or c.startswith(".") or o in BRANCH \
                    or o in ("ret", "reti") or o in JUMPS:
                break
            w = written(c)
            if w is None:
                body_w = set(ALL)
            else:
                body_w |= w
            if o in CALLS:
                body_w |= clob.get(a[0], ALL)
                called = True
            j += 1
        pops = []
        while j < n:
            o, a = split(cs[j]) if cs[j] else ("", [])
            if o == "pop":
                pops.append((j, a[0]))
            elif cs[j]:
                break
            j += 1
        if called and len(pops) >= len(pushes) and \
                [r for _, r in pops[:len(pushes)]] == [r for _, r in reversed(pushes)]:
            for (pl, r), (ql, _) in zip(reversed(pushes), pops):
                m = REG.match(r)
                if m and int(m.group(1)) not in body_w:
                    removed.update((pl, ql))
        i = j if j > i else i + 1
    if removed:
        open(path, "w").write("\n".join(l for k, l in enumerate(lines) if k not in removed))
    return len(removed)


def main():
    build, mods = sys.argv[1], sys.argv[2:]
    blocks = {}
    for asm in glob.glob(os.path.join(build, "*.asm")):
        blocks.update(parse(asm))
    clob = clobbers(blocks)
    skip = set(outline_asm.SKIP) | outline_asm.callees(build, outline_asm.SKIP_TREES)
    total = 0
    for mod in mods:
        k = prune(os.path.join(build, mod + ".asm"), clob, skip)
        total += k
        print(f"prune {mod}: {k} instructions ({2 * k} bytes)")


if __name__ == "__main__":
    main()
