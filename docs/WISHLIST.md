# OrGigs wish list

Collected wishes, sorted and discussed before they are built. Newest round on top.

## Round 4 (from 2026-11-04)

### Fixes
1. ~~**External MIDI: sustain, pitch bend, modulation.**~~ Done 2026-11-04:
   - CC64 now holds the notes of its channel, both inside and out.
   - A new BEND setting (zone screen, SEL + KNOB 4) chooses what bend and CC1 do on the organs:
     - PITCH (factory): bend ±2 semitones, CC1 switches the rotary slow/fast
     - ROTARY (as before): kick/tutti, slow/brake/fast
   - Forwarding already worked; the CC-F filter switch blocks it.

### Features
2. ~~**Settings export / import.**~~ Done 2026-11-05 (option A, a SysEx dump): core/orgigs_backup.c and
   tools/orgigs_backup.py. Save everything or one setup, load everything or one setup into any slot. OrGigs asks
   on its screen before loading (SAVE = yes) and sends the old state first as the undo. The panel can send a
   backup too (SEL+GLO, SEQ). Tests: tests/backup_test.py.
   - Later (B): load and save in the browser simulator, and send to the device over Web MIDI. sysex_send
     already reaches JavaScript as Module.onSysexOut.

### Structure (after the fixes)
3. ~~**Split into core and board layer**, with no change in behaviour.~~ Done 2026-11-05:
   - `firmware/core/`: sound, effects, MIDI zones, presets, and `orgigs_ctl.c` (what any panel can do)
   - `firmware/board/fm1/`: the FM-1's panel, lights and screens, `board.h` (27 keys from F3)
   - Every host test (WAV, screens, MIDI log) is byte-identical to before. The emulator self-test passes.
4. **SMK-37 Elite port** (a special, extended variant). Step 2 done 2026-11-05: the platform is Dinghy 0.4 (its
   SMK-37 layer by genkigenki, device-tested in OberVave); `board/smk37` builds (Pro, Elite, MKE-P37) with the FM-1's
   panel on the SMK buttons; every platform test passes, the FM-1 host tests are byte-identical. Step 3 done
   2026-11-05: the Elite's flash backed up, OrGigs runs on it. Step 4 started 2026-11-05: bank PERFORMANCE
   (board/smk37/board.c): 8 knobs + fader 1 = the 9 drawbars, pads and ARP / PATCH set them to 0 / 8, the pads'
   colours show them (the six schemes of the pad lighting study, GLOW the default), 37 keys with velocity, faders
   2..4 swell / reverb / volume, the wheels, the moved functions. Next: details on the device, more banks and
   layouts, the Elite in the browser simulator.
5. **YST, a Yamaha SEQTRAK module** (the user's seqtrak_module.md, after jaeggerr/seqtrak-docs). Built 2026-11-06:
   core/orgigs_yst.c (finds the SEQTRAK, syncs track types and sounds, follows its sounds and knobs, sends CC / bank
   + program change / mute), the YST screen (fifth MIDI screen, FM-1 and SMK-37), the Elite's bank YST (FADER BANK:
   pads = tracks, faders with pickup), track names in the cloner, mixer and zones. Not synced on connect (the
   addresses are not proven): the knob values (shown "--" until turned) and the mute state.
   2026-11-07 on the device (charged SEQTRAK; the dead link before was its low battery): all critical functions
   work. Added: the selected track goes both ways (CurrentPart 01 10 27).

## Round 5 (from 2026-11-07, the user's notes after YST on the Elite)

### Strategy (discuss first)
- From now on the **Elite has priority** over the FM-1: controls and screens are designed for the Elite, not for
  both. An FM-1 build still comes along, though not after every small Elite change. Options:
  - (a) freeze the FM-1's controls now; new functions are fitted into the FM-1's logic later (usability trade-offs)
  - (b) leave the FM-1 out for now and port a future Elite version to it anew
  - The same question for the Elite itself: a new control concept, or rebuild step by step as now.
  This depends on the points below.

### MIDI module and Elite controls
- The keys always play the active manual etc.; no functions on the keys (pads and knobs are enough).
  E.g. the cloner picks MIDI channels with the 16 pads (so no drawbar display in MIDI mode, but the keys play).
- Every paged 4-knob layout becomes an 8-knob one (YST effect controls, channel mixer, other modules too).
- The MIDI module as a whole feels inelegant as it grows; YST, cloner and mixer overlap (no priority; may
  dissolve in a new concept).
- Two page buttons to step back and forth through the 5 MIDI screens.
- Screen order: YST right after the cloner.
- PERFORMANCE: a Chord Memory button. Held: chord on + capture, the chord held is stored. Short press: chord off.
- MIDI mixer: 2x8 knobs (a page switch) over 2x8 pads (no drawbars there): knobs = volume (other parameters
  selectable), top pad row = mute (green, as on the SEQTRAK), bottom row = solo (turquoise).

