#!/usr/bin/env python3
"""Wrapper-probe scenarios for the JAM-8 ROM.

Each scenario boots jam8/build/jam8_rom.bin in emulator/build/mmt8_clap_wrapper_mmio_probe
(fresh RAM, so the factory scenes load), drives keys and MIDI in, and asserts on the
probe's midi_out bytes, LCD lines and FF02/FF04 LED telemetry.

    python3 jam8/tests/probe_jam8.py                 # all scenarios
    python3 jam8/tests/probe_jam8.py --scenario mute # one scenario (repeatable)
    python3 jam8/tests/probe_jam8.py --dump play     # print the decoded timeline
"""
from __future__ import annotations

import argparse
import random
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / "emulator/build/mmt8_clap_wrapper_mmio_probe"
ROM = ROOT / "jam8/build/jam8_rom.bin"

BLOCK_S = 128 / 48000.0
BOOT = 1000  # first-boot factory build plus splash end near block 890
TEMPO = 124.0
TICK_S = 60.0 / (TEMPO * 96)
STEP_BLOCKS = 24 * TICK_S / BLOCK_S  # one 1/16 step
BAR_BLOCKS = 16 * STEP_BLOCKS

# Key ids: row * 8 + column (jam8/core/jam.h).
K_REW, K_FF, K_PLAY, K_STOP, K_REC, K_TRK1, K_FOOT, K_TRANS = 0, 1, 4, 5, 7, 8, 19, 3
K_LOOP, K_QUANT = 42, 43
K_DIGIT = {1: 33, 2: 34, 3: 35, 4: 36, 5: 37, 6: 25, 7: 26, 8: 27, 9: 28, 0: 29}

L_PLAY, L_REC, L_PART = 0x01, 0x02, 0x04


@dataclass
class Run:
    notes: list = field(default_factory=list)  # (block, status, data1, data2)
    realtime: list = field(default_factory=list)  # (block, byte)
    lcd: list = field(default_factory=list)  # (block, line1, line2)
    leds: list = field(default_factory=list)  # (block, ff02, ff04)
    raw: list = field(default_factory=list)  # (block, [bytes])
    clicks: int = 0
    sp_max: int = 0

    def lcd_at(self, block: int) -> tuple[str, str]:
        cur = ("", "")
        for b, l1, l2 in self.lcd:
            if b > block:
                break
            cur = (l1, l2)
        return cur

    def leds_between(self, a: int, b: int) -> list:
        before = [x for x in self.leds if x[0] <= a]
        out = before[-1:] + [x for x in self.leds if a < x[0] <= b]
        return out

    def ons(self, ch: int | None = None, note: int | None = None, a: int = 0, b: int = 1 << 30):
        return [n for n in self.notes
                if n[1] & 0xF0 == 0x90 and n[3] and a <= n[0] < b
                and (ch is None or n[1] & 15 == ch) and (note is None or n[2] == note)]


def run_probe(blocks: int, press: str = "", midi: str = "", hold: str = "", rom: Path = ROM,
              xram: str = "") -> Run:
    args = [str(PROBE), str(rom), str(blocks), "128", press, midi, "", "", "", "", "", "", "", xram, "", hold]
    out = subprocess.run(args, capture_output=True, text=True, timeout=600).stdout
    r = Run()
    block = 0
    for line in out.splitlines():
        m = re.match(r"midi_out block=(\d+) bytes=(\S+)", line)
        if m:
            b = int(m.group(1))
            for msg in m.group(2).split(";"):
                vals = [int(x, 16) for x in msg.split("-")]
                r.raw.append((b, vals))
                if vals[0] >= 0xF8 or vals[0] in (0xFA, 0xFB, 0xFC):
                    r.realtime.append((b, vals[0]))
                elif len(vals) == 3:
                    r.notes.append((b, vals[0], vals[1], vals[2]))
            continue
        m = re.match(r"block=(\d+) .*ff02=0x([0-9a-f]+) ff04=0x([0-9a-f]+)", line)
        if m:
            block = int(m.group(1))
            led = (block, int(m.group(2), 16), int(m.group(3), 16))
            if not r.leds or r.leds[-1][1:] != led[1:]:
                r.leds.append(led)
            continue
        m = re.match(r"lcd1='(.{16})' lcd2='(.{16})'", line)
        if m:
            if not r.lcd or r.lcd[-1][1:] != (m.group(1), m.group(2)):
                r.lcd.append((block, m.group(1), m.group(2)))
            continue
        m = re.match(r"final sp_max=0x([0-9a-f]+)", line)
        if m:
            r.sp_max = int(m.group(1), 16)
            continue
        m = re.match(r"click_edges block=\d+ n=(\d+)", line)
        if m:
            r.clicks += int(m.group(1))
    return r


class Check:
    def __init__(self, name: str):
        self.name, self.fails, self.notes = name, [], []

    def ok(self, cond: bool, what: str):
        (self.notes if cond else self.fails).append(what)
        return cond


