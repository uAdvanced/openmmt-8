#!/usr/bin/env python3
"""Move instruction sequences that repeat within a module into shared subroutines.

Usage: outline_asm.py build_dir module[:passes]...

Each module's .asm is assembled to a listing for the instruction sizes, then rewritten in
place. A sequence of instructions in cold code that occurs more than once becomes one
`_ol_<module>_<n>` subroutine: each site calls it, and a site the function returns from
right after jumps to it instead, the subroutine's `ret` returning for both. A sequence may
hold local labels and branches when every branch in it lands inside it and nothing outside
branches into it; it holds no return, stack or SP access, indirect jump or PC-relative
read. Sequences are picked greedily by bytes saved until none saves any. Each subroutine
is placed before the function that follows its middle site, so relax_calls.py can make
most of its calls acall.

Called code runs the same instructions, so registers, flags and memory come out the same.
A call costs 4 machine cycles and 2 bytes of stack, so the functions on the tick, the key
scan and MIDI input paths are left alone, as in relax_calls.py.
"""
import collections
import glob
import os
import re
import subprocess
import sys

# Timing or stack depth matters here (see relax_calls.SKIP_FOLD and the leaf notes in ui.c).
SKIP = {
    "seq_tick", "track_walk", "keys_scan", "keys_act_wait", "seq_midi_in", "main",
    "rec_event", "rec_commit", "emit", "mem_move", "tab_fix", "ui_screen",
    "rt_tx", "click", "apply_shift", "echo", "realtime", "rt_start", "emit_sysex", "sx_close",
    "rec_flush", "rec_put", "rec_sysex_byte", "rec_sysex_end", "held_put", "held_dur",
    "rx_now", "rx_stamp", "rec_stamp", "rec_now", "dump_store", "dump_code", "mod96",
    "ui_pass", "ui_leds", "tracks_lit", "empty_tracks", "trk_bit", "ui_cadence",
    "ui_draw_cycles", "key_held",
    # Their pass time decides which key-scan slot a PLAY after recording lands on.
    "render_play", "render_lcd", "ui_render", "render_home",
}
SKIP |= set(filter(None, os.environ.get("OUTLINE_SKIP", "").split(",")))
# With everything they call: storing a take decides how long the play light stays on after
# recording stops.
SKIP_TREES = {"rec_commit", "seq_stop"}
ONLY = set(filter(None, os.environ.get("OUTLINE_ONLY", "").split(",")))
NEVER = {"ret", "reti", "push", "pop", "jmp"}
BRANCH = {"sjmp", "ajmp", "ljmp", "cjne", "djnz", "jz", "jnz", "jc", "jnc", "jb", "jnb", "jbc"}
MAX_LEN = 24
PASSES = int(os.environ.get("OUTLINE_PASSES", "3"))
# Expected bytes of a call site: most become acall once the subroutine sits near them.
CALL = float(os.environ.get("OUTLINE_CALL", "2.5"))
LST = re.compile(r"^\s+[0-9A-F]{6}\s(.*?)\s*\[\s*\d+\]\s+(\d+)\s")
LOCAL = re.compile(r"\b(\d+)\$")
SP_REF = re.compile(r"\bsp\b", re.I)


def sizes(build, mod):
    """asm line index -> instruction size, from an assembler listing."""
    asm = os.path.join(build, mod + ".asm")
    subprocess.run(["sdas8051", "-plosgffw", os.path.join(build, mod + ".rel"), asm],
                   check=True, capture_output=True)
    out = {}
    for line in open(os.path.join(build, mod + ".lst"), errors="replace"):
        m = LST.match(line)
        if m:
            n = len(re.findall(r"[0-9A-F]{2}", m.group(1)))
            if n:
                out[int(m.group(2)) - 1] = n
    return out


def code(line):
    return re.sub(r"\s+", " ", line.split(";", 1)[0].strip())


class El:
    __slots__ = ("line", "size", "text", "seg", "label", "refs", "ok", "ret")

    def __init__(self, line, size, text, seg, label, refs, ok, ret):
        self.line, self.size, self.text, self.seg = line, size, text, seg
        self.label, self.refs, self.ok, self.ret = label, refs, ok, ret


def elements(lines, size):
    """Instructions and local labels; seg changes at anything a sequence cannot span.

    Local labels are numbered again in each scope (between ordinary labels), so they are
    kept as (scope, number)."""
    els, seg, fn, area, scope = [], 0, "", "", 0
    for i, raw in enumerate(lines):
        m = re.search(r";\s*function (\S+)", raw)
        if m:
            fn = m.group(1)
            seg += 1
        c = code(raw)
        if not c:
            continue
        if c.startswith(".area"):
            area = c.split()[1]
        cold = area == "CSEG" and fn not in SKIP and (not ONLY or fn in ONLY)
        lab = re.fullmatch(r"(\d+)\$:", c)
        if lab:
            els.append(El(i, 0, c, seg, (scope, lab.group(1)), (), cold, False))
            continue
        if c.endswith(":"):
            scope += 1
        if c.endswith(":") or c.startswith(".") or i not in size:
            seg += 1
            continue
        op = c.split()[0]
        refs = tuple((scope, r) for r in LOCAL.findall(c))
        ok = cold and op not in NEVER and not SP_REF.search(c) and "@a+pc" not in c
        if op in BRANCH and not refs:
            ok = False  # leaves the sequence
        els.append(El(i, size[i], c, seg, None, refs, ok, op == "ret"))
    return els


