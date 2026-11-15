/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs FM-1: a 3-manual organ, pipe organ and free-reed workstation with three MIDI zones,
 * for the M-VAVE FM-1, on Dinghy's platform (platform.c: boot, USB rescue, updater, USB-MIDI, USB audio,
 * panel, screen, settings).
 *
 * core/ is the same on every board:
 *   orgigs_dsp.c         the generators, 8 models, 3 manuals          (audio interrupt)
 *   orgigs_fx.c          drive / cassotto, vibrato, Leslie, delay, reverb (audio interrupt)
 *   orgigs_midi_zones.c  three MIDI zones: router, cloner, 9-ch harmonic mapper, chord memory (main loop)
 *   orgigs_presets.c     drawbar presets, performance setups, the settings in flash      (main loop)
 *   orgigs_ctl.c         what a panel can do with the organ, the message line             (main loop)
 *   orgigs_backup.c      the settings as a SysEx backup over USB-MIDI (tools/orgigs_backup.py) (main loop)
 *   orgigs_yst.c         YST: a Yamaha SEQTRAK on the USB cable, its tracks, sounds and knobs  (main loop)
 * board/<name>/ is one device:
 *   board.h              its facts (the keys)
 *   board.c              its panel, screens and lights: board/fm1/orgigs_ui.c on the FM-1 (main loop)
 *
 * One compilation unit, as Dinghy builds it. The same file runs on a computer (tests/host.c: WAV + screens)
 * and in a browser (sim/: WebAssembly). Integers only: the chip has no FPU and no 64-bit division. */
#if BOARD_SMK37
#define APP_NAME "ORGIGS SMK-37"
#else
#define APP_NAME "ORGIGS FM-1"
#endif
#define ORGIGS_MASTER_FADER 1               /* (the SMK-37: the volume is fader 4, hal/smk_input.h) */
#include "platform.c"

/* the board (BOARD_SMK37: platform.c, tools/build.py --board): its facts first (the core sizes its key slots by
   them), its panel last */
#if BOARD_SMK37
#include "../board/smk37/board.h"
#else
#include "../board/fm1/board.h"
#endif

#include "../core/orgigs_dsp.c"
#include "../core/orgigs_midi_zones.c"
#include "../core/orgigs_presets.c"
#include "../core/orgigs_ctl.c"
#include "../core/orgigs_backup.c"
#include "../core/orgigs_yst.c"

#if BOARD_SMK37
#include "../board/smk37/board.c"
#else
#include "../board/fm1/board.c"
#endif

static volatile uint32_t org_ready;     /* the interrupt runs before app_main: wait for the tables */

static void app_audio(int32_t *out, uint32_t n)
{
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks();
#endif
    if (!org_ready) return;
    org_audio(out, n);
#ifndef DINGHY_HOST
    org_stat.load_us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
#endif
}

static void app_init(void)
{
    ui_init();
    org_ready = 1;
}

static void app_main(void)
{
    app_init();
    for (;;) {
        platform_poll();
        ui_step();
#if defined(ORGIGS_TONETEST)
        tonetest_step();
#elif defined(ORGIGS_SELFTEST)
        selftest_step();
#endif
    }
}

#ifdef ORGIGS_WASM
#include "../../sim/wasm_exports.c"     /* the browser simulator's entry points */
#endif
