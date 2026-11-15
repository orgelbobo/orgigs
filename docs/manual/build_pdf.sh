#!/bin/sh
# Print a manual to PDF with Windows Chrome (from WSL):
#   docs/manual/build_pdf.sh                -> docs/manual/OrGigs_FM-1_Manual.pdf (manual.html)
#   docs/manual/build_pdf.sh elite          -> docs/manual/OrGigs_Elite_Manual.pdf (manual_elite.html)
#   docs/manual/build_pdf.sh cheat-fm1      -> docs/manual/OrGigs_FM-1_At_a_glance.pdf (cheat_fm1.html, two pages)
#   docs/manual/build_pdf.sh cheat-elite    -> docs/manual/OrGigs_Elite_At_a_glance.pdf (cheat_elite.html, two pages)
# The screenshots in img/ come from tests/run_host.sh (build/host/orgigs-*.png): tests/manual_shots.txt for the
# FM-1, BOARD=smk37 tests/elite_manual_shots.txt for the Elite.
set -e
cd "$(dirname "$0")"
case "${1:-fm1}" in
    elite) SRC=manual_elite.html; PDF=OrGigs_Elite_Manual.pdf ;;
    cheat-fm1) SRC=cheat_fm1.html; PDF=OrGigs_FM-1_At_a_glance.pdf ;;
    cheat-elite) SRC=cheat_elite.html; PDF=OrGigs_Elite_At_a_glance.pdf ;;
    *) SRC=manual.html; PDF=OrGigs_FM-1_Manual.pdf ;;
esac
WTEMP=$(wslpath "$(cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')")
OUT="$WTEMP/orgigs_manual"
rm -rf "$OUT" && mkdir -p "$OUT"
cp -r "$SRC" img "$OUT/"
CHROME="/mnt/c/Program Files/Google/Chrome/Application/chrome.exe"
WOUT=$(wslpath -w "$OUT")
"$CHROME" --headless=new --disable-gpu --no-pdf-header-footer \
    --print-to-pdf="$WOUT\\manual.pdf" "file:///$(echo "$WOUT" | tr '\\' '/')/$SRC" 2>/dev/null
i=0                                     # (Chrome may still be writing when it returns)
while [ ! -s "$OUT/manual.pdf" ] && [ $i -lt 60 ]; do sleep 1; i=$((i + 1)); done
sleep 2
cp "$OUT/manual.pdf" "$PDF"
echo "-> docs/manual/$PDF"
