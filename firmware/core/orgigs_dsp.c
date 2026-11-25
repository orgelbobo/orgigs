/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs FM-1: the organ engine (runs in the audio interrupt).
 *
 *   84 free-running generators (MIDI 24..107, C1..B7), never stolen, never restarted: a key only opens
 *   a gate onto generators that are always turning. Each generator has three phases: the centre one
 *   (equal tempered, or with the +-1.2 cent micro-detune of real pipes and reeds), and two detuned ones
 *   (phase_detune_up/down) for the musette and celeste beating.
 *
 *   3 divisions (upper, lower, pedal) tap the same generators. The "bus summing" is done per generator
 *   and division: every 32 frames each held key adds env x smoothed drawbar x footage gain to its
 *   division's weight of the generators its drawbars reach; the sample loop is then one multiply-add per
 *   generator, layer and division that sounds, whatever the number of keys (zero voice stealing, the cost
 *   follows the generators, not the keys). Each division comes out on its own bus: the effects treat the
 *   manuals apart (drive, vibrato, rotary, sends).
 *   Weights ramp linearly over the 32 frames: no zipper, no clicks.
 *
 *   15 models in 3 departments, each with its own wavetables, drawbar meaning, attack and release; a few
 *   (VOICE_...) bring their own drawbar or pedal registers.
 *
 * Integers only: wavetables Q14, envelopes Q15, drawbar gains Q12, phases 32-bit. */
#include "orgigs_dsp.h"

#ifndef ORG_POOL
#define ORG_POOL __attribute__((section(".pool")))
#endif

#define SUB 32u                    /* control-rate sub-block: 0.73 ms */
#define NLAYER 5u                  /* 0..2 centre phase (three tables), 3 detuned up, 4 detuned down */
#define W_MAX 16383                /* weight ceiling per generator and layer */

/* ---- the generator matrix */
static uint32_t phase[ORG_NGEN], phase_inc[ORG_NGEN];
static uint32_t phase_detune_up[ORG_NGEN], phase_detune_down[ORG_NGEN];
static uint32_t inc_detune_up[ORG_NGEN], inc_detune_down[ORG_NGEN];
static int16_t wavetable[ORG_NWAVE][ORG_NBANDS][ORG_WT_LEN] ORG_POOL;   /* 64 KB, copied from flash at boot */
static const int16_t *wt_ptr[NLAYER][ORG_NBANDS];

/* ---- weights per division (bus): target (this sub-block) and current (where the ramp starts) */
static int32_t w_tgt[ORG_NMAN][NLAYER][ORG_NGEN], w_cur[ORG_NMAN][NLAYER][ORG_NGEN];

/* ---- what the interrupt can skip: only generators with a weight are rendered; the phases of the others
 * are caught up when they come back (gen_clock - gen_stamp samples at their increment: they stay free
 * running). w_tgt is non-zero only for the generators in touched_list. */
static uint32_t gen_clock, gen_stamp[ORG_NGEN];
static uint8_t gen_touched[ORG_NGEN], gen_live[ORG_NGEN], touched_list[ORG_NGEN];
static uint32_t touched_n;
static uint32_t key_seq_seen = 0xFFFFFFFFu, env_moving = 1u, wt_state = 0xFFFFFFFFu;

/* ---- divisions */
static int32_t env[ORG_NMAN][ORG_NGEN];          /* per key, Q15 */
static uint8_t gate_prev[ORG_NMAN][ORG_NGEN];
static uint8_t lag[ORG_NMAN][ORG_NGEN];        /* the pneumatic action: control ticks still to wait before the pipe speaks */
static int32_t smooth_drawbar[ORG_NMAN][ORG_NDB];   /* Q12, glides to DRAWBAR_Q12[level] */
#define smooth_drawbar_upper smooth_drawbar[MAN_UPPER]
#define smooth_drawbar_lower smooth_drawbar[MAN_LOWER]
#define smooth_drawbar_pedal smooth_drawbar[MAN_PEDAL]
static uint32_t held_now, upper_held;

/* ---- transients and noises */
static uint32_t org_rng_state = 0x2545F491u;
static int32_t click_env, chiff_env, valve_env, perc_env, air_lvl;
static int32_t click_lo, click_bp, chiff_lo, chiff_bp, air_lo, air_bp;
static uint32_t chiff_f = 12000u;
static uint32_t noise_bus;                       /* the bus the last key event's noise goes to */
static uint32_t perc_decay_q15 = 31956u;

/* ---- pitch */
static uint32_t pitch_q16 = 65536u, pitch_applied, cents_applied = 999u, sag_q16, equal_applied;
/* the Playroom's fan: the wind's level (Q15, the effects read it), its motor's flutter on the pitch, its hum */
static int32_t wind_q15 = 32768, motor_wob, hum_lvl;
static uint32_t motor_ph, motor_tick, hum_ph;
/* a bend_range set (up to +-3 octaves): each wheel takes the band of the pitch it sounds, and fades out between 12 and
   18 kHz (its harmonics would fold back) */
static uint8_t gen_band[ORG_NGEN];
static uint16_t gen_gain[ORG_NGEN];     /* Q15 (32768: full) */
static const uint32_t SEMI_Q16[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715};
#define GAIN_INC_LO 1168723054u         /* 12 kHz */
#define GAIN_INC_HI 1753084581u         /* 18 kHz */
static uint8_t model_applied = 0xFFu;

static uint32_t org_rand(void) { org_rng_state = org_rng_state * 1664525u + 1013904223u; return org_rng_state; }

static int32_t org_sin(uint32_t ph)          /* a full cycle over 2^32 -> -32767..32767 */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

