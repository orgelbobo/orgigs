#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The SysEx backup (core/orgigs_backup.c, tools/orgigs_backup.py) on the host build: OrGigs sends a backup, a
fresh OrGigs loads it (SAVE), sends the undo first and then holds the same; one setup into another slot; a
button other than SAVE keeps everything; a damaged backup is refused. The SMK-37 Elite's extra records (Leslie
cabinets, insert effect, master) go and come back; backups with and without them load on both boards.

  tests/backup_test.py          (builds build/host/orgigs with tests/run_host.sh first)
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import orgigs_backup as ob  # noqa: E402

HOST = os.path.join(ROOT, "build", "host", "orgigs")
BTN_SAVE, BTN_FX = 9, 2
REQ_ALL = "7D4F4701"


def run(lines, end):
    with tempfile.TemporaryDirectory() as d:
        script = os.path.join(d, "s.txt")
        with open(script, "w") as f:
            f.write("\n".join(lines + [f"{end} end"]) + "\n")
        out = subprocess.run([HOST, os.path.join(d, "o"), script], capture_output=True, text=True, check=True).stdout
    sysex = [bytes.fromhex(ln.split("SYSEX out ")[1]) for ln in out.splitlines() if "SYSEX out " in ln]
    return out, sysex


def backups(sysex):
    j, got = ob.Joiner(), []
    for m in sysex:
        b = j.feed(m)
        if b:
            got.append(ob.Backup(b))
    return got


def states(sysex):
    return [m[5] for m in sysex if m[4] == ob.STATE]


def feed(b, t0):
    return [f"{t0 + 20 * i} sysex {m[1:-1].hex().upper()}" for i, m in enumerate(ob.messages(b.raw))]


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        sys.exit(1)


def main():
    subprocess.run([os.path.join(ROOT, "tests", "run_host.sh"), os.path.join(ROOT, "tests", "leslie.txt")],
                   check=True, stdout=subprocess.DEVNULL)
    # A: a changed OrGigs (model 3, drawbars moved, setup 2 stored), saved, then asked for a backup
    _, sx = run(["100 enc 1 2", "200 enc 2 -5", "300 enc 3 4", "400 down 9", "450 keys 4", "500 keys 0",
                 "550 up 9", "600 enc 1 1", "4000 sysex " + REQ_ALL, "4200 sysex 7D4F470201"], 4500)
    got = backups(sx)
    check(len(got) == 2 and got[0].kind == ob.KIND_ALL and got[1].kind == ob.KIND_SETUP, "backup and setup sent")
    a, a_setup2 = got
    check(a.setup_data(1) == a_setup2.data, "setup 2 inside the whole backup = setup 2 alone")
    # a file round trip
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "a.syx")
        ob.write_file(p, a)
        check(ob.read_file(p).raw == a.raw, "the file holds what came")

    # B: a factory OrGigs loads A
    _, sx = run(["100 sysex " + REQ_ALL] + feed(a, 300) + [f"1000 btn {BTN_SAVE}", "4000 sysex " + REQ_ALL], 4500)
    got = backups(sx)
    check(states(sx) == [1, 2], "asked, then loaded")
    check(len(got) == 3, "factory backup, the undo, the backup after")
    factory, undo, after = got
    check(undo.data == factory.data, "the undo is what was there")
    check(after.data == a.data, "it now holds the backup")
    check(factory.data != a.data, "(and that differs from the factory's)")

    # C: setup 2 of A into setup 7 of a factory OrGigs
    one = ob.Backup(ob.setup_backup(a, 1, 6))
    _, sx = run(feed(one, 100) + [f"600 btn {BTN_SAVE}", "2000 sysex " + REQ_ALL], 2500)
    got = backups(sx)
    after = got[-1]
    check(states(sx) == [1, 2] and after.setup_data(6) == a.setup_data(1), "setup 2 loaded into setup 7")
    check(after.setup_data(1) == factory.setup_data(1), "setup 2 kept")

    # D: another button keeps everything
    _, sx = run(feed(a, 100) + [f"600 btn {BTN_FX}", "2000 sysex " + REQ_ALL], 2500)
    check(states(sx) == [1, 3] and backups(sx)[-1].data == factory.data, "FX: not loaded")

    # E: a damaged backup
    bad = bytearray(a.raw)
    bad[100] ^= 1
    msgs = ob.messages(bytes(bad))
    _, sx = run([f"{100 + 20 * i} sysex {m[1:-1].hex().upper()}" for i, m in enumerate(msgs)], 1000)
    check(states(sx) == [4], "damaged: refused")
    tool_paths(a, factory)
    elite(a)
    print("backup tests passed")