### Elite control concept (proposed and agreed 2026-11-07; page: https://claude.ai/artifact/8HoUJiHvCkCA2oGrFQ4Txj)
- Areas: ORGAN (drawbars), FX (FX), MIDI (GLOBE: ZONE CLONE YST HARMONIC CHORD), MIXER (PARA); again = back.
  FADER BANK stays the jump to YST and back.
- Fixed: SCALE shift, BT SAVE, PLAY/REC rotary, OCT+/-. Keys only play. 8 knobs per page, pads select/switch.
- ORGAN: SCALE + pad = setup 1-16, BT + pad = store; CHORD = chord memory (hold: on + capture, tap: off);
  vibrato moves to NOTE RPT; SEQ coupler.
- MIDI: ARP / PATCH = previous / next screen; < > stays the sound in YST. ZONE: K1-8 in, out, low, high,
  transpose, octave, velocity, BEND; its switches on the pads. CLONE: pads = channels. HARMONIC: pads 1-9.
  CHORD: pad 1 capture, pad 2 clear. YST: K1-8 cutoff res attack decay vol pan rev dly, SCALE + K1-4 FM.
- MIXER: K1-8 volume, KNOB BANK 1-8 / 9-16, SCALE + < > parameter (pan, reverb, delay); top pads mute
  (red), bottom solo (dark blue; changed from green / turquoise 2026-11-07). SEQTRAK tracks by CC 23 / 24; other devices by the volume (mute = 0 and back,
  solo = mute the others).
- FX: K1-4 the manual's drive, vibrato, delay and reverb send, K5-8 the shared ones; pads 1-3 manual, 4 rotary.
  Built 2026-11-07 with a second page ROTARY (ARP / PATCH): cabinets CLASSIC, COMPACT (fixed), USER 1 / 2 (start: gospel,
  vibratone; edited values kept in flash blob 1, no factory reset), pads 1-4 choose, K1-8 + SCALE + K1-4 the 12
  values, BT + pad 3/4 copies. The core's rotor reads les_rt (the FM-1 keeps the CLASSIC: its sound bit-identical).
- FX wishes (2026-11-07, by priority): 2nd rotary (done as cabinets), chorus, wah-wah, auto-wah, flanger, phaser,
  ring mod, amp, EQ, compressor. Plan: one insert slot (one effect at a time, on a chosen manual, before the
  rotary), EQ + compressor on the sum; measure the DSP load (BT tap: DSP %) with full chords first.
  DSP load measured 2026-11-08: 30 % with the doubler and many keys. Plan agreed. Built: the insert slot (core
  fxi_rt / fxi_buf, after the bus's vibrato, before the rotary; a stereo side to the output) and page EFFECT (FX,
  PATCH twice: pads 1-7 the effect, again off, 9-11 the manual, K1-8 its values, all kept in blob 2). First
  effect CHORUS (rate, depth, delay, mix, width, feedback), committed 1d8745a. WAH built 2026-11-08 (mod wheel = pedal
  while WAH is chosen; K1 pedal, low, high, resonance, mix, level), committed 5e433c5. AUTO-WAH built (level follower
  + optional LFO: sens, rate, low, high, reso, decay, mix, level), committed f4d87a2. FLANGER built (rate, depth,
  delay 0.5-5 ms, feedback +-, mix, width), device OK. PHASER built (rate, low, high, stages 2-8, feedback +-, mix,
  width; two all-pass chains for stereo); flanger + phaser committed 52fdabc. RING MOD built (freq 20-2000 Hz in
  ~6 % steps, mix, LFO rate, depth, width), committed 1bc231c. AMP built (CLEAN CRUNCH LEAD FUZZ, gain 1-64x with
  the level held, tone tilt, box OFF 1x12 2x12 4x12, level, mix): the slot is complete, committed f34f7ab. MASTER
  page built (4th FX page, ARP from FX): EQ low shelf, mid band, high shelf +-12 dB; compressor threshold, ratio,
  attack, release, make-up, GR meter; pads 1 / 2 on, kept in blob 3. Next:, phaser, ring, amp; then a
  page MASTER with EQ + compressor. With 70 % free a second slot is possible later.
- Steps (2 done and device-tested 2026-11-07, 3 and 5 device-tested 2026-11-07, 4 built 2026-11-07 (solo silences the channels the zones play and the SEQTRAK tracks, by CC 23 there, not CC 24), 5: hold CHORD, play, let go; tap off / on; vibrato on NOTE RPT): 1 the Elite's own UI file (done 2026-11-07: board/smk37/orgigs_ui.c, the FM-1's frozen; both firmwares
  bit-identical) 2 keys free (setups, clone, harmonic to pads) 3 MIDI area 4 mixer 5 chord memory button 6 FX.

