#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The SMK-37 Elite's panel on the host build (tests/elite_pads.txt, tests/elite_midi.txt): the keys only play;
setups, the cloner's channels and the harmonic drawbars are on the pads; the MIDI area (8 knobs, ARP / PATCH the
screens, the zone's switches on the pads, chord capture); the chord memory on CHORD (tests/elite_chord.txt); the mixer (tests/elite_mix.txt); FX and the Leslie cabinets (tests/elite_fx.txt); the delay's echoes after a short note (tests/elite_delay.txt); the insert effects CHORUS, WAH (tests/elite_chorus.txt, tests/elite_wah.txt), AUTO-WAH (tests/elite_autowah.txt), FLANGER (tests/elite_flanger.txt), PHASER (tests/elite_phaser.txt), RING MOD (tests/elite_ring.txt), AMP (tests/elite_amp.txt); the MASTER page's EQ and compressor (tests/elite_master.txt); the YST sound browser (tests/elite_ysb.txt, < + > K4 K5: tests/elite_ysb_quick.txt, a kit track: tests/elite_ysb_kit.txt); panic and the lights (tests/elite_panel.txt); layout B DRAWBARS + PADS (tests/elite_layout.txt); the sustain pedal (tests/elite_sustain.txt); an expression pedal, the swell (tests/elite_swell.txt, AUTO: tests/elite_swell_auto.txt); layout C KNOBS + 16 PADS (tests/elite_layout_c.txt); layout D SOUND DESIGN (tests/elite_layout_d.txt); the zone's FILTER CC IN (tests/elite_ccin.txt); SUS IN, a sustain coming in holds the keys (tests/elite_susin.txt).

  tests/elite_test.py