/* ---- what each drawbar reaches: up to four (footage offset in semitones, layer, gain Q7) */
typedef struct { int8_t off; uint8_t layer, gain; } comp_t;
typedef struct { uint8_t n; comp_t c[4]; } dbmap_t;

static const dbmap_t MAP_VINTAGE[ORG_NDB] = {     /* 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' */
    {1, {{-12, 0, 128}}}, {1, {{7, 0, 128}}}, {1, {{0, 0, 128}}}, {1, {{12, 0, 128}}}, {1, {{19, 0, 128}}},
    {1, {{24, 0, 128}}}, {1, {{28, 0, 128}}}, {1, {{31, 0, 128}}}, {1, {{36, 0, 128}}},
};
static const dbmap_t MAP_PIPE[ORG_NDB] = {        /* layer 0 principal, 1 flute, 2 string/reed, 3 celeste */
    {1, {{-12, 1, 128}}},                                       /* Bourdon 16' */
    {1, {{0, 0, 128}}},                                         /* Prinzipal 8' */
    {1, {{0, 1, 115}}},                                         /* Rohrfloete 8' */
    {2, {{0, 2, 80}, {0, 3, 80}}},                              /* Gambe + Vox Celeste 8' */
    {1, {{12, 0, 110}}},                                        /* Oktave 4' */
    {2, {{12, 1, 100}, {19, 1, 70}}},                           /* Spitzfloete 4' + Nasat 2 2/3' */
    {2, {{24, 0, 90}, {28, 1, 55}}},                            /* Superoktave 2' + Terz 1 3/5' */
    {4, {{31, 0, 70}, {36, 0, 64}, {43, 0, 56}, {48, 0, 50}}},  /* Mixtur IV: 1 1/3' 1' 2/3' 1/2' */
    {1, {{0, 2, 128}}},                                         /* Trompete 8' */
};
static const dbmap_t MAP_REED[ORG_NDB] = {        /* layer 0 centre, 3 up, 4 down */
    {1, {{-12, 0, 128}}},                                       /* Bassoon 16' */
    {1, {{7, 0, 70}}},                                          /* Sub-Fifth 5 1/3' */
    {1, {{0, 0, 128}}},                                         /* Clarinet 8' centre */
    {1, {{0, 3, 120}}},                                         /* Violin 8' + detune */
    {1, {{0, 4, 120}}},                                         /* Musette 8' - detune */
    {1, {{12, 0, 110}}},                                        /* Piccolo 4' */
    {1, {{12, 3, 100}}},                                        /* Bando 4' detuned */
    {2, {{19, 0, 60}, {24, 0, 60}}},                            /* Harmonic sheen 2 2/3' + 2' */
    {0, {{0, 0, 0}}},                                           /* valve click + bellows air (noise) */
};
static const dbmap_t MAP_PEDAL[3][ORG_NDB_PEDAL] = { /* Subbass 32', Bourdon 16', Octave bass 8', Posaune 16' */
    {{1, {{-24, 0, 128}}}, {1, {{-12, 0, 128}}}, {1, {{0, 0, 110}}}, {2, {{-12, 0, 100}, {0, 0, 60}}}},
    {{1, {{-24, 1, 128}}}, {1, {{-12, 1, 128}}}, {1, {{0, 0, 110}}}, {1, {{-12, 2, 120}}}},
    {{1, {{-24, 0, 110}}}, {1, {{-12, 0, 128}}}, {1, {{0, 0, 110}}}, {1, {{-12, 4, 110}}}},
};
/* OrGigs 2: the models' own registers (VOICE_...) */
static const dbmap_t MAP_CASTELLANO[ORG_NDB] = {  /* the vintage footages; drawbar 9 the mixture */
    {1, {{-12, 0, 128}}}, {1, {{7, 0, 128}}}, {1, {{0, 0, 128}}}, {1, {{12, 0, 128}}}, {1, {{19, 0, 128}}},
    {1, {{24, 0, 128}}}, {1, {{28, 0, 128}}}, {1, {{31, 0, 128}}},
    {4, {{19, 0, 70}, {24, 0, 64}, {28, 0, 56}, {31, 0, 50}}},  /* Mixture IV: 2 2/3' 2' 1 3/5' 1 1/3' */
};
static const dbmap_t MAP_RENAISSANCE[ORG_NDB] = { /* the ripieno: layer 0 principal, 1 flute, 3 the voce umana (+2 Hz) */
    {1, {{0, 0, 128}}},                                         /* Principale 8' */
    {1, {{0, 3, 110}}},                                         /* Voce umana 8' (from c': VU_KEY0) */
    {1, {{12, 0, 110}}},                                        /* Ottava 4' */
    {1, {{12, 1, 105}}},                                        /* Flauto in VIII 4' */
    {1, {{19, 1, 85}}},                                         /* Flauto in XII 2 2/3' */
    {1, {{24, 0, 95}}},                                         /* Decimaquinta 2' */
    {1, {{31, 0, 85}}},                                         /* Decimanona 1 1/3' */
    {1, {{36, 0, 78}}},                                         /* Vigesimaseconda 1' */
    {2, {{43, 0, 70}, {48, 0, 64}}},                            /* Vigesimasesta 2/3' + Vigesimanona 1/2' */
};
static const dbmap_t MAP_ROMANTIC[ORG_NDB] = {   /* layer 0 principal, 1 flute, 2 oboe, 3 / 4 the strings (+-1 cent) */
    {1, {{-12, 1, 120}}},                                       /* Bourdon 16' */
    {1, {{0, 0, 128}}},                                         /* Prinzipal 8' */
    {1, {{0, 1, 110}}},                                         /* Gedackt 8' */
    {1, {{0, 3, 100}}},                                         /* Salicional 8' */
    {1, {{0, 4, 80}}},                                          /* Aeoline 8' */
    {1, {{12, 0, 100}}},                                        /* Oktave 4' */
    {1, {{12, 3, 80}}},                                         /* Fugara 4' */
    {3, {{19, 0, 60}, {24, 0, 55}, {31, 0, 45}}},               /* Mixtur III, dark: 2 2/3' 2' 1 1/3' */
    {1, {{0, 2, 110}}},                                         /* Oboe 8' */
};
static const dbmap_t MAP_BAROQUE[ORG_NDB] = {   /* the Werkprinzip: layer 0 principal, 1 flute, 2 trumpet, 3 regal */
    {2, {{-12, 1, 110}, {7, 1, 40}}},                           /* Quintadena 16' (its twelfth speaks) */
    {1, {{0, 0, 128}}},                                         /* Prinzipal 8' */
    {1, {{0, 1, 115}}},                                         /* Rohrfloete 8' */
    {1, {{12, 0, 110}}},                                        /* Oktave 4' */
    {1, {{24, 0, 95}}},                                         /* Oktave 2' */
    {2, {{19, 1, 75}, {28, 1, 65}}},                            /* Sesquialtera II: 2 2/3' + 1 3/5' */
    {4, {{31, 0, 70}, {36, 0, 62}, {43, 0, 55}, {48, 0, 48}}},  /* Mixtur IV, breaking back (MIX_TOP) */
    {1, {{0, 3, 105}}},                                         /* Regal 8' */
    {1, {{0, 2, 128}}},                                         /* Trompete 8' */
};
static const dbmap_t MAP_CATHEDRAL[ORG_NDB] = { /* layer 0 principal, 1 flute, 2 the chorus reeds, 3 celeste (+), 4 gambe (-) */
    {1, {{-12, 1, 120}}},                                       /* Bourdon 16' */
    {1, {{0, 0, 128}}},                                         /* Montre 8' */
    {2, {{0, 1, 118}, {12, 1, 48}}},                            /* Flute harmonique 8': the strong octave (louder from c') */
    {1, {{0, 4, 95}}},                                          /* Gambe 8' */
    {1, {{0, 3, 85}}},                                          /* Voix celeste 8' */
    {1, {{12, 0, 105}}},                                        /* Prestant 4' */
    {4, {{24, 0, 80}, {31, 0, 66}, {36, 0, 58}, {43, 0, 50}}},  /* Plein-jeu IV, breaking back */
    {1, {{0, 2, 120}}},                                         /* Trompette 8' */
    {1, {{12, 2, 100}}},                                        /* Clairon 4' */
};
#define MIX_DB 6u                       /* the mixture drawbar of the baroque and the cathedral organ */
#define MIX_TOP 74u                     /* its ranks break back an octave above MIDI 98 (D7), as mixtures repeat */
/* the crescendo roller: where each register comes in over the roller's 0..127 (from, full); strings first, then flutes,
   principals and the 16', the 4' and the oboe, the mixture and the trombone last. It adds to what is drawn */
