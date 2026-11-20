#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1's INSERT and MASTER pages on the host build (core/orgigs_params.c, board/fm1/orgigs_ui.c): an effect on
the upper manual changes its sound, one on the lower leaves the upper as it was; the WAH's pedal from CC 1; the
compressor takes the level down; both are saved (flash blobs 2 and 3) once nothing sounds.

  tests/fm1_fx_test.py
"""
import math
import os
import struct
import subprocess
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FX, SEL, ENV, LFO, EDIT = 2, 3, 4, 5, 6
CHORD = ["1500 keys 0x7FFFF", "3500 keys 0", "6500 end"]


def run(lines):
    path = os.path.join(ROOT, "build", "host", "fm1_fx.txt")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    out = subprocess.run([os.path.join(ROOT, "tests", "run_host.sh"), path], capture_output=True, text=True,
                         check=True, env=dict(os.environ, ORGIGS_BLOB_LOG="1")).stdout
    with wave.open(os.path.join(ROOT, "build", "host", "orgigs.wav")) as w:
        raw = w.readframes(w.getnframes())
    s = struct.unpack("<%dh" % (len(raw) // 2), raw)
    return out, s[0::2], s[1::2]


def win(x, t0, t1):
    return x[int(t0 * 44.1):int(t1 * 44.1)]


def rms(x):
    return math.sqrt(sum(v * v for v in x) / max(1, len(x)))


def brightness(x):                      # the first difference against the signal: more highs, more of it
    return rms([b - a for a, b in zip(x, x[1:])]) / max(1.0, rms(x))


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        sys.exit(1)


def main():
    _, l0, r0 = run(CHORD)
    dry = win(l0, 2000, 3400)
    # SEL + FX: INSERT; EDIT: CHORUS on (the upper); EDIT x 6: AMP
    ins = [f"300 down {SEL}", f"350 btn {FX}", f"450 up {SEL}"]
    _, l1, _ = run(ins + [f"500 btn {EDIT}"] + [f"{600 + 50 * i} btn {EDIT}" for i in range(6)] + CHORD)
    amp = win(l1, 2000, 3400)
    check(sum(abs(a - b) for a, b in zip(dry, amp)) > 0.2 * sum(abs(a) for a in dry),
          "INSERT: AMP on the upper changes the chord (EDIT from off: CHORUS, then on to AMP)")
    _, l2, r2 = run(ins + [f"500 btn {EDIT}"] + [f"{600 + 50 * i} btn {EDIT}" for i in range(6)]
                    + [f"1000 btn {LFO}"] + CHORD)
    check(l2 == l0 and r2 == r0, "INSERT: the same on the lower manual (LFO) leaves the upper's chord as it was")
    # WAH (EDIT x 2 from CHORUS): CC 1 the pedal, heel then toe
    wah = ins + [f"500 btn {EDIT}", f"600 btn {EDIT}"]
    _, lh, _ = run(wah + ["1000 midi 0xB0 1 0"] + CHORD)
    _, lt, _ = run(wah + ["1000 midi 0xB0 1 127"] + CHORD)
    bh, bt = brightness(win(lh, 2000, 3400)), brightness(win(lt, 2000, 3400))
    check(bt > 1.5 * bh, f"WAH: CC 1 is its pedal, toe brighter than heel ({bt:.3f} vs {bh:.3f})")
    # SEL + FX twice: MASTER; LFO: the compressor; EDIT: the knobs hold it; KNOB1 -22: threshold -40 dB
    out, lc, _ = run([f"300 down {SEL}", f"350 btn {FX}", f"450 btn {FX}", f"550 up {SEL}", f"600 btn {LFO}",
                      f"700 btn {EDIT}", "800 enc 2 -22"] + CHORD)
    db = 20 * math.log10(rms(win(lc, 2500, 3400)) / rms(win(l0, 2500, 3400)))
    check(db < -6, f"MASTER (SEL + FX twice): the compressor at -40 dB takes the chord down ({db:.1f} dB)")
    saves = [int(ln.split("ms")[0]) for ln in out.splitlines() if "blob 3 saved" in ln]
    check(len(saves) == 1 and saves[0] > 3500, f"MASTER: saved (blob 3) once the chord has ended, not while it sounds {saves}")
    print("FM-1 effect tests passed")


if __name__ == "__main__":
    main()