"""
import os
import subprocess
import math
import struct
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(script):
    env = dict(os.environ, BOARD="smk37")
    out = subprocess.run([os.path.join(ROOT, "tests", "run_host.sh"), os.path.join(ROOT, "tests", script)],
                         capture_output=True, text=True, check=True, env=env).stdout
    return [ln for ln in out.splitlines() if ln.split("ms")[0].strip().isdigit()]


def pads(lines, t):
    for ln in lines:
        if int(ln.split("ms")[0]) == t and "PADS" in ln:
            return ln.split("PADS")[1].split()
    return None


def midi(lines, t0, t1, what):
    return any(t0 <= int(ln.split("ms")[0]) <= t1 and "MIDI out " + what in ln for ln in lines)


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        sys.exit(1)


def main():
    f = run("elite_pads.txt")
    p0, bt, after, sc, back = pads(f, 300), pads(f, 1100), pads(f, 1600), pads(f, 2100), pads(f, 2600)
    check(bt and len(set(bt[1:])) == 1 and bt[1].split(",")[0] != "0" and bt[1].split(",")[2] == "0"
          and len(set(bt[0].split(","))) == 1, "BT held: all 16 pads show the setups (red), setup 1 (the one in use) white")
    check(after and after != p0, "pad 1 pulled drawbar 1 to 0 (BT + pad stored the setup, not a drawbar)")
    check(sc and len(set(sc[:3] + sc[4:])) == 1 and sc[0] != bt[1], "SCALE held: all 16 pads show the setups (amber)")
    check(sc and len(set(sc[3].split(","))) == 1 and int(sc[3].split(",")[0]) > 150,
          "SCALE held: pad 4 white (setup 4, the one stored last)")
    check(midi(f, 2200, 2210, "90  60") and midi(f, 2300, 2310, "80  60"), "SCALE held: the keys still play")
    check(back == p0, "SCALE + pad 4: setup 4 recalled (the drawbars as stored)")
    clone = pads(f, 3300)
    check(clone and clone[2] != clone[1] and clone[1] == clone[3] and len(set(clone[0].split(","))) == 1 and clone[0] != clone[1],
          "CLONE: pad 3 lit, channel 3 cloned; pad 1 white (the zone's own channel)")
    check(midi(f, 3400, 3410, "90  60") and midi(f, 3400, 3410, "92  60"), "CLONE: a key plays channel 1 and 3")
    harm = pads(f, 4300)
    check(harm and harm[0] == harm[2] != harm[1] and harm[9] == "0,0,0", "HARMONIC: pads 1 and 3 off, 2 lit, pads 10..16 dark")
    check(midi(f, 4400, 4410, "90  60"), "HARMONIC: the keys still play")

    m = run("elite_midi.txt")
    check(midi(m, 1200, 1210, "91  60"), "MIDI ZONE: K2 = the out channel (a key plays channel 2)")
    z = pads(m, 1500)
    check(z and z[0] != z[3] and z[3] == z[1] and z[10] == z[1] and z[11] == z[0] and z[12] == z[1] and z[13] == "0,0,0",
          "ZONE: the switches on pads 1..13 (VEL on, INT switched off, FCCI off, MIDI OUT on, SUS IN off)")
    check(pads(m, 3200)[3] == z[0], "ZONE: pad 4 again: the internal sound on")
    c = pads(m, 2100)
    check(c and c[0] != "0,0,0" and c[2] == "0,0,0", "ARP from ZONE: around to CHORD (pad 1 capture lit)")
    check(all(midi(m, 2700, 2710, "90  %d" % n) for n in (60, 64, 67)), "CHORD: pad 1 captured C E G; C4 alone plays it")
    check(pads(m, 3400) == pads(m, 300), "GLOBE on a MIDI screen: back to the drawbars")
    c = run("elite_chord.txt")
    check(all(midi(c, 1400, 1410, "90  %d" % n) for n in (60, 64, 67)), "CHORD held, C E G played: C4 plays C E G")
    check(all(midi(c, 1600, 1610, "90  %d" % n) for n in (62, 66, 69)), "and D4 plays it transposed: D F# A")
    check(midi(c, 1900, 1910, "90  60") and not midi(c, 1900, 1910, "90  64"), "a tap on CHORD: off, C4 alone")
    check(midi(c, 2200, 2210, "90  67"), "a tap again: on, the chord kept")
    check(midi(c, 2800, 2810, "90  64") and not midi(c, 2800, 2810, "90  67"), "a new chord held (C E) replaces it")
    check(pads(c, 3500) != pads(c, 3300), "> alone: the next drawbar preset (on letting go)")
    check(pads(c, 3750) != pads(c, 3500), "< and > held: K1 the next drawbar preset")
    x = run("elite_mix.txt")
    check(midi(x, 1100, 1110, "B0   7 106"), "MIXER: K1 = channel 1's volume")
    check(midi(x, 1200, 1210, "B0   7   0") and not midi(x, 1300, 1310, "B0"), "top pad 1: channel 1 muted (volume 0), K1 kept")
    check(midi(x, 1400, 1410, "B0   7 108"), "top pad 1 again: back, with the volume set while muted")
    solo = [ln for ln in x if 1500 <= int(ln.split("ms")[0]) <= 1510 and "MIDI out" in ln]
    check(len(solo) == 2 and midi(x, 1500, 1510, "B0   7   0") and midi(x, 1500, 1510, "B2   7   0"),
          "bottom pad 2: channel 2 solo: the other channels the zones play silenced (only those)")
    check(pads(x, 1600)[9] != pads(x, 1600)[8] and pads(x, 1600)[0] == pads(x, 1600)[8], "solo pad lit (dark blue)")
    check(midi(x, 1700, 1710, "B2   7 100"), "solo off: back")
    check(midi(x, 1900, 1910, "B1  10  66") and midi(x, 2100, 2110, "B8  10  66"), "> = PAN; KNOB BANK: channels 9-16")
    fx = run("elite_fx.txt")
    lit, dim = "230,135,0", "7,7,7"
    check(pads(fx, 1500)[:4] == [dim, lit, dim, "230,198,0"], "FX: pad 2 = the lower manual (lit), pad 4 its Leslie on")
    check(pads(fx, 1700)[:4] == [lit, dim, dim, dim], "PATCH: LESLIE, the pads the cabinets (122 chosen)")
    check(pads(fx, 2300)[:4] == [dim, dim, lit, dim], "pad 3: USER 1")
    check(pads(fx, 2700)[:4] == [dim, dim, dim, lit], "BT + pad 4: the cabinet copied to USER 2, chosen")
    check(pads(fx, 2900) == pads(fx, 1500), "ARP: back to the FX page")
    run("elite_delay.txt")                                  # (the wav: 0.2 s late, as the screens)
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))[0::2]
    pk = lambda t0, t1: max(abs(v) for v in a[int(t0 * 44100):int(t1 * 44100)])
    check(pk(2.2, 2.25) < 50 and pk(2.3, 2.45) > 2000 and pk(2.95, 3.1) > 200,
          "DELAY: a note shorter than the delay time still echoes, and again (it runs on after the note)")
    ch = run("elite_chorus.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))
    def ms(t0, t1):                                         # mid and side, RMS
        L, R = a[int(t0 * 44100) * 2:int(t1 * 44100) * 2:2], a[int(t0 * 44100) * 2 + 1:int(t1 * 44100) * 2:2]
        rms = lambda x: (sum(v * v for v in x) / len(x)) ** 0.5
        return rms([(l + r) / 2 for l, r in zip(L, R)]), rms([(l - r) / 2 for l, r in zip(L, R)])
    (m0, s0), (m1, s1) = ms(1.4, 1.7), ms(2.9, 3.2)
    check(s0 == 0 and s1 > m1 / 5, "CHORUS: the dry note is mono, with the chorus it has a stereo side")
    check(0.8 < m1 / m0 < 1.2, "CHORUS: the level stays (within 20 %)")
    check(pads(ch, 2600)[0] == lit and pads(ch, 3700)[0] == dim and pads(ch, 2600)[7] == "0,0,0",
          "EFFECT: pad 1 CHORUS lit, again: off; pad 8 dark")
    wh = run("elite_wah.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))[0::2]
    def lb(t0):                                             # level (RMS) and brightness (of the slope)
        x = a[int(t0 * 44100):int((t0 + 0.3) * 44100)]
        rms = lambda v: (sum(u * u for u in v) / len(v)) ** 0.5
        return rms(x), rms([x[i + 1] - x[i] for i in range(len(x) - 1)]) / rms(x)
    (ld, bd), (lh, bh), (lt, bt) = lb(2.0), lb(3.1), lb(3.8)
    check(pads(wh, 2900)[1] == lit, "EFFECT: pad 2 = WAH")
    check(bd < bh < bt, "WAH: the mod wheel is the pedal: darker at the heel, brighter at the toe")
    check(lh > ld * 0.5 and lt > ld * 0.15, "WAH: its level (the heel near the dry chord)")
    aw = run("elite_autowah.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))[0::2]
    (_, b60), (_, b0) = lb(2.0), lb(3.0)
    rms = lambda t: (sum(u * u for u in a[int(t * 44100):int((t + 0.05) * 44100)]) / 2205) ** 0.5
    lfo = [rms(4.1 + k * 0.05) for k in range(18)]
    check(pads(aw, 1500)[2] == lit, "EFFECT: pad 3 = AUTO-WAH")
    check(b60 > b0 * 1.5, "AUTO-WAH: the chord opens it (sensitivity 60 %); at 0 it stays dark")
    check(max(lfo) > 3 * min(lfo), "AUTO-WAH: with a rate its LFO sweeps it")
    fl = run("elite_flanger.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))
    def sweep(t0):                                          # mid, side, the brightness's spread over 18 x 0.1 s
        mids, sides, brs = [], [], []
        for k in range(18):
            i0, i1 = int((t0 + k * 0.1) * 44100), int((t0 + k * 0.1 + 0.1) * 44100)
            L, R = a[i0 * 2:i1 * 2:2], a[i0 * 2 + 1:i1 * 2:2]
            m = [(l + r) / 2 for l, r in zip(L, R)]
            rms = lambda v: (sum(u * u for u in v) / len(v)) ** 0.5
            mids.append(rms(m)); sides.append(rms([(l - r) / 2 for l, r in zip(L, R)]))
            brs.append(rms([m[i + 1] - m[i] for i in range(len(m) - 1)]) / rms(m))
        return sum(mids) / 18, sum(sides) / 18, max(brs) - min(brs)
    (md, sd, bd), (mf, sf, bf) = sweep(1.6), sweep(4.1)
    check(pads(fl, 3900)[3] == lit, "EFFECT: pad 4 = FLANGER")
    check(sd == 0 and sf > mf / 5 and bf > 5 * bd, "FLANGER: its comb sweeps the colour, in stereo")
    check(0.7 < mf / md < 1.3, "FLANGER: the level stays (within 30 %)")
    ph = run("elite_phaser.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))
    (md, sd, bd), (mp, sp, bp) = sweep(1.6), sweep(4.1)
    check(pads(ph, 3900)[4] == lit, "EFFECT: pad 5 = PHASER")
    check(sd == 0 and sp > mp / 5 and bp > 5 * bd, "PHASER: its notches sweep the colour, in stereo")
    check(0.7 < mp / md < 1.3, "PHASER: the level stays (within 30 %)")
    rg = run("elite_ring.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))
    def tone(t0, f):                                        # the level at f (Goertzel over 8192 frames, left)
        k, s1, s2 = 2 * math.cos(2 * math.pi * f / 44100), 0.0, 0.0
        for v in a[int(t0 * 44100) * 2:(int(t0 * 44100) + 8192) * 2:2]:
            s1, s2 = v + k * s1 - s2, s1
        return math.sqrt(s1 * s1 + s2 * s2 - k * s1 * s2) / 8192
    (_, sd, _), (_, sr, _) = sweep(1.6), sweep(4.1)
    check(pads(rg, 3900)[5] == lit, "EFFECT: pad 6 = RING MOD")
    check(tone(4.5, 702) > 10 * tone(2.0, 702) and tone(4.5, 262) < tone(2.0, 262),
          "RING MOD: C4 times 440 Hz: the sum tone 702 Hz appears, C4 itself goes down")
    check(sd == 0 and sr > 0, "RING MOD: in stereo")
    am = run("elite_amp.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))[0::2]
    def amp(t0):                                            # level (RMS), crest (peak / RMS), brightness
        x = a[int(t0 * 44100):int(t0 * 44100) + 8192]
        rms = lambda v: (sum(u * u for u in v) / len(v)) ** 0.5
        return rms(x), max(abs(u) for u in x) / rms(x), rms([x[i + 1] - x[i] for i in range(len(x) - 1)]) / rms(x)
    d, c40, c100, fz = amp(2.0), amp(4.5), amp(6.6), amp(8.1)
    check(pads(am, 3900)[6] == lit, "EFFECT: pad 7 = AMP")
    check(c100[1] < 0.8 * d[1] and c100[2] > 1.4 * d[2] and fz[2] > c100[2],
          "AMP: full gain squares the waves off (crest down, brighter); FUZZ brighter still")
    check(all(0.6 < v[0] / d[0] < 1.6 for v in (c40, c100, fz)), "AMP: the level about the dry one at any gain")
    mx2 = run("elite_master.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))
    lvl = lambda t0: (sum(u * u for u in a[int(t0 * 44100) * 2:(int(t0 * 44100) + 8192) * 2:2]) / 8192) ** 0.5
    check(pads(mx2, 4200)[1] == lit and pads(mx2, 4200)[0] == dim, "MASTER: pad 1 EQ off again, pad 2 COMP on (lit)")
    check(tone(2.8, 130.8) > 2 * tone(1.8, 130.8), "MASTER EQ: low +12 dB lifts the 16' C3 (more than 6 dB)")
    check(lvl(3.8) < lvl(1.8) / 4, "MASTER COMP: threshold -40 dB, 20:1: the chord more than 12 dB down")
    check(3 < lvl(4.8) / lvl(3.8) < 5, "MASTER COMP: make-up +12 dB: four times as loud")
    yb = run("elite_ysb.txt")
    check(pads(yb, 3900)[15] != pads(yb, 3700)[15], "YST: pad 16 opens the sound browser (lit)")
    check(midi(yb, 4150, 4160, "B0   0  63") and midi(yb, 4150, 4160, "B0  32   7") and midi(yb, 4150, 4160, "C0  54   0")
          and not midi(yb, 4000, 4140, "C0"), "SOUNDS: K1 = the next category (Bass -> Synth Lead): its first, 951, once K1 rests")
    check(midi(yb, 4550, 4560, "C0  57   0"), "SOUNDS: K2 +3 = three sounds on in it (954)")
    check(pads(yb, 4900)[15] == pads(yb, 3700)[15], "GLOBE: the browser closed (back to the YST screen)")
    yk = run("elite_ysb_kit.txt")
    check(pads(yk, 3900)[15] != pads(yk, 3450)[15] and not any(midi(yk, 3500, 4900, w) for w in ("C0", "B0   0", "B0  32")),
          "a DRUM KIT track: the browser opens (the kit screen), but K2, > and < + > K5 load nothing")
    yq = run("elite_ysb_quick.txt")
    check(midi(yq, 4150, 4150, "C0  54   0"), "< + > held, K4 +1: the next category's first (951), once K4 rests")
    check(midi(yq, 4300, 4300, "C0  55   0") and not midi(yq, 4310, 4590, "C0"), "K5 +1 and let go at once: 952 loads then")
    check(midi(yq, 4800, 4800, "C0  53   0") and not midi(yq, 4600, 4790, "C0"),
          "K5 -2: over the category's start into Bass (950), loaded on letting go")
    yp = run("elite_yst_pads.txt")
    ramp = [int(ln.split()[-1]) for ln in yp if 3500 <= int(ln.split("ms")[0]) <= 3600 and "MIDI out B0 102" in ln]
    check(ramp and ramp[-1] == 127 and len(ramp) > 3 and midi(yp, 3800, 3800, "B0 102   0"),
          "YST pad 12 struck hard: CC 102 rises to 127 within 70 ms, let go: 0")
    check(not midi(yp, 3900, 4199, "B0 102  20") and midi(yp, 4200, 4200, "B0 102   0"), "pad 12 struck softly: slow")
    check(midi(yp, 4300, 4300, "FA") and midi(yp, 4400, 4400, "FC") and midi(yp, 4700, 4700, "FB") and midi(yp, 4800, 4800, "FC"),
          "pad 14: PLAY (FA), PAUSE (FC), CONTINUE (FB); pad 13: STOP (FC)")
    a, b, c, d, e = (pads(yp, t) for t in (3700, 4350, 4450, 4850, 4950))
    check(a[11] == "255,40,160" and b[13] == "36,207,72" and pads(yp, 4600)[13] != c[13] and d[12] == "204,204,204"
          and d[13] == "5,28,10" and e[13] == "36,207,72", "pads: 12 pink (held: bright), 14 green (blinks paused), 13 white stopped, FA in: playing")
    check(midi(yp, 5000, 5000, "B0 102  60") and midi(yp, 5100, 5100, "B0 106  90"), "YST faders 1, 3: FX LEVEL CC 102, REPEATER CC 106")
    check(pads(yp, 5700)[0] == "0,0,0" and pads(yp, 5900)[0] == "255,255,255",
          "FADER BANK on the MIDI screens: PERFORMANCE; GLOBE: YST again")
    check(not any("SYSEX out F043107F1C0C000001" in ln for ln in yp) and not midi(yp, 6100, 6100, "B0"),
          "PERFORMANCE: fader 2 the reverb send (no CC); no SEQTRAK volume SysEx any more")
    bk = run("elite_banks.txt")
    a0, b1, a1, lr, hm, d7, d7b, e8, e8b = (pads(bk, t) for t in (500, 800, 1400, 2000, 2400, 3300, 3600, 3900, 4200))
    bri = lambda c: sum(int(v) for v in c.split(","))
    check(a0[0] == "0,0,0" and a0[8] == "255,0,0" and b1[0] == "47,160,76" and a1 == a0,
          "FADER BANK: A -> B1 (the live pads), then B2 C D and round to A")
    check(lr[0] == "47,160,76" and bri(lr[1]) < 100 and lr[8] == "0,0,0" and lr[10] == "255,0,0",
          "< + > held, K2 +2, K3 +1: model 3 (no PERC) and layout B1 at once; the pads show it (lower manual), not the cloner")
    check(hm == a0 and pads(bk, 2900) == a0,
          "KNOB BANK + FADER BANK: HOME: model 1, the upper manual, P1 888000000, layout A; no single bank step after")
    check(bri(d7[6]) > 300 and bri(d7b[6]) < 100, "pad 7 held + K7 / K8: the delay on (no toggle on letting go); tapped: off")
    g6, g6b = pads(bk, 3640), pads(bk, 3670)
    check(bri(g6[5]) > 200 and bri(g6b[5]) < 100 and bri(e8[5]) > 200,
          "pad 6 held + K6: the drive (no toggle on letting go); tapped: off; again: on")
    check(bri(e8[7]) < 100 and bri(e8b[7]) > 300, "pad 8 held + K8 (no effect on): nothing toggles; tapped: the effect on")
    check(not midi(bk, 4300, 4499, "B0   7") and midi(bk, 4500, 4540, "B0   7  31") and midi(bk, 4500, 4540, "B2   7  31"),
          "fader 3 the MIDI out's sum: picks up at 127 (nothing at 60), at 40: CC 7 = 100 x 40 / 127 on the zones' channels")
    pn = run("elite_panel.txt")
    check(midi(pn, 650, 700, "B0 123") and midi(pn, 650, 700, "BF 123"),
          "SCALE + PATCH on the drawbar screen: panic (all notes off on the 16 channels)")
    check(pads(pn, 1250) == pads(pn, 1150), "LIGHTS: K4 does nothing")
    p13, p14 = pads(pn, 1350), pads(pn, 1450)
    check(p13 != pads(pn, 1250), "LIGHTS: K1 = the pads' colours")
    check(sum(int(x) for c in p14 for x in c.split(",")) < sum(int(x) for c in p13 for x in c.split(",")),
          "LIGHTS: K3 = the pads' brightness (down: darker)")
    ly = run("elite_layout.txt")
    b0, off, on2, mid, fx, la, rec = (pads(ly, t) for t in (500, 700, 1000, 1600, 1900, 2500, 3000))
    bri = lambda c: sum(int(v) for v in c.split(","))
    check(b0 and b0[0] == "47,160,76" and b0[1] == "193,50,38" and bri(b0[4]) < 100 and b0[8] == "255,0,0" and b0[11] == "0,0,0",
          "B: VIB green, PERC red (on: bright), Leslie stop dim; bottom pads the drawbars (1 out, 4 in)")
    check(off and bri(off[1]) < 100 and off[1] != "0,0,0" and off[0] == b0[0], "B pad 2: PERC off (dim, still red)")
    check(on2 and on2[1] == b0[1], "B pads 3, 2: the next PERC chosen while off, then on")
    check(mid and mid[8] == "0,0,0" and mid[11] == "255,0,0" and mid[0] == b0[0],
          "B: bottom pads: drawbar 1 in, 4 out; pad 1 held + K1: vibrato stays on")
    check(fx and bri(fx[0]) < 100 and bri(fx[7]) > 300 and bri(fx[5]) < 100,
          "B pad 1 tapped: vibrato off; pad 8: the effect on; pad 6: drive off")
    check(la and la[0] == "255,0,0" and la[8] == "0,0,0" and la[3] == "0,0,0" and la[11] == "255,0,0",
          "SCALE + KNOB BANK: back to A, RED-GLOW: drawbar 1 in (top red), 4 out (bottom red)")
    check(rec and rec[:3] + rec[4:] == fx[:3] + fx[4:], "SCALE + pad 2: setup 2 brings B back (pad 4: the pulse aside)")
    su = run("elite_sustain.txt")
    check(midi(su, 2100, 2100, "B0  64 127") and not midi(su, 2200, 2400, "80  60"),
          "sustain down: CC 64 127; a key let go holds (no note off)")
    check(midi(su, 2500, 2500, "80  60") and midi(su, 2500, 2500, "80  64") and midi(su, 2500, 2500, "B0  64   0"),
          "sustain up: the held notes off, CC 64 0")
    check(midi(su, 2800, 2800, "80  60"), "without the pedal a key goes off at once")
    sw = run("elite_swell.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))[0::2]
    rms = lambda t0, t1: (sum(v * v for v in a[int(t0 * 44100):int(t1 * 44100)]) / int((t1 - t0) * 44100)) ** 0.5
    toe, heel, half = rms(2.45, 2.55), rms(2.85, 2.95), rms(3.25, 3.35)
    check(midi(sw, 2200, 2220, "B0  11 127") and midi(sw, 2600, 2650, "B0  11  40") and midi(sw, 3000, 3050, "B0  11  83")
          and not any("B0  64" in ln for ln in sw), "expression pedal (rests at 102, AUTO): the swell, CC 11 127 / 40 / 83, no sustain")
    check(0.07 < heel / toe < 0.13 and heel < half < toe, "the swell closed: soft but there (10 % of the level), half way between")
    sa = run("elite_swell_auto.txt")
    check(midi(sa, 2290, 2310, "B0  64   0") and midi(sa, 2290, 2320, "B0  11  40") and midi(sa, 2600, 2650, "B0  11 127")
          and not midi(sa, 2000, 2280, "B0  11"), "AUTO: resting at an end, then 0.3 s in the middle: the sustain let go, the swell")
    lc = run("elite_layout_c.txt")
    c0, mo, low, zs = (pads(lc, t) for t in (500, 900, 1800, 2300))
    bri = lambda c: sum(int(v) for v in c.split(","))
    check(c0 and bri(c0[8]) > 300 and bri(c0[9]) < 100 and c0[11] != "0,0,0" and bri(c0[12]) > 300
          and bri(c0[13]) > 300 and bri(c0[14]) > 300 and bri(c0[15]) > 300 and c0[12] == c0[14] and c0[13] == c0[15]
          and c0[12] != c0[13], "C: pad 9 (upper) bright, 10 dim, the coupler dim; FCCI, FCCO off, MIDI OUT, INT on: lit (grey, orange)")
    check(midi(lc, 600, 600, "90  60") and mo and bri(mo[14]) < 100, "C: a key goes out; pad 15: MIDI OUT off (dim)")
    check(not midi(lc, 1000, 1100, "") and midi(lc, 1300, 1300, "90  60"),
          "C: MIDI OUT off: the key does not go out; pad 15 again: it does")
    check(low and bri(low[9]) > 300 and bri(low[11]) > 200, "C: pad 10: the lower manual; pad 12: the coupler on")
    check(zs and bri(zs[12]) < 100 and bri(zs[13]) < 100 and bri(zs[14]) > 300 and bri(zs[15]) < 100,
          "C: pads 13 FCCI, 14 FCCO on (dim), 15 MIDI OUT still on (lit), 16 INTERNAL SOUND off (dim)")
    ld = run("elite_layout_d.txt")
    d0, dfx, dles, dmix, ddb = (pads(ld, t) for t in (500, 1000, 1500, 1800, 2100))
    lit = lambda row: [i for i in range(8, 13) if bri(row[i]) > 300]
    check(d0 and lit(d0) == [] and bri(d0[13]) > 300 and d0[0] == b0[0], "D: the pages dim, pad 14 (upper) bright, top pads B's")
    check(dfx and lit(dfx) == [8] and bri(dfx[1]) < 100, "D pad 9: the FX page; pad 2 still the PERC (off)")
    check(dles and lit(dles) == [9] and bri(dles[14]) > 300, "D pad 10: LESLIE; pad 15: the lower manual")
    check(dmix and lit(dmix) == [12], "D pad 13: the mixer")
    check(ddb and lit(ddb) == [], "D pad 13 again: back to the drawbars")
    lk = run("elite_layout_k3.txt")
    k3a, k3h, k3c, k3b, k3d, k3n = (pads(lk, t) for t in (80, 400, 700, 1200, 1700, 440))
    nolr = lambda row: row[:3] + row[4:]                    # (pad 4: the Leslie's pulse)
    check(k3h and len(set(k3h[0].split(","))) == 1 and k3h[4] != k3h[1] and k3h[1] == k3h[2],
          "< + > held: the pads show the cloner (pad 1 white, pad 5 cloned, the rest grey)")
    check(k3c and nolr(k3c)[:2] == nolr(c0)[:2] and k3c[8:] == c0[8:], "< + > held, K3 +3: layout C")
    check(k3n and nolr(k3n)[:2] == nolr(c0)[:2] and k3n[8:] == c0[8:], "... and while still held: the pads show C at once")
    check(k3a and k3b and nolr(k3b) == nolr(k3a), "K3 -4: back to A, no further round; let go: no step")
    check(k3d and nolr(k3d) == nolr(d0), "K3 +9: D, no further round")
    cp = run("elite_coupler.txt")
    check(midi(cp, 600, 600, "90  60") and midi(cp, 600, 600, "91  60"), "COUPLER UM+LM: a key plays the upper and the lower")
    check(midi(cp, 900, 900, "90  60") and midi(cp, 900, 900, "90  64") and midi(cp, 900, 900, "92  60")
          and not midi(cp, 900, 900, "92  64") and not midi(cp, 900, 900, "91"), "COUPLER UM+PM: the pedal takes the lowest note only")
    check(midi(cp, 1100, 1100, "90  60") and not midi(cp, 1100, 1100, "92") and not midi(cp, 1100, 1100, "91"),
          "COUPLER: a note coming in is not coupled")
    q, qh, q2 = pads(cp, 1300), pads(cp, 1800), pads(cp, 1900)
    purple = lambda c: int(c.split(",")[0]) > int(c.split(",")[1]) * 2 and int(c.split(",")[2]) > int(c.split(",")[0])
    check(q and purple(q[8]) and bri(q[8]) > 200 and not purple(q[9]) and bri(q[9]) < 100 and purple(q[10])
          and bri(q[10]) < 150 and bri(q[11]) > 200, "C UM+PM: pads 9 (chosen) bright purple, 10 dim blue, 11 dim purple; 12 bright")
    check(midi(cp, 1500, 1500, "90  60") and not midi(cp, 1500, 1500, "91") and not midi(cp, 1500, 1500, "92"),
          "COUPLER pad 12 again: off, the upper alone")
    check(qh and purple(qh[8]) and bri(qh[8]) > 200 and bri(qh[9]) < 100 and purple(qh[10]) and bri(qh[10]) > 200
          and bri(qh[11]) < 150, "pad 12 held: UM, PM marked purple (remembered), LM dim; 12 dim (off)")
    check(q2 and bri(q2[11]) < 150 and not purple(q2[8]), "pad 12 + pad 10 let go: still off, pads 9-11 blue again")
    check(midi(cp, 2000, 2000, "90  60") and midi(cp, 2000, 2000, "91  60") and midi(cp, 2000, 2000, "92  60"),
          "COUPLER pad 12: on with UM+LM+PM")
    b2 = run("elite_layout_b2.txt")
    v0, v1, v2 = (pads(b2, t) for t in (500, 1000, 1300))
    check(v0 and v0[:8] == c0[8:] and v0[8:11] == ["255,0,0"] * 3 and v0[11] == "0,0,0",
          "B2: the top pads as C's bottom row; the bottom pads the drawbars (RED-GLOW: 888 out)")
    check(v1 and bri(v1[1]) > 200 and bri(v1[0]) < 100 and bri(v1[3]) > 200 and midi(b2, 800, 800, "90  60")
          and midi(b2, 800, 800, "91  60"), "B2 pad 2: the lower manual; pad 4: the coupler on, a key plays both")
    check(v2 and v2[8] != v1[8] and v2[9:] == v1[9:], "B2 pad 9: the lower's drawbar 1 flipped")
    wl = run("elite_wheels.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    aw = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))[0::2]
    def gz(t0, f):                                          # the level at f (Goertzel, 8192 frames)
        k, s1, s2 = 2 * math.cos(2 * math.pi * f / 44100), 0.0, 0.0
        for v in aw[int(t0 * 44100):int(t0 * 44100) + 8192]:
            s1, s2 = v + k * s1 - s2, s1
        return math.sqrt(max(0.0, s1 * s1 + s2 * s2 - k * s1 * s2)) / 8192
    rpn = lambda t, r: all(midi(wl, t, t, x) for x in ("B0 101   0", "B0 100   0", "B0   6 %3d" % r, "B0 101 127"))
    check(rpn(1250, 2) and rpn(1300, 12), "WHEELS K5: PITCH, SHIFT 1: the bend range out (RPN 0: 2, 12)")
    check(gz(2.0, 261.6) > 200 and gz(2.0, 523.3) < 50, "WHEELS: C4 plain (8' 261.6 Hz)")
    check(gz(3.0, 523.3) > 200 and gz(3.0, 130.8) < 60 and midi(wl, 2600, 2600, "E0 127 127"),
          "SHIFT 1, the bend wheel up: an octave higher (523 Hz), the bend sent out")
    check(rpn(3750, 36) and gz(5.0, 2093) > 30 and gz(5.0, 261.6) < 60, "SHIFT 3 up: three octaves higher (2093 Hz)")
    check(gz(6.0, 32.7) > 200 and gz(6.0, 261.6) < 30, "SHIFT 3 down: three octaves lower (32.7 Hz)")
    check(rpn(8300, 24) and rpn(8400, 2) and not midi(wl, 7700, 8200, "B0   1"),
          "K5 back down to PITCH: the range 2 again; the mod wheel on DRIVE sends no CC 1")
    pd = run("elite_perc_decay.txt")
    w = wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav"))
    a = struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes()))[0::2]
    pk = lambda t0, t1: max(abs(v) for v in a[int(t0 * 44100):int(t1 * 44100)])
    check(pk(1.0, 1.1) > 1000 and pk(2.4, 2.9) < pk(1.0, 1.1) / 50, "PERC: the classic fast decay (gone after 1 s)")
    check(pk(4.0, 4.1) > 1000 and pk(5.5, 6.0) > pk(4.0, 4.1) / 2, "PERC: pad 3 + K3 up to HOLD: it sounds on")
    check(pads(pd, 3700)[1] == b0[1], "PERC: still the same percussion after pad 3 + K3 (no step on letting go)")
    check(pk(6.5, 6.6) > 1000 and pk(8.0, 8.4) < pk(6.5, 6.6) / 30, "PERC: 3RD SLOW keeps its own (classic) decay")
    check(pk(9.5, 9.6) > 1000 and pk(11.0, 11.4) > pk(9.5, 9.6) / 2 and pads(pd, 9100)[1] == b0[1],
          "PERC: pad 3 round 4F 4S 2F 2S back to 3RD FAST: it still holds")
    ci = run("elite_ccin.txt")
    check(not midi(ci, 1200, 1200, "80  60") and midi(ci, 1300, 1300, "80  60"), "CC IN off: sustain on channel 1 holds the note")
    check(midi(ci, 2200, 2200, "80  60") and midi(ci, 2100, 2100, "B0  64 127"),
          "FILTER CC IN (ZONE pad 11): the sustain holds nothing inside, it still goes out")
    si = run("elite_susin.txt")
    check(midi(si, 1200, 1200, "80  60"), "SUS IN off: a sustain coming in does not hold the Elite's keys")
    check(not midi(si, 2200, 2299, "80  60") and midi(si, 2300, 2300, "80  60"),
          "SUS IN (ZONE pad 13): CC 64 on the zone's channel holds the Elite's key until it lifts")
    check(not midi(si, 2800, 2999, "80  60") and midi(si, 3000, 3000, "80  60"),
          "SUS IN and the Elite's own pedal: the key ends when both are up")
    print("elite tests passed")


if __name__ == "__main__":
    main()