static const uint8_t ROLL_MAN[ORG_NDB][2] = {{40, 58}, {32, 52}, {26, 45}, {8, 28}, {0, 20}, {52, 72}, {58, 78}, {90, 115}, {66, 88}};
static const uint8_t ROLL_PED[ORG_NDB_PEDAL][2] = {{20, 40}, {45, 65}, {60, 80}, {92, 118}};
static uint32_t roll_level(uint32_t m, uint32_t d)     /* a drawbar's 0..8 from the roller */
{
    const uint8_t *r = m == MAN_PEDAL ? ROLL_PED[d] : ROLL_MAN[d];
    uint32_t x = org_par.roller;
    if (x <= r[0]) return 0;
    if (x >= r[1]) return 8;
    return (x - r[0]) * 8u / (r[1] - r[0]);
}
#define VU_KEY0 36u                     /* the voce umana speaks from c' (MIDI 60) up, as it is built */
static const dbmap_t MAP_PEDAL_VOICE[ORG_NVOICE][ORG_NDB_PEDAL] = {
    {{0}},
    {{1, {{-12, 0, 128}}}, {1, {{0, 0, 128}}}, {1, {{12, 0, 100}}}, {2, {{-12, 0, 90}, {0, 0, 60}}}},   /* bass section; 4 sustains */
    {{1, {{-12, 0, 128}}}, {1, {{0, 0, 120}}}, {1, {{12, 0, 100}}}, {1, {{24, 0, 80}}}},              /* the bass reeds, 16' to 2' */
    {{1, {{-12, 1, 128}}}, {1, {{0, 1, 120}}}, {1, {{0, 0, 110}}}, {1, {{12, 0, 90}}}},               /* helicons (layer 1), reeds */
    {{1, {{-12, 1, 128}}}, {1, {{-12, 0, 120}}}, {1, {{0, 0, 110}}}, {1, {{12, 0, 90}}}},             /* contrabassi, principale */
    {{1, {{-12, 1, 128}}}, {1, {{-12, 3, 110}}}, {1, {{0, 0, 110}}}, {1, {{-12, 2, 115}}}},            /* subbass, violon, oboe-posaune */
    {{1, {{-12, 0, 128}}}, {1, {{-12, 1, 128}}}, {1, {{0, 0, 110}}}, {1, {{-12, 2, 120}}}},             /* prinzipal, subbass, posaune */
    {{1, {{-24, 1, 128}}}, {1, {{-12, 0, 120}}}, {2, {{0, 1, 110}, {12, 1, 40}}}, {1, {{-12, 2, 115}}}},   /* soubasse 32', bombarde */
};
static const dbmap_t *const MAP_VOICE[ORG_NVOICE] = {0, MAP_CASTELLANO, 0, 0, MAP_RENAISSANCE, MAP_ROMANTIC,
                                                   MAP_BAROQUE, MAP_CATHEDRAL};

