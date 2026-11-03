# JAM-8 manual

JAM-8 is the second ROM in the OpenMMT-8 plugin.
It plays loops and writes new patterns while it plays.
It does not record songs. OpenMMT-8 does that.
This project is not affiliated with Alesis.
The key names in this manual are the names printed on the panel.

## Terms

- A lane is one of the eight tracks. Track key 1 is track 1 (T1).
- A lane has a role: DRUM, BASS, CHORD or LEAD.
- A step is one position in a lane. A lane has 1 to 64 steps.
- A scene is a set of eight lanes. There are 16 scenes.
- The focused lane is the lane that the pages and the edit keys change.
- The chain is a list of scenes that plays in order.

## Power-on

The LCD shows `JAM-8 LOOP ROM` and `GENERATIVE V1.0`.
Memory stays in the battery RAM.

Hold ERASE at power-on to clear the memory.
The LCD then shows `MEMORY CLEARED` and `FACTORY SCENES`.
A first power-on does the same when the memory is empty or holds data from another ROM, including OpenMMT-8.

## Home screen

```
HOUSE    Am   T1
DDDDBCLD 1.1 S01
```

Line 1, from left to right:

- The style of the focused lane.
- The current chord. `m` is minor, `o` is diminished.
- The focused track, T1 to T8.

The tempo is on the TEMPO page (hold TEMPO).

Line 2, from left to right:

- One character for each lane: `D`, `B`, `C` or `L` for the role.
  - `.` is a muted lane.
  - `*` is a lane whose mute or solo waits for the launch point.
  - `-` is a lane held out by QUANT, or silenced because another lane is soloed.
  - `S` is a lane that is soloed and playing.
  - `R` is a lane armed for capture.
  - `F` is a drum lane during a fill.
- When a scene is queued, `>02` shows the next scene.
- Otherwise, during play, `1.1` shows the bar of the scene and the beat.
- `S01`, at the right end, is the current scene.

A message replaces line 2 for about 1 second.

## Transport

| Key | Function |
| --- | --- |
| PLAY | Start from the top of the scene. With the chain on, start from the first chain entry. |
| STOP | Stop. When stopped, continue from the stop position. |
| CLICK held, then PLAY | Tap tempo. Tap PLAY two or more times. |

PLAY starts after a short pre-roll of about 40 ms.
JAM-8 sends MIDI Start, Continue, Stop and Clock when CLK OUT is ON.

## Home keys

| Key | Function |
| --- | --- |
| Track key, tap | Focus the lane and mute or unmute it. Tap several track keys together to mute or unmute them all at once. |
| Track key, hold longer than 300 ms | Focus the lane only. |
| QUANT held, then a track key held | That lane drops out while both keys are held. Release either key and it plays again on its next step. The stored mute does not change. |
| LOOP held, then a track key tapped | Latch or unlatch solo on that lane. Several lanes can be latched. The last one clears solo. |
| LOOP held, then a track key held past 300 ms | Solo the lanes whose keys are held. Release the track keys and the latch is unchanged. |
| Track key held, then + or - | Change the density (0 to 9) of that lane. On a lane with the EUCLID style, change Euclid K. |
| + or - | Change the tempo by 1 BPM. Line 2 shows the new tempo for a moment. Does nothing with SOURCE MIDI. |
| 1 to 9, 0 | Queue scene 1 to 9. The key 0 queues scene 10. |
| REW or FF | Rotate the focused lane by one step. |
| LOOP held, then REW or FF | Beat-repeat stutter while held: REW repeats a 2-step (1/8) window, FF repeats a 1-step (1/16) roll. |
| FOOT or FILTER, held | Fill. Drum lanes play their fill steps. |
| MERGE, tap | Mutate the focused lane: a new seed and a new pattern. |
| MERGE held, then + | Mutate the focused lane. |
| MERGE held, then - | Undo: go back to the previous seed. The last 8 seeds are kept. |
| MERGE held, then a track key | Focus and mutate that lane directly. |
| MERGE held, then REC | Mutate all non-captured lanes (lanes 2 to 8) at once. |
| COPY held, then MERGE | Save a snapshot of the current scene in battery RAM. |
| ERASE held, then MERGE | Reload the saved scene snapshot into the current scene. |
| REC held, then a track key | Arm or disarm capture on that lane. |
| REC, tap | Arm or disarm capture on the focused lane. |
| ECHO | MIDI thru on or off. |

