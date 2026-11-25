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
       MODEL_BANDONEON, ORG_NMODEL1,                /* the models of OrGigs 1 (the store keeps a row for each) */
       MODEL_OSAKA = ORG_NMODEL1, MODEL_CASTELLANO, MODEL_DUTCH, MODEL_TROSSINGEN, MODEL_STEIRISCHE, MODEL_RENAISSANCE, MODEL_ROMANTIC,
       MODEL_PLAYROOM, ORG_NMODEL };
enum { DEPT_VINTAGE, DEPT_PIPE, DEPT_REED };
enum { VIB_OFF, VIB_V1, VIB_V2, VIB_V3, VIB_C1, VIB_C2, VIB_C3, ORG_NVIB };
enum { PERC_OFF, PERC_2ND_FAST, PERC_2ND_SLOW, PERC_3RD_FAST, PERC_3RD_SLOW, ORG_NPERC,
       PERC_4TH_FAST = ORG_NPERC, PERC_4TH_SLOW, ORG_NPERC_ALL };   /* 4TH (2'): the SMK-37's panel only */
#define PERC_IS_SLOW(p) ((p) == PERC_2ND_SLOW || (p) == PERC_3RD_SLOW || (p) == PERC_4TH_SLOW)
enum { REV_SPRING, REV_STAGE, REV_HALL, REV_CATHEDRAL, ORG_NREV };
enum { LESLIE_SLOW, LESLIE_BRAKE, LESLIE_FAST };
enum { KNOB_DEPT, KNOB_TONE, KNOB_AMP, KNOB_ACTION, KNOB_FAN };   /* what the character knob is: the department's, a tone
                                                         filter, the valve amp, the pneumatic action's delay, the fan's speed */
enum { VOICE_DEPT, VOICE_CASTELLANO, VOICE_TROSSINGEN, VOICE_STEIRISCHE, VOICE_RENAISSANCE, VOICE_ROMANTIC,
       VOICE_BAROQUE, VOICE_CATHEDRAL, ORG_NVOICE };   /* drawbar maps:
                                                                                         the department's, or the model's own */

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
    volatile uint8_t equal;                         /* 1: equal temperament for every model (a historic one off) */
    volatile uint8_t roller;                        /* 0..127: the crescendo roller (the romantic organ), from the
                                                       swell pedal or CC 11 only: 0 without one */
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
    uint16_t trim_q8;           /* vintage: the level after the cabinet, by the wave's crest */
    uint8_t knob;               /* KNOB_... */
    uint8_t voice;              /* VOICE_...: the drawbars' and the pedal registers' meaning */
    uint16_t ped4_release_ms;   /* the release while pedal register 4 is drawn (0: the model's) */
    const uint32_t *temper;     /* a historic temperament's increments (0: equal, with the micro-detune) */
} org_model_t;