static const uint8_t MAN_GAIN_Q7[ORG_NMAN] = {128, 110, 140};

#include "orgigs_fx.c"

/* ---- set up: the tables into RAM, the generators at random phases (as tonewheels are) */
static void org_init(void)
{
    uint32_t w, b, i, g;
    for (w = 0; w < ORG_NWAVE; w++)
        for (b = 0; b < ORG_NBANDS; b++)
            for (i = 0; i < ORG_WT_LEN; i++)
                wavetable[w][b][i] = WT_ROM[w][b][i];
    for (g = 0; g < ORG_NGEN; g++) {
        phase[g] = org_rand();
        phase_detune_up[g] = org_rand();
        phase_detune_down[g] = org_rand();
        phase_inc[g] = INC_ET[g];
        gen_band[g] = GEN_BAND[g];
        gen_gain[g] = 32768u;
    }
    for (g = 0; g < ORG_NMAN; g++)
        for (i = 0; i < ORG_NDB; i++)
            smooth_drawbar[g][i] = 0;
    fx_init();
}

static void org_key_set(uint32_t m, uint32_t k, uint32_t on)   /* main loop: a key of a division */
{
    org_par.key[m][k] = (uint8_t)on;
    org_par.key_seq++;                          /* after the key: the interrupt that sees the count sees the key */
}

static void org_all_off(void)                  /* main loop: every key up */
{
    uint32_t m, k;
    for (m = 0; m < ORG_NMAN; m++)
        for (k = 0; k < ORG_NGEN; k++)
            org_par.key[m][k] = 0;
    org_par.key_seq++;
}

/* ---- control rate */
static void model_apply(uint32_t model)
{
    const org_model_t *md = &ORG_MODELS[model];
    uint32_t l, b;
    for (l = 0; l < NLAYER; l++)
        for (b = 0; b < ORG_NBANDS; b++)
            wt_ptr[l][b] = wavetable[md->wave[l]][b];
    model_applied = (uint8_t)model;
    cents_applied = 999u;                       /* recompute the increments */
    click_env = chiff_env = valve_env = perc_env = 0;
}

static void gen_catch_up(uint32_t g)
{
    uint32_t d = gen_clock - gen_stamp[g];
    if (!d) return;
    phase[g] += phase_inc[g] * d;
    phase_detune_up[g] += inc_detune_up[g] * d;
    phase_detune_down[g] += inc_detune_down[g] * d;
    gen_stamp[g] = gen_clock;
}

static void pitch_apply(uint32_t model)
{
    const org_model_t *md = &ORG_MODELS[model];
    const uint32_t *src = md->temper && !org_par.equal ? md->temper : md->dept == DEPT_VINTAGE ? INC_ET : INC_MICRO;
    uint32_t g, cents = md->detune, f = pitch_q16;
    if (md->dept == DEPT_REED)                  /* the character knob widens or narrows the musette */
        cents = (cents * (64u + org_par.character)) >> 7;
    if (cents > 22u) cents = 22u;
    for (g = 0; g < ORG_NGEN; g++) {
        uint32_t inc = (uint32_t)(((uint64_t)src[g] * f) >> 16);
        gen_catch_up(g);                        /* the phase so far at the old speed */
        int32_t c = (int32_t)cents + (md->dept == DEPT_REED ? DET_VAR[g] : 0);
        if (c < 0) c = 0;
        phase_inc[g] = inc;
        if (!org_par.bend_range) { gen_band[g] = GEN_BAND[g]; gen_gain[g] = 32768u; }
        else {                                  /* (the band tops: wheels 29, 47, 65, a cent or so above) */
            gen_band[g] = (uint8_t)(inc <= INC_ET[29] + (INC_ET[29] >> 7) ? 0u : inc <= INC_ET[47] + (INC_ET[47] >> 7) ? 1u
                                    : inc <= INC_ET[65] + (INC_ET[65] >> 7) ? 2u : 3u);
            gen_gain[g] = (uint16_t)(inc <= GAIN_INC_LO ? 32768u : inc >= GAIN_INC_HI ? 0u
                                     : (uint32_t)(((uint64_t)(GAIN_INC_HI - inc) << 15) / (GAIN_INC_HI - GAIN_INC_LO)));
        }
        inc_detune_up[g] = md->voice == VOICE_RENAISSANCE ? inc + INC_VOCE_UMANA : inc + (((inc >> 8) * CENT_UP_K[c]) >> 8);
        inc_detune_down[g] = inc - (((inc >> 8) * CENT_DN_K[c]) >> 8);
    }
    pitch_applied = f;
    cents_applied = cents;
    equal_applied = org_par.equal;
}

static uint32_t action_lag(uint32_t k)        /* the pneumatic action: 10..40 ms by the character knob, +-4 ms per key */
{
    int32_t ms = 10 + (int32_t)org_par.character * 30 / 127 + DET_VAR[k] * 2;
    return (uint32_t)(ms * 441 / 320);         /* in control ticks of 32 frames */
}
static uint32_t barker_lag(uint32_t k)        /* the cathedral's Barker lever: 12 ms, +-2 ms per key */
{
    return (uint32_t)((12 + DET_VAR[k]) * 441 / 320);
}

