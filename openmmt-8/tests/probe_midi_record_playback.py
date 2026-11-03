#!/usr/bin/env python3
"""Headless probe: MIDI recorded into a track, punched out with PLAY, plays back the same bytes."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MIDI_INJECT_RE = re.compile(r"^inject block=(\d+) midi=([0-9a-fA-F\-]+)$")
MIDI_OUT_RE = re.compile(r"^midi_out block=(\d+) bytes=(.+)$")


def parse_bytes(token: str) -> tuple[int, ...]:
    parts = [p for p in token.strip().split("-") if p]
    return tuple(int(p, 16) & 0xFF for p in parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--probe",
        default=ROOT / "emulator/build/mmt8_clap_wrapper_mmio_probe",
        help="Path to wrapper probe binary",
    )
    parser.add_argument(
        "--rom",
        default=ROOT / "openmmt-8/build/openmmt8_rom.bin",
        help="Path to OpenMMT-8 ROM image",
    )
    parser.add_argument(
        "--output",
        default=ROOT / "openmmt-8/build/probes/midi_record_playback_probe.txt",
        help="Probe transcript output path",
    )
    # The UI starts near block 620. At 120 BPM the 4-beat count-in after the record PLAY
    # lasts about 750 blocks, so the MIDI goes in after block 1625.
    parser.add_argument("--blocks", type=int, default=3400)
    parser.add_argument("--frames", type=int, default=128)
    parser.add_argument(
        "--record-play-block",
        type=int,
        default=870,
        help="PLAY press that enters record after record-ready hold",
    )
    parser.add_argument(
        "--punchout-play-block",
        type=int,
        default=1900,
        help="PLAY press that punches out and restarts from beginning",
    )
    parser.add_argument(
        "--record-hold-range",
        default="760-830:7",
        help="Hold-spec for RECORD-ready setup",
    )
    parser.add_argument(
        "--midi-spec",
        default="1700:90-3C-40;1720:80-3C-40",
        help="Injected MIDI used while recording. The ROM sends note-off with velocity 0x40.",
    )
    args = parser.parse_args()

    probe_path = Path(args.probe)
    rom_path = Path(args.rom)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    if not probe_path.exists():
        raise SystemExit(f"probe binary not found: {probe_path}")
    if not rom_path.exists():
        raise SystemExit(f"rom not found: {rom_path}")

    press_spec = f"{args.record_play_block}:4,{args.punchout_play_block}:4"
    cmd = [
        str(probe_path),
        str(rom_path),
        str(args.blocks),
        str(args.frames),
        press_spec,
        args.midi_spec,
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        args.record_hold_range,
    ]
    run = subprocess.run(cmd, capture_output=True, text=True, check=True)
    output_path.write_text(run.stdout)

    injected_messages: list[tuple[int, tuple[int, ...]]] = []
    replay_messages: list[tuple[int, tuple[int, ...]]] = []

    for line in run.stdout.splitlines():
        m_in = MIDI_INJECT_RE.match(line)
        if m_in:
            block = int(m_in.group(1))
            payload = parse_bytes(m_in.group(2))
            injected_messages.append((block, payload))
            continue
        m_out = MIDI_OUT_RE.match(line)
        if m_out:
            block = int(m_out.group(1))
            for chunk in m_out.group(2).split(";"):
                replay_messages.append((block, parse_bytes(chunk)))

    if not injected_messages:
        raise SystemExit("no MIDI inject events parsed from probe output")

    expected_payloads = [payload for _, payload in injected_messages]
    post_punchout = [item for item in replay_messages if item[0] > args.punchout_play_block]

    cursor = 0
    for _, payload in post_punchout:
        if cursor < len(expected_payloads) and payload == expected_payloads[cursor]:
            cursor += 1
    if cursor != len(expected_payloads):
        rendered = ", ".join(f"{b}:{'-'.join(f'{x:02x}' for x in p)}" for b, p in post_punchout[:16])
        raise SystemExit(
            "record/playback check failed: expected recorded MIDI payloads "
            f"after punch-out PLAY at block {args.punchout_play_block}; observed={rendered or 'none'}"
        )

    print(
        "PASS: record/playback, "
        f"{len(expected_payloads)} recorded MIDI message(s) replayed after block {args.punchout_play_block}"
    )
    print(f"probe transcript: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
