#!/usr/bin/env python3
"""Headless probe: the record-hold track picker, the panel LED bus, and telemetry.

Expects (OpenMMT-8 ROM):
  - While RECORD is held (probe button 7), LCD shows pick a track / to record into.
  - FF04 record-armed telemetry (recarm=1) during that hold.
  - Track LEDs (the FF02 latch while FF04 selects the track bank): the empty tracks flash,
    so their bits change during the hold, and the selected record track's bit stays steady.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BLOCK_RE = re.compile(
    r"^block=(\d+)\b.*\brecarm=(\d+)\b.*\bff02=0x([0-9a-fA-F]+)\b.*\bff04=0x([0-9a-fA-F]+)\b.*"
    r"\btrack=0x([0-9a-fA-F]+)\b.*\btrack_obs=0x([0-9a-fA-F]+)\b"
)
LCD_RE = re.compile(r"^lcd1='([^']*)' lcd2='([^']*)'")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--probe",
        default=ROOT / "emulator/build/mmt8_clap_wrapper_mmio_probe",
        help="Path to wrapper_mmio_probe binary",
    )
    parser.add_argument(
        "--rom",
        default=ROOT / "openmmt-8/build/openmmt8_rom.bin",
        help="OpenMMT-8 ROM image",
    )
    # The UI starts near block 620, after the splash.
    parser.add_argument("--blocks", type=int, default=1300)
    parser.add_argument("--frames", type=int, default=128)
    parser.add_argument(
        "--hold",
        default="780-1060:7",
        help="hold-spec: RECORD held over block range (button 7 = RECORD)",
    )
    parser.add_argument(
        "--settle",
        type=int,
        default=4,
        help="blocks after the press before record-armed is required (key scan latency)",
    )
    parser.add_argument(
        "--output",
        default=ROOT / "openmmt-8/build/probes/record_track_led_probe.txt",
        help="Transcript path",
    )
    args = parser.parse_args()

    probe = Path(args.probe)
    rom = Path(args.rom)
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)

    if not probe.is_file():
        raise SystemExit(f"missing probe: {probe}")
    if not rom.is_file():
        raise SystemExit(f"missing rom: {rom}")

    cmd = [
        str(probe),
        str(rom),
        str(args.blocks),
        str(args.frames),
        "",  # press-spec
        "",  # midi-spec
        "",  # sync-spec
        "",  # serial-spec
        "",  # ffxx-spec
        "",  # transport-spec
        "",  # reset-pc-hex
        "",  # sfr-spec
        "",  # iram-spec
        "",  # xram-spec
        "",  # pc-spec
        args.hold,
        "",  # xpeek-spec
        "",  # ipeek-spec
        "",  # serial-stream-spec
    ]
    run = subprocess.run(cmd, capture_output=True, text=True, check=True)
    out.write_text(run.stdout)

    a, b = args.hold.split(":")[0].split("-", 1)
    hold_start, hold_end = int(a) + args.settle, int(b)

    latch_values: set[int] = set()
    recarm_when_prompt = False

    current_block = 0
    for line in run.stdout.splitlines():
        mb = BLOCK_RE.match(line)
        if mb:
            current_block = int(mb.group(1))
            recarm = int(mb.group(2))
            track_latch = int(mb.group(5), 16)
            if hold_start <= current_block <= hold_end:
                latch_values.add(track_latch)
                if recarm != 1:
                    raise SystemExit(
                        f"block {current_block}: expected recarm=1 during RECORD hold, got {recarm}\n"
                        f"line: {line}"
                    )
            continue
        ml = LCD_RE.match(line)
        if ml and hold_start <= current_block <= hold_end:
            l1, l2 = ml.group(1), ml.group(2)
            if "PICK A TRACK" in l1.upper() and "TO RECORD INTO" in l2.upper():
                recarm_when_prompt = True

    if not recarm_when_prompt:
        raise SystemExit(
            "never saw pick a track / to record into on lcd while RECORD held "
            f"(blocks {hold_start}-{hold_end}); see {out}"
        )

    first = next(iter(latch_values), 0)
    flashing = 0
    for v in latch_values:
        flashing |= v ^ first
    steady = ~flashing & 0xFF
    if not flashing or not steady:
        raise SystemExit(
            "expected some track LED bits to flash and the record track's bit to stay steady "
            f"during the RECORD hold; latch values={[hex(v) for v in sorted(latch_values)]}. See {out}"
        )

    print(f"PASS: record-hold track LEDs ({out})")
    print(f"  hold window: latch={[hex(v) for v in sorted(latch_values)]} "
          f"flashing=0x{flashing:02x} steady=0x{steady:02x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