static void note_event(uint32_t m, uint32_t k, uint32_t on, const org_model_t *md)
{
    if (on || m != MAN_PEDAL) noise_bus = m;
    if (md->dept == DEPT_VINTAGE) {             /* key contacts: a click on press and on release */
        int32_t lvl = model_applied == MODEL_B3 ? 9000 : model_applied == MODEL_VOX ? 6500 : 4000;
        if (!on) lvl >>= 1;
        if (click_env < lvl) click_env = lvl;
        if (on && m == MAN_UPPER && model_applied == MODEL_B3 && org_par.perc && !upper_held)
            perc_env = 32767;                   /* single trigger: only from all keys up */
    } else if (md->dept == DEPT_PIPE) {
        if (on) {                               /* wind chiff on the speech of the pipe */
            int32_t lvl = 2500 + (int32_t)org_par.character * 110;
            if (model_applied == MODEL_CATHEDRAL) lvl >>= 2;            /* high, steady wind: a soft breath */
            else if (model_applied == MODEL_RENAISSANCE) lvl >>= 2;   /* low wind: barely a breath */
            else if (model_applied == MODEL_ROMANTIC) lvl = 0;          /* deep nicks: no chiff at all */
            if (chiff_env < lvl) chiff_env = lvl;
            chiff_f = CHIFF_F[k];
        }
    } else {                                    /* the pallet valve of the reed */
        int32_t lvl = (int32_t)org_par.drawbar[m == MAN_PEDAL ? MAN_LOWER : m][8] * 900;
        if (!on) lvl >>= 1;
        if (valve_env < lvl) valve_env = lvl;
    }
}

