#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Back up and restore the settings of OrGigs (FM-1, SMK-37 Elite) over USB-MIDI.

  orgigs_backup.py --save FILE.syx [--setup N]       fetch everything (or setup N, 1..16) into FILE
  orgigs_backup.py --listen FILE.syx                 wait for a backup sent from the panel (SEL+GLO, then SEQ)
  orgigs_backup.py --load FILE.syx [--setup N] [--to M]
                                                     send FILE to OrGigs. A whole backup replaces everything;
                                                     --setup N takes setup N out of a whole backup, --to M
                                                     loads a setup into setup M (default: where it came from).
                                                     OrGigs asks on its screen: SAVE loads it, any other
                                                     button keeps what is there. Before it loads, OrGigs
                                                     sends everything as it was: kept as FILE-before-DATE.syx
  orgigs_backup.py --show FILE.syx                   what FILE holds
  --port NAME                                        the MIDI port (default: the first OrGigs that answers)

The files are plain SysEx (firmware/core/orgigs_backup.c has the format): any SysEx program can keep them and
send them back, with about 50 ms between the messages. The FM-1 and the SMK-37 Elite read each other's backups.
Needs mido with python-rtmidi (as fm1_install.py). On Windows, close other programs that use the port first.

Exit codes: 0 done, 1 not loaded (kept on the device, or no answer), 2 bad arguments or file, 3 OrGigs not found.
"""
import argparse
import datetime
import os
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

HEAD = bytes([0xF0, 0x7D, 0x4F, 0x47])
REQ_ALL, REQ_SETUP, PART, ACK, STATE = 0x01, 0x02, 0x10, 0x20, 0x21
PART_BYTES, HDR, FMT = 256, 16, 1
KIND_ALL, KIND_SETUP = 0, 1
NSETUP = 16
STATES = {0: "ok", 1: "asking", 2: "loaded", 3: "kept (not loaded)", 4: "the backup arrived damaged",
          5: "made by another OrGigs version", 6: "no answer on the device (60 s)", 7: "parts missing"}
DELAY = {"ack": 2.0, "part": 2.0, "ask": 70.0, "listen": 600.0}     # seconds; the tests shorten them


class BackupError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


# ------------------------------------------------------------- the format ---

def pack7(data):
    """8 bytes carry 7: first the top bits (bit i = byte i), then the low 7 bits"""
    out = bytearray()
    for i in range(0, len(data), 7):
        g = data[i:i + 7]
        out.append(sum((b >> 7) << j for j, b in enumerate(g)))
        out += bytes(b & 0x7F for b in g)
    return bytes(out)


def unpack7(s):
    out = bytearray()
    for i in range(0, len(s), 8):
        top, g = s[i], s[i + 1:i + 8]
        out += bytes(b | ((top >> j) & 1) << 7 for j, b in enumerate(g))
    return bytes(out)


def messages(backup):
    """a backup (header + data + CRC) -> its SysEx messages"""
    parts = (len(backup) + PART_BYTES - 1) // PART_BYTES
    out = []
    for s in range(parts):
        chunk = backup[s * PART_BYTES:(s + 1) * PART_BYTES]
        out.append(HEAD + bytes([PART, s & 127, s >> 7, parts & 127, parts >> 7]) + pack7(chunk) + b"\xF7")
    return out


def split_syx(raw):
    """a .syx file -> its F0..F7 messages"""
    out, at = [], 0
    while (s := raw.find(b"\xF0", at)) >= 0:
        e = raw.find(b"\xF7", s)
        if e < 0:
            break
        out.append(raw[s:e + 1])
        at = e + 1
    return out


class Joiner:
    """collects the parts of one backup; feed() -> the backup when complete"""

    def __init__(self):
        self.buf, self.next, self.parts = bytearray(), 0, 0

    def feed(self, msg):
        if not (msg[:4] == HEAD and len(msg) >= 10 and msg[4] == PART):
            return None
        s, t = msg[5] | msg[6] << 7, msg[7] | msg[8] << 7
        if s == 0:
            self.buf, self.next, self.parts = bytearray(), 0, t
        if s != self.next or t != self.parts:
            raise BackupError(2, f"part {s + 1} of {t} out of order")
        self.buf += unpack7(msg[9:-1])
        self.next += 1
        if self.next < t:
            return None
        self.next = 0
        return bytes(self.buf)


class Backup:
    def __init__(self, raw):
        if len(raw) < HDR + 4 or raw[:4] != b"OGBK":
            raise BackupError(2, "not an OrGigs backup")
        (self.fmt, self.store_ver, self.kind, self.setup, n, self.st0_len, self.setups_at,
         self.setup_len) = struct.unpack_from("<BBBBHHHH", raw, 4)
        if len(raw) != HDR + n + 4:
            raise BackupError(2, "the backup's length is wrong")
        if zlib.crc32(raw[:HDR + n]) != struct.unpack_from("<I", raw, HDR + n)[0]:
            raise BackupError(2, "the backup is damaged (CRC)")
        if self.fmt != FMT:
            raise BackupError(2, f"backup format {self.fmt}: this tool knows {FMT}")
        self.raw, self.data = raw, raw[HDR:HDR + n]

    def extras(self):
        """the SMK-37's extra records after "OGEX" (tag -> bytes): L the rotary cabinets, F the insert effect,
        M the master; {} for a backup without them (the FM-1's, or older ones)"""
        at, got = self.data.find(b"OGEX", self.st0_len) if self.kind == KIND_ALL else -1, {}
        if at < 0:
            return got
        at += 4
        while at + 3 <= len(self.data):
            n = self.data[at + 1] | self.data[at + 2] << 8
            got[chr(self.data[at])] = self.data[at + 3:at + 3 + n]
            at += 3 + n
        return got

    def describe(self):
        what = "everything (16 setups, the panel, the user drawbars, the lights)" if self.kind == KIND_ALL \
            else f"setup {self.setup + 1}"
        x = self.extras()
        more = ""
        if x:
            names = {"L": "the rotary cabinets", "F": "the insert effect", "M": "the master", "P": "the panel layouts"}
            more = "; with " + ", ".join(names.get(k, k) for k in x)
        return f"OrGigs backup: {what}{more}, settings version {self.store_ver}, {len(self.data)} bytes"

    def setup_data(self, n):
        if self.kind == KIND_SETUP:
            return self.data
        if not 0 <= n < NSETUP:
            raise BackupError(2, "setups are 1..16")
        at = self.setups_at + n * self.setup_len
        return self.data[at:at + self.setup_len]


def make_backup(kind, setup, data, store_ver, st0_len=0, setups_at=0, setup_len=0):
    head = b"OGBK" + struct.pack("<BBBBHHHH", FMT, store_ver, kind, setup, len(data), st0_len, setups_at, setup_len)
    body = head + data
    return body + struct.pack("<I", zlib.crc32(body))


def setup_backup(b, n, to):
    """one setup of b (n: in a whole backup) as a backup that loads into setup `to`"""
    return make_backup(KIND_SETUP, to, b.setup_data(n), b.store_ver, setup_len=b.setup_len)


def read_file(path):
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise BackupError(2, str(e))
    j, got = Joiner(), None
    for m in split_syx(raw):
        got = j.feed(m) or got
    if got is None:
        raise BackupError(2, f"{path}: no complete OrGigs backup in it")
    return Backup(got)


def write_file(path, b):
    with open(path, "wb") as f:
        f.write(b"".join(messages(b.raw)))


# ------------------------------------------------------------- the device ---

def receive(link, secs, say=None):
    """the next whole backup that comes in -> Backup (None: nothing within secs)"""
    j = Joiner()
    end = time.monotonic() + secs
    while (left := end - time.monotonic()) > 0:
        m = link.read(min(left, DELAY["part"]) if j.next else left)
        if m is None:
            if j.next:
                raise BackupError(1, "the backup stopped half way")
            continue
        got = j.feed(m)
        if got:
            return Backup(got)
    return None


def fetch(link, setup=None):
    link.drain()
    link.send(HEAD + (bytes([REQ_ALL]) if setup is None else bytes([REQ_SETUP, setup])) + b"\xF7")
    b = receive(link, 5.0)
    if b is None:
        raise BackupError(1, "OrGigs sent nothing (is it OrGigs 0.95 or later?)")
    return b


def wait_state(link, secs, keep=None):
    """the next state message; parts arriving meanwhile go to keep (a Joiner) -> (code, backup or None)"""
    end, undo = time.monotonic() + secs, None
    while (left := end - time.monotonic()) > 0:
        m = link.read(left)
        if m is None:
            break
        if m[:4] != HEAD or len(m) < 6:
            continue
        if m[4] == STATE:
            return m[5], undo
        if m[4] == PART and keep is not None:
            got = keep.feed(m)
            if got:
                undo = Backup(got)
    return None, undo


def send(link, b, out, tries=3):
    """b to OrGigs, part by part (each acknowledged); -> the device's state after the last part"""
    msgs = messages(b.raw)
    for _ in range(tries):
        link.drain()
        for i, m in enumerate(msgs):
            link.send(m)
            code = None
            end = time.monotonic() + DELAY["ack"]
            while (left := end - time.monotonic()) > 0:
                r = link.read(left)
                if r and r[:4] == HEAD and len(r) >= 9 and r[4] == ACK and (r[5] | r[6] << 7) == i:
                    code = r[7]
                    break
                if r and r[:4] == HEAD and len(r) >= 6 and r[4] == STATE:
                    return r[5]
            if code != 0:
                print(f"part {i + 1} of {len(msgs)} not taken, again", file=out)
                break
        else:
            code, _ = wait_state(link, DELAY["ack"])
            return code
    raise BackupError(1, "OrGigs did not take the backup")


def find(port):
    import fm1_install as fi
    up = fi.Updater(fi.MidoBackend(), port)
    dev = up.find(lambda i: not i.loader and (674000 <= i.version <= 674999 or 970 <= i.version <= 979))   # (97N: up to 1.0.1)
    if not dev:
        raise BackupError(3, "OrGigs not found (connected by USB, and no other program using the port?)")
    return dev


def main(argv=None, out=sys.stdout, link=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--save", metavar="FILE")
    g.add_argument("--listen", metavar="FILE")
    g.add_argument("--load", metavar="FILE")
    g.add_argument("--show", metavar="FILE")
    ap.add_argument("--setup", type=int, metavar="N", help="1..16")
    ap.add_argument("--to", type=int, metavar="M", help="1..16")
    ap.add_argument("--port")
    try:
        a = ap.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    for v in (a.setup, a.to):
        if v is not None and not 1 <= v <= NSETUP:
            print("setups are 1..16", file=out)
            return 2
    dev = None
    try:
        if a.show:
            b = read_file(a.show)
            print(b.describe(), file=out)
            return 0
        if a.load:
            b = read_file(a.load)
            if a.setup is not None or a.to is not None or b.kind == KIND_SETUP:
                n = (a.setup or 1) - 1 if b.kind == KIND_ALL else b.setup
                if b.kind == KIND_ALL and a.setup is None:
                    raise BackupError(2, "--to needs --setup N with a whole backup")
                b = Backup(setup_backup(b, n, (a.to - 1) if a.to else n))
        if link is None:
            dev = find(a.port)
            link = dev.link
            print(f"OrGigs: {dev.id.text} on {dev.name}", file=out)
        if a.save:
            b = fetch(link, None if a.setup is None else a.setup - 1)
            write_file(a.save, b)
            print(f"{b.describe()} -> {a.save}", file=out)
            return 0
        if a.listen:
            print("waiting: on OrGigs press SEL + GLO, then SEQ", file=out)
            b = receive(link, DELAY["listen"])
            if b is None:
                print("nothing came", file=out)
                return 1
            write_file(a.listen, b)
            print(f"{b.describe()} -> {a.listen}", file=out)
            return 0
        print(f"sending {b.describe()}", file=out)
        code = send(link, b, out)
        if code != 1:
            print(f"not loaded: {STATES.get(code, 'no answer')}", file=out)
            return 1
        key = "BT" if dev and dev.id.model != "FM-1" else "SAVE"   # (the FM-1's SAVE is printed BT on the SMK-37)
        print(f"OrGigs asks on its screen: press {key} to load it, any other button keeps what is there", file=out)
        code, undo = wait_state(link, DELAY["ask"], Joiner())
        if undo is not None:
            stem = a.load[:-4] if a.load.lower().endswith(".syx") else a.load
            path = f"{stem}-before-{datetime.datetime.now():%Y%m%d-%H%M%S}.syx"
            write_file(path, undo)
            print(f"what was there before -> {path}", file=out)
        print(STATES.get(code, "no answer") if code is not None else "no answer", file=out)
        return 0 if code == 2 else 1
    except BackupError as e:
        print(e, file=out)
        return e.code
    finally:
        if dev:
            dev.link.close()


if __name__ == "__main__":
    sys.exit(main())
