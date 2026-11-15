#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""YST (core/orgigs_yst.c) on the host builds, with the SEQTRAK's replies typed into the scripts:
tests/yst.txt (FM-1) and tests/yst_elite.txt (SMK-37 Elite, bank YST). Checks what OrGigs sends.

  tests/yst_test.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(board, script):
    env = dict(os.environ, BOARD=board)
    out = subprocess.run([os.path.join(ROOT, "tests", "run_host.sh"), os.path.join(ROOT, "tests", script)],
                         capture_output=True, text=True, check=True, env=env).stdout
    return [ln for ln in out.splitlines() if ln.split("ms")[0].strip().isdigit()]   # the timed lines


def has(lines, t, what):
    return any(ln.split("ms")[0].strip() == str(t) and what in ln for ln in lines)


def near(lines, t, what, d=5):
    return any(t <= int(ln.split("ms")[0]) <= t + d and what in ln for ln in lines)


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        sys.exit(1)


def midi(lines, t, st, d1, d2):
    return has(lines, t, f"MIDI out {st} {d1:3d} {d2:3d}")


def main():
    f = run("fm1", "yst.txt")
    check(not any("SYSEX out" in ln or "MIDI out" in ln for ln in f if int(ln.split("ms")[0]) < 3000),
          "FM-1: YST off (the factory setting): nothing goes to the USB")
    check(near(f, 3000, "SYSEX out F043307F1C0C01103CF7"), "FM-1: ENV switches it on: it asks the type of track 1")
    asks = [ln.split("SYSEX out ")[1] for ln in f if "SYSEX out F043307F1C0C01" in ln and 3000 <= int(ln.split("ms")[0]) < 3400]
    want = ["F043307F1C0C01%02X%sF7" % (0x10 | k >> 1, "33" if k & 1 else "3C") for k in range(21)]
    check(asks == want, "FM-1: then asks for the type and sound of all 11 tracks, one at a time, each once")
    check(not any("SYSEX out F04330" in ln for ln in f if 3220 <= int(ln.split("ms")[0]) < 5450),
          "FM-1: and then waits (no sampler sound request, nothing repeated)")
    check(midi(f, 4300, "97", 60, 100), "FM-1: track 8 selected: the upper zone plays channel 8")
    check(near(f, 4200, "SYSEX out F043107F1C0C01102707F7"), "FM-1: and the SEQTRAK is told (CurrentPart 01 10 27 = 7)")
    check(midi(f, 5320, "99", 60, 100) and not any("SYSEX out F043107F1C0C011027" in ln for ln in f
          if 5310 <= int(ln.split("ms")[0]) < 5400), "FM-1: a track chosen on the SEQTRAK: the keys follow, nothing sent back")
    check(midi(f, 4600, "B7", 74, 70), "FM-1: KNOB1 = cutoff (CC 74) of track 8")
    check(midi(f, 4750, "B7", 7, 60), "FM-1: SEL + KNOB1 = volume (CC 7)")
    check(midi(f, 4900, "B7", 0, 63) and midi(f, 4900, "B7", 32, 6) and midi(f, 4900, "C7", 90, 0),
          "FM-1: PRESETS = the next sound (858 -> 859: bank 63 / 6, program 90)")
    check(midi(f, 5000, "B7", 23, 127) and midi(f, 5100, "B7", 26, 1), "FM-1: ARP mute, SEQ mono / poly / chord")
    check(not has(f, 5300, "MIDI out"), "FM-1: the SEQTRAK's own cutoff goes nowhere else")
    check(not any("MIDI out" in ln for ln in f if 5420 <= int(ln.split("ms")[0]) <= 5440),
          "FM-1: its notes and clock do not go back to it (no loop through the zones)")
    check(near(f, 5450, "SYSEX out F043307F1C0C01103CF7") and near(f, 5460, "SYSEX out F043307F1C0C011033F7"),
          "FM-1: LFO = REFRESH: it reads from the start, the next question once answered")
    check(not any("SYSEX out" in ln for ln in f if 5500 <= int(ln.split("ms")[0]) < 5550)
          and midi(f, 5520, "97", 60, 100) and has(f, 5525, "MIDI out 87"),
          "FM-1: ENV off: no questions, the SEQTRAK's notes play the zones again")
    check(near(f, 5550, "SYSEX out F043307F1C0C01103CF7"), "FM-1: ENV on again: it reads again")

    e = run("smk37", "yst_elite.txt")
    pads = [ln for ln in e if "PADS" in ln]
    check(pads and pads[0].split()[3] == "255,255,255" and pads[0].split()[12] == "133,42,179",
          "Elite: bank YST: the pads show the tracks (selected white, DX violet)")
    check(pads[0].split()[4:6] == ["0,140,179"] * 2 and pads[0].split()[6] == "179,84,0",
          "Elite: the types read from the SEQTRAK: tracks 2, 3 synth (blue), track 4 drum (orange)")
    check(near(e, 3800, "SYSEX out F043107F1C0C01102709F7"), "Elite: pad 10 selects the DX track on the SEQTRAK too")
    check(midi(e, 3900, "B9", 116, 8) and midi(e, 4000, "B9", 7, 58), "Elite: pad 10 = DX; SCALE + K1 its FM algorithm, K5 volume")
    check(near(e, 4100, "MIDI out B9   0  63", 60) and near(e, 4100, "MIDI out B9  32  15", 60)
          and near(e, 4100, "MIDI out C9  13   0", 60), "Elite: > (on letting go) = the next DX sound (1933 -> 1934)")
    check(midi(e, 4400, "B0", 102, 30) and midi(e, 4600, "B0", 102, 90) and not any("MIDI out B9  74" in ln for ln in e),
          "Elite: fader 1 = the SEQTRAK's FX LEVEL (CC 102, channel 1), no longer the cutoff")
    check(not any("MIDI out B9  75" in ln for ln in e), "Elite: fader 4 = the volume in bank YST too (no decay)")
    check(midi(e, 4950, "B1", 23, 127), "Elite: SCALE + pad 2 = mute track 2")
    check(pads[-1].split()[3] == "0,0,0" and pads[-1].split()[6] == "255,0,0", "Elite: FADER BANK: back to the drawbars")
    print("yst tests passed")


if __name__ == "__main__":
    main()
