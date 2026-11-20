#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""docs/PARAMETERS.md from the parameter table (core/orgigs_params.c): builds the host app for each board, prints the
table (tests/host.c --params) and writes one Markdown table per group, with where each value sits on the FM-1 and on
the Elite and how often a player reaches for it (OFTEN, the player's estimate; empty until filled in).

  tools/params_doc.py           write docs/PARAMETERS.md
  tools/params_doc.py --check   exit 1 if docs/PARAMETERS.md is not what the table gives
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.path.join(ROOT, "docs", "PARAMETERS.md")

# where a value sits: (group, tag) -> (FM-1, Elite); the insert's and the master's follow from their place
WHERE = {
    ("ORGAN", "MODEL"): ("ALGORITHM", "SCALE + < >; < + > held K2"),
    ("ORGAN", "DB"): ("PLAY: KNOB1-4 (ENV LFO EDIT the four)", "K1-K8, fader 1 (drawbar 9), the pads (layouts A, B)"),
    ("ORGAN", "PERC"): ("FX: SEL + KNOB1", "FX K5; pad 3 (live switches)"),
    ("ORGAN", "CHIFF"): ("FX: SEL + KNOB1 (pipes, reeds)", "FX K5 (pipes, reeds)"),
    ("ORGAN", "II/I"): ("PLAY: SEQ", "SEQ (OFF, UM+LM, UM+PM, LM+PM, all)"),
    ("ORGAN", "OCT"): ("OCT- OCT+", "OCT- OCT+"),
    ("ORGAN", "DRIVE"): ("FX: KNOB1", "FX K1; pad 6 held + K6"),
    ("ORGAN", "VIB"): ("FX: KNOB2; PLAY: ARP on / off", "FX K2; NOTE RPT on / off; pad 1"),
    ("ORGAN", "TREM"): ("FX: KNOB2 (pipes, reeds); PLAY: ARP", "FX K2 (pipes, reeds); NOTE RPT"),
    ("ORGAN", "ROT"): ("FX: SEQ; SEL + REC", "FX pad 4; SCALE + REC"),
    ("ORGAN", "DELAY"): ("FX: KNOB3", "FX K3; pad 7 held + K8"),
    ("ORGAN", "REVERB"): ("FX: KNOB4", "FX K4; fader 2"),
    ("ORGAN", "SPEED"): ("PLAY (brake), REC (slow / fast); CC 1", "PLAY, REC; the mod wheel; pads 4, 5"),
    ("ORGAN", "D-TIME"): ("FX: SEL + KNOB3", "FX K7; pad 7 held + K7"),
    ("ORGAN", "FEEDBK"): ("FX: SEL + KNOB2", "FX K6"),
    ("ORGAN", "SPACE"): ("FX: SEL + KNOB4", "FX K8"),
    ("ORGAN", "EXPR"): ("CC 11 in", "the pedal (an expression pedal); CC 11 in"),
    ("ORGAN", "BEND"): ("MIDI ZONE: SEL + KNOB4", "ZONE K8"),
    ("MIDI", "IN CH"): ("MIDI ZONE: KNOB1", "ZONE K1"),
    ("MIDI", "OUT CH"): ("MIDI ZONE: SEL + KNOB1", "ZONE K2"),
    ("MIDI", "LOWEST"): ("MIDI ZONE: KNOB2; LRN", "ZONE K3; pad 5 (LRN)"),
    ("MIDI", "HIGHEST"): ("MIDI ZONE: SEL + KNOB2; LRN", "ZONE K4; pad 5 (LRN)"),
    ("MIDI", "TRANSP"): ("MIDI ZONE: KNOB3", "ZONE K5"),
    ("MIDI", "OCTAVE"): ("MIDI ZONE: SEL + KNOB3", "ZONE K6"),
    ("MIDI", "VEL"): ("MIDI ZONE: KNOB4", "ZONE K7"),
    ("MIDI", "CC7"): ("HOME (the mixer): KNOB1-4, ENV LFO ARP SEQ the row", "PARA (the mixer): K1-K8 (VOL)"),
}
# how often a player reaches for it, the player's estimate: (group, tag) -> "live" / "per song" / "setup" / "rarely"
OFTEN = {}

FM1_MST = {"LOW": "KNOB1", "MID": "KNOB2", "HIGH": "KNOB3", "L FREQ": "SEL + KNOB1", "M FREQ": "SEL + KNOB2",
           "H FREQ": "SEL + KNOB3", "THRESH": "EDIT (COMP), KNOB1", "RATIO": "EDIT (COMP), KNOB2",
           "ATTACK": "EDIT (COMP), KNOB3", "RELEASE": "EDIT (COMP), KNOB4", "MAKEUP": "EDIT (COMP), SEL + KNOB1"}
ELITE_MST = ["K1", "K2", "K3", "K4", "K5", "K6", "K7", "K8", "SCALE + K1", "SCALE + K2", "SCALE + K3"]
KEPT = {"setup": "setup", "device": "device", "blob 2": "flash (insert)", "blob 3": "flash (master)",
        "blob 1": "flash (cabinets)", "-": "not kept"}
INTRO = """# OrGigs: the parameters

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
"""


def dump(board):
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, "orgigs")
        flags = ["-DBOARD_SMK37=1"] if board == "smk37" else []
        subprocess.run(["cc", "-O1", "-w", "-DDINGHY_HOST", "-DORGIGS_NO_INTRO", "-DAPP_VER=\"dev\""] + flags
                       + ["-I" + os.path.join(ROOT, "firmware", p) for p in ("src", "gen")]
                       + ["-o", exe, os.path.join(ROOT, "firmware", "src", "orgigs.c")], check=True)
        out = subprocess.run([exe, "--params"], capture_output=True, text=True, check=True).stdout
    return [ln.split("|") for ln in out.splitlines() if ln.count("|") == 10]


