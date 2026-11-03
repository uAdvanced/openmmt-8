# OpenMMT-8 manual

This manual describes functions that the original unit does not have.
Read the manual of the unit for the original functions.
This project is not affiliated with Alesis.

The built-in ROM has these functions.
The stock ROM does not have these functions.
Read `README.md` for the two ROM builds.

## Terms

- The unit is the sequencer.
- A key is a button on the panel.
- A part is one pattern.
- A track is one of the eight tracks in a part.
- Hold a function key to use its pages.
- Release the function key to close the page.

## Part-edit extensions

Open part edit with the procedure in the unit manual.
These extensions run during part edit.

### Option pages

1. Open part edit.
2. Hold the MIDI FILTER key.
3. The first page is EDIT DATA KNOB.
4. Press PAGE UP or PAGE DOWN to move through the four pages.
5. Press + or - to change the value.
6. Release MIDI FILTER.

The unit stores the four values with the other settings.

| Page | Line 1 | Line 2 | Values |
| --- | --- | --- | --- |
| Data knob | EDIT DATA KNOB | MIDI CC #: | OFF, or 1 to 119 |
| Audition | EDIT AUDITION | PLAY NOTES: | OFF or ON |
| Grid move | EDIT FF/REW MOVE | BY QUANTIZE: | OFF or ON |
| Bar time | EDIT TIME SHOWN | IN BARS: | OFF or ON |

A controller number of 0 shows OFF.
The number keys set the controller number.

### Data knob

1. Set a controller number from 1 to 119.
2. Stop the transport.
3. Close every function page.
4. Send that controller to MIDI in.

The first message does not move the field.
The next message moves the current field by the change in the value.
The unit does not send that controller message to MIDI out.
At the end of the track, the knob does not change a value.

### Audition

1. Set PLAY NOTES to ON.
2. Stop the transport.

The current note plays for a short time after a change to that note.
The note also plays after fast-forward, rewind, or a track change.
An empty step does not play.

Audition uses the MIDI channel of the track.
When the track channel is no change, audition uses the channel in the note.

### Grid move

1. Set BY QUANTIZE to ON.

Fast-forward and rewind then move through the events on the current step.
The keys then move to the next step or the previous step.
The step size is the quantize value.

When the step has no event, line 1 shows the time and (EMPTY).
Line 2 shows GRID and the quantize value.

Set BY QUANTIZE to OFF for the original event move.

### Time in bars

1. Set IN BARS to ON.

The time shows the bar, the beat, and the sub-beat.
A bar has 4 beats.
The beat in the bar is 1 to 4.
The sub-beat is 0 to 95.
The display form is `bar.beat.sub-beat`.
Example: `001.1.00` is bar 1, beat 1, sub-beat 0.

Set IN BARS to OFF for the original time display.

### MIDI note entry

1. Stop the transport.
2. Close every function page.
3. Send a note-on message.

When the current event is a note, the note number changes.
The velocity of that note also changes.

Hold RECORD to write the note at the current step.
The note length is one quantize step.
Release RECORD to leave step entry.

During step entry, line 1 shows STEP AT and the time.
Line 2 shows PLAY NOTES and the quantize value.
When memory is full, the display shows the memory-full message.

### Undo

Undo restores the event from before the last field change.
The unit stores one undo.
A different event starts a new undo.

1. Change a field.
2. Hold ERASE.
3. At the end of the track, the line shows UNDO LAST EDIT when undo is available.
4. On an event, press PAGE UP or PAGE DOWN until the line shows UNDO LAST EDIT.
5. Press RECORD.

The display shows EDIT UNDONE.
When memory is full, the display shows the memory-full message.

## Plugin controls

These controls belong to the plugin.
The original unit does not have these controls.

The panel window is in the Windows, Linux, and macOS builds.
The macOS panel has not been tested on a Mac.

### Mouse

1. Press the left button to hold a key.
2. Release the left button to release the key.
3. Press the right button to latch a key.
4. Press the latched key again to release it.
5. Use the FOOTSWITCH control in the same way.

### Computer keys

Send all keyboard input to the plugin.

| Computer key | Panel key |
| --- | --- |
| 0 to 9 | Number keys |
| - | Minus |
| + or = | Plus |
| Page Up | Page up |
| Page Down | Page down |
| F1 to F8 | Track 1 to track 8 |

### DAW sync

DAW sync is on by default.
The plugin sends start, stop, continue, song position, and clock from the host.
The dot is bright when the host sends a tempo and a beat position.

Set the clock source to midi + internal.
That source is the default.
The unit then follows the tempo of the host.
The clock-source procedure is in the manual of the unit.

Set DAW sync off to stop those messages.

### Click level

The Click output carries the click from the ROM.
The output is stereo.
Move the CLICK control to set the level.
The level is 0% to 100%.
The default level is 80%.

### Tape ports

Tape In is a mono input.
The input drives pin P3.2.
Tape Out is a mono output.
The output is pin P3.4.

The built-in ROM does not use these ports for tape save.
The built-in ROM does not use these ports for tape load.
A loaded ROM can use the ports when that ROM has tape functions.

### ROM menu

The ROM menu is at the top right of the panel.

1. On Windows, select Load ROM file.
2. Select the file in the dialog.
3. On Linux, select Load ROM file.
4. Type the path of the file.
5. Press Load.
6. Select Use built-in OpenMMT-8 to return to the built-in ROM.
7. Select Power off and on to restart the unit.

Power off and on keeps the memory.
A loaded ROM starts with empty memory.
Load a ROM file only with the right to use that file.

### Project data

The host project stores these items:

- Songs, parts, and settings
- The built-in ROM choice, or the loaded ROM file
- DAW sync
- Click level
- Panel width

The plugin id is `com.mmt8.emulator.stage2`.
Do not change that id.
A different id does not open saved projects.

## MIDI dump

Hold TAPE and press RECORD to send the memory over MIDI.
The display shows "send everything over midi" while TAPE is held.
In the stock ROM, this is the last page of the TAPE key.
The message is an OpenMMT-8 dump.
It starts with `F0 7D 4F 4D 38 01`.
OpenMMT-8 loads that message back into its memory and restarts.
An original unit does not load this dump.
OpenMMT-8 does not load a dump made by an original unit.
