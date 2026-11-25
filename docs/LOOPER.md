# OrGigs 2.0: the looper (concept, 2026-11-24)

Six tracks of up to 32 bars, on the Elite and the FM-1. A track records what a manual plays, the notes as played and
(on request) the moves of the organ's controls, and plays it back into a manual or out on a MIDI channel. Nothing is
quantized when it is recorded; the grid is applied while it plays, and can be changed or switched off afterwards.
The user's answers (2026-11-24): clock internal and external (MIDI clock); controller moves too ("almost an
automation lane"); overdub; length fixed or free; export instead of keeping it over power-off; both devices.

## 1. Time

- One timeline in **ticks, 384 per quarter note** (16 per MIDI clock). 32 bars of 4/4 = 49 152 ticks: an event's
  time fits 16 bits. 3/4 and 6/8 as a later option (the bar length is a number, not a constant).
- The tick counter runs in the audio callback (sample-exact phase accumulator), not on the 1 ms main loop: the tempo
  is any value 30.0 to 300.0 BPM, the playback jitter under one audio block.
- Because the takes are stored in ticks and not in milliseconds, **the tempo can be changed after recording** (it is
  MIDI: no pitch, no artefacts). An audio looper cannot do that.

### Clock: INT, EXT, AUTO
- **INT**: own tempo (knob, TAP). Sends MIDI clock (F8) and START/STOP (FA/FC) on the zone's MIDI out, switchable: the
  SEQTRAK can follow OrGigs.
- **EXT**: follows MIDI clock coming in (the SEQTRAK's, a DAW's): 24 PPQN, interpolated to 384 with a smoothed
  period (a small PLL), START/STOP/CONTINUE and song position. Today ml_midi_in passes F8..FF through only
  (orgigs_midi_zones.c): the looper taps in there.
- **AUTO** (default): INT until a clock comes in, then EXT; INT again 2 s after the clock stops.
- To check on hardware first: that the SEQTRAK sends clock and START/STOP to the Elite / FM-1 over USB.

### Length and tempo: three modes (cf. phrase loopers)
| Mode | Set before | First take does |
| --- | --- | --- |
| **FIXED** | tempo + bars (1..32) | starts at the next bar line (count-in 1 bar, optional), stops by itself |
| **FIT** | bars | REC ... REC: its length sets the **tempo** (bars fixed) |
| **FREE** | nothing | REC ... REC: its length sets tempo **and** bars: the number of bars n (1, 2, 4, 8, 16, 32, then 3, 6, 12, 24) that puts the tempo nearest the middle of 80..160 BPM |

- Under EXT the tempo is the clock's: FIT and FREE snap the take to whole bars instead.
- Later tracks: their length is a whole number of bars (1..32) taken from their own first take, snapped; they all run
  on the master bar grid (bar 1 together). A 1-bar bass under an 8-bar comping works.
- The second REC of FIT/FREE may come a little late or early: the end is snapped to the nearest beat of the derived
  tempo (and the take trimmed or padded), so the loop is seamless.

## 2. Tracks

- **Six tracks**. Each: state (EMPTY, REC, PLAY, OVERDUB, MUTED), length in bars, target, quantize, level.
- **Target**: UPPER, LOWER, PEDAL (the organ) or a MIDI channel 1..16. Default: the manual (zone) that was focused
  when the track was recorded. A track recorded on UPPER can be sent to PEDAL or to the SEQTRAK afterwards.
- **What a track hears**: the focused manual's notes **after** the zone (transpose, cloner, harmonic, chord memory:
  what you hear is what is recorded), the sustain pedal, and with AUTOMATION on the controls (section 4).
- **Live keys and loop on the same manual**: each key gets an owner mask (live, track 1..6) like ml_key_sus's
  sources (KSUS_PANEL, KSUS_IN): a key sounds while any owner holds it. No stuck notes, no cut notes; the 84
  generators are shared as today (no voice stealing).
- **Level**: scales the velocities (for velocity-sensitive targets, MIDI) and the expression; 0 = silent but running.
- Stop, mute, clear, a target change: the track's sounding notes get their note-off first (a 128-bit note map per
  track and target).

## 3. Memory

- The events live in the app RAM (96 KiB; .data+.bss 60.8 KB in 1.5, the stacks are elsewhere: ~35 KB free), **not
  the pool** (3 KB left, kept for the sounds).
- **One event = 4 bytes**: tick (16 bit), a, b.
  - a < 128: a note: a = note, b = velocity (0 = note-off)
  - a >= 128: a control: a - 128 = the control's slot (section 4), b = its value 0..127
- **One shared arena of 24 KB = 6 144 events** for all six tracks (one track may take nearly all). Tracks are
  contiguous, sorted by tick; a pass is merged into its track at the loop end (one memmove, < 0.1 ms).
- **Overdub with one undo/redo**: a pass records into a 2 KB pass buffer (512 events; full: the pass is merged early,
  then that pass has no undo). UNDO drops the last merged pass (its events are kept apart until the next pass starts),
  REDO puts it back.
- The screen shows the memory left in %. Full: recording stops, MEMORY FULL; playback goes on.
- Estimated use: a dense comping chord every beat (4 notes on + off) = 32 events per bar = 1 KB per 8 bars.

## 4. Automation (the controls)

