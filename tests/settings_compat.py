#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""An update keeps the settings: OrGigs 1.1 (tag v1.1) changes its settings and saves them to the flash, then this
build starts on the same flash (ORGIGS_FLASH, tests/host.c) and must hold exactly what 1.1 held: the same backup
(setups, drawbar lists, zones, the Elite's rotary cabinets, effect, master and layouts). On the FM-1 and the Elite.

  tests/settings_compat.py [TAG]        (default v1.1; needs the tag in this repository)
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import orgigs_backup as ob  # noqa: E402

REQ_ALL = "7D4F4701"
# what each board changes under the old version: a model, drawbars, a stored setup, the effects (FM-1: model 3,
# drawbars moved, setup 2 stored, as tests/backup_test.py; Elite: also the rotary cabinet USER 1, the effect
# CHORUS, drawbar 9 by fader 1, setup 5 stored with SCALE + BT + pad 5)
SCENES = {
    "fm1": ["100 enc 1 2", "200 enc 2 -5", "300 enc 3 4", "400 down 9", "450 keys 4", "500 keys 0", "550 up 9",
            "600 enc 1 1", "700 btn 7", "800 enc 0 3", "900 btn 8"],
    "smk37": ["500 fader 0 30", "600 enc 1 -3", "1000 btn 14", "1100 btn 12", "1200 pad 2 100", "1210 pad 2 0",
              "1300 enc 0 5", "1400 btn 12", "1500 pad 0 100", "1510 pad 0 0", "1600 btn 12", "1700 down 8",
              "1750 down 11", "1800 pad 4 100", "1810 pad 4 0", "1850 up 11", "1900 up 8"],
}

# tests/host.c of an old version gets ORGIGS_FLASH as this build has it
FLASH_FN = '''static void host_flash(int save)
{
    const char *path = getenv("ORGIGS_FLASH");
    FILE *f;
    if (!path || !(f = fopen(path, save ? "wb" : "rb"))) return;
    if (save) { fwrite(host_store, 1, sizeof host_store, f); fwrite(host_blobs, 1, sizeof host_blobs, f); }
    else if (fread(host_store, 1, sizeof host_store, f) != sizeof host_store
             || fread(host_blobs, 1, sizeof host_blobs, f) != sizeof host_blobs) { fprintf(stderr, "%s: short\\n", path); exit(1); }
    fclose(f);
}
'''


def patch_host(path):
    s = open(path).read()
    if "ORGIGS_FLASH" in s:
        return
    a = 'static void host_settings_saved(void) { printf("%6u ms  settings saved\\n", fm1_ms); }\n'
    b = "    if (fm1_ms >= end_ms) {\n        write_wav();"
    c = '    host_blob_log = getenv("ORGIGS_BLOB_LOG") != 0;\n'
    if a not in s or b not in s or c not in s:
        raise SystemExit(f"{path}: cannot add ORGIGS_FLASH")
    s = s.replace(a, a + FLASH_FN, 1).replace(b, "    if (fm1_ms >= end_ms) {\n        host_flash(1);\n        write_wav();", 1)
    open(path, "w").write(s.replace(c, c + "    host_flash(0);\n", 1))


def build(tree, board, out):
    bdef = ["-DBOARD_SMK37=1"] if board == "smk37" else []
    subprocess.run(["cc", "-O2", "-w", "-DDINGHY_HOST", "-DORGIGS_NO_INTRO", *bdef, '-DAPP_VER="compat"',
                    "-Ifirmware/src", "-Ifirmware/gen", "-o", out, "firmware/src/orgigs.c"], cwd=tree, check=True)


def run(host, flash, lines, end, d):
    script = os.path.join(d, "s.txt")
    with open(script, "w") as f:
        f.write("\n".join(lines + [f"{end} end"]) + "\n")
    env = dict(os.environ, ORGIGS_FLASH=flash) if flash else dict(os.environ)
    out = subprocess.run([host, os.path.join(d, "o"), script], capture_output=True, text=True, check=True, env=env).stdout
    j, got = ob.Joiner(), []
    for ln in out.splitlines():
        if "SYSEX out " in ln:
            b = j.feed(bytes.fromhex(ln.split("SYSEX out ")[1]))
            if b:
                got.append(ob.Backup(b))
    return out, got


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        sys.exit(1)


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else "v1.1"
    with tempfile.TemporaryDirectory() as d:
        old = os.path.join(d, "old")
        os.mkdir(old)
        arc = subprocess.run(["git", "archive", tag], cwd=ROOT, capture_output=True, check=True).stdout
        subprocess.run(["tar", "-x", "-C", old], input=arc, check=True)
        patch_host(os.path.join(old, "tests", "host.c"))
        for board, name in (("fm1", "FM-1"), ("smk37", "Elite")):
            h_old, h_new = os.path.join(d, "old_" + board), os.path.join(d, "new_" + board)
            build(old, board, h_old)
            build(ROOT, board, h_new)
            flash = os.path.join(d, board + ".flash")
            out, got = run(h_old, flash, SCENES[board] + ["6000 sysex " + REQ_ALL], 6500, d)
            check("settings saved" in out and len(got) == 1, f"{name}: {tag} changed and saved its settings, sent a backup")
            was = got[0]
            _, fac = run(h_new, None, ["300 sysex " + REQ_ALL], 800, d)
            check(fac and fac[0].data != was.data, f"{name}: (the factory settings differ from them)")
            out, got = run(h_new, flash, ["300 sysex " + REQ_ALL], 800, d)
            check(len(got) == 1 and got[0].data == was.data, f"{name}: this build starts with {tag}'s settings, all of them")
            # the other way over: 1.1's backup (SysEx) loaded into a factory-fresh build (SAVE: FM-1 9, Elite BT 11)
            feed = [f"{300 + 20 * i} sysex {m[1:-1].hex().upper()}" for i, m in enumerate(ob.messages(was.raw))]
            save = 11 if board == "smk37" else 9
            _, got2 = run(h_new, None, feed + [f"1500 btn {save}", "3000 sysex " + REQ_ALL], 3500, d)
            check(got2 and got2[-1].data == was.data and (board != "smk37" or got2[-1].extras() == was.extras()),
                  f"{name}: {tag}'s backup loads into a factory-fresh build, all of it")
            if board == "smk37":
                check(got[0].extras() == was.extras(), f"{name}: the rotary cabinets, the effect, the master, the layouts kept")
            # and it keeps them when it saves again (a setup stored under the new version)
            more = ["300 fader 0 90", "400 down 8", "450 down 11", "500 pad 6 100", "510 pad 6 0", "550 up 11", "600 up 8"] \
                if board == "smk37" else ["300 enc 2 2", "400 down 9", "450 keys 16", "500 keys 0", "550 up 9"]
            out, _ = run(h_new, flash, more, 5000, d)
            _, got = run(h_new, flash, ["300 sysex " + REQ_ALL], 800, d)
            diff = [i + 1 for i in range(16) if got and got[0].setup_data(i) != was.setup_data(i)]
            check("settings saved" in out and len(diff) == 1,
                  f"{name}: one setup stored under this build (setup {diff}), {tag}'s other 15 still there")
    print("settings compatibility tests passed")


if __name__ == "__main__":
    main()
