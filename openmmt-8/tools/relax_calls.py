#!/usr/bin/env python3
"""Shorten lcall/ljmp to acall/ajmp where the linked target shares the 2 KB page.

Reads each module's .rst from a link of the unshortened code and rewrites the .asm files in
place. Each shortened site moves everything after it down one byte, so the new layout is
computed from the chosen set, sites that no longer share a page with their target are
dropped, and this repeats until the set is stable. Only CSEG code is shortened (HOME holds
the fixed-address interrupt vectors), and ljmp entries of switch jump tables keep their
3-byte stride. Prints the number of sites converted.
"""
import bisect
import glob
import os
import re
import sys

RST = re.compile(r"\s+([0-9A-F]{6})\s+(12|02) ([0-9A-F]{2}) ([0-9A-F]{2})\s+\[\s*\d+\]\s+(\d+)\s+(lcall|ljmp)\s")


def table_lines(lines):
    """Line indexes of ljmp entries following a jmp @a+dptr."""
    out, in_table = set(), False
    for i, raw in enumerate(lines):
        s = raw.split(";")[0].strip()
        if not s or s.endswith(":"):
            continue
        op = s.split()[0]
        if op == "jmp" and "@a+dptr" in s:
            in_table = True
        elif in_table and op == "ljmp":
            out.add(i)
        else:
            in_table = False
    return out


def page(a):
    return a & ~0x7FF


# These run on the tick, the key grid, or once per played event. Folding a call in
# changes how many repeats fit while a key is held.
# Register pairs the 9-byte reload uses often enough that one helper is smaller.
# r6:r7 is the compiler's usual temp; r4:r5 and r5:r6 show up in the edit/copy walks.
LOAD_PAIRS = (("4", "5", "_load_r45"), ("5", "6", "_load_r56"), ("6", "7", "_load_xptr"))
SKIP_FOLD = {
    "seq_tick", "track_walk", "keys_scan", "keys_act_wait", "seq_midi_in", "main",
    "rec_event", "rec_commit", "emit", "mem_move", "tab_fix", "ui_screen",
    *(name for _, _, name in LOAD_PAIRS),
}


def _op(line):
    return re.sub(r"\s+", "", line.split(";")[0]).lower()


def _load_seq(lo, hi):
    return [
        "movxa,@dptr", f"movr{lo},a", "incdptr", "movxa,@dptr",
        f"movr{hi},a", f"movdpl,r{lo}", f"movdph,r{hi}",
    ]


def fold_xptr_loads(lines):
    """Replace a 9-byte xdata-pointer reload with lcall to a helper.

    The helper runs those exact instructions, so the register pair and dptr come
    out the same. Stack grows by the return address only. Tick and key-scan
    functions are left inline so their cycle counts stay put.
    """
    seqs = [(_load_seq(lo, hi), name) for lo, hi, name in LOAD_PAIRS]
    fn, out, i, n = "", [], 0, 0
    used = set()
    while i < len(lines):
        m = re.search(r";\s*function (\S+)", lines[i])
        if m:
            fn = m.group(1)
        elif lines[i].startswith("_load_"):
            fn = lines[i].split(":", 1)[0]
        window = [_op(lines[i + k]) for k in range(7)] if i + 7 <= len(lines) else None
        hit = next((name for seq, name in seqs if window == seq), None)
        if hit and fn not in SKIP_FOLD:
            indent = re.match(r"\s*", lines[i]).group(0)
            out.append(f"{indent}lcall\t{hit}")
            used.add(hit)
            i += 7
            n += 1
        else:
            out.append(lines[i])
            i += 1
    return out, n, used


