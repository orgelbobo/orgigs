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
  6600 like the Elite until measured). Built. Measured 2026-11-20: a full FM-1 reads 624 off the cable (628 on a
  charger), 88 % with 6600; BOARD_BAT_SCALE 6810 for the FM-1, full = 100 %. 0 % (3.40 V) is still an estimate.
- V2 (Merkposten): the screen's look, all elements consistent: drawbars, buttons/tabs, knobs, lamps. Either a
  more realistic drawbar (rounded, shaded, "smoky" glass) or a mix of real faders with single LED lamps as on old
  organs. Proposals in the chat of 2026-11-17; mock-ups to choose from before building.
- V2 look chosen (2026-11-17): Varela Round as the font, everything as proposal C, the FX screens' 2 x 6 keys as
  in A (real and smoky). Mock-ups in the design canvas.
- V2 look built (2026-11-19, branch v2-models; on the Elite, runs smoothly, user OK): the font (tools/gen_font_ttf.py, labels squeezed
  into their boxes by cv_ctext), firmware/board/look.c (glass, drawbars, lamps, ring knobs, keycaps, pads),
  tools/gen_look.py (the glass and the shaft curves). Both boards, all screens; both devices run it smoothly (user OK). Open: the YST browser and the welcome screen as they are, the manuals' pictures.

## 2026-11-19: five more models for OrGigs 2 (built, not committed)
- 9 OSAKA KING (after a Japanese transistor combo of the 60s), 10 IL CASTELLANO (an Italian combo: drawbar 9 is a
  MIXTURE IV, SEL + KNOB 1 its TONE filter 1.5 to 10 kHz, the pedal its bass section BASS16 / 8 / 4 / SUSTAIN with
  a 0.9 s release), 11 DUTCH ROCK (a Dutch valve organ: SEL + KNOB 1 the built-in VALVE AMP from warm to hard into
  a small 3 kHz speaker; SPRING reverb as its default room), 12 TROSSINGEN BASS (a German accordion with a dry
  reed and the bass side's registers BASS16 / 8 / 4 / 2 on the pedal), 13 STEIRISCHE (the Styrian button
  accordion: wet tuning, 16 cents; helicon bass reeds on pedal 1-2). Model names: the user's for 9-11, proposals
  for 12 and 13.
- Five new wavetables (40 KB of the pool, 60 KB left); the models 1-8 sound bit-identical. The store keeps its
  size (a longer one would not load): the drawbar preset shown per model is kept for models 1-8 only.