### Power (settled 2026-11-08)
- The user's setup works as it is; nothing to build.

### Later (just noted)
- Elite: ARP (top) / PATCH (bottom) DB9 "row swap": it was fader 1 (up must be drawbar 9 in = 0, down out = 8, as a
  drawbar is pulled); fixed 2026-11-08, the LEDs stay.
- A new OrGigs lettering on the welcome screen (done 2026-11-10).
- Expression from the pedal socket (low priority).

## Round 6 (2026-11-10): two keyboards live (the Elite and a Yamaha reface CP)
- Needs: each instrument plays over both keyboards' ranges, alternating or layered; each on its own; one sustain
  sometimes on both, but above all each with its own sustain (pitch, mod): hold a note on one, play over it on the other.
- Short term (built 2026-11-10): FILTER CC IN per zone (ZONE pad 11, CCI): CC and pitch bend coming in on the zone's
  channel do not act inside (drawbars, rotary, expression, sustain, bend); all notes off still does. The out filter
  (CCF) is separate.
- MERKPOSTEN (long term, do not forget; the user: not much thinking time now): grow the MIDI functions. A MIDI-in
  mixer beside today's out mixer; or further: a classic MIDI filter module with freely chosen CCs instead of the three
  fixed filters; how many MIDI processors, and logic operators for them. Belongs also to a planned sister project
  that turns the FM-1 into a "MIDI logic monster".

## Round 7 (2026-11-11): stage wishes, sorted with the user
- Round 1 (now): the setup in use lights white while SCALE / BT is held (built); the tonewheel percussion's decay on pad 3
  held + K3, fast and slow each, 100 ms .. 12 s and HOLD, kept by the device (blob 4 v3), not in setups (built);
  layout C pads 9-12: SEQTRAK tracks on / off and the upper's cloner (purple), and a place for the coupler (proposal:
  top pad 6, DRV, in B-D, the drive is on fader 2) - open questions: which tracks (1-3 are drums on the SEQTRAK; or
  the ones the manuals play?), mute on the SEQTRAK (as SCALE + pad in YST)?
- Decided 2026-11-11 and built: the coupler (SEQ, C pad 12, non-tonewheel top pad 2) in turn OFF, UM+LM, UM+PM, LM+PM,
  UM+LM+PM in place of II/I on the Elite (zones' ZX_CPL; only the Elite's keys; a coupled pedal takes the lowest note;
  old II/I -> UM+LM); C pads 9-11 the manual (blue, purple coupled; bright the chosen); the cloner on < + > held
  anywhere (pads = 16 channels, white the own one, dim green while off), a channel chosen switches it on.
  Percussion (built 2026-11-11): 2ND 3RD 4TH x FAST SLOW = 6 (4TH 2' the Elite's only), each its own decay, not
  saved (back to classic at power-on; CLASSIC shown on the classic step), the FM-1 untouched.