static void control_update(void)
{
    uint32_t model = org_par.model < ORG_NMODEL ? org_par.model : 0u;
    const org_model_t *md;
    uint32_t m, k, d, c, l, held = 0, up_held = 0;
    int32_t att, rel, rel_ped, f;
    uint32_t coupled = org_par.coupler || org_par.boost;
    uint32_t seq, dirty = 0, state;
    const dbmap_t *map;

    if (model != model_applied) model_apply(model);
    md = &ORG_MODELS[model];

    /* pitch: bend and the wind sag of the baroque organ */
    f = 65536;
    if (org_par.bend_range) {                   /* +-bend_range semitones, every model; in 1/32 semitone */
        int32_t b = org_par.bend, r = org_par.bend_range > 36u ? 36 : org_par.bend_range, q, o, sm;
        b = b < -8192 ? -8192 : b > 8191 ? 8191 : b;
        q = (int32_t)(((int64_t)b * r * 32) / 8192);
        o = (q >> 5) >= 0 ? (q >> 5) / 12 : -((11 - (q >> 5)) / 12);      /* octaves (floor), semitone, 1/32 */
        sm = (q >> 5) - o * 12;
        f = (int32_t)(((uint64_t)SEMI_Q16[sm] * BEND_Q16[64 + (q & 31)]) >> 16);
        f = o >= 0 ? f << o : f >> -o;
    } else if (org_par.bend && (org_par.bend_all || md->dept == DEPT_REED)) {
        int32_t b = org_par.bend + 8192;
        b = b < 0 ? 0 : b > 16383 ? 16383 : b;
        f = (int32_t)BEND_Q16[(b + 64) >> 7];
    }
    f = (int32_t)(((int64_t)f * (65536 - (int32_t)(sag_q16 >> 8) + motor_wob)) >> 16);
    pitch_q16 = (uint32_t)f;
    {
        uint32_t cents = md->dept == DEPT_REED ? (md->detune * (64u + org_par.character)) >> 7 : md->detune;
        if (cents > 22u) cents = 22u;
        if (pitch_q16 != pitch_applied || cents != cents_applied || org_par.equal != equal_applied) pitch_apply(model);
    }

    /* envelopes, and the transients on every key change */
    {                                           /* (the Playroom's reeds speak slower on a slow fan: up to +65 ms) */
        uint32_t ams = md->attack_ms + (model == MODEL_PLAYROOM ? (127u - org_par.character) * 65u / 127u : 0u);
        att = (int32_t)((32767u * SUB) / (ams * 44u)) + 1;
    }
    rel = (int32_t)((32767u * SUB) / (md->release_ms * 44u)) + 1;
    rel_ped = org_par.drawbar[MAN_PEDAL][3] && md->ped4_release_ms   /* Posaune decay, the bass sustain */
            ? (int32_t)((32767u * SUB) / (md->ped4_release_ms * 44u)) + 1 : rel;
    seq = org_par.key_seq;                      /* read before the keys: a later change moves it again */
    if (seq != key_seq_seen || env_moving) {    /* else every envelope sits at 0 or full: nothing to do */
        uint32_t moving = 0;
        key_seq_seen = seq;
        dirty = 1;
        for (k = 0; k < ORG_NGEN; k++)
            if (gate_prev[MAN_UPPER][k]) up_held++;
        upper_held = up_held;
        for (m = 0; m < ORG_NMAN; m++) {
            int32_t r = m == MAN_PEDAL ? rel_ped : rel;
            for (k = 0; k < ORG_NGEN; k++) {
                uint32_t g = org_par.key[m][k] != 0;
                int32_t e = env[m][k];
                if (g != gate_prev[m][k]) {
                    note_event(m, k, g, md);
                    lag[m][k] = !g ? 0u : md->knob == KNOB_ACTION ? action_lag(k) : md->voice == VOICE_CATHEDRAL ? barker_lag(k) : 0u;
                    if (m == MAN_UPPER) upper_held += g ? 1u : (upper_held ? (uint32_t)-1 : 0u);
                    gate_prev[m][k] = (uint8_t)g;
                }
                if (g) {
                    if (lag[m][k]) { lag[m][k]--; moving++; }  /* the pneumatic action: the pipe speaks a little later */
                    else if (e < 32767) { e += att; if (e > 32767) e = 32767; moving++; }
                    held++;
                }
                else if (e) { e -= r; if (e < 0) e = 0; moving++; }
                env[m][k] = e;
            }
        }
        env_moving = moving;
        held_now = held;
        org_stat.held = held;
    }
    held = held_now;

    /* the baroque organ's wedge bellows sag under full chords (> 5 keys): up to 6 cents flat, settling in ~150 ms */
    {
        uint32_t tgt = (model == MODEL_BAROQUE && held > 5u) ? (held - 5u) * 40u : 0u;
        if (tgt > 230u) tgt = 230u;
        if (model == MODEL_PLAYROOM) {          /* the toy's fan: flat by up to 20 cents when slow, 4 more each key past
                                                   two, a quarter tone at most; the wind softer as well (to -6 dB) */
            uint32_t slow = 127u - org_par.character, extra = held > 2u ? held - 2u : 0u;
            int32_t wt = 32768 - (int32_t)(extra * 1300u > 9000u ? 9000u : extra * 1300u) - (int32_t)slow * 55;
            tgt = slow * 760u / 127u + extra * 150u;
            if (tgt > 1900u) tgt = 1900u;
            wind_q15 += (wt - wind_q15) >> 7;
            motor_ph += (4u * 127u + org_par.character * 5u) * 24540u;        /* the motor's flutter: 4..9 Hz, +-2 cents */
            if (!(++motor_tick & 7u)) {
                int32_t x = (int32_t)(motor_ph >> 16), tri = x < 32768 ? x * 2 - 32768 : (65535 - x) * 2 - 32768;
                motor_wob = (tri * 76) >> 15;
            }
            hum_lvl = 90 + (int32_t)org_par.character * 2 + (held ? 220 : 0);
        } else { wind_q15 = 32768; motor_wob = 0; hum_lvl = 0; }
        sag_q16 += (int32_t)((tgt << 8) - sag_q16) >> (model == MODEL_PLAYROOM ? 8 : 7);
    }

    /* percussion decay (fast ~200 ms, slow ~1 s; or as set: 32768 holds) */
    {
        uint32_t p = org_par.perc < ORG_NPERC_ALL ? org_par.perc : 0u, q = org_par.perc_q15[p];
        perc_decay_q15 = q ? q : PERC_IS_SLOW(p) ? 32603u : 31956u;
    }
    if (perc_env) dirty = 1;                    /* (once more after it reaches 0) */
    perc_env = (int32_t)(((uint32_t)perc_env * perc_decay_q15) >> 15);

    /* drawbars glide (~12 ms over the full range) */
    for (m = 0; m < ORG_NMAN; m++)
        for (d = 0; d < (m == MAN_PEDAL ? ORG_NDB_PEDAL : ORG_NDB); d++) {
            uint32_t v = org_par.drawbar[m][d] > 8u ? 8u : org_par.drawbar[m][d];
            int32_t t;
            if (model == MODEL_ROMANTIC && org_par.roller) { uint32_t r = roll_level(m, d); if (r > v) v = r; }   /* the roller */
            t = DRAWBAR_Q12[v];
            int32_t s = smooth_drawbar[m][d];
            if (m == MAN_UPPER && d == 8u && model == MODEL_B3 && org_par.perc) t = 0;   /* the tonewheel takes 1' for percussion */
            if (s < t) { s += 256; if (s > t) s = t; dirty = 1; }
            else if (s > t) { s -= 256; if (s < t) s = t; dirty = 1; }
            smooth_drawbar[m][d] = s;
        }

    /* bellows air: drawbar 9 of the reed models, while keys are held */
    air_lvl = 0;
    if (model == MODEL_PLAYROOM && held) air_lvl = 300 + (int32_t)org_par.character * 4;   /* the fan's breath */
    if (md->dept == DEPT_REED && held)
        air_lvl = (smooth_drawbar[MAN_UPPER][8] > smooth_drawbar[MAN_LOWER][8] ? smooth_drawbar[MAN_UPPER][8]
                                                                              : smooth_drawbar[MAN_LOWER][8]) >> 2;

    /* weights: every held key of every division adds its registration to the generators; only when
       something they depend on moved */
    state = model | coupled << 4 | (uint32_t)org_par.boost << 5 | (uint32_t)org_par.perc << 6;
    if (state != wt_state) { wt_state = state; dirty = 1; }
    if (!dirty) return;
    while (touched_n) {
        uint32_t g = touched_list[--touched_n];
        for (m = 0; m < ORG_NMAN; m++) for (l = 0; l < NLAYER; l++) w_tgt[m][l][g] = 0;
        gen_touched[g] = 0;
    }
    for (m = 0; m < ORG_NMAN; m++) {
        uint32_t ndb = m == MAN_PEDAL ? ORG_NDB_PEDAL : ORG_NDB;
        int32_t mg = MAN_GAIN_Q7[m];
        if (m == MAN_PEDAL) map = md->voice ? MAP_PEDAL_VOICE[md->voice] : MAP_PEDAL[md->dept];
        else map = MAP_VOICE[md->voice] ? MAP_VOICE[md->voice]
                 : md->dept == DEPT_PIPE ? MAP_PIPE : md->dept == DEPT_REED ? MAP_REED : MAP_VINTAGE;
        if (org_par.boost && m != MAN_PEDAL) mg += mg >> 2;
        for (k = 0; k < ORG_NGEN; k++) {
            int32_t e = env[m][k];
            if (m == MAN_LOWER && coupled && env[MAN_UPPER][k] > e) e = env[MAN_UPPER][k];   /* II/I */
            if (!e) continue;
            for (d = 0; d < ndb; d++) {
                int32_t a = smooth_drawbar[m][d];
                if (!a) continue;
                a = (((e * a) >> 15) * mg) >> 7;
                for (c = 0; c < map[d].n; c++) {
                    int32_t g = (int32_t)k + map[d].c[c].off;
                    if (map[d].c[c].layer == 3u && md->voice == VOICE_RENAISSANCE && k < VU_KEY0) continue;
                    if (d == MIX_DB && (md->voice == VOICE_BAROQUE || md->voice == VOICE_CATHEDRAL))
                        while (g > (int32_t)MIX_TOP) g -= 12;   /* the mixture's breaks */
                    if (model == MODEL_B3) {    /* tonewheel foldback: 16' below C2, the top above wheel 79 */
                        if (map[d].c[c].off < 0 && g < 12) g += 12;
                        while (map[d].c[c].off > 0 && g > 79) g -= 12;
                    }
                    while (g < 0) g += 12;
                    while (g >= (int32_t)ORG_NGEN) g -= 12;
                    {
                        int32_t gn = map[d].c[c].gain;
                        if (d == 2u && md->voice == VOICE_CATHEDRAL && k >= VU_KEY0) gn += gn >> 2;   /* flute harmonique */
                        w_tgt[m][map[d].c[c].layer][g] += (a * gn) >> 7;
                    }
                    if (!gen_touched[g]) { gen_touched[g] = 1; touched_list[touched_n++] = (uint8_t)g; }
                }
            }
            if (m == MAN_UPPER && perc_env && model == MODEL_B3 && org_par.perc) {   /* percussion: 4', 2 2/3' or 2' */
                int32_t g = (int32_t)k + (org_par.perc <= PERC_2ND_SLOW ? 12 : org_par.perc <= PERC_3RD_SLOW ? 19 : 24);
                while (g > (org_par.perc >= PERC_4TH_FAST ? 83 : 79)) g -= 12;   /* (4TH, the SMK-37's: up to the top wheel) */
                int32_t pw = (((e >> 3) * perc_env) >> 15) * 3;
                w_tgt[MAN_UPPER][0][g] += org_par.perc_soft ? pw >> 1 : pw;
                if (!gen_touched[g]) { gen_touched[g] = 1; touched_list[touched_n++] = (uint8_t)g; }
            }
        }
    }
}

