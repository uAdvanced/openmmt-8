# JAM-8 development record

JAM-8 is the second ROM in this project.
The CLAP plugin has it built in next to OpenMMT-8.
Select "Use built-in JAM-8" in the ROM menu.
This project is not affiliated with Alesis.

It is a loop player and pattern generator, not a long-form recorder.
It runs on the same hardware as OpenMMT-8: an 8031 at 12 MHz, a 32 KB EPROM, battery RAM, one MIDI in, one MIDI out, a 16x2 LCD, the keys and the LEDs.
`MANUAL.md` in this directory is the operator manual.

## Build and test

```bash
make -C jam8 rom        # jam8/build/jam8_rom.bin
make -C jam8 host-test  # gcc unit tests of the algorithm code
make -C jam8 test       # host tests, then the wrapper-probe scenarios
```

The ROM build needs SDCC 4.2 or later.
`tools/build_jam8.sh` compiles with the small model and links at 0x0000.
The build fails when the image is larger than 32768 bytes, when XSEG leaves 0x0200-0x07FF, or when the stack (0x28 bytes) does not fit under 0x80.
The build prints a line like `JAM-8: 32361 of 32768 bytes used (407 free), stack at 0x4f`.

`tests/probe_jam8.py` runs `emulator/build/mmt8_clap_wrapper_mmio_probe` against the ROM.
It has 26 scenarios: boot, foreign, play, mute, dropout, solo, solohold, fill, scene, root, sync, click, dense, capture, dump, chain, edit, genpage, exit, transpose, euclid, tap, knob, thru, mutate, timing.
Use `--scenario NAME` (repeatable) to run some of them.
Every scenario also checks the stack peak against the 8031's 128 bytes of IRAM.
The probe reports in blocks of 128 frames at 48 kHz (2.67 ms).
A first boot builds the factory scenes and shows the splash until about block 890, so scenarios start at block 1000.

The host tests (`jam8/tests/test_jam8.c`) compile `gen.c`, `harmony.c`, `tables.c` and `scene.c` with gcc and `JAM8_HOST`.
They cover Euclid patterns, scales and chords, voice leading (no voice moves more than 2 semitones for C, Am, F, G and for ii-V-I sevenths), generator determinism and density, every style, mutate and undo, step encoding, scenes, and the sysex dump round trip.

## Files

| File | Contents |
| --- | --- |
| `core/hw.h` | SFRs and the panel map, the same as `openmmt-8/core/hw.h` |
| `core/jam.h` | memory map, step and header formats, every shared declaration |
| `core/main.c` | timer0 tick, tempo, start-up, the main loop |
| `core/midi.c` | MIDI rings at 0x0000 and 0x0100 and the serial ISR |
| `core/clock.c` | MIDI in: clock, transport, sysex, CC knob, capture, thru, control channel |
| `core/engine.c` | transport, lanes, step conditions, the event queue, scenes and chain at bar lines, click |
| `core/gen.c` | Euclid, style generators, Turing motif, density, mutate and seed history, background generation |
| `core/tables.c` | step lengths, scales, progressions, drum rows, bass and arp tables |
| `core/harmony.c` | key, scale, progression, control-channel root, chord tones, voice leading |
| `core/scene.c` | lane defaults, scene select, copy, erase, factory set, sysex dump and load |
| `core/keys.c` | key matrix scan, debounce, +/- repeat, footswitch |
| `core/lcd.c` | LCD buffer and incremental flush |
| `core/ui.c` | home screen, pages, step edit, messages, LEDs |

## Memory

