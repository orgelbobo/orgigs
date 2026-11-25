#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the browser simulators (the WebAssembly is inside the .js):
#   sim/build_wasm.sh          the FM-1:  sim/web/orgigs_sim.js
#   sim/build_wasm.sh elite    the Elite: sim/web/elite/orgigs_sim.js (BOARD_SMK37, the Elite's name)
# then sim/serve.sh (Web MIDI wants http://localhost): / is the FM-1, /elite/ the Elite.
# Needs Emscripten (emcc) on the PATH, e.g. source ~/emsdk/emsdk_env.sh
set -e
cd "$(dirname "$0")/.."
command -v emcc >/dev/null 2>&1 || { [ -f "$HOME/emsdk/emsdk_env.sh" ] && . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1; }
command -v emcc >/dev/null 2>&1 || { echo "build_wasm.sh: no emcc (install the emsdk)"; exit 1; }
python3 tools/gen_orgigs_tables.py
if [ "$1" = elite ]; then
    DEFS='-DBOARD_SMK37=1 -DSMK_MODEL="SMK-37 Elite"'; OUT=sim/web/elite/orgigs_sim.js
else
    DEFS=; OUT=sim/web/orgigs_sim.js
fi
eval emcc -O3 -Wall -Wno-unused-function -DDINGHY_HOST -DORGIGS_WASM $DEFS -DAPP_VER="'\"${APP_VER:-sim}\"'" \
     -Ifirmware/hal -Ifirmware/src -Ifirmware/gen firmware/src/orgigs.c -o $OUT \
     -sMODULARIZE=1 -sEXPORT_NAME=OrGigsModule -sSINGLE_FILE=1 -sENVIRONMENT=web \
     -sINITIAL_MEMORY=16777216 -sALLOW_MEMORY_GROWTH=0 \
     -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP16,HEAPU16
ls -l $OUT