- A track with **AUTO** on also records the moves of the organ's controls on its manual, as events in the same
  stream: drawbars (9 + 4 pedal), swell/expression, sustain, rotary fast/slow/stop, percussion, vibrato on/off, drive,
  delay and reverb sends, the insert effect on/off. Slots: one number per control, from the parameter table
  (core/orgigs_params.c) and the drawbars, at most 128.
- **Thinned while recording**: one event per control per 1/64 note at most, and only on change; a knob sweep over a
  bar costs ~16 events, not hundreds.
- **A snapshot at the loop start**: when the first take starts, the track stores the values of its controls (~30
  bytes); at every loop start they are set again, so the drawbars come back where they were.
- **Touch wins**: you move a control the loop also moves: the loop lets go of that control until its next loop start
  (as a DAW's "touch" mode). Two tracks moving the same shared control (rotary): the later event wins.
- Automation on a MIDI target: the controls are sent as their CCs (the ones in the MIDI chart), the drawbars as the
  drawbar CCs: a second organ or a soft synth follows.

## 5. Quantize (on playback, never destructive)

- Per track: grid OFF, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32; strength 0..100 %; swing 50..75 % (on 1/8 and 1/16).
- The note-on moves to the grid, the note-off by the same amount (the length stays). Controls are not moved.
- Playback reads half a grid ahead and puts the moved events in a small sorted queue (32 entries), so an event moved
  earlier still sounds in time.
- APPLY (hold): writes the quantized times into the track (then it is destructive, with UNDO).

## 6. Export and import (instead of keeping it in the flash)

- **Export**: a SysEx dump of the six tracks (tempo, lengths, targets, the events), in the format of the settings
  backup (core/orgigs_backup.c: 7-bit packed, checksummed, in parts).
- **A page on the site** (WebMIDI, like the installer and the backup) asks for the dump and saves a **Standard MIDI
  File** (.mid, type 1: one MIDI track per looper track, the controls as CCs), for a DAW.
- **Import** the other way: a .mid of up to six tracks, 32 bars, becomes a SysEx stream into the looper.
- Later, if wanted: a few loops in the flash (the store has room for blobs; to measure).

## 7. Panel

The rule stays: **the keys only play**. Everything else on pads and buttons.

### Elite: layout L LOOP (and the functions for the free mapping)
- Pads 1..6 (top): the tracks. Press: EMPTY -> REC -> PLAY -> OVERDUB -> PLAY ...; colours: dim empty, red REC,
  green PLAY, amber OVERDUB, blue muted; blinking = waiting for the bar line.
- Pads 9..14 (bottom): the tracks' STOP/MUTE (press), CLEAR (hold 1 s).
- Pad 7 UNDO / REDO (hold), pad 8 ALL: start/stop all; pad 15 TAP, pad 16 the clock (INT/EXT/AUTO, hold).
- REC and PLAY (today the rotary): in layout L REC = record on the selected track, PLAY = start/stop all.
- Knobs: K1 tempo, K2 bars, K3 mode (FIXED/FIT/FREE), K4 quantize grid, K5 strength / swing (SCALE), K6 target,
  K7 level, K8 AUTO on/off; of the selected track (the last pad pressed).
- The screen: six bars with the position in the loop, bar:beat, BPM, INT/EXT, memory left.
- For the free mapping (WISHLIST 2026-11-20): the same as functions (MF_LOOP_TRK1..6, MF_LOOP_STOP1..6, MF_LOOP_ALL,
  MF_LOOP_UNDO, MF_TAP), so a USER layout can put two tracks beside the drawbars.

### FM-1: a LOOP screen
- Its twelve buttons are two rows of six: the top row the six tracks (as the Elite's pads 1..6), the bottom row
  their STOP/MUTE, SEL + a bottom button CLEAR.
- KNOB 1 tempo, KNOB 2 bars / mode (SEL), KNOB 3 quantize / strength (SEL), KNOB 4 target / level (SEL).
- Which button opens the screen: to decide with the FM-1's other 2.0 changes.

### A click
- Count-in and click: on the screen (the bar blinks) and, switchable, a short click made in the DSP (a few lines, no
  pool); or the SEQTRAK's own, under EXT.

## 8. Steps (each with host tests; the browser simulator plays it before the devices)

1. **Clock**: tick counter in the audio path, INT/EXT/AUTO, START/STOP, TAP, clock out. Test: timing of F8 out, follow
   a clock script at 90/120/174 BPM, jitter.
2. **Tracks, notes**: arena, record/play/overdub/undo, FIXED, targets, the owner masks. Test: scripts that play keys
   and check the MIDI and the organ's keys over several loops; memory full; stop/mute/clear without stuck notes.
3. **FIT and FREE**, the snap of the take end; **quantize** on playback, APPLY.
4. **Automation**: slots, thinning, snapshot, touch wins, CCs on MIDI targets.
5. **Panel**: Elite layout L + MF functions, FM-1 LOOP screen, the click; hints; the manuals.
6. **Export/import**: SysEx dump, the web page with .mid out and in; tests like web_test.mjs.

Goes after the free mapping's USER layouts (the looper's pads are mapping functions). Risks: the SEQTRAK's clock over
USB (check first), the ~35 KB RAM (to confirm on the device with the 2.0 mapping in it), the FM-1's panel (tight).