def fold_const_loads(lines):
    """Fold `mov dptr,#abs / movx / mov rX / inc / movx / mov rY` when it repeats.

    The two instructions that put that pair back into dptr are the other fold;
    those sites are left alone. The helper is the same six instructions, so dptr
    still points at the high byte afterwards. Cold code only.
    """
    fn, i = "", 0
    hits = {}
    while i + 6 <= len(lines):
        m = re.search(r";\s*function (\S+)", lines[i])
        if m:
            fn = m.group(1)
        lab = lines[i].split(";")[0].strip()
        if lab.startswith("_ldc_"):
            fn = lab[:-1]
        ops = [_op(lines[i + k]) for k in range(6)]
        mo = re.match(r"movdptr,#(.+)", ops[0])
        rx = re.match(r"movr([0-7]),a", ops[2])
        ry = re.match(r"movr([0-7]),a", ops[5])
        if (mo and rx and ry and ops[1] == "movxa,@dptr" and ops[3] == "incdptr"
                and ops[4] == "movxa,@dptr" and fn not in SKIP_FOLD
                and not fn.startswith("_ldc_")):
            nxt = [_op(lines[i + 6]), _op(lines[i + 7])] if i + 8 <= len(lines) else ["", ""]
            if nxt != [f"movdpl,r{rx.group(1)}", f"movdph,r{ry.group(1)}"]:
                key = (mo.group(1), rx.group(1), ry.group(1))
                hits.setdefault(key, []).append(i)
        i += 1
    chosen = {k: v for k, v in hits.items() if len(v) >= 4}
    if not chosen:
        return lines, 0
    names = {}
    for n, key in enumerate(chosen):
        addr = re.sub(r"[^0-9a-z]+", "", key[0].lower())[:12]
        names[key] = f"_ldc_{key[1]}{key[2]}_{addr}"
    skip = {i for v in chosen.values() for i in v}
    out, i = [], 0
    while i < len(lines):
        key = next((k for k, v in chosen.items() if i in v), None)
        if key is None:
            out.append(lines[i])
            i += 1
            continue
        indent = re.match(r"\s*", lines[i]).group(0)
        out.append(f"{indent}lcall\t{names[key]}")
        i += 6
    extra = []
    have = "".join(lines)
    for key, name in names.items():
        if f"{name}:" in have:
            continue
        addr, lo, hi = key
        # Keep the assembler's own spacing by copying a site's address operand.
        src = lines[chosen[key][0]]
        imm = src.split("#", 1)[1].split(";")[0].strip()
        extra.extend([
            "",
            "\t.area CSEG    (CODE)",
            f"\t.globl {name}",
            f"{name}:",
            f"\tmov\tdptr,#{imm}",
            "\tmovx\ta,@dptr",
            f"\tmov\tr{lo},a",
            "\tinc\tdptr",
            "\tmovx\ta,@dptr",
            f"\tmov\tr{hi},a",
            "\tret",
        ])
    return out + extra, sum(len(v) for v in chosen.values())


def ensure_load_helpers(lines, used):
    extra = []
    have = {l.split(":", 1)[0] for l in lines if l.startswith("_load_")}
    for lo, hi, name in LOAD_PAIRS:
        if name not in used or name in have:
            continue
        extra.extend([
            "",
            "\t.area CSEG    (CODE)",
            f"\t.globl {name}",
            f"{name}:",
            "\tmovx\ta,@dptr",
            f"\tmov\tr{lo},a",
            "\tinc\tdptr",
            "\tmovx\ta,@dptr",
            f"\tmov\tr{hi},a",
            f"\tmov\tdpl,r{lo}",
            f"\tmov\tdph,r{hi}",
            "\tret",
        ])
    return lines + extra