def note_name(n: int) -> str:
    return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][n % 12] + str(n // 12 - 1)


def dump(r: Run, start: int = 0):
    for b, st, d1, d2 in r.notes:
        if b < start:
            continue
        kind = "on " if st & 0xF0 == 0x90 and d2 else "off"
        step = (b - start) / STEP_BLOCKS
        print(f"  block {b:5d} step {step:6.2f} ch{(st & 15) + 1:2d} {kind} {note_name(d2 and d1 or d1):4s} v{d2}")
    for b, l1, l2 in r.lcd:
        if b >= start:
            print(f"  block {b:5d} lcd |{l1}|{l2}|")


# ---- scenarios ----

PLAY_AT = BOOT + 10


def sc_boot(c: Check):
    r = run_probe(BOOT + 200, press=f"{BOOT + 100}:18", hold=f"{BOOT + 20}-{BOOT + 60}:{K_TEMPO}")
    l1, l2 = r.lcd_at(BOOT)
    c.ok(l1 == "HOUSE    Am   T1", f"home line 1 {l1!r}")
    c.ok(l2.startswith("DDDDBCLD") and l2.endswith("S01"), f"home line 2 {l2!r}")
    l1, l2 = r.lcd_at(BOOT + 50)
    c.ok(l1 == "TEMPO   SYNC INT" and "124.0" in l2, f"TEMPO page shows the tempo and SYNC INT ({l1!r} {l2!r})")
    l1, l2 = r.lcd_at(BOOT + 120)
    c.ok(l2.startswith("TEMPO") and l2.endswith("125"), f"+ on home sets the tempo and shows it ({l2!r})")
    c.ok(not r.notes and not r.realtime, "no MIDI out while stopped")
    c.ok(any(x[1] == 0x00 for x in r.leds[-1:]), "track LEDs lit for the 8 unmuted lanes")
    c.ok(r.leds and r.leds[-1][2] & L_PLAY, "PLAY LED off while stopped")
    return r


def sc_foreign(c: Check):
    # Battery RAM left by another ROM, with JAM-8's old one-byte magic (0x4A) at 0x0800 and
    # nonsense settings and lane headers: it must not pass for a JAM-8 bank.
    rnd = random.Random(1)
    mem = {a: rnd.randrange(256) for a in [*range(0x0800, 0x0900), *range(0x2000, 0x2900)]}
    mem[0x0800] = 0x4A
    r = run_probe(BOOT + 600, xram=",".join(f"0:{a:04X}={v:02X}" for a, v in mem.items()))
    c.ok(any(l1.startswith("MEMORY CLEARED") for _, l1, _ in r.lcd), "MEMORY CLEARED splash")
    l1, l2 = r.lcd_at(BOOT + 600)
    c.ok(l1 == "HOUSE    Am   T1", f"factory home line 1 {l1!r}")
    c.ok(l2.startswith("DDDDBCLD") and "S01" in l2, f"factory home line 2 {l2!r}")
    return r


def check_pairs(c: Check, r: Run, end: int):
    """Every note-on has its note-off, and offs never precede their on."""
    open_ = {}
    bad = 0
    for b, st, d1, d2 in r.notes:
        key = (st & 15, d1)
        if st & 0xF0 == 0x90 and d2:
            open_[key] = open_.get(key, 0) + 1
        else:
            if open_.get(key, 0) <= 0:
                bad += 1
            else:
                open_[key] -= 1
    hanging = sum(v for v in open_.values() if v > 0)
    c.ok(bad == 0, f"note-offs without a sounding note: {bad}")
    c.ok(hanging <= 12, f"notes still sounding at the end: {hanging} (lanes mid-gate)")


def sc_play(c: Check):
    end = PLAY_AT + int(2 * BAR_BLOCKS)
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY}")
    rt = [x for x in r.realtime if x[0] >= PLAY_AT]
    c.ok(rt and rt[0][1] == 0xFA, "Start (FA) first after PLAY")
    f8 = [b for b, v in rt if v == 0xF8]
    if c.ok(len(f8) > 50, f"{len(f8)} clocks in two bars"):
        span = (f8[-1] - f8[0]) / (len(f8) - 1)
        want = 4 * TICK_S / BLOCK_S
        c.ok(abs(span - want) / want < 0.03, f"clock spacing {span:.2f} blocks, 24 PPQN at 124 BPM is {want:.2f}")
    kicks = [n[0] for n in r.ons(9, 36)]
    if c.ok(len(kicks) >= 6, f"{len(kicks)} kicks on ch10 in two bars"):
        gaps = [b - a for a, b in zip(kicks, kicks[1:])]
        c.ok(all(abs(g - 4 * STEP_BLOCKS) < 3 for g in gaps), f"four-on-the-floor gaps {gaps}")
    bass = r.ons(1)
    c.ok(bass and all(36 <= n[2] <= 72 for n in bass), f"bass notes in range {[n[2] for n in bass][:8]}")
    c.ok(all(n[2] % 12 in (9, 11, 0, 2, 4, 5, 7) for n in bass), "bass notes in A minor")
    chords = r.ons(2)
    c.ok(len(chords) >= 4, f"{len(chords)} chord notes on ch3")
    led = r.leds_between(PLAY_AT + 50, end)
    c.ok(led and all(not (x[2] & L_PLAY) for x in led), "PLAY LED lit while playing")
    check_pairs(c, r, end)
    return r


def sc_mute(c: Check):
    press_at = PLAY_AT + int(BAR_BLOCKS * 0.5)
    bar2 = PLAY_AT + BAR_BLOCKS
    end = PLAY_AT + int(3 * BAR_BLOCKS)
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY},{press_at}:{K_TRK1}")
    before = r.ons(9, 36, PLAY_AT, bar2 - 10)
    after = r.ons(9, 36, int(bar2 + 10), end)
    c.ok(len(before) >= 3, f"{len(before)} kicks before the bar line")
    c.ok(not after, f"kick muted from the next bar ({len(after)} after)")
    c.ok(r.ons(9, 42, int(bar2 + 10), end), "hats keep playing")
    l1, l2 = r.lcd_at(press_at + 60)
    c.ok(l2[0] == "*", f"pending mute shown as '*' ({l2!r})")
    l1, l2 = r.lcd_at(end)
    c.ok(l2[0] == ".", f"muted lane shown as '.' ({l2!r})")
    late = r.leds_between(int(bar2 + 60), end)
    c.ok(late and all(x[1] & 1 for x in late), "track 1 LED off once muted")
    pend = r.leds_between(press_at + 30, int(bar2 - 5))
    c.ok(len({x[1] & 1 for x in pend}) == 2, "track 1 LED blinks while the mute is pending")
    return r