/* ---- audio rate: one generator layer, weight ramping from w0 to w1 over n frames */
static void render_layer(int32_t *acc, uint32_t n, const int16_t *t, uint32_t ph, uint32_t inc, int32_t w0, int32_t w1)
{
    int32_t w = w0, dw = (w1 - w0) / (int32_t)n;
    uint32_t i;
    if (w0 == w1) {
        for (i = 0; i < n; i++) {
            ph += inc;
            acc[i] += (t[ph >> (32u - ORG_WT_BITS)] * w) >> 6;
        }
    } else {
        for (i = 0; i < n; i++) {
            ph += inc;
            acc[i] += (t[ph >> (32u - ORG_WT_BITS)] * w) >> 6;
            w += dw;
        }
    }
}

/* the three centre layers share one phase: when two or three sound, one pass reads all their tables */
static void render_centre3(int32_t *acc, uint32_t n, uint32_t b, uint32_t ph, uint32_t inc, uint32_t g, uint32_t m, int32_t gn)
{
    const int16_t *t0 = wt_ptr[0][b], *t1 = wt_ptr[1][b], *t2 = wt_ptr[2][b];
    int32_t (*wc)[ORG_NGEN] = w_cur[m], (*wt)[ORG_NGEN] = w_tgt[m];
    int32_t w0 = wc[0][g], w1 = wc[1][g], w2 = wc[2][g];
    int32_t e0 = wt[0][g] > W_MAX ? W_MAX : wt[0][g], e1 = wt[1][g] > W_MAX ? W_MAX : wt[1][g];
    int32_t e2 = wt[2][g] > W_MAX ? W_MAX : wt[2][g];
    if (gn != 32768) { e0 = (e0 * gn) >> 15; e1 = (e1 * gn) >> 15; e2 = (e2 * gn) >> 15; }
    int32_t d0 = (e0 - w0) / (int32_t)n, d1 = (e1 - w1) / (int32_t)n, d2 = (e2 - w2) / (int32_t)n;
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t ix;
        ph += inc;
        ix = ph >> (32u - ORG_WT_BITS);
        acc[i] += (t0[ix] * w0 + t1[ix] * w1 + t2[ix] * w2) >> 6;
        w0 += d0; w1 += d1; w2 += d2;
    }
    wc[0][g] = e0; wc[1][g] = e1; wc[2][g] = e2;
}

static void gen_render(int32_t (*acc)[SUB], uint32_t n)
{
    uint32_t g, l, m, layers = 0;
    for (g = 0; g < ORG_NGEN; g++) {
        uint32_t b, inc, live = 0;
        int32_t gn = gen_gain[g];
        if (!gen_touched[g] && !gen_live[g]) continue;          /* silent: its phase waits (gen_catch_up) */
        gen_catch_up(g);
        b = gen_band[g]; inc = phase_inc[g];
        for (m = 0; m < ORG_NMAN; m++) {        /* each division into its own bus */
            int32_t (*wc)[ORG_NGEN] = w_cur[m], (*wt)[ORG_NGEN] = w_tgt[m];
            uint32_t l0 = 0, nc = 0;
            for (l = 0; l < 3u; l++)
                if (wt[l][g] || wc[l][g]) nc++;
            if (nc >= 2u) {                     /* (3 x 16383 x 16383 >> 6 stays inside 32 bits) */
                render_centre3(acc[m], n, b, phase[g], inc, g, m, gn);
                layers += nc;
                l0 = 3;
            }
            for (l = l0; l < NLAYER; l++) {
                int32_t t = wt[l][g];
                if (t > W_MAX) t = W_MAX;
                if (gn != 32768) t = (t * gn) >> 15;
                if (t || wc[l][g]) {
                    uint32_t ph = l < 3u ? phase[g] : l == 3u ? phase_detune_up[g] : phase_detune_down[g];
                    uint32_t li = l < 3u ? inc : l == 3u ? inc_detune_up[g] : inc_detune_down[g];
                    render_layer(acc[m], n, wt_ptr[l][b], ph, li, wc[l][g], t);
                    wc[l][g] = t;
                    layers++;
                }
            }
            live |= (uint32_t)(wc[0][g] | wc[1][g] | wc[2][g] | wc[3][g] | wc[4][g]);
        }
        phase[g] += inc * n;
        phase_detune_up[g] += inc_detune_up[g] * n;
        phase_detune_down[g] += inc_detune_down[g] * n;
        gen_stamp[g] = gen_clock + n;
        gen_live[g] = (uint8_t)(live != 0u);
    }
    gen_clock += n;
    org_stat.active_gens = layers;
}