- Open: the user listens (~/orgigs/v2-models/*.wav); the manuals; factory setups that use the new models; the
  Steirische's bellows shake (the tremulant now) and push/pull; the DSP load on the hardware.

## 2026-11-19: Merkposten for the next release: the short text
- Card on the hub, README, manuals, at-a-glance: after "It's an organ." the sentence starts with the drawbars:
  "Nine drawbars, three manuals with 84 free-running ..." or "2 x 9 + 4 drawbars" (a set for each manual).
  With 13 models the count in the text changes too.
- Later: promote OrGigs, maybe a YouTube video.

## 2026-11-20: 1.1 published, V1 closed
- OrGigs 1.1 published (tag v1.1, GitHub release, site): the Elite's expression pedal, the FM-1's insert effect,
  master, battery display (calibrated) and welcome screen, the charging bolt, Elite hints that fit. Both devices on it.
- V1 closed: main takes only bug fixes (1.1.x) from now on; main merged into v2-models, which becomes 2.0.

## 2026-11-20: OrGigs 2, a freely mappable surface (concept, decided)
- The factory layouts A-D become data (control -> function -> behaviour -> colour), run by one mapping engine;
  user layouts use the same format and start as a copy of a factory one. Targets: the parameter table, a new
  action table (setup recall, panic, brake, chord, ...), MIDI out (CC, note, PC). Behaviours by control type
  (momentary, toggle, set value, step / cycle, radio group, range and curve, pickup); a second level with SCALE,
  long press; no double tap. Pad colours on / off from a palette.
- Decided by the user: 4 USER layouts are enough; user layouts set NO screen contents (only pads, faders,
  knobs on the play level; the special pages stay fixed). Fixed anchors never mappable: volume, panic, HOME,
  back to the factory layout.
- Editing: in the browser over Web MIDI / SysEx (the site), on the device only a learn mode. FM-1: same
  engine, browser editor only.
- Order: inventory -> assignment sheets -> engine + factory layouts as tables (all tests unchanged) -> USER
  layouts + learn -> browser editor.

## 2026-11-21: four pipe organs (from the synthesis spec: renaissance, baroque, French symphonic, German romantic)
- Built: model 14, the Italian renaissance organ: its own soft principal wave, the ripieno on the nine drawbars
  (Principale 8', Voce umana 8' from c' at +2 Hz, Ottava, Flauto in VIII, Flauto in XII, XV, XIX, XXII, XXVI + XXIX),
  barely any chiff, quarter-comma meantone; its own drawbar list (12). TUNING on the LIGHTS page (Elite K5, FM-1
  SEL + KNOB1): HISTORIC / EQUAL for all, kept in a free bit of the device settings (store unchanged).
- The models are chosen and numbered by department (vintage 1-7, pipes 8-10, reeds 11-14); setups keep the
  model's own number, so OrGigs 1 still reads them.
- Next: the romantic organ (the crescendo roller on the expression pedal), then sharpen the baroque (Werckmeister,
  wind sag, no gamba) and the cathedral (harmonic flute, French reeds, a swell box): asks the user first, as both
  change how models of 1.1 sound.

## 2026-11-22: Merkposten for the next release (2.0)
- Hub card picture: no logo and name, but a screenshot of the main screen (as most cards do), the drawbars pulled as
  on the card's and the manuals' cover picture (not only the first three, as in the manual now).
- The SMK-37 Pro (and MKE-P37): the same keyboard; publish the Pro package too (built as orgigs_fm1-smk37.fwsc,
  identity SMK-37 Pro_...: each name needs its own package, M-VAVE's updater takes back only the unit's own), marked
  untested, and ask for testers. Check first what a Pro's stock firmware reports (the installer's V016 check).
- The hub card shows "Elite only: install the -smk37-elite package on an Elite, never on a Pro ..." prominently: not
  from our fmhub.json, README or release notes; ask the hub to change it (mail by the user) once a Pro package exists.
- Colours that link screen and pads (2026-11-23, from testing): finding the right pad should be easier. Use more
  colours that match between a switch on the screen and its pad (as some pages already do: ZONE switches, layout B's
  strip, the mixer's mute / solo), so a pad is found by its colour, not by counting. Think about a colour code that
  fits the look of the other screens and the pad lights, or a colour scheme for the whole project (screens, pads,
  button LEDs, manuals, site, card), usability first: a colour means one kind of thing everywhere (e.g. manual,
  switch on / off, page, danger), readable on the pads' LEDs and on the screen, safe for colour-blind players.
  Goes with the V2 look (2026-11-17) and the free mapping (2026-11-20).
- Name of the live switch on pad 8 (2026-11-24, user): "EFFECT" should read "INSERT FX": the switches beside it
  (drive, delay, rotary, ...) are effects too, so "EFFECT" alone does not say which. In the live-switch strip on the
  screen, the pad labels, the hints and the manuals. Version 2 is enough.
- Expression pedal with a type switch (2026-11-24, user): an expression pedal with a type or polarity switch
  (one maker's wiring / "other") seemed not to respond on the Elite under 1.5, apparently until set to the maker's. Not
  reconstructed (the user is no longer sure how it was): wait until it comes up again or is reported, then check the
  PED reading in both positions and decide on a manual line (which position) or a fix.
- TRS MIDI between the Elite and the FM-1 (2026-11-25, user): connect the two with a stereo jack cable, the Elite's
  MIDI out to the FM-1's MIDI in. Today: the FM-1's TRS MIDI IN is in the firmware (src/midi_uart.c, from the
  platform: into the zones like USB) but never tested on hardware; the Elite's TRS MIDI OUT is not driven by OrGigs
  (USB only, as the manual says). Three parts: find the Elite's jack (which pin, which UART; from the stock firmware)
  and send the zones' MIDI out there too (paced as TRS-SAFE); test the FM-1's input with a real cable; check type A
  or B on both (a plain stereo cable only works if both use the same).

## 2026-11-24: release 1.5 (decided with the user)
- 1.5 = v2-models as it is (the 16 models, the V2 look, ORGAN OCTAVE, SUS IN, the Elite simulator), without the free
  mapping (that stays 2.0, with the colour code). No new features, only finishing.
- Content: manuals (new models, ORGAN OCTAVE, SUS IN, screenshots in the new look, PDFs and at-a-glance sheets
  again); the short text (drawbars first, 16 models) in README, card, manuals; hub card picture = main screen
  screenshot; the simulators on the site (try/ FM-1, try/elite/ Elite) and the Elite's own play link in fmhub.json;
  the Pro package, marked untested, with a call for testers.
- Safety: settings and setups of 1.1 read right by 1.5 (a 1.1 backup in a test); all tests, all packages, both
  devices for the user's OK.
- Publish (on the user's OK, push needs a token): v2-models onto main, identity 674150, tag v1.5, GitHub release,
  site; the hub mail by the user.
- Later (not 1.5): the roller's stages on the screen.
- Merkposten (user, 2026-11-24): the hub's SMK-37 device page does not show the at-a-glance sheet (the FM-1 home
  screen does), it only links on to our installer. So the installer page (web/installer.html) should show the
  at-a-glance sheet for the chosen device (FM-1 / Elite), so people get some information before installing.
  Candidate for 1.5, it goes with the site work.

## 2026-11-24: wish: a small MIDI looper (user)
Six tracks, up to 32 bars, recording quantized and unquantized, playing back on the manuals or over MIDI. First
thoughts (to discuss, nothing decided):
- Memory: the events go to the app RAM (96 KiB, .data+.bss 60.8 KB in 1.5: ~35 KB free; stacks are elsewhere), not
  the pool (3 KB left). One event in 4 bytes (16-bit tick at 384 PPQN: 32 bars of 4/4 = 49 152 ticks; note + on/off;
  velocity; the track gives channel and target): a shared 24 KB buffer = ~6 000 events = ~3 000 notes for all six.
- Quantize on playback, never on recording: the takes stay as played, the grid (1/8, 1/16, triplets, strength) per
  track can be changed or switched off afterwards.
- Targets per track: upper, lower, pedal (the organ, with the live keys sharing the generators) or a MIDI channel
  (e.g. the SEQTRAK or a second keyboard).
- Clock: own tempo, or follow MIDI clock in (the SEQTRAK's), so the drums and the organ loops stay together.
- Open: also record sustain, swell, rotary speed (more events); overdub with undo of the last pass; length set by the
  first take; kept over power-off (flash) or not; the Elite's pads/transport (REC, PLAY are rotary today: the free
  mapping); the FM-1 maybe play/record only.
- 2026-11-24, the user's answers: clock INT and EXT (MIDI clock); controls too (automation); overdub; length
  FIXED / FIT / FREE; export instead of keeping; both devices. Concept for 2.0: docs/LOOPER.md.
