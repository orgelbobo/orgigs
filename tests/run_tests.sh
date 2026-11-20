#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# OrGigs's tests without hardware: the platform's (Dinghy 0.4: the flash map, the update path, the loader, the packages,
# the installer; from the fm1-to-smk37 kit's reference layer, genkigenki/sloop-fm1 smk37) and the app on the host.
# Build the SMK-37 first, the FM-1 last (both write the same build/ intermediates; the tests read the FM-1's):
#   ./build.sh --board smk37; ./build.sh --board smk37 --model elite; ./build.sh --board smk37 --model mke; ./build.sh
#   sh tests/run_tests.sh
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # (fm1pkg_make.py: the SDK files of a package)
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f build/orgigs_fm1.fwsc ] || { echo "run ./build.sh first"; exit 1; }

run "FM-1 app on the host: the demo" sh tests/run_host.sh tests/demo.txt
run "SMK-37 app on the host: the demo" env BOARD=smk37 sh tests/run_host.sh tests/demo.txt
run "SMK-37 Elite panel: the keys only play; setups, cloner, harmonic on the pads; the MIDI area" python3 tests/elite_test.py
run "parameter table (core/orgigs_params.c): docs/PARAMETERS.md is what it gives" python3 tools/params_doc.py --check
run "FM-1 INSERT and MASTER: the effect on its manual only, the WAH's pedal from CC 1, the compressor, saved when silent" python3 tests/fm1_fx_test.py

$CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_flash_test" tests/smk_flash_test.c
run "SMK-37 flash map: settings, blobs, owner tag, slots in the store; never the head, BTIF/USRTRIM, USR, key_mac" "$OUT/smk_flash_test"
$CC -o "$OUT/smk_wheel_test" tests/smk_wheel_test.c
run "SMK-37 pitch wheel: the bend is back to 0 at rest, however slowly the wheel was moved" "$OUT/smk_wheel_test"

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (FM-1)" "$OUT/ota_test" build/orgigs_fm1.fwsc
head -c 60000 build/orgigs_fm1.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_screen_test" tests/ldr_screen_test.c
run "update loader's screen (from Skiff): UPDATE MODE, WRITING and its bar, UPDATE FAILED" "$OUT/ldr_screen_test" "$OUT"
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader (FM-1): other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/orgigs_fm1.fwsc

# SMK-37: the OTA staging and the loader windows on SMK-layout packages (made here from the FM-1 binaries: only the
# layout and the protocol are under test), then the real SMK packages
python3 tools/fm1pkg_make.py --board smk37 build/orgigs_fm1.bin build/loader/ota.bin "$OUT/smk_new.fwsc" >/dev/null
python3 tools/fm1pkg_make.py --board smk37 "$OUT/old_app.bin" build/loader/ota.bin "$OUT/smk_old.fwsc" >/dev/null
run "SMK-37 package layout (fm1pkg_make --board smk37): PRCT 0x9D000, stock region table" \
    python3 tests/smk_pkg_test.py "$OUT/smk_new.fwsc" "SMK-37 Pro_900"
$CC -DBOARD_SMK37=1 -o "$OUT/ota_test_smk" tests/ota_test.c
run "M-UPGRADE entry, SMK-37 Pro (staging 0xBB000, parm ota-SMK-37 Pro_016)" "$OUT/ota_test_smk" "$OUT/smk_new.fwsc"
for m in 1 2; do
    $CC -DBOARD_SMK37=1 -DT_MODEL=$m -o "$OUT/ota_test_smk_m$m" tests/ota_test.c
    run "M-UPGRADE entry, SMK-37 model $m (1 Elite, 2 MKE-P37): identity and parm under its name" "$OUT/ota_test_smk_m$m" "$OUT/smk_new.fwsc"
done
$CC -DBOARD_SMK37=1 -o "$OUT/ldr_test_smk" tests/ldr_test.c
run "update loader, SMK-37: app area 0x4000-0x9CFFF, protected ranges untouched" "$OUT/ldr_test_smk" "$OUT/smk_old.fwsc" "$OUT/smk_new.fwsc"
found=0
for m in ":SMK-37 Pro_674" "-elite:SMK-37 Elite_674" "-mke:MKE-P37_674"; do
    for f in build/orgigs_fm1*-smk37${m%%:*}.fwsc; do
        [ -f "$f" ] || continue
        found=1
        v=$(echo "$f" | sed -n 's/.*orgigs_fm1-\([0-9]\)\.\([0-9]\)\(\.\([0-9]\)\)\{0,1\}[-.].*/\1\2\4/p')   # X.Y[.Z] -> XYZ
        [ ${#v} = 2 ] && v=${v}0; v=${v:-000}
        run "SMK-37 package $f: layout, identity ${m#*:}$v, the loader marker" python3 tests/smk_pkg_test.py "$f" "${m#*:}$v"
    done
done
[ "$found" = 1 ] || echo "(no SMK-37 packages in build/: ./build.sh --board smk37 first)"

run "installer (tools/fm1_install.py): FM-1 and SMK-37, install and the update-mode resume check the model" python3 tests/install_test.py
run "settings backup (core/orgigs_backup.c, tools/orgigs_backup.py): send, load with the undo, one setup, keep, damaged" python3 tests/backup_test.py
run "YST (core/orgigs_yst.c): the SEQTRAK found, its tracks, sounds, knobs; the Elite's bank YST with pickup" python3 tests/yst_test.py
if [ -f build/orgigs_fm1.fwsc ] && [ -f build/orgigs_fm1-smk37-elite.fwsc ] && command -v node >/dev/null 2>&1; then
    run "site (tools/make_site.py) and its web installer against a simulated Elite / FM-1" \
        sh -c 'python3 tools/make_site.py >/dev/null && node tests/web_test.mjs'
else
    echo "(web installer test skipped: needs node and both packages in build/)"
fi

[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit "$fail"