def sc_multimute(c: Check):
    # All eight track keys pressed together, overlapping: each one toggles its own mute.
    a = BOOT
    b = a + 120
    holds = ",".join(f"{a + 2 * t}-{a + 30 + t}:{K_TRK1 + t}" for t in range(8))
    unholds = ",".join(f"{b + 2 * t}-{b + 30 + t}:{K_TRK1 + t}" for t in range(8))
    play = b + 120
    p = play + int(BAR_BLOCKS * 0.5)
    bar2 = play + BAR_BLOCKS
    end = play + int(2.5 * BAR_BLOCKS)
    phold = ",".join(f"{p + 2 * t}-{p + 30 + t}:{K_TRK1 + t}" for t in range(8))
    r = run_probe(end, press=f"{play}:{K_PLAY}", hold=f"{holds},{unholds},{phold}")
    _l1, l2 = r.lcd_at(a + 80)
    c.ok(l2[:8] == "........", f"stopped: eight keys together mute all eight tracks ({l2!r})")
    _l1, l2 = r.lcd_at(b + 80)
    c.ok(l2[:8] == "DDDDBCLD", f"stopped: eight keys together unmute all eight ({l2!r})")
    _l1, l2 = r.lcd_at(p + 80)
    c.ok(l2[:8] == "********", f"playing: all eight mutes wait for the bar ({l2!r})")
    _l1, l2 = r.lcd_at(int(bar2 + 40))
    c.ok(l2[:8] == "........", f"playing: all eight muted from the next bar ({l2!r})")
    late = r.ons(None, None, int(bar2 + 20), end)
    c.ok(not late, f"no notes once all eight are muted ({len(late)})")
    return r


def sc_dropout(c: Check):
    # QUANT down first, then track 1, across two beats. The kick should vanish and return.
    a = PLAY_AT + int(BAR_BLOCKS) + 40
    b = a + int(8 * STEP_BLOCKS)
    end = PLAY_AT + int(3 * BAR_BLOCKS)
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY}",
                  hold=f"{a - 15}-{b}:{K_QUANT},{a}-{b - 5}:{K_TRK1}")
    during = r.ons(9, 36, a + 20, b - 10)
    after = r.ons(9, 36, b + int(STEP_BLOCKS), end)
    hats = r.ons(9, 42, a + 20, b - 10)
    c.ok(not during, f"no kicks while QUANT and track 1 are held ({len(during)})")
    c.ok(after, f"kicks return after release ({len(after)})")
    c.ok(hats, f"hats keep playing during the dropout ({len(hats)})")
    _l1, l2 = r.lcd_at(a + 40)
    c.ok(l2[0] == "-", f"dropout shown as '-' ({l2!r})")
    _l1, l2 = r.lcd_at(b + 40)
    c.ok(l2[0] == "D", f"stored mute unchanged ({l2!r})")
    check_pairs(c, r, end)
    return r


def sc_solo(c: Check):
    # LOOP held across a short track tap: solo waits for the next bar, then only lane 1 plays.
    press_at = PLAY_AT + int(0.4 * BAR_BLOCKS)
    bar2 = PLAY_AT + int(BAR_BLOCKS)
    end = PLAY_AT + int(2.5 * BAR_BLOCKS)
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY},{press_at + 15}:{K_TRK1}",
                  hold=f"{press_at}-{press_at + 40}:{K_LOOP}")
    bass_before = r.ons(1, None, PLAY_AT, int(bar2 - 20))
    bass_after = r.ons(1, None, int(bar2 + STEP_BLOCKS), end)
    kicks_after = r.ons(9, 36, int(bar2 + STEP_BLOCKS), end)
    hats_after = r.ons(9, 42, int(bar2 + STEP_BLOCKS), end)
    c.ok(bass_before, f"bass plays before the solo launches ({len(bass_before)})")
    c.ok(not bass_after, f"bass silent once solo launches ({len(bass_after)})")
    c.ok(kicks_after, f"soloed kick keeps playing ({len(kicks_after)})")
    c.ok(not hats_after, f"other drums silent ({len(hats_after)})")
    _l1, l2 = r.lcd_at(press_at + 50)
    c.ok(l2[0] == "*", f"solo waits for the bar ({l2!r})")
    _l1, l2 = r.lcd_at(int(bar2 + 40))
    c.ok(l2[0] == "S" and "-" in l2[1:8], f"solo shows S and dashes ({l2!r})")
    check_pairs(c, r, end)
    return r


def sc_solohold(c: Check):
    # Hold LOOP and track 1 past 300 ms. Solo lasts only while the track is down.
    a = PLAY_AT + int(BAR_BLOCKS)
    track_a = a + 8
    track_b = a + int(BAR_BLOCKS) + 160
    end = track_b + int(BAR_BLOCKS)
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY}",
                  hold=f"{a}-{track_b + 8}:{K_LOOP},{track_a}-{track_b}:{K_TRK1}")
    solo_on = track_a + 150
    during_bass = r.ons(1, None, solo_on, track_b - 10)
    during_kick = r.ons(9, 36, solo_on, track_b - 10)
    after_bass = r.ons(1, None, track_b + int(2 * STEP_BLOCKS), end)
    c.ok(not during_bass, f"bass drops out while the solo is held ({len(during_bass)})")
    c.ok(during_kick, f"soloed kick plays during the hold ({len(during_kick)})")
    c.ok(after_bass, f"bass returns after release ({len(after_bass)})")
    _l1, l2 = r.lcd_at(solo_on + 30)
    c.ok(l2[0] == "S", f"held solo shown as S ({l2!r})")
    _l1, l2 = r.lcd_at(track_b + 40)
    c.ok(l2[0] == "D", f"release does not latch the solo ({l2!r})")
    check_pairs(c, r, end)
    return r


def sc_fill(c: Check):
    hold_a = PLAY_AT + int(BAR_BLOCKS)
    hold_b = PLAY_AT + int(2 * BAR_BLOCKS)
    end = PLAY_AT + int(3 * BAR_BLOCKS)
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY}", hold=f"{hold_a}-{hold_b}:{K_FOOT}")
    hats_before = r.ons(9, 42, PLAY_AT, hold_a)
    hats_fill = r.ons(9, 42, hold_a + 20, hold_b)
    hats_after = r.ons(9, 42, hold_b + 20, end)
    c.ok(hats_before and hats_after, f"hats play outside the fill ({len(hats_before)}, {len(hats_after)})")
    c.ok(not hats_fill, f"no closed hats during the fill ({len(hats_fill)})")
    sn_fill = r.ons(9, 39, hold_a + 20, hold_b)
    sn_before = r.ons(9, 39, PLAY_AT, hold_a)
    c.ok(len(sn_fill) > len(sn_before), f"more claps in the fill bar ({len(sn_fill)} vs {len(sn_before)})")
    l1, l2 = r.lcd_at(hold_a + 80)
    c.ok("F" in l2[:8], f"fill shown on the drum lanes ({l2!r})")
    return r