def where(group, tag, index):
    if group.startswith("INSERT"):
        return ("SEL + FX (INSERT): " + ("SEL + " if index >= 4 else "") + "KNOB%d" % (index % 4 + 1),
                "EFFECT page: K%d" % (index + 1))
    if group == "MASTER":
        return "SEL + FX twice (MASTER): " + FM1_MST[tag], "MASTER page: " + ELITE_MST[index]
    if group.startswith("ROTARY CABINET"):
        return "- (the CLASSIC, fixed)", "ROTARY page: " + ("K%d" % (index + 1) if index < 8 else "SCALE + K%d" % (index - 7))
    return WHERE.get((group, tag), ("?", "?"))


def render():
    fm1, smk = dump("fm1"), dump("smk37")
    fm1_keys = {(r[0], r[1]) for r in fm1}
    rows, seen = [], set()
    for r in smk + [r for r in fm1 if (r[0], r[1]) not in {(s[0], s[1]) for s in smk}]:
        if (r[0], r[1]) not in seen:
            seen.add((r[0], r[1]))
            rows.append(r)
    out, group, index = [INTRO], None, 0
    for grp, tag, name, lo, hi, step, start, unit, per, keep, cc in rows:
        if grp != group:
            group, index = grp, 0
            out.append("\n## %s\n" % grp.title().replace("Midi", "MIDI"))
            out.append("| Value | Name | Range | Step | Start | Per | Kept | CC | FM-1 | Elite | How often |")
            out.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
        f, e = where(grp, tag, index)
        if (grp, tag) not in fm1_keys:
            f = "-"
        rng = lo + " .. " + hi if lo != hi else lo
        out.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            tag, name, rng, step if step != "0" else "~6 %", start, per, KEPT.get(keep, keep), cc if cc != "0" else "", f, e,
            OFTEN.get((grp, tag), "")))
        index += 1
    out.append("\n%d values: %d on both boards, %d on the Elite only.\n" % (
        len(rows), sum(1 for r in rows if (r[0], r[1]) in fm1_keys), sum(1 for r in rows if (r[0], r[1]) not in fm1_keys)))
    return "\n".join(out)


def main():
    text = render()
    if "--check" in sys.argv:
        ok = os.path.exists(DOC) and open(DOC).read() == text
        print("docs/PARAMETERS.md " + ("is up to date" if ok else "is out of date: run tools/params_doc.py"))
        sys.exit(0 if ok else 1)
    with open(DOC, "w") as f:
        f.write(text)
    print("wrote " + DOC)


if __name__ == "__main__":
    main()