static const org_model_t ORG_MODELS[ORG_NMODEL] = {
    {"TONEWHEEL ORGAN", "WHEEL", DEPT_VINTAGE, {WT_TONEWHEEL, WT_TONEWHEEL, WT_TONEWHEEL, WT_TONEWHEEL, WT_TONEWHEEL}, 1, 2, 0, REV_SPRING, 24, 256, KNOB_DEPT, VOICE_DEPT, 350, 0},
    {"BRIT COMBO", "BRIT", DEPT_VINTAGE, {WT_VOX, WT_VOX, WT_VOX, WT_VOX, WT_VOX}, 1, 3, 0, REV_SPRING, 20, 448, KNOB_DEPT, VOICE_DEPT, 350, 0},
    {"ITALO COMBO", "ITALO", DEPT_VINTAGE, {WT_FARFISA, WT_FARFISA, WT_FARFISA, WT_FARFISA, WT_FARFISA}, 2, 10, 0, REV_SPRING, 24, 371, KNOB_DEPT, VOICE_DEPT, 350, 0},
    {"TOKYO COMBO", "TOKYO", DEPT_VINTAGE, {WT_YAMAHA_YC, WT_YAMAHA_YC, WT_YAMAHA_YC, WT_YAMAHA_YC, WT_YAMAHA_YC}, 2, 12, 0, REV_STAGE, 24, 282, KNOB_DEPT, VOICE_DEPT, 350, 0},
    {"BAROQUE ORGAN", "BAROQ", DEPT_PIPE, {WT_PIPE_PRINCIPAL, WT_PIPE_FLUTE, WT_PIPE_STRING_REED, WT_PIPE_REGAL, WT_PIPE_REGAL}, 20, 45, 1, REV_HALL, 48, 256, KNOB_DEPT, VOICE_BAROQUE, 350, INC_WERCKMEISTER},
    {"CATHEDRAL ORGAN", "CATH", DEPT_PIPE, {WT_PIPE_PRINCIPAL, WT_PIPE_FLUTE, WT_PIPE_ANCHE, WT_PIPE_STRING_REED, WT_PIPE_STRING_REED}, 40, 80, 2, REV_CATHEDRAL, 70, 256, KNOB_DEPT, VOICE_CATHEDRAL, 350, 0},
    {"PARIS MUSETTE", "MUSET", DEPT_REED, {WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED}, 30, 50, 14, REV_STAGE, 30, 256, KNOB_DEPT, VOICE_DEPT, 350, 0},
    {"TANGO BANDONEON", "BANDO", DEPT_REED, {WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED, WT_ACCORDION_REED}, 40, 60, 6, REV_STAGE, 30, 256, KNOB_DEPT, VOICE_DEPT, 350, 0},
    /* OrGigs 2 */
    {"OSAKA KING", "OSAKA", DEPT_VINTAGE, {WT_OSAKA, WT_OSAKA, WT_OSAKA, WT_OSAKA, WT_OSAKA}, 2, 8, 0, REV_SPRING, 24, 294, KNOB_DEPT, VOICE_DEPT, 350, 0},
    {"IL CASTELLANO", "CASTL", DEPT_VINTAGE, {WT_CASTELLANO, WT_CASTELLANO, WT_CASTELLANO, WT_CASTELLANO, WT_CASTELLANO}, 2, 10, 0, REV_SPRING, 24, 533, KNOB_TONE, VOICE_CASTELLANO, 900, 0},
    {"DUTCH ROCK", "DUTCH", DEPT_VINTAGE, {WT_DUTCH, WT_DUTCH, WT_DUTCH, WT_DUTCH, WT_DUTCH}, 3, 14, 0, REV_SPRING, 44, 170, KNOB_AMP, VOICE_DEPT, 350, 0},
    {"TROSSINGEN BASS", "TROSS", DEPT_REED, {WT_REED_DRY, WT_REED_DRY, WT_REED_DRY, WT_REED_DRY, WT_REED_DRY}, 30, 45, 5, REV_STAGE, 26, 256, KNOB_DEPT, VOICE_TROSSINGEN, 0, 0},
    {"STEIRISCHE", "STEIR", DEPT_REED, {WT_REED_DRY, WT_HELIKON, WT_REED_DRY, WT_REED_DRY, WT_REED_DRY}, 22, 35, 16, REV_STAGE, 22, 256, KNOB_DEPT, VOICE_STEIRISCHE, 0, 0},
    /* the Italian renaissance organ: the ripieno, a voce umana, meantone; layer 1 the flutes, 3 the voce umana */
    {"RENAISSANCE ORGAN", "RENAI", DEPT_PIPE, {WT_PIPE_RENAIS, WT_PIPE_FLUTE, WT_PIPE_RENAIS, WT_PIPE_RENAIS, WT_PIPE_RENAIS}, 45, 55, 0, REV_HALL, 40, 256, KNOB_DEPT, VOICE_RENAISSANCE, 0, INC_MEANTONE},
    /* the German romantic organ: many dark 8' that melt (two strings on their own phases, a cent apart), a crescendo
       roller and a gentle swell on the pedal, a pneumatic action; layer 1 the flutes, 2 the oboe, 3 / 4 the strings */
    {"ROMANTIC ORGAN", "ROMAN", DEPT_PIPE, {WT_PIPE_ROMANTIC, WT_PIPE_FLUTE, WT_PIPE_REED_SOFT, WT_PIPE_STRING_SOFT, WT_PIPE_STRING_SOFT}, 90, 120, 1, REV_CATHEDRAL, 60, 256, KNOB_ACTION, VOICE_ROMANTIC, 0, 0},
    /* the Playroom (a toy chord organ of the 70s): plastic free reeds blown by a small fan, a few cents off each, through
       a plastic speaker; the wind sags as keys go down, the knob is the fan's speed */
    {"PLAYROOM ORGAN", "PLAY", DEPT_VINTAGE, {WT_TOY_REED, WT_TOY_REED, WT_TOY_REED, WT_TOY_REED, WT_TOY_REED}, 35, 70, 0, REV_STAGE, 18, 1400, KNOB_FAN, VOICE_DEPT, 350, INC_TOY},
};