def sc_scene(c: Check):
    press_at = PLAY_AT + int(BAR_BLOCKS * 0.5)
    loop_end = PLAY_AT + 4 * BAR_BLOCKS  # factory scenes loop every 4 bars
    end = int(loop_end + BAR_BLOCKS)
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY},{press_at}:{K_DIGIT[2]}")
    shown = [x[2] for x in r.lcd if press_at < x[0] < loop_end]
    c.ok(any(l2[9:12] == ">02" for l2 in shown), "scene 2 queued as '>02' on the home screen")
    led = r.leds_between(press_at + 30, int(loop_end - 10))
    c.ok(len({x[2] & L_PART for x in led}) == 2, "PART LED blinks while queued")
    l1, l2 = r.lcd_at(end)
    c.ok("S02" in l2 and ">" not in l2, f"scene 2 playing after the loop ({l2!r})")
    c.ok(l1.startswith("TEC"), f"focused lane shows the techno style ({l1!r})")
    early = r.lcd_at(int(loop_end - 20))[1]
    c.ok("S01" in early, f"scene 1 holds until the loop end ({early!r})")
    return r


def sc_root(c: Check):
    # Control channel 1, chord mode: D (a scale tone of A minor, degree iv) at the next bar.
    send_at = PLAY_AT + int(BAR_BLOCKS * 0.5)
    bar2 = PLAY_AT + BAR_BLOCKS
    send2 = int(bar2 + BAR_BLOCKS * 0.5)
    bar3 = PLAY_AT + 2 * BAR_BLOCKS
    end = PLAY_AT + int(3.2 * BAR_BLOCKS)
    # Bar 1 -> 2: single note D (3E) -> Dm7. Bar 2 -> 3: multi-note D-F#-A (3E, 42, 45) -> borrowed D major!
    midi = (f"{send_at}:90-3E-64;{send_at + 4}:80-3E-00;"
            f"{send2}:90-3E-64;{send2 + 1}:90-42-64;{send2 + 2}:90-45-64;"
            f"{send2 + 10}:80-3E-00;{send2 + 11}:80-42-00;{send2 + 12}:80-45-00")
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY}", midi=midi)
    l1, _ = r.lcd_at(int(bar2 - 10))
    c.ok(l1[9:12] == "Am ", f"Am before the bar line ({l1!r})")
    l1, _ = r.lcd_at(int(bar2 + 60))
    c.ok(l1[9:12] == "Dm ", f"Dm from the next bar ({l1!r})")
    chord = sorted({n[2] % 12 for n in r.ons(2, None, int(bar2 + 5), int(bar3 - 5))})
    c.ok(chord and set(chord) <= {2, 5, 9, 0}, f"chord lane plays Dm7 tones {chord}")
    l1, _ = r.lcd_at(int(bar3 + 60))
    c.ok(l1[9:12] == "D  ", f"multi-note D-F#-A on control channel sets borrowed D major ({l1!r})")
    chord_maj = sorted({n[2] % 12 for n in r.ons(2, None, int(bar3 + 5), end)})
    c.ok(6 in chord_maj and set(chord_maj) <= {2, 6, 9}, f"chord lane voice-leads to D major {chord_maj}")
    bass = {n[2] % 12 for n in r.ons(1, None, int(bar2 + 5), end)}
    c.ok(2 in bass, f"bass follows the root D {sorted(bass)}")
    c.ok(not [n for n in r.notes if n[2] == 0x3E and n[1] & 15 == 0], "control notes are not echoed")
    return r


def sc_sync(c: Check):
    # CLOCK held: SOURCE + -> MIDI, release returns home. Then FA and F8s every 8 blocks.
    at = BOOT
    start = at + 100
    end = start + 8 * 24 * 8
    clocks = ";".join(f"{start + 8 * (i + 1)}:F8" for i in range(24 * 8))
    r = run_probe(end, press=f"{at + 20}:18", hold=f"{at}-{at + 40}:32", midi=f"{start}:FA;{clocks}")
    c.ok(not [x for x in r.realtime if x[1] in (0xF8, 0xFA)], "no clock echoed out when synced")
    l1, _ = r.lcd_at(at + 80)
    c.ok(l1 == "HOUSE    Am   T1", f"home shows the style and chord when synced ({l1!r})")
    ext = run_probe(at + 200, press=f"{at + 20}:18,{at + 100}:18",
                    hold=f"{at}-{at + 40}:32,{at + 140}-{at + 180}:{K_TEMPO}")
    l1, l2 = ext.lcd_at(at + 170)
    c.ok(l1 == "TEMPO   SYNC EXT" and "124.0" in l2, f"+ while synced leaves the tempo; TEMPO page shows SYNC EXT ({l1!r} {l2!r})")
    kicks = [n[0] for n in r.ons(9, 36, start)]
    if c.ok(len(kicks) >= 6, f"{len(kicks)} kicks on two bars of MIDI clock"):
        gaps = [b - a for a, b in zip(kicks, kicks[1:])]
        c.ok(all(abs(g - 8 * 24) <= 3 for g in gaps), f"kicks follow the incoming clock, gaps {gaps}")
    stop = run_probe(end + 200, press=f"{at + 20}:18", hold=f"{at}-{at + 40}:32",
                     midi=f"{start}:FA;{clocks};{end + 4}:FC")
    c.ok(not stop.ons(None, None, end + 20), "FC stops the lanes")
    return r


def sc_click(c: Check):
    at = BOOT
    end = PLAY_AT + 200 + int(BAR_BLOCKS)
    quiet = run_probe(end, press=f"{PLAY_AT + 200}:{K_PLAY}")
    r = run_probe(end, press=f"{at + 20}:18,{PLAY_AT + 200}:{K_PLAY}", hold=f"{at}-{at + 40}:24")
    c.ok(quiet.clicks <= 1, f"click off by default ({quiet.clicks} edges)")
    c.ok(r.clicks - quiet.clicks >= 4, f"click pulses on P3.5 each beat ({r.clicks - quiet.clicks} pulses in a bar)")
    return r