| Address | Contents |
| --- | --- |
| 0x0000-0x01FF | MIDI receive and transmit rings |
| 0x0200-0x07FF | compiler work area, cleared at every boot |
| 0x0800-0x08FF | settings (`cfg_t`): magic byte 0x4A at 0x0800 and `JAM8` at 0x0821 |
| 0x1000-0x17FF | generator output: the next grid of each lane |
| 0x2000-0xAFFF | 16 scenes of 0x900 bytes: 8 lane headers of 32 bytes, then 8 grids of 64 steps |
| 0xB000-0xB03F | chain: 32 entries of scene and repeats |
| 0xB040-0xB041 | snapshot signature (`SNAP_SIGN_ADDR`) and saved scene bar length (`SNAP_BARS_ADDR`) |
| 0xB100-0xB9FF | scene snapshot buffer (`SNAP_BASE`, 0x900 bytes) for instant save/reload |
| 0xBA00-0xC9FF | polyphonic chord capture bank (`CCAP_BASE`, 16 scenes x 64 steps x 4 MIDI notes) |

At boot, memory without both the magic byte and the signature gets the factory scenes.
A single byte was not enough: OpenMMT-8's event data can hold 0x4A at 0x0800.
A signed bank, and every loaded dump, then has each setting, lane header field and chain entry clamped to the range the UI allows.

A step is 4 bytes:

- Byte 0: bit 0 on, bit 1 accent, bit 2 tie, bits 3-4 ratchet minus 1, bits 5-7 condition.
- Byte 1: on a drum lane the MIDI note (0 is the lane note). On a chord lane, `0x01..0x0F` (or `0x81..0x8F`) is an active voice mask for the 4 chord voices, and `0x41..0x44` selects 1 to 4 custom captured MIDI notes from `CCAP(scene, step)`. On a melodic lane the degree code: bits 7-5 octave plus 4, bits 4-0 the scale degree.
- Byte 2: velocity (0 is the lane velocity).
- Byte 3: probability in the high nibble (15 is always), delay in ticks in the low nibble.

A lane header holds role, channel, note, length, step length, gate, style, seed, density, Euclid k, n and rotation, evolve, flags (mute, follow harmony, captured, sevenths, open/Drop-2 spread), velocity, humanize, the Turing register, voices, transpose, rotation, 8 entries of seed history, and the per-lane MIDI CC modulation target (`H_MCC`, offset 30).

The sysex dump is `F0 7D 4A 38 01`, then the settings page and 0x2000-0xB03F as nibbles, then a 7-bit sum as two nibbles, then `F7`.
A dump is 74376 bytes and takes about 24 s at 31250 baud.

## Timing

Timer0 runs at 96 ticks per beat of the internal tempo (40.0 to 250.0 BPM).
With MIDI clock in, each F8 gives 4 ticks.
The main loop plays one pending tick per pass, scans the keys every 4 ms of tick time, refreshes the LEDs every 16 ms, and gives the rest to the background generator, the sysex dump, and the LCD.

A lane works out each step before it is due.
Lane l does so `8 - l` ticks early and queues its notes that far ahead.
So the eight lanes do their work on eight different ticks, and the notes still leave on their tick.
Bar and beat changes (scene launch, chain, progression, quantized mutes and chord changes) are applied 8 ticks before the bar or the beat.
PLAY starts the transport after 8 ticks of pre-roll, about 40 ms at 124 BPM.
With MIDI clock in, the pre-roll runs at once on Start.

The event queue has 32 entries with absolute due ticks.
On each tick, note-offs that are due go out before note-ons that are due.
Note-ons due together go out in the order they were queued, so lane 1 (the kick in the factory scenes) goes first.

Measured with the timing scenario (factory scene 1, 124 BPM, 8 bars):

- Kick to kick: 181 to 182 blocks, against 181.5 nominal.
- MIDI clock to clock: 7 to 9 blocks, against 7.56 nominal. Real-time `0xF8` clock bytes take priority in the UART TX ISR (`midi_rt`) ahead of queued channel messages, and chord voice leading is pre-computed in background passes (`engine_slice`).