- Round 2: lighting mode RED-GLOW (default): one red, top + bottom pad together constant, in 9 steps from top to
  bottom as the drawbar is pulled (B: the bottom pad's brightness only?); full-screen pop-up for on / off messages
  (knob values stay in the status line); YST sound browser full-screen (category small on top, sounds the full
  width; check the scrolling: it stops oddly around the first page, start and end), global by < + > held, K4 the
  category, K5 the sound, for the focused manual's track.
  Decided 2026-11-11: two B layouts independent of the colours: B1 the live switches on top, B2 C's bottom row
  (manual, coupler, FCCI, FCCO, MIDI OUT, INT) on top; both the drawbars below (round A B1 B2 C D). C pad 12 tapped:
  the coupler on / off, held + 9-11: a manual in / out (built). RED-GLOW the default, GLOW kept. Pop-ups: the knob
  values too, if the display allows (SPI 12 MHz: a full screen ~80 ms). YST via < + >: the sound changes on turning.
  Then (2026-11-11): pop-ups dropped altogether, only the full-screen YST browser stays. New: the wheels' targets
  (FX page WHEELS, per setup) and the pitch shifter, way A: the tonewheels bend +-1/2/3 octaves (SHIFT 1-3; every
  manual; bands follow the pitch, 12-18 kHz fade; RPN 0 out). Built.
  The YST browser full-screen (11 rows, the head small, K2 / K5 on over the categories, the list moves only at its
  edges, rows redrawn alone) and < + > held K4 / K5 anywhere for the focused manual's track. Built.
- Round 3 (2026-11-12, after the user's test): the mod wheel learns its travel (the Elite's reads backwards over a
  part of the range: it showed 59 % .. 40 %); 4TH percussion up to wheel 83 (B) before folding (no 85th wheel: the
  user agreed); K3 (< + >) stops at A and D; FADER BANK: home (PERFORMANCE) from anywhere, from there YST; YST pads
  12 the action pad (pink, a CC pulled up as fast as struck, 0 on release; CC chosen on WHEELS K3, default 102 =
  master effect 1 param 1, e.g. VINYL BREAK's BREAK), 13 STOP, 14 PLAY / PAUSE (FA FC FB; heard back too), 15 REC
  (red, no function: the SEQTRAK takes no record over MIDI); YST faders 1-3 the touch sliders FX LEVEL / HIGH PASS /
  REPEATER (CC 102 / 105 / 106 ch 1 after seqtrak-docs: 105 / 106 to be confirmed on the device), fader 4 always the
  volume; the browser in the big font (6 rows, the cursor's name runs). Built.
  Stays as it is: percussion takes the 1' drawbar (tonewheel); a performance changed on the SEQTRAK needs NOTE RPT (refresh).
  Open: DrumKit tracks (OS 2.0: type via SysEx 01 1p 3C; 7 sounds on the synth keys, notes unknown): the browser's
  list by the track's type; REC; the charge indicator in the battery symbol.
- Merkposten (2026-11-12): a second YST screen for the SEQTRAK's single and master effects (types via SysEx
  30 43..45 00, e.g. VINYL BREAK 0C 70; seqtrak-docs), see how KDx (SysEx only) does it, the DX track as a reface DX;
  layout C: pad 6 redundant, 7 badly placed, 8 the right direction; the GUI redesign.
- Later / Merkposten: delay on pads 6-8 held + K6-K8 (time, feedback; "rate"?); a new GUI design for the stage;
  expression pedal on the sustain socket (the user tests: does PED n move smoothly?); a double piano pedal (likely
  only one contact read).

### More to come
The user collects further wishes while playing v3 on the FM-1.

## 2026-11-15: the release round (the SEQTRAK less in front)

- Neutral drawbar preset names (no artists, no song titles; the values unchanged).
- The welcome screen without the head (kept: ./build.sh --intro-head), FM-1 and Elite.
- The SEQTRAK volume by SysEx out (kept as the project skill .claude/skills/seqtrak-volume). The YST screen only
  by GLOBE now.
- FADER BANK the next performance layout (A B1 B2 C D A); KNOB BANK and FADER BANK act on letting go; both
  together: HOME (upper manual, model 1, P1, every coupler off; the cloner kept).
- A model or layout chosen shows its name big over the screen (1.2 s). < + > held and K1..K3 turned: the pads
  show the layout at once (before: the cloner's channels all the while).
- Fader 3 the MIDI out's sum (every mixer volume, CC 7, times it; the SEQTRAK's tracks too).
- Faders 1..3 pick up after their value or job changed (the message line: FADER UP / DOWN TO n).
- Layouts B1, C, D: pad 7 held: K7 the delay time, K8 the delay send; pad 8 held: K8 the effect's MIX.

## 2026-11-13: fader 3 = SEQTRAK volume (PERFORMANCE)

- Fader 2 the reverb send, the drive off the faders (still on FX K1 and pad 6 in layout B).
- Fader 3 the SEQTRAK's volume (SysEx 00 00 01, what its own buttons and meter show: user-tested). The project
  volume (30 40 66) tried first on fader 3 is gone, and so is SCALE + fader 3; switching between the two gave
  dropouts of a few seconds.

## 2026-11-13: DRUM KIT tracks in the browser

- A kit track (PartAwmType 03) gets no list: a screen says KIT TRACK, K2 / < > / < + > K5 load nothing (drum sounds
  loaded onto a kit track did who knows what). The kits, how to pick one over MIDI and the kit's note map are not
  documented (jaeggerr/seqtrak-docs: only a DrumKit bank MSB 32 + part, unverified). Later: the note map tried on
  the hardware, the pads named after it.
- REC (no MIDI for it) and layout C pads 6-8: dropped for now (user, 2026-11-13).

## 2026-11-17: battery, and the look for OrGigs 2
- Charge indicator (Elite) and battery display (FM-1): core/orgigs_bat.c for both boards; a bolt in the symbol
  while the keyboard is on a USB host (computer, SEQTRAK; a plain charger does not show); no BATTERY LOW while
  charging. FM-1: symbol, SEL shows "BAT n%" and (for now) the raw reading, to set its divider (BOARD_BAT_SCALE,
  6600 like the Elite until measured). Built.
- V2 (Merkposten): the screen's look, all elements consistent: drawbars, buttons/tabs, knobs, lamps. Either a
  more realistic drawbar (rounded, shaded, "smoky" glass) or a mix of real faders with single LED lamps as on old
  organs. Proposals in the chat of 2026-11-17; mock-ups to choose from before building.