def sc_dense(c: Check):
    # Hold track 3 (closed hat) and tap + three times: density 6 -> 9 adds ratchets.
    a = PLAY_AT + 20
    end = PLAY_AT + int(3 * BAR_BLOCKS)
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY},{a + 20}:18,{a + 60}:18,{a + 100}:18",
                  hold=f"{a}-{a + 140}:{K_TRK1 + 2}")
    l1, l2 = r.lcd_at(a + 130)
    c.ok("DENSITY" in l1 + l2 and "9" in l2, f"density 9 shown ({l1!r} {l2!r})")
    late = [n[0] for n in r.ons(9, 42, int(PLAY_AT + 2 * BAR_BLOCKS), end)]
    early = [n[0] for n in r.ons(9, 42, PLAY_AT, a)]
    close = sum(1 for x, y in zip(late, late[1:]) if y - x < STEP_BLOCKS * 0.75)
    c.ok(close >= 1, f"ratchets: {close} hat pairs closer than a step")
    c.ok(len(late) > len(early) * 2, f"denser hats ({len(late)} in the last bar)")
    l1, l2 = r.lcd_at(end)
    c.ok(l2[2] == "D", f"track 3 not muted by the hold ({l2!r})")
    return r


K_ERASE, K_CLOCK, K_SONG, K_PGDN, K_PLUS, K_COPY = 2, 32, 38, 23, 18, 6
K_TEMPO, K_LENGTH = 16, 44


def sc_capture(c: Check):
    # Erase lane 5 (bass) while stopped, play, arm it with REC + track 5, play two notes in.
    t5 = K_TRK1 + 4
    play = BOOT + 100
    arm = play + 60
    notes = [arm + 60, arm + 60 + int(4 * STEP_BLOCKS)]
    end = play + int(3 * BAR_BLOCKS)
    midi = ";".join(f"{b}:91-45-64;{b + 8}:81-45-00" for b in notes)
    cap_end = arm + 5 + 16 * STEP_BLOCKS
    r = run_probe(end, press=f"{BOOT + 20}:{t5},{play}:{K_PLAY},{arm + 5}:{t5}", midi=midi,
                  hold=f"{BOOT}-{BOOT + 40}:{K_ERASE},{arm}-{arm + 30}:{K_REC}")
    c.ok(not r.ons(1, None, play, arm + 50), "erased bass lane is silent")
    shown = [x[2] for x in r.lcd if arm < x[0] < cap_end]
    c.ok(any(l2[4] == "R" for l2 in shown), "lane 5 shows R on the home screen while capturing")
    led = r.leds_between(arm + 40, notes[0])
    c.ok(led and all(not (x[2] & L_REC) for x in led), "REC LED lit while a lane is armed")
    later = r.ons(1, None, int(cap_end + 10), end)
    c.ok(len(later) >= 2, f"captured notes play back on ch2 ({len(later)} notes)")
    gaps = sorted({round((y[0] - x[0]) / STEP_BLOCKS) for x, y in zip(later, later[1:])})
    c.ok(4 in gaps, f"captured notes keep their 4-step spacing ({gaps})")
    l1, l2 = r.lcd_at(end)
    c.ok(l2[4] == "B", f"capture disarms after one pass ({l2!r})")
    c.ok(not [n for n in r.notes if n[2] == 0x45 and n[1] & 15 == 1 and notes[0] <= n[0] <= notes[0] + 2],
         "played notes are not echoed with thru off")
    return r


def sc_dump(c: Check):
    # CLOCK held, PAGE down twice to DUMP, REC sends. The wrapper reports the sysex at its F7.
    at = BOOT
    total = 256 + (0xB040 - 0x2000)
    length = 5 + 2 * total + 3
    end = at + 100 + int(length / 3125 / BLOCK_S * 1.1)
    r = run_probe(end, press=f"{at + 20}:{K_PGDN},{at + 40}:{K_PGDN},{at + 60}:{K_REC}",
                  hold=f"{at}-{at + 80}:{K_CLOCK}")
    l1, l2 = r.lcd_at(at + 50)
    c.ok(l1.startswith("CLOCK") and "DUMP" in l2, f"CLOCK page DUMP field ({l1!r} {l2!r})")
    shown = [x[1] + x[2] for x in r.lcd if x[0] > at + 60]
    c.ok(any("SENDING DUMP" in s for s in shown), "LCD says SENDING DUMP")
    c.ok(any("DUMP SENT" in s for s in shown), "LCD says DUMP SENT at the end")
    sx = [(b, v) for b, v in r.raw if v[0] == 0xF0]
    if not c.ok(len(sx) == 1, f"one sysex message ({len(sx)})"):
        return r
    b, data = sx[0]
    c.ok(len(data) == length, f"dump is {len(data)} bytes, {length} expected")
    c.ok(data[:5] == [0xF0, 0x7D, 0x4A, 0x38, 0x01] and data[-1] == 0xF7, "header F0 7D 4A 38 01, ends F7")
    body = data[5:-1]
    c.ok(all(x < 16 for x in body), "body is 4-bit nibbles")
    raw = [(body[i] << 4) | body[i + 1] for i in range(0, len(body) - 2, 2)]
    c.ok(raw[0] == 0x4A and raw[1] == 1240 >> 8 and raw[2] == 1240 & 0xFF, "settings: magic, tempo 124.0")
    c.ok(((body[-2] << 4) | body[-1]) == sum(raw) & 0x7F, "7-bit checksum matches")
    lane0 = raw[256:256 + 32]
    c.ok(lane0[0] == 0 and lane0[2] == 36, "scene 1 lane 1 header: drum, note 36")
    c.ok(not r.ons(), "no notes during the dump")
    return r


