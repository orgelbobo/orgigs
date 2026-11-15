#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/orgigs.c on this computer: build/host/orgigs.wav, the screens as PNG, the MIDI it sends.
#   tests/run_host.sh [script.txt]      (default: tests/demo.txt; BOARD=smk37: the SMK-37 build)
set -e
cd "$(dirname "$0")/.."
SCRIPT="${1:-tests/demo.txt}"
mkdir -p build/host
rm -f build/host/orgigs-*.ppm build/host/orgigs-*.png
case "${BOARD:-fm1}" in
    smk37) BDEF="-DBOARD_SMK37=1" ;;           # the SMK-37 (BOARD=smk37 tests/run_host.sh ...)
    *) BDEF="" ;;
esac
cc -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -DDINGHY_HOST -DORGIGS_NO_INTRO $BDEF -DAPP_VER="\"${APP_VER:-dev}\"" \
   -Ifirmware/src -Ifirmware/gen -o build/host/orgigs firmware/src/orgigs.c
build/host/orgigs build/host/orgigs "$SCRIPT"
python3 tools/ppm2png.py build/host/orgigs-*.ppm
