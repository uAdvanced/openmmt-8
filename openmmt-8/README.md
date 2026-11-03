# OpenMMT-8 ROM

This file is the development record for the ROM.
The user instructions are in `README.md` at the repository root.
The user manual is `MANUAL.md`.

The ROM is built from `openmmt-8/core/` with SDCC:

```bash
bash openmmt-8/tools/build_openmmt8_core.sh
```

The image has to fit in a 32 KB EPROM. The build fails otherwise.
It is written to `openmmt-8/build/openmmt8_core.bin` and copied to
`openmmt-8/build/openmmt8_rom.bin`.

`tools/build_roms.sh` builds both profiles and JAM-8, and copies the images to `dist/roms/`:

```bash
bash tools/build_roms.sh
```

## Build profiles

`OPENMMT8_PROFILE` selects the feature set.

- `default`: the part EDIT extensions (MIDI note and step entry, data knob,
  audition, FF/REW by the quantize grid, bar:beat:tick times, one-step undo).
  No tape: the TAPE key holds only the MIDI dump page.
- `stock`: tape save, load, and verify on the TAPE key, and part EDIT without
  those extensions. It builds in `build/core-stock` and writes
  `build/openmmt8_stock_rom.bin`.

```bash
OPENMMT8_PROFILE=stock bash openmmt-8/tools/build_openmmt8_core.sh
```

In the sources these are the `OPENMMT8_TAPE` and `OPENMMT8_EDITX` defines
(`core/core.h`).

The ROM build is C only. There is no byte-level patching of another ROM.

Screen text is OpenMMT-8's own wording. Short function labels such as
SELECT PART, TEMPO, and BEAT stay, because they name the function.

## Memory

Battery-backed RAM holds the part table at `0x0400`, the settings, the song
table at `0x0502`, and a heap of part and song records from `0x0600` up to
`cfg.heap_end`. The key-scan state and the dump receiver sit between the song
table and the heap. They are not part of a dump.

## MIDI dump

The last TAPE page (the only one in the default profile) sends an OpenMMT-8 dump,
and OpenMMT-8 loads a received one.
The header is `F0 7D 4F 4D 38 01`.
The payload is `0x0400` through `0x05C9`, then the heap through `cfg.heap_end`,
with each byte packed into 7-bit MIDI data.
A load writes those bytes back and restarts through the reset vector.
An original unit does not accept this header.
This ROM does not accept a dump from an original unit.

## Checks

Build the emulator programs, then run the probes:

```bash
cmake -S emulator -B emulator/build -DMMT8_EMU_ENABLE_CLAP=ON
cmake --build emulator/build -j
make -C openmmt-8 test
```

The probes drive the built ROM in `emulator/build/mmt8_clap_wrapper_mmio_probe`.
Each one prints PASS or fails with the reason.
The transcripts are in `openmmt-8/build/probes/`.

- `tests/probe_record_track_leds.py`: while RECORD is held, the LCD asks
  for a track, and empty tracks flash.
- `tests/probe_midi_record_playback.py`: arms RECORD, records the
  injected MIDI, punches out with PLAY, and checks that the same bytes come back on MIDI out.