A stored mute, and a latched solo, wait for the launch point (QUANTIZE page).
A dropout and a held solo cut notes that are already sounding. The lane plays again on its next step.
A queued scene starts at the end of the current scene's bars (LOOP page). A scene change clears a latched solo.
When stopped, mutes and a latched solo change at once.
QUANT and LOOP show their pages while held. As soon as a track key, REW or FF is used with them, the LCD returns to the home screen.

## Pages

Hold a function key to show its page. Release it to return to the home screen.
While the key is held:

- PGUP and PGDN pick the field.
- + and - change the value.
- The keypad enters a number. A number still being typed is set when the key is released.
- REC runs the page's action: send the dump on CLOCK, copy the scene on COPY, erase the scene on ERASE.
- The CC knob sets the field when KNOB CC is not OFF.

EDIT is different: a tap enters step edit, and another tap leaves it.
Pressing STOP while a function key is held returns home at once. That press does not start or stop playback. A STOP tap by itself still does.

Lane pages show the focused lane on line 1.
A track key on a lane page focuses that lane.

### GEN (TAPE key)

| Field | Range | Function |
| --- | --- | --- |
| STYLE | list | The pattern style. The list depends on the role. |
| SEED | 0-99 | The seed. The same seed always gives the same pattern. |
| DENSITY | 0-9 | How many steps play. |
| EVOLVE | 0-99 | The chance in percent that the lane mutates at the end of each loop. |
| EUCLID K | 0-64 | Hits of the Euclid pattern. |
| EUCLID N | 1-64 | Steps of the Euclid pattern. |
| EUC ROT | 0-63 | Rotation of the Euclid pattern. |
| VELOCITY | 1-127 | The lane velocity. |
| HUMANIZE | 0-9 | Random change of velocity. |

Styles:

- DRUM: HOUSE, TECHNO, BREAKS, BOOMBAP, GARAGE, DEMBOW, TRAP, ELECTRO, EUCLID.
- BASS: ROOT 8, OFFBEAT, OCTAVE, ACID, WALK, EUCLID.
- CHORD: STAB, PAD, STRUM, EUCLID.
- LEAD: ARP UP, ARP DOWN, ARP UPDN, ARP RAND, CONVERGE, CHORD, TURING, EUCLID.

A change to STYLE, SEED, DENSITY or the Euclid fields writes a new pattern.
The new pattern starts at the next loop of the lane.

### LENGTH

| Field | Range | Function |
| --- | --- | --- |
| LENGTH | 1-64 | Steps in the lane. Lanes of different lengths make a polymeter. |
| STEP | 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4 | The length of one step. |
| GATE | 1-99% | Note length as part of the step. |
| ROTATE | 0-63 | Start step of the lane. |

### CHANNEL (CHAN key)

| Field | Range | Function |
| --- | --- | --- |
| ROLE | DRUM, BASS, CHORD, LEAD | A new role resets the lane. |
| CHANNEL | 1-16 | MIDI channel of the lane. |
| NOTE | C-1 to G9 | The drum note, or the base note of a melodic lane. |
| VOICES | 3-4 | Notes in a chord. |
| SEVENTHS | OFF, ON | Chords with the seventh. |
| HARMONY | OFF, ON | The lane follows the chord. |
| TRANSP | -24 to +24 | Transpose in semitones. |
| SPREAD | OFF, ON | Drop-2 open chord voicing (raises the second chord voice by an octave). |
| MOD CC | OFF, 1-119 | Send this MIDI CC before each note on the lane's channel using the step velocity/accent. When set to 65 (Portamento Switch), sends 127 on tied steps and 0 otherwise. |

### HARMONY (TRANS key)

| Field | Range | Function |
| --- | --- | --- |
| KEY | C to B | The key. |
| SCALE | MAJOR, MINOR, DORIAN, PHRYGIAN, MIXOLYD, HARM MIN, MIN PENT, MAJ PENT, BLUES, CHROMA | The scale. |
| PROGRESS | VAMP, i VI III VII, I V vi IV, ii V I, i iv v, BLUES 12, i VII VI, TWO CHRD | The chord progression. Each chord lasts one bar. |
| CTRL CH | OFF, 1-16 | The control channel. |
| CTRL BY | KEY, CHORD | What a note on the control channel sets. |
| TRANSPOS | -24 to +24 | Transpose all melodic lanes together, in semitones. Shows the first melodic lane's TRANSP. PGUP from KEY goes straight here. |