def sc_chain(c: Check):
    # SONG held: CHAIN + -> ON, release home, then PLAY. Chain: scene 1 x2, scene 2 x2.
    at = BOOT
    play = at + 100
    loop = 4 * BAR_BLOCKS
    end = int(play + 2 * loop + BAR_BLOCKS)
    r = run_probe(end, press=f"{at + 20}:{K_PLUS},{play}:{K_PLAY}", hold=f"{at}-{at + 40}:{K_SONG}")
    c.ok("S01" in r.lcd_at(int(play + loop + 50))[1], "chain repeats scene 1 for a second loop")
    c.ok("S02" in r.lcd_at(int(play + 2 * loop + 100))[1], "chain moves on to scene 2 after two loops")
    led = r.leds_between(play + 10, end)
    c.ok(led and all(not (x[2] & 0x10) for x in led), "SONG LED lit in chain mode")
    acid = r.ons(1, None, int(play + 2 * loop + 20), end)
    c.ok(acid, "scene 2 bass plays")
    return r


K_EDIT, K_TAPE, K_CLICK, K_NAME, K_ECHO, K_MERGE, K_MINUS = 46, 31, 24, 47, 41, 39, 17


def sc_edit(c: Check):
    # EDIT on lane 1 (kick, steps 1 5 9 13): tap step 2 on; hold step 1 and press 3 (ratchet x3).
    at = BOOT
    end = PLAY_AT + 200 + int(BAR_BLOCKS)
    r = run_probe(end, press=f"{at}:{K_EDIT},{at + 20}:{K_TRK1 + 1},{at + 80}:{K_DIGIT[3]},"
                  f"{PLAY_AT + 200}:{K_PLAY}", hold=f"{at + 60}-{at + 110}:{K_TRK1}")
    l1, l2 = r.lcd_at(at + 50)
    c.ok(l1.startswith("EDIT T1") and l1[15] == "*", f"EDIT page, step 2 on ({l1!r})")
    led = r.leds_between(at + 40, at + 50)
    c.ok(led and (~led[-1][1] & 0xFF) == 0x13, f"step LEDs 1 2 5 lit (ff02 0x{led[-1][1]:02x})")
    c.ok(any(not (x[2] & 0x08) for x in led), "EDIT LED lit")
    kicks = [n[0] for n in r.ons(9, 36, PLAY_AT + 200)]
    s = kicks[0] if kicks else PLAY_AT + 200
    first = [b for b in kicks if b < s + STEP_BLOCKS * 0.9]
    c.ok(len(first) == 3, f"step 1 ratchets three kicks ({len(first)})")
    c.ok(any(abs(b - s - STEP_BLOCKS) < 4 for b in kicks), "step 2 kick added")
    l1, l2 = r.lcd_at(at + 100)
    c.ok(l2.startswith("VELOCITY"), f"held step keeps the field ({l2!r})")
    return r


def sc_exit(c: Check):
    # A function page shows only while its key is held; release returns home. STOP held
    # across a function key leaves too, without starting playback. QUANT + a track returns
    # to the live view at once. EDIT still toggles. A later STOP tap still stops.
    at = BOOT
    play = at + 700
    r = run_probe(
        play + 80,
        press=(f"{at + 120}:{K_CLOCK},{at + 280}:{K_TEMPO},"
               f"{at + 540}:{K_EDIT},{at + 600}:{K_EDIT},{play}:{K_PLAY},{play + 40}:{K_STOP}"),
        hold=(f"{at}-{at + 40}:{K_TAPE},{at + 100}-{at + 150}:{K_STOP},{at + 200}-{at + 330}:{K_LENGTH},"
              f"{at + 250}-{at + 310}:{K_STOP},{at + 340}-{at + 400}:{K_QUANT},"
              f"{at + 430}-{at + 500}:{K_QUANT},{at + 460}-{at + 490}:{K_TRK1}"))
    l1, _ = r.lcd_at(at + 20)
    c.ok(l1.startswith("GEN"), f"TAPE held shows GEN ({l1!r})")
    l1, _ = r.lcd_at(at + 70)
    c.ok(l1.startswith("HOU"), f"releasing TAPE returns home ({l1!r})")
    l1, _ = r.lcd_at(at + 170)
    c.ok(l1.startswith("HOU"), f"STOP + CLOCK stays home ({l1!r})")
    c.ok(not [x for x in r.realtime if x[0] < play] and not r.ons(b=play),
         "STOP + a function key does not start playback")
    l1, _ = r.lcd_at(at + 230)
    c.ok(l1.startswith("LENGTH"), f"LENGTH held shows LENGTH ({l1!r})")
    l1, _ = r.lcd_at(at + 320)
    c.ok(l1.startswith("HOU"), f"STOP + TEMPO leaves LENGTH ({l1!r})")
    l1, _ = r.lcd_at(at + 370)
    c.ok(l1.startswith("QUANTIZE"), f"QUANT held shows QUANTIZE ({l1!r})")
    l1, _ = r.lcd_at(at + 420)
    c.ok(l1.startswith("HOU"), f"releasing QUANT returns home ({l1!r})")
    l1, _ = r.lcd_at(at + 450)
    c.ok(l1.startswith("QUANTIZE"), f"QUANT held again ({l1!r})")
    l1, l2 = r.lcd_at(at + 480)
    c.ok(l1.startswith("HOU") and l2[0] == "-", f"QUANT + track 1 returns to the live view ({l1!r} {l2!r})")
    l1, _ = r.lcd_at(at + 570)
    c.ok(l1.startswith("EDIT"), f"EDIT tap enters step edit and stays ({l1!r})")
    l1, _ = r.lcd_at(at + 640)
    c.ok(l1.startswith("HOU"), f"EDIT tap again leaves step edit ({l1!r})")
    led = r.leds_between(play + 15, play + 35)
    c.ok(led and not (led[-1][2] & L_PLAY), "PLAY starts")
    led = r.leds_between(play + 55, play + 80)
    c.ok(led and led[-1][2] & L_PLAY, "a STOP tap stops")
    return r