def windows(els):
    """Every valid sequence: key -> [(first, last, bytes, tail)]."""
    total_refs = collections.Counter(r for e in els for r in e.refs)
    occ = collections.defaultdict(list)
    n = len(els)
    for a in range(n):
        if not els[a].ok:
            continue
        defined, inner = set(), collections.Counter()
        names, key = {}, []
        body = 0
        for b in range(a, min(n, a + MAX_LEN)):
            e = els[b]
            if not e.ok or e.seg != els[a].seg:
                break
            if e.label:
                defined.add(e.label)
            for r in e.refs:
                inner[r] += 1
            body += e.size
            key.append(LOCAL.sub(lambda m: "L%d" % names.setdefault(m.group(1), len(names)), e.text)
                       if e.label or e.refs else e.text)
            if body < 4:
                continue
            if any(r not in defined for r in inner) or any(inner[l] != total_refs[l] for l in defined):
                continue
            tail = b + 1 < n and els[b + 1].ret and els[b + 1].seg == e.seg
            occ[tuple(key)].append((a, b, body, tail))
    return occ


def outline(build, mod):
    path = os.path.join(build, mod + ".asm")
    lines = open(path).read().split("\n")
    # A line after a sequence's last element that is the next code line (no label between).
    els = elements(lines, sizes(build, mod))
    occ = windows(els)
    touch = collections.defaultdict(set)
    for key, sites in occ.items():
        for a, b, _, _ in sites:
            for q in range(a, b + 1):
                touch[q].add(key)
    used = [False] * len(els)

    def gain(key):
        sel, last = [], -1
        for s in occ[key]:
            if s[0] > last and not any(used[q] for q in range(s[0], s[1] + 1 + s[3])):
                sel.append(s)
                last = s[1] + s[3]
        if len(sel) < 2:
            return 0, sel
        body = sel[0][2]
        return sum(body + 1 - CALL if s[3] else body - CALL for s in sel) - (body + 1), sel

    gains = {k: gain(k) for k in occ}
    picks = []
    while True:
        key = max(gains, key=lambda k: gains[k][0], default=None)
        if key is None or gains[key][0] <= 0:
            break
        g, sel = gains[key]
        dirty = set()
        for a, b, _, tail in sel:
            for q in range(a, b + 1 + tail):
                used[q] = True
                dirty |= touch[q]
        picks.append((key, sel, g))
        for k in dirty:
            gains[k] = gain(k)
    if not picks:
        return 0
    # Places between functions where the code before cannot run on into the subroutine.
    heads, cur_area, last_op = [], "", ""
    for i, raw in enumerate(lines):
        c = code(raw)
        if c.startswith(".area"):
            cur_area = raw
        elif c and not c.endswith(":") and not c.startswith("."):
            last_op = c.split()[0]
        if re.search(r";\s*function \S+", raw) and last_op in ("ret", "reti", "ljmp", "sjmp", "ajmp"):
            heads.append((i, cur_area))
    repl, drop = {}, set()
    before = collections.defaultdict(list)
    base = 1 + max((int(m) for m in re.findall(rf"^_ol_{mod}_(\d+):", "\n".join(lines), re.M)),
                   default=-1)
    for k, (key, sel, _) in enumerate(picks, base):
        name = f"_ol_{mod}_{k}"
        for a, b, _, tail in sel:
            first = els[a].line
            indent = "\t"
            repl[first] = f"{indent}{'ljmp' if tail else 'lcall'}\t{name}"
            for q in range(a, b + 1 + tail):
                if els[q].line != first:
                    drop.add(els[q].line)
        mid = els[sel[len(sel) // 2][0]].line
        at, area_line = next(((h, ar) for h, ar in heads if h > mid), (len(lines), ""))
        block = ["\t.area CSEG    (CODE)", f";\t function {name[1:]}", f"{name}:"]
        for t in key:
            t = re.sub(r"\bL(\d+)\b", lambda m: "%05d$" % (int(m.group(1)) + 1), t)
            block.append(t if t.endswith(":") else "\t" + t.replace(" ", "\t", 1))
        block.append("\tret")
        if area_line:
            block.append(area_line)
        before[at] += block
    out = []
    for i, l in enumerate(lines):
        out += before.get(i, [])
        if i not in drop:
            out.append(repl.get(i, l))
    out += before.get(len(lines), [])
    open(path, "w").write("\n".join(out) + "\n")
    return int(sum(g for _, _, g in picks))


def callees(build, roots):
    """The functions reachable from roots, from the calls and jumps in every module."""
    calls = collections.defaultdict(set)
    for asm in glob.glob(os.path.join(build, "*.asm")):
        fn = None
        for line in open(asm):
            m = re.search(r";\s*function (\S+)", line)
            if m:
                fn = m.group(1)
                continue
            m = re.match(r"\s*(?:lcall|acall|ljmp|ajmp)\s+_(\w+)", line)
            if m and fn:
                calls[fn].add(m.group(1))
    seen, todo = set(), list(roots)
    while todo:
        f = todo.pop()
        if f not in seen:
            seen.add(f)
            todo += calls.get(f, ())
    return seen


if __name__ == "__main__":
    build = sys.argv[1]
    SKIP |= callees(build, SKIP_TREES)
    total = 0
    # Later passes find repeats among the calls the earlier ones left.
    for spec in sys.argv[2:]:
        mod, _, passes = spec.partition(":")
        g = 0
        for _ in range(int(passes or PASSES)):
            n = outline(build, mod)
            g += n
            if not n:
                break
        total += g
        print(f"outline {mod}: {g} bytes")
    print(f"outline total: {total} bytes")