Line 1 shows the current chord.
Chord lanes move each voice to the nearest note of the next chord.

The control channel steers the harmony from a keyboard:

- CTRL BY KEY: a note sets the key. The progression continues in the new key.
- CTRL BY CHORD: a single note in the scale sets the scale-degree chord (single notes outside the scale are ignored). Holding 2 or more notes simultaneously detects the exact played chord (including borrowed/chromatic chords, sus4, maj7, etc.) and voice-leads chord lanes to those tones. The chord holds until the next control note or a change on the HARMONY page.

The change waits for the launch point.
Notes on the control channel are not captured and do not pass thru.

### QUANTIZE (QUANT key)

| Field | Range | Function |
| --- | --- | --- |
| LAUNCH | STEP, BEAT, BAR | When mutes and control-channel changes take effect. |
| SWING | 50-75% | Swing of the 1/32, 1/16 and 1/8 steps. |

### LOOP

| Field | Range | Function |
| --- | --- | --- |
| BARS | 1-16 | Bars in the current scene. A queued scene starts after these bars. |

### CLOCK

| Field | Range | Function |
| --- | --- | --- |
| SOURCE | INTERNAL, MIDI | MIDI follows MIDI Clock, Start, Continue and Stop at MIDI in. |
| CLK OUT | OFF, ON | Send MIDI Clock and transport. |
| DUMP | SEND REC | Press REC to send all memory as sysex. |

### CLICK

| Field | Range | Function |
| --- | --- | --- |
| CLICK | OFF, ON | The click on each beat during play. |

Hold CLICK and press PLAY for tap tempo.

### TEMPO

| Field | Range | Function |
| --- | --- | --- |
| TEMPO | 40-250 | BPM. |
| FINE | 0-9 | Tenths of a BPM. |

Line 1 shows `SYNC INT` when the internal tempo runs the clock, and `SYNC EXT` when SOURCE is MIDI. With SYNC EXT the tempo comes from the incoming MIDI clock, and the value here is kept for when SOURCE goes back to INTERNAL.

### KNOB (NAME key)

| Field | Range | Function |
| --- | --- | --- |
| KNOB CC | OFF, 1-119 | The MIDI CC number of the knob. |

On the home screen the knob sets the density of the focused lane.
On a page the knob sets the current field.
The knob CC does not pass thru.

### SONG

| Field | Range | Function |
| --- | --- | --- |
| CHAIN | OFF, ON | Play the chain. |
| POSITION | 1-32 | The chain entry to edit. |
| SCENE | END, 1-16 | The scene of the entry. END stops the chain list there. |
| REPEATS | 1-16 | How many times the scene plays. |

After the last entry the chain starts again at entry 1.
The SONG LED is lit when the chain is on.

### SCENE (PART key)

| Field | Range | Function |
| --- | --- | --- |
| SCENE | 1-16 | Queue that scene. |

### COPY

| Field | Range | Function |
| --- | --- | --- |
| TO SCENE | 1-16 | The target scene. |

While COPY is held:

- Press REC to copy the current scene to the target scene.
- Press a track key to copy the focused lane to that lane.

### ERASE

While ERASE is held:

- Press REC to erase the current scene.
- Press a track key to erase that lane.

## Capture

Capture writes played notes into a lane.

1. Hold REC and press a track key. The lane shows `R`. Its LED blinks slowly.
2. Play notes at MIDI in.
3. Each note goes on the nearest step. On a CHORD lane, up to 4 notes played on the same step are captured as a polyphonic chord. When LAUNCH is STEP, sub-step timing offset (`0-15` ticks) is also captured, and notes held into the next step automatically set TIE.
4. After one loop of the lane, capture stops.

When armed while stopped, capture starts with PLAY.
A captured lane keeps its notes.
EVOLVE and the generator do not change it.
A change to STYLE, SEED, DENSITY or the Euclid fields writes a new pattern on it again.

## Step edit (EDIT key)

```
EDIT T1 P1 S 1 *
VELOCITY    LANE
```

Line 1 shows the track (T1 to T8), the page of 8 steps, the selected step, and `*` when the step is on (`-` when off).
Line 2 shows the field of the selected step.