def main(build_dir):
    asms, sites = {}, []
    for rst in glob.glob(os.path.join(build_dir, "*.rst")):
        asm = rst[:-4] + ".asm"
        if not os.path.exists(asm):
            continue
        lines = open(asm).read().split("\n")
        asms[asm] = lines
        tables = table_lines(lines)
        area = None
        for line in open(rst):
            a = re.search(r"\.area\s+(\w+)", line)
            if a:
                area = a.group(1)
            m = RST.match(line)
            if m and area == "CSEG" and int(m.group(5)) - 1 not in tables:
                sites.append((int(m.group(1), 16), int(m.group(3) + m.group(4), 16), asm, int(m.group(5)) - 1))
    chosen = [s for s in sites if page(s[0]) == page(s[0] + 2) == page(s[1])]
    while True:
        at = sorted(s[0] for s in chosen)

        def moved(a):
            return a - bisect.bisect_left(at, a)

        keep = []
        for s in chosen:
            a, t = moved(s[0]), moved(s[1])
            if page(a) == page(a + 2) == page(t):
                keep.append(s)
        if len(keep) == len(chosen):
            break
        chosen = keep
    for _, _, asm, n in chosen:
        lines = asms[asm]
        m = re.match(r"(\s*)(lcall|ljmp)(\s.*)", lines[n])
        if not m:
            continue
        lines[n] = m.group(1) + ("acall" if m.group(2) == "lcall" else "ajmp") + m.group(3)
    # A later shortening can push an already-short call onto the previous 2 KB page.
    # The encoded address is then a different label, so the call must be long again.
    lab_re = re.compile(r"^\s*([0-9A-F]{6})\s+\d+\s+(\S+):")
    short_re = re.compile(
        r"^\s*([0-9A-F]{6})\s+([0-9A-F]{2})\s+([0-9A-F]{2})\s+\[\s*\d+\]\s+(\d+)\s+(acall|ajmp)\s+(\S+)"
    )
    fn_re = re.compile(r";\s*function (\S+)")
    labels = set()
    flabels = {}
    glabels = {}
    shorts = []
    for asm, rst in ((a, a[:-4] + ".rst") for a in asms):
        if not os.path.exists(rst):
            continue
        fn = ""
        for line in open(rst):
            fm = fn_re.search(line)
            if fm:
                fn = fm.group(1)
            m = lab_re.match(line)
            if m:
                addr, name = int(m.group(1), 16), m.group(2)
                labels.add(addr)
                flabels[(asm, fn, name)] = addr
                glabels.setdefault(name, set()).add(addr)
            m = short_re.match(line)
            if m:
                shorts.append((m, asm, fn))
    expanded = 0
    for m, asm, fn in shorts:
        addr = int(m.group(1), 16)
        op, b2 = int(m.group(2), 16), int(m.group(3), 16)
        enc = ((addr + 2) & 0xF800) | ((op & 0xE0) << 3) | b2
        intended = flabels.get((asm, fn, m.group(6)))
        if intended is None:
            g = glabels.get(m.group(6))
            if g and len(g) == 1:
                intended = next(iter(g))
        # Straddling a page boundary it may still encode right, but the linker rejects it.
        if page(addr) != page(addr + 2):
            pass
        elif intended is not None:
            if enc == intended:
                continue
        elif enc in labels:
            continue
        lines = asms[asm]
        n = int(m.group(4)) - 1
        hit = re.match(r"(\s*)(acall|ajmp)(\s.*)", lines[n])
        if not hit:
            continue
        lines[n] = hit.group(1) + ("lcall" if hit.group(2) == "acall" else "ljmp") + hit.group(3)
        expanded += 1
    folded = 0
    used = set()
    for asm, lines in list(asms.items()):
        lines, n, got = fold_xptr_loads(lines)
        lines, n2 = fold_const_loads(lines)
        folded += n + n2
        used |= got
        asms[asm] = lines
    if used:
        seq = next(a for a in asms if a.endswith("seq.asm"))
        asms[seq] = ensure_load_helpers(asms[seq], used)
    dirty = {s[2] for s in chosen} | ({a for _, a, _ in shorts} if expanded else set())
    if folded:
        dirty.update(asms)
    for asm in dirty:
        open(asm, "w").write("\n".join(asms[asm]))
    print(len(chosen) + expanded + folded)


if __name__ == "__main__":
    main(sys.argv[1])
