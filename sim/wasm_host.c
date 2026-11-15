/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs in a browser: the fake FM-1 (sim_platform.h) plus the bridges to JavaScript.
 * platform.c includes this file instead of the hardware when ORGIGS_WASM is set; the exported entry points
 * are in sim/wasm_exports.c, included at the end of orgigs.c (they need the app's state). sim/build_wasm.sh. */
#include <emscripten.h>
#include "sim_platform.h"

EM_JS(void, js_midi_out, (int st, int d1, int d2), {
    if (Module.onMidiOut) Module.onMidiOut(st, d1, d2);
});
EM_JS(void, js_settings_saved, (void), {
    if (Module.onSettingsSaved) Module.onSettingsSaved();
});

EM_JS(void, js_sysex_out, (const uint8_t *p, int n), {
    if (Module.onSysexOut) Module.onSysexOut(HEAPU8.slice(p, p + n));
});
static int sysex_send(const uint8_t *p, uint32_t n) { js_sysex_out(p, (int)n); return 0; }
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { js_midi_out((int)st, (int)d1, (int)d2); }
static void host_settings_saved(void) { js_settings_saved(); }
static void platform_poll(void) {}       /* the browser drives the loop: sim_step() */