def sc_gen_page(c: Check):
    at = BOOT
    end = at + 300
    r = run_probe(end, press=f"{at + 30}:{K_PLUS},{at + 60}:{K_PGDN},{at + 90}:{K_DIGIT[4]},"
                  f"{at + 100}:{K_DIGIT[2]}", hold=f"{at}-{at + 150}:{K_TAPE}")
    l1, l2 = r.lcd_at(at + 20)
    c.ok(l1.startswith("GEN     T1 DRUM") and l2.startswith("STYLE   HOUSE"), f"GEN page ({l1!r} {l2!r})")
    l1, l2 = r.lcd_at(at + 50)
    c.ok(l2 == "STYLE   TECHNO  ", f"+ picks the next style ({l2!r})")
    l1, l2 = r.lcd_at(at + 140)
    c.ok(l2.startswith("SEED") and l2.endswith("42"), f"keypad enters seed 42 ({l2!r})")
    l1, _ = r.lcd_at(at + 200)
    c.ok(l1.startswith("TEC"), f"release returns home with the new style ({l1!r})")
    return r


def sc_transpose(c: Check):
    a = PLAY_AT + int(BAR_BLOCKS)
    end = PLAY_AT + int(3 * BAR_BLOCKS)
    # TRANS held: PGUP wraps to TRANSPOS (the last HARMONY field), + twice.
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY},{a + 10}:22,{a + 30}:{K_PLUS},{a + 60}:{K_PLUS}",
                  hold=f"{a}-{a + 100}:{K_TRANS}")
    l1, l2 = r.lcd_at(a + 90)
    c.ok(l1.startswith("HARMONY") and l2.startswith("TRANSPOS") and "+" in l2 and l2.endswith("2"),
         f"HARMONY page TRANSPOS +2 ({l1!r} {l2!r})")
    l1, l2 = r.lcd_at(end)
    c.ok(l1.startswith("HOU"), f"home again after TRANS is released ({l1!r})")
    ref = run_probe(end, press=f"{PLAY_AT}:{K_PLAY}")
    w = (int(PLAY_AT + 2 * BAR_BLOCKS), end)
    base = sorted(n[2] + 2 for n in ref.ons(1, None, *w))
    moved = sorted(n[2] for n in r.ons(1, None, *w))
    c.ok(base and moved == base, f"bass two semitones up ({moved[:6]} vs reference+2 {base[:6]})")
    chords = sorted(n[2] for n in r.ons(2, None, *w))
    c.ok(chords == sorted(n[2] + 2 for n in ref.ons(2, None, *w)), "chords two semitones up")
    kick = {n[2] for n in r.ons(9, None, a, end)}
    c.ok(36 in kick, "drums are not transposed")
    return r


def sc_euclid(c: Check):
    # Lane 8 of the house scene is a Euclid perc lane E(5,16): hold track 8 and press +.
    a = BOOT
    r = run_probe(a + 600, press=f"{a + 20}:{K_PLUS}", hold=f"{a}-{a + 60}:{K_TRK1 + 7}")
    shown = [x[2] for x in r.lcd if a < x[0] < a + 200]
    c.ok(any(l2.startswith("EUCLID K T8") and l2.endswith("6") for l2 in shown), "EUCLID K T8 6 shown")
    l1, l2 = r.lcd_at(a + 599)
    c.ok(l1.startswith("EUC") and l1.endswith("T8"), f"holding track 8 focuses it ({l1!r})")
    c.ok(l2[7] == "D", f"track 8 not muted ({l2!r})")
    return r


def sc_tap(c: Check):
    a = BOOT
    gap = int(0.5 / BLOCK_S)
    r = run_probe(a + 3 * gap + 300, press=f"{a + 40}:{K_PLAY},{a + 40 + gap}:{K_PLAY},{a + 40 + 2 * gap}:{K_PLAY}",
                  hold=f"{a}-{a + 60 + 2 * gap}:{K_CLICK}")
    shown = [x[2] for x in r.lcd if a < x[0]]
    c.ok(any(l2.startswith("TAP TEMPO") and l2.endswith("120") for l2 in shown), "taps 0.5 s apart give 120 BPM")
    c.ok(not r.notes, "tapping does not start playback")
    return r


def sc_knob(c: Check):
    # NAME page: KNOB CC = 74 by keypad, home, then CC 74 at 127 drives lane 1 density to 9.
    a = BOOT
    end = PLAY_AT + 200 + int(2 * BAR_BLOCKS)
    r = run_probe(end, press=f"{a + 20}:{K_DIGIT[7]},{a + 30}:{K_DIGIT[4]},{PLAY_AT + 200}:{K_PLAY}",
                  hold=f"{a}-{a + 60}:{K_NAME}", midi=f"{a + 100}:B0-4A-7F")
    l1, l2 = r.lcd_at(a + 50)
    c.ok(l2.startswith("KNOB CC") and l2.endswith("74"), f"knob CC 74 set ({l2!r})")
    kicks = r.ons(9, 36, PLAY_AT + 200)
    c.ok(len(kicks) > 8, f"density 9 adds kicks ({len(kicks)} in two bars, 8 at density 5)")
    return r


def sc_thru(c: Check):
    a = BOOT
    r = run_probe(a + 200, press=f"{a}:{K_ECHO}", midi=f"{a + 100}:92-3C-64;{a + 110}:82-3C-00")
    c.ok(r.ons(9, 0x3C), "ECHO: a note in goes out on the focused lane's channel (10)")
    led = r.leds_between(a + 50, a + 200)
    c.ok(led and all(not (x[2] & 0x20) for x in led), "ECHO LED lit")
    off = run_probe(a + 200, midi=f"{a + 100}:92-3C-64;{a + 110}:82-3C-00")
    c.ok(not off.notes, "thru off by default")
    # With ECHO on, the control channel still steers the harmony and is not passed thru.
    play = a + 60
    send_at = play + int(BAR_BLOCKS * 0.5)
    ctl = run_probe(play + int(1.5 * BAR_BLOCKS), press=f"{a}:{K_ECHO},{play}:{K_PLAY}",
                    midi=f"{send_at}:90-3E-64;{send_at + 4}:80-3E-00")
    led = ctl.leds_between(send_at - 10, send_at + 10)
    c.ok(led and all(not (x[2] & 0x20) for x in led), "ECHO LED lit when the control note arrives")
    l1, _ = ctl.lcd_at(int(play + BAR_BLOCKS + 60))
    c.ok(l1[9:12] == "Dm ", f"ECHO on: a control-channel D still sets the chord ({l1!r})")
    c.ok(not [n for n in ctl.notes if n[2] == 0x3E and send_at <= n[0] <= send_at + 10],
         "ECHO on: control notes are not passed thru")
    return r


