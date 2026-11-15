/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs FM-1: the organ engine's shared state.
 *
 * The FM-1 has one place where sound is made: the audio interrupt (app_audio, every 5.8 ms, 256 frames at
 * 44.1 kHz). Everything else (screen, panel, MIDI logic) runs in the main loop. They share this struct:
 * the main loop writes org_par, the interrupt only reads it; the interrupt writes org_stat, the main loop
 * only reads it. Every field is a byte or a 32-bit word, so each store is atomic.
 *
 * Integers only (the chip has no FPU): levels Q15, gains Q12, a 32-bit phase per generator. */
#pragma once
#include <stdint.h>
#include "orgigs_tables.h"

enum { MAN_UPPER, MAN_LOWER, MAN_PEDAL, ORG_NMAN };
enum { MODEL_B3, MODEL_VOX, MODEL_FARFISA, MODEL_YC, MODEL_BAROQUE, MODEL_CATHEDRAL, MODEL_MUSETTE,
       MODEL_BANDONEON, ORG_NMODEL };
enum { DEPT_VINTAGE, DEPT_PIPE, DEPT_REED };
enum { VIB_OFF, VIB_V1, VIB_V2, VIB_V3, VIB_C1, VIB_C2, VIB_C3, ORG_NVIB };
enum { PERC_OFF, PERC_2ND_FAST, PERC_2ND_SLOW, PERC_3RD_FAST, PERC_3RD_SLOW, ORG_NPERC,
       PERC_4TH_FAST = ORG_NPERC, PERC_4TH_SLOW, ORG_NPERC_ALL };   /* 4TH (2'): the SMK-37's panel only */
#define PERC_IS_SLOW(p) ((p) == PERC_2ND_SLOW || (p) == PERC_3RD_SLOW || (p) == PERC_4TH_SLOW)
enum { REV_SPRING, REV_STAGE, REV_HALL, REV_CATHEDRAL, ORG_NREV };
enum { LESLIE_SLOW, LESLIE_BRAKE, LESLIE_FAST };

#define ORG_NDB 9u                 /* drawbars of a manual */
#define ORG_NDB_PEDAL 4u           /* registers of the pedal */

/* what the main loop sets (the interrupt reads) */
typedef struct {
    volatile uint8_t model;                         /* MODEL_... */
    volatile uint8_t drawbar[ORG_NMAN][ORG_NDB];    /* 0..8; the pedal uses [MAN_PEDAL][0..3] */
    volatile uint8_t key[ORG_NMAN][ORG_NGEN];       /* gate per manual per key (MIDI note - 24), 0 = up */
    volatile uint32_t key_seq;                      /* +1 after every write to key[][] (org_key_set): the interrupt
                                                       scans the keys only when it moved */
    volatile uint8_t coupler;                       /* II/I: the upper keys also play the lower registration */
    volatile uint8_t boost;                         /* momentary tutti: coupler on and +2 dB */
    volatile uint8_t character;                     /* 0..127: chiff (pipe), musette width (reed) */
    volatile uint8_t perc;                          /* PERC_... (tonewheel upper manual) */
    volatile uint8_t perc_soft;                     /* 1: the percussion softer (-6 dB; the SMK-37's panel) */
    volatile uint16_t perc_q15[ORG_NPERC_ALL];      /* the percussion's decay per control tick, per PERC_... (Q15;
                                                       0: the classic ~200 ms fast / ~1 s slow; the SMK-37's panel) */
    volatile uint8_t leslie;                        /* LESLIE_...: the speed */
    volatile uint8_t rev_mode;                      /* REV_... */
    volatile uint8_t delay_time, delay_fb;          /* 0..127: 20..1000 ms, feedback */
    /* per manual (bus): what its sound goes through */
    volatile uint8_t drive[ORG_NMAN];               /* 0..127: the tube */
    volatile uint8_t vib[ORG_NMAN];                 /* VIB_...: scanner vibrato / chorus (combo organs) */
    volatile uint8_t trem[ORG_NMAN];                /* 0..127: tremulant depth (pipes, reeds) */
    volatile uint8_t les_on[ORG_NMAN];              /* 1: through the rotary */
    volatile uint8_t dsend[ORG_NMAN], rsend[ORG_NMAN];  /* 0..127: delay and reverb send */
    volatile uint8_t expression;                    /* 0..127: CC 11 / bellows (127 = full) */
    volatile int32_t bend;                          /* -8192..8191, +-2 semitones */
    volatile uint8_t bend_all;                      /* 1: every model bends; 0: the free reeds only */
    volatile uint8_t bend_range;                    /* semitones a full bend; 0: the +-2 of bend_all (the FM-1's);
                                                       set: every model, the wheels' bands follow, up to +-36 */
    volatile uint8_t mute;                          /* local audio off (EXTERNAL ONLY) */
    volatile uint32_t panic_seq;                    /* +1 = panic: envelopes, delay and reverb to silence at once */
} org_params_t;