One tick at 124 BPM is 5040 machine cycles (1 cycle is 1 us).
Voice leading for a 4-voice chord costs about 5800 cycles, once per chord change.
In a PC-sample profile of the factory scene, the tick work took about 24% of the CPU, the key scan about 22%, and the LED refresh about 10%.
The LCD flush and the background generator use the idle passes.

SDCC promotes 8-bit `%` and `*` to 16-bit library calls.
The hot paths use `MOD12()` and `mul8()` (MUL AB) instead.
Most function locals are `static __xdata`, because the small model would otherwise place them in IRAM, which has 128 bytes.

## Plugin integration

- `emulator/CMakeLists.txt`: `MMT8_JAM_ROM` (default `jam8/build/jam8_rom.bin`) is embedded as `kJamRom` in `generated/jam_rom.cpp`.
- `engine.h`: `enum class Builtin { File, OpenMMT8, Jam8 }`. `use_builtin_rom(Builtin::Jam8)` loads JAM-8. The value for a ROM file is `File`, because X11 defines `None`.
- `rom_store.h`: JAM-8 keeps its memory under the key `builtinjam8`. Store keys allow only digits and lowercase letters.
- `project_state.cpp`: flag bit 2 marks built-in JAM-8. An older plugin reads only bit 0 and opens such a project with OpenMMT-8.
- `panel_ui.cpp`: the ROM menu has "Use built-in JAM-8".
- `tools/build_clap_plugin_*.sh` build JAM-8 before the plugin.

## Differences from the plan

- A step's micro-timing is a delay of 0 to 15 ticks. It cannot play early.
- MERGE tap mutates the focused lane. MERGE held with + mutates, with - steps back through the seed history.
- On the home screen the keypad digits queue scenes 1 to 10, and +/- set the tempo.
- The control channel and its mode (KEY or CHORD) are on the HARMONY page (TRANS), not on the CLOCK page.
- Function pages show only while their key is held, and release returns to the home screen. EDIT alone toggles. Page actions on REC (dump, scene copy, scene erase) run on the REC press while the function key is held. Transposing every melodic lane is the TRANSPOS field on the HARMONY page, not TRANS with + or -.
- The top line of the home screen shows the focused track (T1 to T8) instead of its evolve value.
- PLAY always starts from the top. STOP while stopped continues.
- On the ERASE page a track key erases that lane, and REC erases the scene. On the COPY page a track key copies the focused lane to that track, and REC copies the scene.
- A dump that fails its checksum marks memory invalid, so the next power-on loads the factory scenes.
- Holding a track key longer than 300 ms focuses the lane without changing its mute.
- Hold QUANT and a track key to drop that lane out while both are held. Hold LOOP and tap a track to latch solo. Hold LOOP and the track past 300 ms to solo only while the track is held. A latched solo waits for LAUNCH. The held gestures cut sounding notes at once. Neither is stored in the scene.
- The CC knob sets the focused lane's density on the home screen.
- In CHORD mode, a single control note outside the scale is ignored; holding 2 or more notes simultaneously on the control channel detects the exact custom chord pitch-class mask (including borrowed/chromatic chords, sus4, maj7, etc.) and voice-leads chord lanes to those tones.
- Hold LOOP and press REW for a 1/8 (2-step) beat-repeat stutter or FF for a 1/16 (1-step) roll while held.
- Hold MERGE and tap a track key to mutate that lane directly, or hold MERGE and press REC to mutate all non-captured lanes (except lane 1 kick) at once.
- Hold COPY and press MERGE to save a snapshot of the current scene, and hold ERASE and press MERGE to reload the saved snapshot.
- When a step follows a probabilistic step (`PROB < 100%`), `1 OF 2` acts as `PRE` (plays only if the previous step fired) and `2 OF 2` acts as `!PRE` (plays only if the previous step skipped).
- The PGUP and PGDN keys pick the field on every page, also in step edit. REW and FF page through the steps in step edit.

## Known limits

- MIDI clock in is followed per F8, so incoming clock jitter passes through.