def sc_mutate(c: Check):
    # Hold track 5 (offbeat bass) to focus it, play; MERGE mutates its seed, MERGE + - steps back.
    play = BOOT + 250
    a = play + 100
    end = play + int(3 * BAR_BLOCKS)
    r = run_probe(end, press=f"{play}:{K_PLAY},{a}:{K_MERGE},{a + 420}:{K_MINUS},"
                             f"{a + 600}:{K_TRK1 + 6},{a + 750}:{K_REC},"
                             f"{a + 900}:{K_MERGE},{a + 1050}:{K_MERGE},{a + 1200}:{K_REW}",
                  hold=f"{BOOT - 100}-{BOOT + 150}:{K_TRK1 + 4},{a + 400}-{a + 460}:{K_MERGE},"
                       f"{a + 580}-{a + 640}:{K_MERGE},{a + 730}-{a + 790}:{K_MERGE},"
                       f"{a + 880}-{a + 940}:{K_COPY},{a + 1030}-{a + 1090}:{K_ERASE},"
                       f"{a + 1180}-{a + 1250}:{K_LOOP}")
    shown = [x[2] for x in r.lcd if a < x[0] < a + 380]
    c.ok(any(l2.startswith("MUTATE   T5") for l2 in shown), "MUTATE T5 shown")
    shown = [x[2] for x in r.lcd if a + 420 < x[0] < a + 580]
    c.ok(any(l2 == "UNDO     T5    1" for l2 in shown), "MERGE + - restores seed 1")
    all_l2 = [x[2] for x in r.lcd if x[0] > a + 580]
    c.ok(any(l2.startswith("MUTATE   T7") for l2 in all_l2), "MERGE + Track 7 directly mutates T7")
    c.ok(any(l2.startswith("MUTATE ALL") for l2 in all_l2), "MERGE + REC mutates all non-kick lanes")
    c.ok(any(l2.startswith("SNAPSHOT SAVED") for l2 in all_l2), "COPY + MERGE saves snapshot")
    c.ok(any(l2.startswith("SNAPSHOT LOADED") for l2 in all_l2), "ERASE + MERGE reloads snapshot")
    c.ok(any(l2.startswith("STUTTER 1/8") for l2 in all_l2), "LOOP + REW engages 1/8 stutter")
    l1, l2 = r.lcd_at(end)
    c.ok(l2[4] == "B", f"holding track 5 to focus it does not mute it ({l2!r})")
    return r


def sc_timing(c: Check):
    # Eight bars of the factory scene: the kick and the MIDI clock keep time through every
    # bar line and chord change (the probe reports in 2.67 ms blocks).
    end = PLAY_AT + int(8 * BAR_BLOCKS) + 20
    r = run_probe(end, press=f"{PLAY_AT}:{K_PLAY}")
    kicks = [n[0] for n in r.ons(9, 36)]
    gaps = [b - a for a, b in zip(kicks, kicks[1:])]
    beat = 4 * STEP_BLOCKS
    c.ok(len(kicks) >= 30 and all(abs(g - beat) < 2 for g in gaps),
         f"{len(kicks)} kicks, gaps {min(gaps)}-{max(gaps)} blocks, a beat is {beat:.1f}")
    f8 = [b for b, v in r.realtime if v == 0xF8 and b > PLAY_AT]
    gaps = [b - a for a, b in zip(f8, f8[1:])]
    want = 4 * TICK_S / BLOCK_S
    # The longest gap is one clock per bar, on the chord lane's first hit after a chord
    # change. It measures 13 probe blocks.
    c.ok(len(f8) > 750 and max(gaps) <= want + 6,
         f"{len(f8)} clocks, longest gap {max(gaps)} blocks, nominal {want:.2f}")
    return r


SCENARIOS = {
    "boot": sc_boot,
    "foreign": sc_foreign,
    "play": sc_play,
    "mute": sc_mute,
    "multimute": sc_multimute,
    "dropout": sc_dropout,
    "solo": sc_solo,
    "solohold": sc_solohold,
    "fill": sc_fill,
    "scene": sc_scene,
    "root": sc_root,
    "sync": sc_sync,
    "click": sc_click,
    "dense": sc_dense,
    "capture": sc_capture,
    "dump": sc_dump,
    "chain": sc_chain,
    "edit": sc_edit,
    "genpage": sc_gen_page,
    "exit": sc_exit,
    "transpose": sc_transpose,
    "euclid": sc_euclid,
    "tap": sc_tap,
    "knob": sc_knob,
    "thru": sc_thru,
    "mutate": sc_mutate,
    "timing": sc_timing,
}


def main() -> int:
    global ROM
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", type=Path, default=ROM)
    ap.add_argument("--scenario", action="append", choices=sorted(SCENARIOS))
    ap.add_argument("--exclude-scenario", action="append", default=[], choices=sorted(SCENARIOS))
    ap.add_argument("--dump", action="store_true", help="print each scenario's decoded timeline")
    a = ap.parse_args()
    ROM = a.rom.resolve()
    if not PROBE.exists():
        print(f"error: {PROBE} not built (cmake --build emulator/build)")
        return 2
    if not ROM.exists():
        print(f"error: {ROM} not built (make -C jam8)")
        return 2
    names = [n for n in (a.scenario or SCENARIOS) if n not in a.exclude_scenario]
    failed = 0
    for n in names:
        c = Check(n)
        r = SCENARIOS[n](c)
        c.ok(0 < r.sp_max <= 0x7F, f"stack peak 0x{r.sp_max:02x} inside the 8031's 128 bytes")
        if a.dump:
            dump(r, BOOT)
        status = "PASS" if not c.fails else "FAIL"
        print(f"{status} {n}")
        for f in c.fails:
            print(f"   FAIL {f}")
        if a.dump or c.fails:
            for f in c.notes:
                print(f"   ok   {f}")
        failed += bool(c.fails)
    print(f"{len(names) - failed}/{len(names)} scenarios passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