/* what the interrupt reports (the main loop reads) */
typedef struct {
    volatile uint32_t horn_ph, drum_ph;             /* rotor angles, for the screen */
    volatile uint32_t horn_inc;                     /* rotor speed now */
    volatile uint32_t active_gens;                  /* generator layers rendered in the last block */
    volatile uint32_t held;                         /* keys held over all manuals */
    volatile uint32_t peak;                         /* output peak of the last block (0..32767) */
    volatile uint32_t load_us;                      /* time app_audio took (firmware only), budget 5805 */
} org_stat_t;

static org_params_t org_par;
static org_stat_t org_stat;

typedef struct {
    const char *name;           /* long, for the screen */
    const char *tag;            /* short, for the top bar */
    uint8_t dept;               /* DEPT_... */
    uint8_t wave[5];            /* wavetable of each layer: 0..2 centre, 3 detuned up, 4 detuned down */
    uint16_t attack_ms, release_ms;
    uint8_t detune;             /* cents of the detuned ranks (reed: musette; pipe: the celeste) */
    uint8_t rev_default;        /* REV_... chosen when the model is selected */
    uint8_t rev_mix_default;
} org_model_t;

static const org_model_t ORG_MODELS[ORG_NMODEL] = {
    {"TONEWHEEL ORGAN", "WHEEL", DEPT_VINTAGE, {WT_TONEWHEEL, WT_TONEWHEEL, WT_TONEWHEEL, WT_TONEWHEEL, WT_TONEWHEEL}, 1, 2, 0, REV_SPRING, 24},
    {"BRIT COMBO", "BRIT", DEPT_VINTAGE, {WT_VOX, WT_VOX, WT_VOX, WT_VOX, WT_VOX}, 1, 3, 0, REV_SPRING, 20},
    {"ITALO COMBO", "ITALO", DEPT_VINTAGE, {WT_FARFISA, WT_FARFISA, WT_FARFISA, WT_FARFISA, WT_FARFISA}, 2, 10, 0, REV_SPRING, 24},
    {"TOKYO COMBO", "TOKYO", DEPT_VINTAGE, {WT_YAMAHA_YC, WT_YAMAHA_YC, WT_YAMAHA_YC, WT_YAMAHA_YC, WT_YAMAHA_YC}, 2, 12, 0, REV_STAGE, 24},
    {"BAROQUE ORGAN", "BAROQ", DEPT_PIPE, {WT_PIPE_PRINCIPAL, WT_PIPE_FLUTE, WT_PIPE_STRING_REED, WT_PIPE_STRING_REED, WT_PIPE_STRING_REED}, 25, 90, 7, REV_HALL, 48},
    {"CATHEDRAL ORGAN", "CATH", DEPT_PIPE, {WT_PIPE_PRINCIPAL, WT_PIPE_FLUTE, WT_PIPE_STRING_REED, WT_PIPE_STRING_REED, WT_PIPE_STRING_REED}, 45, 160, 8, REV_CATHEDRAL, 70},
    {"PARIS MUSETTE", "MUSET", DEPT_REED, {WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED}, 30, 50, 14, REV_STAGE, 30},
    {"TANGO BANDONEON", "BANDO", DEPT_REED, {WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED}, 40, 60, 6, REV_STAGE, 30},
};

/* the drawbar labels of each department, for the screen */
static const char *const DB_LABEL[3][ORG_NDB] = {
    {"16", "5'", "8", "4", "2'", "2", "1'", "1'", "1"},
    {"BRD16", "PRN8", "ROHR8", "GMB8", "OKT4", "SPZ4", "SUP2", "MIX", "TRP8"},
    {"BSN16", "5TH", "CLAR", "VIOL", "MUS", "PICC", "BAND", "SHEEN", "AIR"},
};
static const char *const DB_FOOT[3][ORG_NDB] = {
    {"16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'"},
    {"Bourdon 16'", "Prinzipal 8'", "Rohrfloete 8'", "Gambe/Celeste 8'", "Oktave 4'", "Spitzfl 4'+Nasat",
     "Superokt 2'+Terz", "Mixtur IV", "Trompete 8'"},
    {"Bassoon 16'", "Sub-Fifth 5 1/3'", "Clarinet 8'", "Violin 8' +", "Musette 8' -", "Piccolo 4'",
     "Bando 4' det", "Sheen 2 2/3'+2'", "Valve & Air"},
};
static const char *const PEDAL_LABEL[ORG_NDB_PEDAL] = {"SUB32", "BRD16", "OCT8", "POS16"};

static void org_init(void);
static void org_audio(int32_t *out, uint32_t n);
static void org_all_off(void);