| Key | Function |
| --- | --- |
| Track key, tap | Turn the step on or off. |
| Track key, press | Select the step. |
| Track key held, then 1 to 4 | Set the ratchet of that step. |
| Track key held, then + or - | Change the field of that step. |
| REW or FF | Previous or next page of 8 steps. |
| PGUP or PGDN | Pick the field. |
| + or - | Change the field of the selected step. |
| 1 to 8 | On a melodic lane, set the scale degree. |
| Digits | On a drum lane, enter the MIDI note. |

Step fields:

| Field | Range | Function |
| --- | --- | --- |
| VELOCITY | LANE, 1-127 | LANE uses the lane velocity. |
| NOTE | drum: LANE or a note. Melodic: DEG 1-32 | The note of the step. |
| OCTAVE | -4 to +3 | Octave of a melodic step. |
| RATCHET | 1-4 | Repeats inside the step. |
| PROB | 6-100% | The chance that the step plays. |
| COND | ALWAYS, FILL, NOT FILL, 1 OF 2, 2 OF 2, 1 OF 4, 4 OF 4, FIRST | When the step plays. |
| DELAY | 0-15 | Late start in ticks. A 1/16 step is 24 ticks. |
| ACCENT | OFF, ON | Accented velocity. |
| TIE | OFF, ON | Hold the note into the next step. |

Conditions:

- FILL plays only during a fill. NOT FILL plays only without a fill.
- 1 OF 2 plays on the first of every 2 loops, or when the previous step had `PROB < 100%`, plays only if the previous step fired (`PRE`). 2 OF 2 plays on the second loop, or after a probabilistic step, plays only if the previous step skipped (`!PRE`).
- 1 OF 4 plays on the first of every 4 loops. 4 OF 4 plays on the fourth.
- FIRST plays only on the first loop after PLAY.

An edited lane is a captured lane.
During play the step under the playhead blinks.

## LEDs

| LED | Lit when |
| --- | --- |
| Track 1-8 | The lane is audible. It blinks fast while a mute or solo change is waiting. It blinks slowly when armed for capture. In step edit, the LEDs show the steps of the page. |
| PLAY | Playing. |
| REC | A lane is armed for capture. |
| PART | Blinks while a scene is queued. |
| SONG | The chain is on. |
| EDIT | Step edit is open. |
| ECHO | MIDI thru is on. |
| LOOP | Flashes on each beat during play. |

## MIDI

- MIDI in notes go to lanes armed for capture.
- With ECHO on, MIDI in passes thru to the channel of the focused lane.
- Notes on the control channel steer the harmony. They do not pass thru.
- The knob CC sets a value. It does not pass thru.
- With SOURCE MIDI, JAM-8 follows MIDI Clock, Start, Continue and Stop.
- With CLK OUT ON and SOURCE INTERNAL, JAM-8 sends MIDI Clock, Start, Continue and Stop.

## Factory scenes

The factory settings are:

- Tempo 124.0, swing 54%, key A, scale MINOR, progression i VI III VII.
- Control channel 1, CTRL BY CHORD, CLK OUT ON, LAUNCH BAR, 4 bars per scene.
- Drum lanes on channel 10, bass on channel 2, chords on channel 3, lead on channel 4.

| Scene | Contents |
| --- | --- |
| 1 | House: kick, clap, hats, open hat, offbeat bass, seventh stabs, an up-down arp of 12 steps, a Euclid rim (5 of 16). |
| 2 | Techno: kick, clap, hats, open hat, acid bass, pads, a Turing lead, a Euclid rim (3 of 8). The bass and the lead evolve. |
| 3 | Euclid polymeter: lanes of 16, 8, 12, 7, 5, 32 and 10 steps. |
| 4-16 | Copies of scene 1. |

The factory chain is scene 1 two times, scene 2 two times, scene 1 two times, scene 3 four times.
CHAIN is OFF.

## Sysex dump

1. Hold CLOCK.
2. Press PGDN until DUMP shows.
3. Press REC, still holding CLOCK. The LCD shows `SENDING DUMP`, then `DUMP SENT`.

The dump holds the settings, the 16 scenes and the chain.
It takes about 24 seconds.
Playback stops during a dump.

To load a dump, send it back to MIDI in.
The LCD shows `RECEIVING DUMP`, then `DUMP LOADED`.
`DUMP ERROR` means the data was not complete.
After a dump error, the next power-on loads the factory scenes.