/* the order the models are chosen and numbered in: by department, each one's newer models after its first ones. The
   setups keep the model's own number (MODEL_...), which OrGigs 1 reads too */
static const uint8_t MODEL_ORDER[ORG_NMODEL] = {
    MODEL_B3, MODEL_VOX, MODEL_FARFISA, MODEL_YC, MODEL_OSAKA, MODEL_CASTELLANO, MODEL_DUTCH, MODEL_PLAYROOM,
    MODEL_BAROQUE, MODEL_CATHEDRAL, MODEL_RENAISSANCE, MODEL_ROMANTIC,
    MODEL_MUSETTE, MODEL_BANDONEON, MODEL_TROSSINGEN, MODEL_STEIRISCHE,
};
static uint32_t org_model_pos(uint32_t model)          /* where a model stands in that order, 0.. */
{
    uint32_t i;
    for (i = 0; i < ORG_NMODEL; i++) if (MODEL_ORDER[i] == model) return i;
    return 0;
}

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
/* the models with their own drawbar meaning (VOICE_...; NULL: the department's) */
static const char *const VOICE_FOOT[ORG_NVOICE][ORG_NDB] = {
    {0},
    {"16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "Mixture IV"},
    {0},
    {0},
    {"Principale 8'", "Voce umana 8'", "Ottava 4'", "Flauto in VIII 4'", "Flauto in XII", "XV 2'", "XIX 1 1/3'", "XXII 1'",
     "XXVI + XXIX"},
    {"Bourdon 16'", "Prinzipal 8'", "Gedackt 8'", "Salicional 8'", "Aeoline 8'", "Oktave 4'", "Fugara 4'", "Mixtur III",
     "Oboe 8'"},
    {"Quintadena 16'", "Prinzipal 8'", "Rohrfloete 8'", "Oktave 4'", "Oktave 2'", "Sesquialtera II", "Mixtur IV",
     "Regal 8'", "Trompete 8'"},
    {"Bourdon 16'", "Montre 8'", "Flute harmon 8'", "Gambe 8'", "Voix celeste 8'", "Prestant 4'", "Plein-jeu IV",
     "Trompette 8'", "Clairon 4'"},
};
static const char *const VOICE_PEDAL[ORG_NVOICE][ORG_NDB_PEDAL] = {
    {0},
    {"BASS16", "BASS8", "BASS4", "SUSTAIN"},
    {"BASS16", "BASS8", "BASS4", "BASS2"},
    {"HELI16", "HELI8", "BASS8", "BASS4"},
    {"CTRB16", "PRIN16", "OTTV8", "XV4"},
    {"SUB16", "VIOL16", "OKT8", "POS16"},
    {"PRIN16", "SUB16", "OKT8", "POS16"},
    {"SOUB32", "CTRB16", "FLUT8", "BOMB16"},
};
static const char *org_db_foot(uint32_t model, uint32_t d)   /* drawbar d of a manual, for the message line */
{
    const org_model_t *md = &ORG_MODELS[model < ORG_NMODEL ? model : 0u];
    return VOICE_FOOT[md->voice][d] ? VOICE_FOOT[md->voice][d] : DB_FOOT[md->dept][d];
}
static const char *org_ped_label(uint32_t model, uint32_t d)
{
    const org_model_t *md = &ORG_MODELS[model < ORG_NMODEL ? model : 0u];
    return VOICE_PEDAL[md->voice][d] ? VOICE_PEDAL[md->voice][d] : PEDAL_LABEL[d];
}

static void org_init(void);
static void org_audio(int32_t *out, uint32_t n);
static void org_all_off(void);
