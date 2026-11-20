# OrGigs: the parameters

Every value a panel of OrGigs sets, from the parameter table in `firmware/core/orgigs_params.c` (written by
`tools/params_doc.py`; do not edit by hand). The boards step these values through the table and show them as it
formats them, so this list is what the firmware does.

- **Range, start:** as the screen shows them. The start is the value at the factory settings (the organ's own:
  performance setup 1).
- **Step:** what one detent of a knob adds, in the value's own units (the delay's time: 0..127 inside, shown in ms;
  ~6 %: a sixteenth of the value, at least 1).
- **Kept:** *setup*: in each of the 16 performance setups and the panel as it was left; *device*: one for the device;
  *flash*: in its own place in flash, the same for every setup (the insert effect, the master, the Elite's rotary
  cabinets); *not kept*: as you play.
- **CC:** the MIDI CC that sets it from outside (the drawbars: CC 12..20, the pedal's 12..15).
- **How often:** the player's estimate (live, per song, setup, rarely), for the control concept of OrGigs 2.


## Organ

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| MODEL | MODEL | 1 .. 8 | 1 | 1 | one | setup |  | ALGORITHM | SCALE + < >; < + > held K2 |  |
| DB | DRAWBARS 1-9 | 0 .. 8 | 1 | 0 | per manual | setup | 12 | PLAY: KNOB1-4 (ENV LFO EDIT the four) | K1-K8, fader 1 (drawbar 9), the pads (layouts A, B) |  |
| PERC | PERCUSSION | OFF .. 4TH SLOW | 1 | OFF | one | setup |  | FX: SEL + KNOB1 | FX K5; pad 3 (live switches) |  |
| CHIFF | CHIFF / MUSETTE DETUNE | 0 .. 127 | 2 | 0 | one | setup |  | FX: SEL + KNOB1 (pipes, reeds) | FX K5 (pipes, reeds) |  |
| II/I | COUPLER II/I | OFF .. ON | 1 | OFF | one | setup |  | PLAY: SEQ | SEQ (OFF, UM+LM, UM+PM, LM+PM, all) |  |
| OCT | OCTAVE | -2 .. +3 | 1 | +0 | one | setup |  | OCT- OCT+ | OCT- OCT+ |  |
| DRIVE | DRIVE | 0 .. 127 | 2 | 0 | per manual | setup |  | FX: KNOB1 | FX K1; pad 6 held + K6 |  |
| VIB | VIBRATO | OFF .. C3 | 1 | OFF | per manual | setup |  | FX: KNOB2; PLAY: ARP on / off | FX K2; NOTE RPT on / off; pad 1 |  |
| TREM | TREMULANT | 0 .. 127 | 4 | 0 | per manual | setup |  | FX: KNOB2 (pipes, reeds); PLAY: ARP | FX K2 (pipes, reeds); NOTE RPT |  |
| ROT | ROTARY | OFF .. ON | 1 | OFF | per manual | setup |  | FX: SEQ; SEL + REC | FX pad 4; SCALE + REC |  |
| DELAY | DELAY SEND | 0 .. 127 | 3 | 0 | per manual | setup |  | FX: KNOB3 | FX K3; pad 7 held + K8 |  |
| REVERB | REVERB SEND | 0 .. 127 | 3 | 0 | per manual | setup |  | FX: KNOB4 | FX K4; fader 2 |  |
| SPEED | ROTARY | SLOW .. FAST | 1 | SLOW | one | not kept | 1 | PLAY (brake), REC (slow / fast); CC 1 | PLAY, REC; the mod wheel; pads 4, 5 |  |
| D-TIME | DELAY MS | 20 .. 1000 | 2 | 328 | one | setup |  | FX: SEL + KNOB3 | FX K7; pad 7 held + K7 |  |
| FEEDBK | DELAY FEEDBACK | 0 .. 127 | 3 | 50 | one | setup |  | FX: SEL + KNOB2 | FX K6 |  |
| SPACE | REVERB | SPRING .. CATHEDRAL | 1 | SPRING | one | setup |  | FX: SEL + KNOB4 | FX K8 |  |
| EXPR | EXPRESSION | 0 .. 127 | 1 | 127 | one | not kept | 11 | CC 11 in | the pedal (an expression pedal); CC 11 in |  |
| BEND | BEND + MOD | PITCH .. ROTARY | 1 | PITCH | one | device |  | MIDI ZONE: SEL + KNOB4 | ZONE K8 |  |

## MIDI

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| IN CH | MIDI IN CH | 1 .. OFF | 1 | 1 | per manual | setup |  | MIDI ZONE: KNOB1 | ZONE K1 |  |
| OUT CH | MIDI OUT CH | 1 .. OFF | 1 | 1 | per manual | setup |  | MIDI ZONE: SEL + KNOB1 | ZONE K2 |  |
| LOWEST | LOWEST KEY | C-1 .. G9 | 1 | C-1 | per manual | setup |  | MIDI ZONE: KNOB2; LRN | ZONE K3; pad 5 (LRN) |  |
| HIGHEST | HIGHEST KEY | C-1 .. G9 | 1 | G9 | per manual | setup |  | MIDI ZONE: SEL + KNOB2; LRN | ZONE K4; pad 5 (LRN) |  |
| TRANSP | OUT TRANSPOSE | -24 .. +24 | 1 | +0 | per manual | setup |  | MIDI ZONE: KNOB3 | ZONE K5 |  |
| OCTAVE | OUT OCTAVE | -3 .. +3 | 1 | +0 | per manual | setup |  | MIDI ZONE: SEL + KNOB3 | ZONE K6 |  |
| VEL | FIXED VELOCITY | 1 .. 127 | 3 | 100 | per manual | setup |  | MIDI ZONE: KNOB4 | ZONE K7 |  |
| CC7 | MIXER CC7 | 0 .. 127 | 2 | 100 | per channel | setup |  | HOME (the mixer): KNOB1-4, ENV LFO ARP SEQ the row | PARA (the mixer): K1-K8 (VOL) |  |

## Insert Chorus

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| RATE | CHORUS RATE HZ | 0.10 .. 5.00 | 5 | 0.80 | one | flash (insert) |  | SEL + FX (INSERT): KNOB1 | EFFECT page: K1 |  |
| DEPTH | CHORUS DEPTH | 0% .. 100% | 5 | 50% | one | flash (insert) |  | SEL + FX (INSERT): KNOB2 | EFFECT page: K2 |  |
| DELAY | CHORUS DELAY MS | 3.0 .. 25.0 | 5 | 12.0 | one | flash (insert) |  | SEL + FX (INSERT): KNOB3 | EFFECT page: K3 |  |
| MIX | CHORUS MIX | 0% .. 100% | 5 | 50% | one | flash (insert) |  | SEL + FX (INSERT): KNOB4 | EFFECT page: K4 |  |
| WIDTH | CHORUS WIDTH | 0% .. 100% | 5 | 70% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB1 | EFFECT page: K5 |  |
| FEEDBK | CHORUS FEEDBACK | 0% .. 90% | 5 | 0% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB2 | EFFECT page: K6 |  |

## Insert Wah-Wah

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| PEDAL | WAH PEDAL | 0% .. 100% | 2 | 50% | one | flash (insert) |  | SEL + FX (INSERT): KNOB1 | EFFECT page: K1 |  |
| LOW | WAH LOW HZ | 200 .. 800 | 10 | 350 | one | flash (insert) |  | SEL + FX (INSERT): KNOB2 | EFFECT page: K2 |  |
| HIGH | WAH HIGH HZ | 1000 .. 4000 | 50 | 2200 | one | flash (insert) |  | SEL + FX (INSERT): KNOB3 | EFFECT page: K3 |  |
| RESO | WAH RESONANCE | 1 .. 10 | 1 | 5 | one | flash (insert) |  | SEL + FX (INSERT): KNOB4 | EFFECT page: K4 |  |
| MIX | WAH MIX | 0% .. 100% | 5 | 100% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB1 | EFFECT page: K5 |  |
| LEVEL | WAH LEVEL | 30% .. 300% | 10 | 120% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB2 | EFFECT page: K6 |  |

## Insert Auto-Wah

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| SENS | AUTO-WAH SENSITIVITY | 0% .. 100% | 5 | 60% | one | flash (insert) |  | SEL + FX (INSERT): KNOB1 | EFFECT page: K1 |  |
| RATE | AUTO-WAH LFO HZ | OFF .. 5.00 | 5 | OFF | one | flash (insert) |  | SEL + FX (INSERT): KNOB2 | EFFECT page: K2 |  |
| LOW | AUTO-WAH LOW HZ | 200 .. 800 | 10 | 300 | one | flash (insert) |  | SEL + FX (INSERT): KNOB3 | EFFECT page: K3 |  |
| HIGH | AUTO-WAH HIGH HZ | 1000 .. 4000 | 50 | 2500 | one | flash (insert) |  | SEL + FX (INSERT): KNOB4 | EFFECT page: K4 |  |
| RESO | AUTO-WAH RESONANCE | 1 .. 10 | 1 | 6 | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB1 | EFFECT page: K5 |  |
| DECAY | AUTO-WAH DECAY MS | 20 .. 2000 | 20 | 260 | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB2 | EFFECT page: K6 |  |
| MIX | AUTO-WAH MIX | 0% .. 100% | 5 | 100% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB3 | EFFECT page: K7 |  |
| LEVEL | AUTO-WAH LEVEL | 30% .. 300% | 10 | 120% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB4 | EFFECT page: K8 |  |

## Insert Flanger

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| RATE | FLANGER RATE HZ | 0.05 .. 5.00 | 5 | 0.25 | one | flash (insert) |  | SEL + FX (INSERT): KNOB1 | EFFECT page: K1 |  |
| DEPTH | FLANGER DEPTH | 0% .. 100% | 5 | 70% | one | flash (insert) |  | SEL + FX (INSERT): KNOB2 | EFFECT page: K2 |  |
| DELAY | FLANGER DELAY MS | 0.5 .. 5.0 | 1 | 2.0 | one | flash (insert) |  | SEL + FX (INSERT): KNOB3 | EFFECT page: K3 |  |
| FEEDBK | FLANGER FEEDBACK | -90% .. +90% | 5 | +50% | one | flash (insert) |  | SEL + FX (INSERT): KNOB4 | EFFECT page: K4 |  |
| MIX | FLANGER MIX | 0% .. 100% | 5 | 100% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB1 | EFFECT page: K5 |  |
| WIDTH | FLANGER WIDTH | 0% .. 100% | 5 | 60% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB2 | EFFECT page: K6 |  |

## Insert Phaser

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| RATE | PHASER RATE HZ | 0.05 .. 5.00 | 5 | 0.40 | one | flash (insert) |  | SEL + FX (INSERT): KNOB1 | EFFECT page: K1 |  |
| LOW | PHASER LOW HZ | 100 .. 1000 | 10 | 300 | one | flash (insert) |  | SEL + FX (INSERT): KNOB2 | EFFECT page: K2 |  |
| HIGH | PHASER HIGH HZ | 1000 .. 6000 | 100 | 2500 | one | flash (insert) |  | SEL + FX (INSERT): KNOB3 | EFFECT page: K3 |  |
| STAGES | PHASER STAGES | 2 .. 8 | 2 | 4 | one | flash (insert) |  | SEL + FX (INSERT): KNOB4 | EFFECT page: K4 |  |
| FEEDBK | PHASER FEEDBACK | -90% .. +90% | 5 | +30% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB1 | EFFECT page: K5 |  |
| MIX | PHASER MIX | 0% .. 100% | 5 | 100% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB2 | EFFECT page: K6 |  |
| WIDTH | PHASER WIDTH | 0% .. 100% | 5 | 50% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB3 | EFFECT page: K7 |  |

## Insert Ring Mod

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| FREQ | RING FREQ HZ | 20 .. 2000 | ~6 % | 440 | one | flash (insert) |  | SEL + FX (INSERT): KNOB1 | EFFECT page: K1 |  |
| MIX | RING MIX | 0% .. 100% | 5 | 50% | one | flash (insert) |  | SEL + FX (INSERT): KNOB2 | EFFECT page: K2 |  |
| RATE | RING LFO HZ | OFF .. 10.00 | 5 | OFF | one | flash (insert) |  | SEL + FX (INSERT): KNOB3 | EFFECT page: K3 |  |
| DEPTH | RING LFO DEPTH | 0% .. 100% | 5 | 30% | one | flash (insert) |  | SEL + FX (INSERT): KNOB4 | EFFECT page: K4 |  |
| WIDTH | RING WIDTH | 0% .. 100% | 5 | 50% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB1 | EFFECT page: K5 |  |

## Insert Amp

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| MODEL | AMP MODEL | CLEAN .. FUZZ | 1 | CRUNCH | one | flash (insert) |  | SEL + FX (INSERT): KNOB1 | EFFECT page: K1 |  |
| GAIN | AMP GAIN | 0% .. 100% | 2 | 40% | one | flash (insert) |  | SEL + FX (INSERT): KNOB2 | EFFECT page: K2 |  |
| TONE | AMP TONE | -10 .. +10 | 1 | +0 | one | flash (insert) |  | SEL + FX (INSERT): KNOB3 | EFFECT page: K3 |  |
| CAB | AMP CABINET | OFF .. 4X12 | 1 | 2X12 | one | flash (insert) |  | SEL + FX (INSERT): KNOB4 | EFFECT page: K4 |  |
| LEVEL | AMP LEVEL | 10% .. 200% | 5 | 50% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB1 | EFFECT page: K5 |  |
| MIX | AMP MIX | 0% .. 100% | 5 | 100% | one | flash (insert) |  | SEL + FX (INSERT): SEL + KNOB2 | EFFECT page: K6 |  |

## Master

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| LOW | EQ LOW | -12dB .. +12dB | 1 | +0dB | one | flash (master) |  | SEL + FX twice (MASTER): KNOB1 | MASTER page: K1 |  |
| MID | EQ MID | -12dB .. +12dB | 1 | +0dB | one | flash (master) |  | SEL + FX twice (MASTER): KNOB2 | MASTER page: K2 |  |
| M FREQ | EQ MID HZ | 200 .. 5000 | ~6 % | 1000 | one | flash (master) |  | SEL + FX twice (MASTER): SEL + KNOB2 | MASTER page: K3 |  |
| HIGH | EQ HIGH | -12dB .. +12dB | 1 | +0dB | one | flash (master) |  | SEL + FX twice (MASTER): KNOB3 | MASTER page: K4 |  |
| THRESH | COMP THRESHOLD | -40dB .. +0dB | 1 | -18dB | one | flash (master) |  | SEL + FX twice (MASTER): EDIT (COMP), KNOB1 | MASTER page: K5 |  |
| RATIO | COMP RATIO | 1:1 .. 20:1 | 1 | 4:1 | one | flash (master) |  | SEL + FX twice (MASTER): EDIT (COMP), KNOB2 | MASTER page: K6 |  |
| ATTACK | COMP ATTACK MS | 1 .. 100 | 1 | 10 | one | flash (master) |  | SEL + FX twice (MASTER): EDIT (COMP), KNOB3 | MASTER page: K7 |  |
| RELEASE | COMP RELEASE MS | 20 .. 1000 | ~6 % | 200 | one | flash (master) |  | SEL + FX twice (MASTER): EDIT (COMP), KNOB4 | MASTER page: K8 |  |
| L FREQ | EQ LOW HZ | 50 .. 400 | ~6 % | 120 | one | flash (master) |  | SEL + FX twice (MASTER): SEL + KNOB1 | MASTER page: SCALE + K1 |  |
| H FREQ | EQ HIGH HZ | 2000 .. 12000 | ~6 % | 6000 | one | flash (master) |  | SEL + FX twice (MASTER): SEL + KNOB3 | MASTER page: SCALE + K2 |  |
| MAKEUP | COMP MAKEUP | +0dB .. +12dB | 1 | +0dB | one | flash (master) |  | SEL + FX twice (MASTER): EDIT (COMP), SEL + KNOB1 | MASTER page: SCALE + K3 |  |

## Rotary Cabinet (User 1, 2)

| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| H SLOW | HORN SLOW HZ | 0.30 .. 1.50 | 2 | 0.80 | one | flash (cabinets) |  | - | ROTARY page: K1 |  |
| H FAST | HORN FAST HZ | 3.00 .. 9.00 | 10 | 6.80 | one | flash (cabinets) |  | - | ROTARY page: K2 |  |
| H RISE | HORN RISE S | 0.2s .. 4.0s | 1 | 0.8s | one | flash (cabinets) |  | - | ROTARY page: K3 |  |
| D SLOW | DRUM SLOW HZ | 0.20 .. 1.50 | 2 | 0.66 | one | flash (cabinets) |  | - | ROTARY page: K4 |  |
| D FAST | DRUM FAST HZ | 3.00 .. 8.00 | 10 | 5.90 | one | flash (cabinets) |  | - | ROTARY page: K5 |  |
| D RISE | DRUM RISE S | 0.5s .. 10.0s | 2 | 3.0s | one | flash (cabinets) |  | - | ROTARY page: K6 |  |
| XOVER | CROSSOVER HZ | 200 .. 2000 | 20 | 800 | one | flash (cabinets) |  | - | ROTARY page: K7 |  |
| BALANCE | BALANCE HORN | -10 .. +10 | 1 | +0 | one | flash (cabinets) |  | - | ROTARY page: K8 |  |
| H DOPP | HORN DOPPLER | 0 .. 18 | 1 | 14 | one | flash (cabinets) |  | - | ROTARY page: SCALE + K1 |  |
| D DOPP | DRUM DOPPLER | 0 .. 7 | 1 | 6 | one | flash (cabinets) |  | - | ROTARY page: SCALE + K2 |  |
| AM | LEVEL SWING % | 0% .. 200% | 5 | 100% | one | flash (cabinets) |  | - | ROTARY page: SCALE + K3 |  |
| WIDTH | STEREO WIDTH % | 0% .. 150% | 5 | 100% | one | flash (cabinets) |  | - | ROTARY page: SCALE + K4 |  |

93 values: 81 on both boards, 12 on the Elite only.