def elite(fm1_backup):
    """F: the SMK-37 Elite's backup carries its Leslie cabinets, insert effect and master; it loads back; one
    without them loads too and leaves them as they are; the FM-1 takes the effect and the master from it"""
    import struct
    subprocess.run([os.path.join(ROOT, "tests", "run_host.sh"), os.path.join(ROOT, "tests", "elite_fx.txt")],
                   check=True, stdout=subprocess.DEVNULL, env=dict(os.environ, BOARD="smk37"))
    bt = 11                                                 # the Elite's BT (SAVE)
    horn = lambda b: struct.unpack_from("<h", b.extras()["L"], 4)[0]   # USER 1's horn slow, 1/100 Hz
    # FX, PATCH: LESLIE; pad 3: USER 1; K1 +5: horn slow 0.60 -> 0.70; EFFECT (PATCH): pad 1 CHORUS
    _, sx = run(["1000 btn 14", "1100 btn 12", "1200 pad 2 100", "1210 pad 2 0", "1300 enc 0 5", "1400 btn 12",
                 "1500 pad 0 100", "1510 pad 0 0", "4000 sysex " + REQ_ALL], 4500)
    e = backups(sx)[0]
    x = e.extras()
    check(sorted(x) == ["F", "L", "M", "P"] and horn(e) == 70 and x["L"][1] == 2 and x["F"][1] == 1,
          "Elite: the backup carries the rotary cabinets (USER 1, horn 0.70), the effect (CHORUS), the master, the layouts")
    check("rotary" in e.describe() and sorted(fm1_backup.extras()) == ["F", "M"],
          "the tool names them (the FM-1's backup: the effect and the master, not the Elite's cabinets and layouts)")
    _, sx = run(feed(e, 100) + [f"1500 btn {bt}", "5000 sysex " + REQ_ALL], 5500)
    got = backups(sx)
    check(states(sx) == [1, 2] and horn(got[0]) == 60 and horn(got[-1]) == 70 and got[-1].extras()["F"][1] == 1,
          "Elite: a fresh one loads it: the undo had 0.60, now 0.70 and CHORUS")
    plain = ob.Backup(ob.make_backup(ob.KIND_ALL, 0, e.data[:e.data.find(b"OGEX")], e.store_ver, e.st0_len,
                                     e.setups_at, e.setup_len))
    _, sx = run(["1000 btn 14", "1100 btn 12", "1200 pad 2 100", "1210 pad 2 0", "1300 enc 0 5"] + feed(plain, 1500)
                + [f"2500 btn {bt}", "6000 sysex " + REQ_ALL], 6500)
    check(states(sx) == [1, 2] and horn(backups(sx)[-1]) == 70,
          "Elite: a backup without them (as one of before) loads; the cabinets stay as they were")
    # (and the FM-1 takes the Elite's backup, skipping what it does not know)
    subprocess.run([os.path.join(ROOT, "tests", "run_host.sh"), os.path.join(ROOT, "tests", "leslie.txt")],
                   check=True, stdout=subprocess.DEVNULL)
    _, sx = run(feed(e, 100) + [f"1500 btn {BTN_SAVE}", "5000 sysex " + REQ_ALL], 5500)
    got = backups(sx)
    fm1x = got[-1].extras()
    check(states(sx) == [1, 2] and got[-1].data[:got[-1].data.find(b"OGEX")] == e.data[:e.data.find(b"OGEX")]
          and sorted(fm1x) == ["F", "M"] and fm1x["F"] == x["F"] and fm1x["M"] == x["M"],
          "FM-1: takes the Elite's backup (the shared part, the effect and the master), skips the cabinets and layouts")


class FakeOrGigs:
    """the device side as the firmware answers (for the tool's --save / --load paths)"""

    def __init__(self, held, answer):
        self.held, self.answer, self.q, self.j, self.got = held, answer, [], ob.Joiner(), None

    def drain(self):
        self.q.clear()

    def send(self, m):
        if m[4] == ob.REQ_ALL:
            self.q += ob.messages(self.held.raw)
        elif m[4] == ob.PART:
            s = m[5] | m[6] << 7
            got = self.j.feed(m)
            self.q.append(ob.HEAD + bytes([ob.ACK, s & 127, s >> 7, 0]) + b"\xF7")
            if got:
                self.got = ob.Backup(got)
                self.q.append(ob.HEAD + bytes([ob.STATE, 1]) + b"\xF7")
                if self.answer:
                    self.q += ob.messages(self.held.raw)
                self.q.append(ob.HEAD + bytes([ob.STATE, 2 if self.answer else 3]) + b"\xF7")
        return True

    def read(self, timeout):
        return self.q.pop(0) if self.q else None


def tool_paths(a, factory):
    import io
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "a.syx")
        dev = FakeOrGigs(a, True)
        check(ob.main(["--save", p], io.StringIO(), dev) == 0 and ob.read_file(p).raw == a.raw, "tool: --save")
        dev = FakeOrGigs(factory, True)
        out = io.StringIO()
        check(ob.main(["--load", p], out, dev) == 0 and dev.got.raw == a.raw, "tool: --load")
        before = [f for f in os.listdir(d) if f.startswith("a-before-")]
        check(len(before) == 1 and ob.read_file(os.path.join(d, before[0])).data == factory.data, "tool: the undo file")
        dev = FakeOrGigs(factory, True)
        check(ob.main(["--load", p, "--setup", "2", "--to", "7"], io.StringIO(), dev) == 0 and dev.got.kind == 1
              and dev.got.setup == 6 and dev.got.data == a.setup_data(1), "tool: --setup 2 --to 7")
        dev = FakeOrGigs(factory, False)
        check(ob.main(["--load", p], io.StringIO(), dev) == 1, "tool: kept on the device -> exit 1")
        check(ob.main(["--load", p, "--to", "3"], io.StringIO(), dev) == 2, "tool: --to without --setup -> exit 2")


if __name__ == "__main__":
    main()