/* key click, pipe chiff, reed valve and bellows air: band-passed noise (Chamberlin SVF, Q15) */
static void noise_render(int32_t *dry, uint32_t n)
{
    uint32_t i;
    if (!click_env && !chiff_env && !valve_env && !air_lvl && !hum_lvl) {
        click_lo = click_bp = chiff_lo = chiff_bp = air_lo = air_bp = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = (int32_t)org_rand() >> 18, hp, s = 0;     /* +-8192 */
        if (click_env) {                        /* 3 ms contact burst around 2.6 kHz */
            click_lo += (CLICK_SVF_F * click_bp) >> 15;
            hp = x - click_lo - ((9000 * click_bp) >> 15);
            click_bp += (CLICK_SVF_F * hp) >> 15;
            s += (click_bp * click_env) >> 14;
            click_env -= (click_env >> 5) + 1;
            if (click_env < 0) click_env = 0;
        }
        if (chiff_env) {                        /* 25 ms (baroque) to 45 ms (cathedral) of wind */
            chiff_lo += ((int32_t)chiff_f * chiff_bp) >> 15;
            hp = x - chiff_lo - ((7000 * chiff_bp) >> 15);
            chiff_bp += ((int32_t)chiff_f * hp) >> 15;
            s += (chiff_bp * chiff_env) >> 14;
            chiff_env -= (chiff_env >> (model_applied == MODEL_CATHEDRAL ? 10 : 9)) + 1;
            if (chiff_env < 0) chiff_env = 0;
        }
        if (valve_env || air_lvl) {             /* pallet click and the breath of the bellows */
            air_lo += (AIR_SVF_F * air_bp) >> 15;
            hp = x - air_lo - ((20000 * air_bp) >> 15);
            air_bp += (AIR_SVF_F * hp) >> 15;
            s += (air_bp * (valve_env + air_lvl)) >> 14;
            if (valve_env) { valve_env -= (valve_env >> 6) + 1; if (valve_env < 0) valve_env = 0; }
        }
        if (hum_lvl) {                          /* the Playroom's fan motor: a buzzing 90..180 Hz pulse */
            hum_ph += INC_FAN_HUM * (90u + org_par.character * 90u / 127u);
            s += ((hum_ph < 0x4CCCCCCCu ? 8192 : -3511) * hum_lvl) >> 14;
        }
        dry[i] += s;
    }
}

/* ---- the interrupt's entry: n frames (a multiple of 32), stereo, added to out */
#ifdef ORGIGS_PROF
static volatile uint32_t org_prof[4];   /* TIMER4 ticks per block: control, generators, noise, effects */
#endif
static uint32_t panic_seen;
static void org_panic(void)              /* interrupt: every envelope to 0 (the weights still ramp: no click) */
{
    uint32_t m, k;
    for (m = 0; m < ORG_NMAN; m++)
        for (k = 0; k < ORG_NGEN; k++) env[m][k] = 0;
    env_moving = 1;
    key_seq_seen = 0xFFFFFFFFu;             /* rebuild the weights in this block */
    click_env = chiff_env = valve_env = perc_env = 0;
}

static void org_audio(int32_t *out, uint32_t n)
{
    uint32_t i0, i, m;
    int32_t acc[ORG_NMAN][SUB], peak = 0;
    uint32_t panic = org_par.panic_seq != panic_seen;
    if (panic) { panic_seen = org_par.panic_seq; org_panic(); }
#ifdef ORGIGS_PROF
    uint32_t pt[4] = {0, 0, 0, 0}, t;
#define PROF(k) do { uint32_t t1 = fm1_ticks(); pt[k] += t1 - t; t = t1; } while (0)
#else
#define PROF(k) do { } while (0)
#endif
    for (i0 = 0; i0 + SUB <= n; i0 += SUB) {
#ifdef ORGIGS_PROF
        t = fm1_ticks();
#endif
        control_update();
        PROF(0);
        for (m = 0; m < ORG_NMAN; m++) for (i = 0; i < SUB; i++) acc[m][i] = 0;
        gen_render(acc, SUB);
        PROF(1);
        for (m = 0; m < ORG_NMAN; m++) for (i = 0; i < SUB; i++) acc[m][i] >>= 8;
        noise_render(acc[noise_bus < ORG_NMAN ? noise_bus : 0u], SUB);
        PROF(2);
        if (org_par.mute) continue;
        fx_block(acc, out + 2u * i0, SUB, &peak);
        PROF(3);
    }
#ifdef ORGIGS_PROF
    for (i = 0; i < 4u; i++) org_prof[i] = pt[i];
#endif
    if (panic) fx_panic();                  /* after the block: the 32-frame fade above is not left in the reverb */
    org_stat.peak = (uint32_t)peak;
}
