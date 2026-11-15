#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the browser simulator: sim/web/orgigs_sim.js (the WebAssembly is inside it).
#   sim/build_wasm.sh            then open sim/web/index.html (or sim/serve.sh for Web MIDI)
# Needs Emscripten (emcc) on the PATH, e.g. source ~/emsdk/emsdk_env.sh
set -e
cd "$(dirname "$0")/.."
command -v emcc >/dev/null 2>&1 || { [ -f "$HOME/emsdk/emsdk_env.sh" ] && . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1; }
command -v emcc >/dev/null 2>&1 || { echo "build_wasm.sh: no emcc (install the emsdk)"; exit 1; }
python3 tools/gen_orgigs_tables.py
emcc -O3 -Wall -Wno-unused-function -DDINGHY_HOST -DORGIGS_WASM -DAPP_VER="\"${APP_VER:-sim}\"" \
     -Ifirmware/src -Ifirmware/gen firmware/src/orgigs.c -o sim/web/orgigs_sim.js \
     -sMODULARIZE=1 -sEXPORT_NAME=OrGigsModule -sSINGLE_FILE=1 -sENVIRONMENT=web \
     -sINITIAL_MEMORY=16777216 -sALLOW_MEMORY_GROWTH=0 \
     -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP16,HEAPU16
ls -l sim/web/orgigs_sim.js
