/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs FM-1: drawbar presets, performance setups and what the flash keeps (main loop only).
 *
 *   drawbar presets     one fixed list for the manuals: vintage (the 36 classic tonewheel registrations), pipe,
 *                       free reed; then USER 1..16. The renaissance and the romantic organ have their own,
 *                       then the same USER 1..16. The pedal has its own list (vintage, pipe, reed) and
 *                       PEDAL USER 1..8. PRESETS turns through the list; each model remembers per manual
 *                       where it was, a model's first visit starts in its department's part of the list.
 *                       SAVE stores the focused manual's drawbars into the user slot shown, or (on a factory
 *                       entry) into the first free user slot.
 *   performance setups  16: the whole instrument (model, all drawbars, the effects of the three manuals, the
 *                       MIDI mixer, octave, the MIDI zones). SEL + one of the 16 white keys recalls one, SAVE
 *                       held + the key stores it.
 *
 * Flash: sector 0 holds the panel as it was left, the 16 setups and the lights, sector 1 the user drawbars.
 * Each is written 2 s after a change once nothing sounds (an erase stalls the audio ~50 ms). Version 5
 * (8 setups, one effect chain) moves into setups 1..8; version 4 (four registrations) into 1..4. */

#define NSETUP 16u
#define NUSER 16u                       /* user drawbar slots, manuals */
#define NUSER_PED 8u                    /* user drawbar slots, pedal */

typedef struct {                        /* the effects of one manual */
    uint8_t drive, vib, trem, les, dsend, rsend;
} fxman_t;

typedef struct {                        /* the sound of a setup */
    uint8_t model, db[ORG_NMAN][ORG_NDB];
    uint8_t character, perc, rev_mode, delay_time, delay_fb, coupler;
    fxman_t fx[ORG_NMAN];
} reg_t;

typedef struct {                        /* a performance setup */
    reg_t r;
    uint8_t focus;                      /* the manual the panel works on */
    uint8_t oct;                        /* octave + 2 (unsigned: a signed byte load from the stack crashes the emulator) */
    uint8_t dbp[ORG_NMAN];              /* the drawbar preset each manual shows */
    uint8_t mix[16];                    /* the MIDI mixer: CC 7 per channel */
    zones_t zn;
} setup_t;

enum { LT_OFF, LT_DIM, LT_FULL, LT_N };
#define LT_LEVELS 10u

#define STORE_VER 6u
typedef struct {                        /* flash sector 0 */
    uint8_t ver, setup, rate_safe;
    uint8_t lt_keys, lt_btns, lt_level; /* the lights: LT_... for the keys and the buttons, the dim level 0..7 */
    uint8_t ctl_mode;                   /* ml_ctl_mode (0, the PITCH default, in a store of before) */
    uint8_t pads;                       /* the SMK-37's pad colours: bits 0..2 the scheme, 3..5 8 - brightness (0 before) */
    setup_t now;                        /* the panel as it was left */
    setup_t setups[NSETUP];
    uint8_t dbp_sel[ORG_NMODEL1][ORG_NMAN];  /* the drawbar preset each model of OrGigs 1 and manual shows (the store
                                                keeps its size: a longer one would not load, the settings would be lost) */
} store0_t;
typedef struct {                        /* flash sector 1 */
    uint8_t ver, spare[3];
    uint8_t user[NUSER][ORG_NDB];
    uint8_t user_ped[NUSER_PED][ORG_NDB_PEDAL];
} store1_t;
typedef char store0_fits[sizeof(store0_t) <= 4084u ? 1 : -1];
typedef char store1_fits[sizeof(store1_t) <= 4084u ? 1 : -1];

static store0_t st0;
static store1_t st1;
static uint32_t st_dirty;               /* bit s: sector s must be written */
static uint8_t dbp_sel2[ORG_NMODEL - ORG_NMODEL1][ORG_NMAN];   /* the same for the models of OrGigs 2 (not kept) */
static uint8_t *dbp_sel(uint32_t model)
{
    if (model >= ORG_NMODEL) model = 0;
    return model < ORG_NMODEL1 ? st0.dbp_sel[model] : dbp_sel2[model - ORG_NMODEL1];
}

/* ---- the drawbar list. The drawbars mean what the model's department makes of them:
 *   vintage  16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'
 *   pipe     Bourdon 16', Prinzipal 8', Rohrfloete 8', Gambe 8', Oktave 4', Spitzfloete 4' + Nasat,
 *            Superoktave 2' + Terz, Mixtur, Trompete 8' (OrGigs 1's; every pipe organ has its own list since
 *            OrGigs 2, the part stays for the store's numbers)
 *   reed     Bassoon 16', sub-fifth, Clarinet 8', Violin 8' +, Musette 8' -, Piccolo 4', Bandoneon 4',
 *            sheen, valve & air
 * Vintage 1..36: drawbar_presets_36.csv (jazz / soul / gospel, rock / pop / reggae, the classical imitations);
 * neutral style names, no artists or song titles. */
typedef struct { const char *name; uint8_t db[ORG_NDB]; } dbp_t;

#define DBL_PIPE 36u                    /* where the departments start */
#define DBL_REED 55u
static const dbp_t DBL[] = {
    /* vintage: jazz / soul / gospel */
    {"CLASSIC JAZZ LEAD", {8,8,8,0,0,0,0,0,0}}, {"WARM JAZZ COMP", {8,4,8,0,0,0,0,0,0}}, {"LOWER MANUAL COMPING", {0,0,8,4,0,0,0,0,0}},
    {"HOLLOW FUNK", {8,0,0,0,0,0,8,8,8}}, {"MEMPHIS SOUL", {8,8,8,8,0,0,0,0,0}}, {"WHISTLE / SOUL SOLO", {8,8,8,0,0,0,0,0,8}},
    {"GOSPEL SHOUTING", {6,6,8,8,4,8,5,8,8}}, {"GOSPEL SIMMERING", {8,3,0,0,0,0,3,7,8}}, {"REAL GOSPEL", {8,0,0,0,0,0,6,7,8}},
    {"MELLOW R&B BALLAD", {8,0,3,6,0,0,0,0,0}}, {"MODERN JAZZ SOLO", {8,8,8,4,0,0,0,0,0}}, {"BIG BAND PUNCH", {8,8,8,8,8,8,0,0,0}},
    /* vintage: rock / pop / reggae */
    {"FULL ORGAN (ALL OUT)", {8,8,8,8,8,8,8,8,8}}, {"BRITISH HARD ROCK", {8,8,8,8,0,0,0,0,0}}, {"BAROQUE ROCK BALLAD", {6,8,8,6,0,0,0,0,0}},
    {"LATIN ROCK GROOVE", {8,8,6,4,0,0,0,0,0}}, {"60S BRIT SOUL", {8,8,8,8,8,0,0,0,0}}, {"HEAVY BIKER ROCK", {8,8,8,6,8,6,8,6,8}},
    {"KINGSTON REGGAE BUBBLE", {8,0,8,0,0,0,0,0,0}}, {"PSYCHEDELIC SPACE PAD", {0,0,8,6,4,2,0,0,0}}, {"70S ARENA ROCK", {8,8,8,8,8,8,8,0,0}},
    {"60S TRANSISTOR COMBO", {0,0,4,8,8,8,6,4,2}},
    /* vintage: the classical imitations */
    {"STOPPED FLUTE 8'", {0,0,5,3,2,0,0,0,0}}, {"DULCIANA 8'", {0,0,4,4,3,2,0,0,0}}, {"FRENCH HORN 8'", {0,0,8,7,4,0,0,0,0}},
    {"SALICIONAL 8'", {0,0,4,5,4,4,2,2,2}}, {"FLUTES 8' & 4'", {0,0,5,4,0,3,0,0,0}}, {"OBOE 8'", {0,0,4,6,7,5,3,0,0}},
    {"SWELL DIAPASON 8'", {0,0,5,6,4,4,3,2,0}}, {"TRUMPET 8'", {0,0,6,8,7,6,5,4,0}}, {"FULL SWELL", {3,2,7,6,4,5,2,2,2}},
    {"CELLO 8'", {0,0,4,5,4,4,2,2,1}}, {"FLUTE & STRING 8'", {0,0,5,6,4,3,3,2,2}}, {"CLARINET 8'", {0,0,6,0,7,0,5,4,0}},
    {"THEATER TIBIA 8'", {0,0,8,2,4,0,0,0,0}}, {"VOX HUMANA 8'", {0,0,3,4,0,0,3,3,2}},
    /* pipe (36): the stops by name */
    {"PRINZIPAL 8'", {0,8,0,0,0,0,0,0,0}}, {"PRINZ 8' 4'", {0,8,0,0,8,0,0,0,0}}, {"BEGLEITUNG", {0,6,8,5,0,4,0,0,0}},
    {"ROHRFLOETE 8'", {0,0,8,0,0,0,0,0,0}}, {"FLOETEN 8' 4'", {0,0,8,0,0,8,0,0,0}}, {"FLOETE 4'", {0,0,0,0,0,8,0,0,0}},
    {"GAMBE CELESTE", {0,0,0,8,0,0,0,0,0}}, {"STREICHER", {0,0,4,8,0,0,0,0,0}}, {"PIANISSIMO", {0,0,3,3,0,0,0,0,0}},
    {"TRIO", {0,0,8,0,0,8,6,0,0}}, {"CORNET", {0,0,8,0,0,8,8,0,0}}, {"BOURDON 16' 8'", {8,0,8,0,0,0,0,0,0}},
    {"TROMPETE 8'", {0,0,0,0,0,0,0,0,8}}, {"TROMP + PRINZ", {0,8,0,0,8,0,0,0,8}}, {"CHORAL", {4,8,6,0,6,0,0,0,0}},
    {"MIXTUR", {0,8,0,0,8,0,8,8,0}}, {"PLENUM", {6,8,0,0,8,0,6,6,0}}, {"ORGANO PLENO", {8,8,6,0,8,6,8,8,8}},
    {"TUTTI", {8,8,8,8,8,8,8,8,8}},
    /* free reed (55) */
    {"CLARINET", {0,0,8,0,0,0,0,0,0}}, {"BASS CHORDS", {6,0,8,0,0,0,0,0,1}}, {"MUSETTE", {0,0,8,8,8,0,0,0,2}},
    {"VIOLIN", {0,0,8,8,0,0,0,0,0}}, {"BASSOON", {8,0,0,0,0,0,0,0,0}}, {"PICCOLO", {0,0,0,0,0,8,0,0,0}},
    {"ORGAN", {8,0,0,0,0,8,0,0,0}}, {"BANDONEON", {8,0,8,0,0,0,0,0,0}}, {"BANDO 4'", {0,0,0,0,0,0,8,0,2}},
    {"TANGO", {8,0,8,0,0,0,8,0,2}}, {"OBOE", {8,0,8,0,0,8,0,0,0}}, {"ACCORDION", {8,0,8,8,0,0,0,0,0}},
    {"MASTER", {8,0,8,8,8,8,0,0,0}}, {"HARMONIUM", {6,0,8,0,0,6,0,0,4}}, {"BRIGHT", {0,0,8,0,0,8,0,8,0}},
    {"DARK", {8,6,4,0,0,0,0,0,0}}, {"SOFT", {0,0,6,0,0,0,0,0,0}}, {"WHISPER", {0,0,3,0,0,0,0,0,3}},
    {"AIR", {0,0,4,0,0,0,0,0,8}}, {"FULL + AIR", {8,6,8,8,8,8,8,8,4}},
};
#define DBL_N (sizeof DBL / sizeof DBL[0])
typedef char dbl_parts[DBL_N == 75u && DBL_PIPE < DBL_REED && DBL_REED < DBL_N ? 1 : -1];

/* the renaissance organ's own: Principale 8', Voce umana 8', Ottava 4', Flauto in VIII 4', Flauto in XII, XV 2',
   XIX 1 1/3', XXII 1', XXVI + XXIX; the registrations of the Italian ripieno */
static const dbp_t DBL_RENAIS[] = {
    {"PRINCIPALE", {8,0,0,0,0,0,0,0,0}}, {"VOCE UMANA", {8,8,0,0,0,0,0,0,0}}, {"PRINCIPALE PIANO", {5,0,0,4,0,0,0,0,0}},
    {"PRINC + OTTAVA", {8,0,8,0,0,0,0,0,0}}, {"PRINC + XV", {8,0,0,0,0,8,0,0,0}}, {"FLAUTO IN VIII", {0,0,0,8,0,0,0,0,0}},
    {"PRINC + FLAUTO", {8,0,0,8,0,0,0,0,0}}, {"FLAUTI", {0,0,0,8,8,0,0,0,0}}, {"PRINC + FLAUTI", {8,0,0,8,8,0,0,0,0}},
    {"RIPIENO A 4", {8,0,8,0,0,8,8,0,0}}, {"RIPIENO", {8,0,8,0,0,8,8,8,8}}, {"TIRATUTTI", {8,0,8,8,8,8,8,8,8}},
};
#define DBL_RENAIS_N (sizeof DBL_RENAIS / sizeof DBL_RENAIS[0])
/* the romantic organ's own: Bourdon 16', Prinzipal 8', Gedackt 8', Salicional 8', Aeoline 8', Oktave 4', Fugara 4',
   Mixtur III, Oboe 8'; from the softest strings to the full organ (the roller on the pedal adds to them) */
static const dbp_t DBL_ROMANTIC[] = {
    {"AEOLINE", {0,0,0,0,8,0,0,0,0}}, {"SALICIONAL", {0,0,0,8,0,0,0,0,0}}, {"STREICHER 8'", {0,0,0,8,8,0,0,0,0}},
    {"GEDACKT", {0,0,8,0,0,0,0,0,0}}, {"GEDACKT + AEOLINE", {0,0,6,0,6,0,0,0,0}}, {"PRINZIPAL 8'", {0,8,0,0,0,0,0,0,0}},
    {"GRUNDSTIMMEN 8'", {0,8,8,6,6,0,0,0,0}}, {"GRUNDST 16' 8'", {8,8,8,6,6,0,0,0,0}}, {"OBOE", {0,0,0,0,0,0,0,0,8}},
    {"OBOE + GEDACKT", {0,0,6,0,0,0,0,0,8}}, {"PRINZ 8' 4'", {0,8,0,0,0,8,0,0,0}}, {"MEZZOFORTE", {6,8,8,6,0,6,4,0,0}},
    {"FORTE", {8,8,8,6,6,8,6,0,6}}, {"PLENUM", {8,8,6,0,0,8,6,8,0}}, {"TUTTI", {8,8,8,8,8,8,8,8,8}},
};
#define DBL_ROMANTIC_N (sizeof DBL_ROMANTIC / sizeof DBL_ROMANTIC[0])
/* the baroque organ's own: Quintadena 16', Prinzipal 8', Rohrfloete 8', Oktave 4', Oktave 2', Sesquialtera II,
   Mixtur IV, Regal 8', Trompete 8'; the registrations of the North German Werkprinzip */
static const dbp_t DBL_BAROQUE[] = {
    {"PRINZIPAL 8'", {0,8,0,0,0,0,0,0,0}}, {"PRINZ 8' 4'", {0,8,0,8,0,0,0,0,0}}, {"ROHRFLOETE 8'", {0,0,8,0,0,0,0,0,0}},
    {"ROHRFL + OKT 4'", {0,0,8,6,0,0,0,0,0}}, {"QUINTADENA 16'", {8,0,0,0,0,0,0,0,0}}, {"TRIO", {0,0,8,0,6,0,0,0,0}},
    {"SESQUIALTERA", {0,0,8,6,0,8,0,0,0}}, {"REGAL", {0,0,0,0,0,0,0,8,0}}, {"REGAL + ROHRFL", {0,0,6,0,0,0,0,8,0}},
    {"TROMPETE 8'", {0,0,0,0,0,0,0,0,8}}, {"TROMP + PRINZ", {0,8,0,8,0,0,0,0,8}}, {"CHORAL", {6,8,6,6,0,0,0,0,0}},
    {"PRINZ 8' 4' 2'", {0,8,0,8,8,0,0,0,0}}, {"PLENUM", {6,8,0,8,8,0,8,0,0}}, {"ORGANO PLENO", {8,8,6,8,8,0,8,0,8}},
    {"TUTTI", {8,8,8,8,8,8,8,8,8}},
};
#define DBL_BAROQUE_N (sizeof DBL_BAROQUE / sizeof DBL_BAROQUE[0])
/* the cathedral organ's own: Bourdon 16', Montre 8', Flute harmonique 8', Gambe 8', Voix celeste 8', Prestant 4',
   Plein-jeu IV, Trompette 8', Clairon 4'; the French symphonic registrations, the fonds to the grand choeur */
static const dbp_t DBL_CATHEDRAL[] = {
    {"MONTRE 8'", {0,8,0,0,0,0,0,0,0}}, {"FLUTE HARMONIQUE", {0,0,8,0,0,0,0,0,0}}, {"GAMBE", {0,0,0,8,0,0,0,0,0}},
    {"VOIX CELESTE", {0,0,0,8,8,0,0,0,0}}, {"FLUTE + CELESTE", {0,0,5,6,6,0,0,0,0}}, {"FONDS 8'", {0,8,8,6,0,0,0,0,0}},
    {"FONDS 16' 8'", {8,8,8,6,0,0,0,0,0}}, {"FONDS 8' 4'", {0,8,8,6,0,8,0,0,0}}, {"FLUTE + PRESTANT", {0,0,8,0,0,6,0,0,0}},
    {"TROMPETTE 8'", {0,0,0,0,0,0,0,8,0}}, {"TROMPETTE + FONDS", {0,8,8,0,0,0,0,8,0}}, {"PLEIN-JEU", {6,8,0,0,0,8,8,0,0}},
    {"ANCHES", {0,0,0,0,0,0,0,8,8}}, {"GRAND CHOEUR", {8,8,6,0,0,8,8,8,8}}, {"TUTTI", {8,8,8,8,8,8,8,8,8}},
};
#define DBL_CATHEDRAL_N (sizeof DBL_CATHEDRAL / sizeof DBL_CATHEDRAL[0])

/* the pedal: Subbass 32', Bourdon 16', Octave bass 8', Posaune 16' (vintage: 16' 8' as on a tonewheel's pedal) */
#define DBP_PED_PIPE 8u
#define DBP_PED_REED 16u
static const dbp_t DBL_PED[] = {
    {"16' 8'", {0,8,4,0}}, {"16'", {0,8,0,0}}, {"SOFT", {0,5,3,0}}, {"FULL", {4,8,6,0}},
    {"32' 16'", {6,8,0,0}}, {"PUNCH", {0,8,8,0}}, {"WALKING", {0,7,5,0}}, {"ROCK", {0,8,8,4}},
    {"PLENUM", {6,8,6,0}}, {"SUBBASS 16'", {0,8,0,0}}, {"32' 16' 8'", {8,8,6,0}}, {"POSAUNE 16'", {0,6,4,8}},
    {"OKTAVBASS 8'", {0,0,8,0}}, {"SOFT 16'", {0,5,0,0}}, {"CANTUS FIRMUS", {0,0,6,6}}, {"TUTTI", {8,8,8,8}},
    {"BASS", {0,8,4,0}}, {"DEEP", {8,8,0,0}}, {"REEDY", {0,6,0,8}}, {"TANGO", {4,8,4,4}},
    {"SUB", {8,4,0,0}}, {"LIGHT", {0,4,4,0}}, {"BALLAD", {0,6,2,0}}, {"DARK", {8,6,0,0}},
};
#define DBL_PED_N (sizeof DBL_PED / sizeof DBL_PED[0])

/* where a model's first visit starts, per department and manual */
static const uint8_t DBP_START[3][ORG_NMAN] = {
    {0, 2, 0},                                  /* CLASSIC JAZZ LEAD, LOWER MANUAL COMPING, 16' 8' */
    {DBL_PIPE, DBL_PIPE + 2u, DBP_PED_PIPE},    /* PRINZIPAL 8', BEGLEITUNG, PLENUM */
    {DBL_REED, DBL_REED + 1u, DBP_PED_REED},    /* CLARINET, BASS CHORDS, BASS */
};

/* ---- the factory setups */
#define ZN_STD {{ZONE_INIT(0, 3), ZONE_INIT(1, 3), ZONE_INIT(2, 3)}, KEYS_FOLLOW, 0, 0, 0}
#define MIX_STD {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100}
/* model, upper, lower, pedal drawbars, character, perc, room, delay time, feedback, coupler,
   the effects of upper, lower, pedal: drive, vibrato, tremulant, rotary, delay send, reverb send */
#define SETUP(model, u, l, p, ch, perc, room, dt, fb, cpl, fu, fl, fp) \
    {{model, {u, l, p}, ch, perc, room, dt, fb, cpl, {fu, fl, fp}}, MAN_UPPER, 2, {0, 0, 0}, MIX_STD, ZN_STD}
#define DB(a, b, c, d, e, f, g, h, i) {a, b, c, d, e, f, g, h, i}
#define FX(drv, vib, trem, les, ds, rs) {drv, vib, trem, les, ds, rs}
static const setup_t SETUP_FACTORY[NSETUP] = {
    /* 1 tonewheel jazz: 888000000, 3rd perc, C3, rotary */
    SETUP(MODEL_B3, DB(8,8,8,0,0,0,0,0,0), DB(0,0,8,8,0,0,0,0,0), DB(0,8,4,0,0,0,0,0,0), 64, PERC_3RD_FAST, REV_SPRING, 40, 50, 0,
          FX(30, VIB_C3, 0, 1, 0, 24), FX(30, VIB_C3, 0, 1, 0, 24), FX(30, 0, 0, 1, 0, 16)),
    /* 2 cathedral plein-jeu: bourdon, montre, prestant, plein-jeu over the fonds; pedal 32' 16' 8' */
    SETUP(MODEL_CATHEDRAL, DB(6,8,0,0,0,8,8,0,0), DB(0,8,8,6,0,0,0,0,0), DB(6,8,6,0,0,0,0,0,0), 50, 0, REV_CATHEDRAL, 40, 40, 0,
          FX(0, 0, 0, 0, 0, 70), FX(0, 0, 0, 0, 0, 70), FX(0, 0, 0, 0, 0, 70)),
    /* 3 Paris musette: three 8' reeds wet, a little air */
    SETUP(MODEL_MUSETTE, DB(0,0,8,8,8,0,0,0,2), DB(6,0,8,0,0,0,0,0,1), DB(0,8,4,0,0,0,0,0,0), 64, 0, REV_STAGE, 50, 35, 0,
          FX(0, 0, 30, 0, 18, 30), FX(0, 0, 30, 0, 10, 30), FX(0, 0, 0, 0, 0, 20)),
    /* 4 Brit Combo: 8' 4' 2 2/3' 2', vibrato */
    SETUP(MODEL_VOX, DB(0,0,8,8,8,8,0,0,0), DB(0,0,8,8,0,0,0,0,0), DB(0,8,4,0,0,0,0,0,0), 64, 0, REV_SPRING, 60, 45, 0,
          FX(40, VIB_V2, 0, 1, 22, 20), FX(40, VIB_V2, 0, 1, 10, 20), FX(20, 0, 0, 0, 0, 14)),
    /* 5 tonewheel rock: full upper, drive, rotary */
    SETUP(MODEL_B3, DB(8,8,8,8,8,8,8,8,8), DB(8,8,8,8,0,0,0,0,0), DB(4,8,6,0,0,0,0,0,0), 64, 0, REV_STAGE, 40, 50, 0,
          FX(90, VIB_C3, 0, 1, 0, 30), FX(90, VIB_C3, 0, 1, 0, 30), FX(40, 0, 0, 1, 0, 20)),
    /* 6 Italo Combo: bright combo, a slapback delay */
    SETUP(MODEL_FARFISA, DB(8,8,8,8,0,0,0,0,8), DB(0,0,8,8,0,0,0,0,0), DB(0,8,4,0,0,0,0,0,0), 64, 0, REV_SPRING, 18, 20, 0,
          FX(50, VIB_V1, 0, 0, 30, 20), FX(50, VIB_V1, 0, 0, 10, 20), FX(20, 0, 0, 0, 0, 12)),
    /* 7 baroque organ: principal 8' 4', rohrfloete 8' with the octave on the lower manual */
    SETUP(MODEL_BAROQUE, DB(0,8,0,8,0,0,0,0,0), DB(0,0,8,6,0,0,0,0,0), DB(0,8,6,0,0,0,0,0,0), 70, 0, REV_HALL, 40, 40, 0,
          FX(0, 0, 0, 0, 0, 48), FX(0, 0, 0, 0, 0, 48), FX(0, 0, 0, 0, 0, 48)),
    /* 8 tango bandoneon: bassoon + clarinet, dry */
    SETUP(MODEL_BANDONEON, DB(8,0,8,0,0,0,0,0,0), DB(6,0,8,0,0,0,0,0,1), DB(0,8,4,0,0,0,0,0,0), 64, 0, REV_STAGE, 50, 35, 0,
          FX(0, 0, 20, 0, 0, 26), FX(0, 0, 20, 0, 0, 26), FX(0, 0, 0, 0, 0, 20)),
    /* 9 British hard rock: 888800000 hard into the tube, lower 888 */
    SETUP(MODEL_B3, DB(8,8,8,8,0,0,0,0,0), DB(8,8,8,0,0,0,0,0,0), DB(0,8,6,0,0,0,0,0,0), 64, 0, REV_STAGE, 40, 50, 0,
          FX(110, 0, 0, 1, 0, 28), FX(70, 0, 0, 1, 0, 28), FX(40, 0, 0, 1, 0, 18)),
    /* 10 gospel shout: 668848588 over the lower comping */
    SETUP(MODEL_B3, DB(6,6,8,8,4,8,5,8,8), DB(0,0,8,4,0,0,0,0,0), DB(0,8,4,0,0,0,0,0,0), 64, 0, REV_STAGE, 40, 50, 0,
          FX(50, VIB_C3, 0, 1, 0, 24), FX(40, VIB_C3, 0, 1, 0, 24), FX(30, 0, 0, 1, 0, 16)),
    /* 11 baroque rock ballad: 688600000, chorus, a hall */
    SETUP(MODEL_B3, DB(6,8,8,6,0,0,0,0,0), DB(0,0,8,4,0,0,0,0,0), DB(0,8,4,0,0,0,0,0,0), 64, 0, REV_HALL, 40, 50, 0,
          FX(20, VIB_C3, 0, 1, 0, 50), FX(20, VIB_C3, 0, 1, 0, 50), FX(10, 0, 0, 1, 0, 36)),
    /* 12 60s transistor combo: Brit Combo 004888642 */
    SETUP(MODEL_VOX, DB(0,0,4,8,8,8,6,4,2), DB(0,0,8,8,0,0,0,0,0), DB(0,8,4,0,0,0,0,0,0), 64, 0, REV_SPRING, 40, 40, 0,
          FX(30, VIB_V2, 0, 0, 0, 22), FX(30, VIB_V2, 0, 0, 0, 22), FX(20, 0, 0, 0, 0, 14)),
    /* 13 reggae bubble: 808000000, a quarter-note delay */
    SETUP(MODEL_B3, DB(8,0,8,0,0,0,0,0,0), DB(0,0,8,4,0,0,0,0,0), DB(0,8,4,0,0,0,0,0,0), 64, 0, REV_SPRING, 30, 30, 0,
          FX(20, VIB_C1, 0, 1, 25, 18), FX(20, VIB_C1, 0, 1, 0, 18), FX(10, 0, 0, 1, 0, 10)),
    /* 14 cathedral: the grand choeur over the fonds, pedal with the bombarde */
    SETUP(MODEL_CATHEDRAL, DB(8,8,6,0,0,8,8,8,8), DB(0,8,8,6,0,0,0,0,0), DB(8,8,6,8,0,0,0,0,0), 40, 0, REV_CATHEDRAL, 40, 40, 0,
          FX(0, 0, 0, 0, 0, 75), FX(0, 0, 0, 0, 0, 75), FX(0, 0, 0, 0, 0, 75)),
    /* 15 musette master: all reeds, wide */
    SETUP(MODEL_MUSETTE, DB(8,0,8,8,8,8,0,0,0), DB(6,0,8,0,0,0,0,0,1), DB(0,8,4,0,0,0,0,0,0), 80, 0, REV_STAGE, 50, 35, 0,
          FX(0, 0, 0, 0, 0, 24), FX(0, 0, 0, 0, 0, 24), FX(0, 0, 0, 0, 0, 16)),
    /* 16 psychedelic space pad: Tokyo Combo 008642000, chorus, a long delay into the hall */
    SETUP(MODEL_YC, DB(0,0,8,6,4,2,0,0,0), DB(0,0,5,4,0,3,0,0,0), DB(0,8,4,0,0,0,0,0,0), 64, 0, REV_HALL, 60, 55, 0,
          FX(20, VIB_C2, 0, 1, 40, 60), FX(20, VIB_C2, 0, 1, 20, 60), FX(10, 0, 0, 1, 0, 30)),
};

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t dept_of(uint32_t model) { return ORG_MODELS[model < ORG_NMODEL ? model : 0u].dept; }
/* the models' own lists: 1 the renaissance's, 2 the romantic's, 3 the baroque's, 4 the cathedral's (0: the shared one) */
static const dbp_t *const DBL_OWN[5] = {DBL, DBL_RENAIS, DBL_ROMANTIC, DBL_BAROQUE, DBL_CATHEDRAL};
static const uint8_t DBL_OWN_N[5] = {DBL_N, DBL_RENAIS_N, DBL_ROMANTIC_N, DBL_BAROQUE_N, DBL_CATHEDRAL_N};
static uint32_t dbl_own(uint32_t model)
{
    uint32_t v = ORG_MODELS[model < ORG_NMODEL ? model : 0u].voice;
    return v == VOICE_RENAISSANCE ? 1u : v == VOICE_ROMANTIC ? 2u : v == VOICE_BAROQUE ? 3u : v == VOICE_CATHEDRAL ? 4u : 0u;
}
static uint32_t dbp_start(uint32_t model, uint32_t man)    /* where a model's first visit starts */
{
    /* PRINCIPALE, PRINC + FLAUTO; GRUNDSTIMMEN, GEDACKT + AEOLINE; PRINZ 8' 4', ROHRFLOETE; FONDS 8', VOIX CELESTE */
    static const uint8_t OWN_START[5][2] = {{0, 0}, {0, 6}, {6, 4}, {1, 2}, {5, 3}};
    return dbl_own(model) && man != MAN_PEDAL ? OWN_START[dbl_own(model)][man] : DBP_START[dept_of(model)][man];
}

/* ---- the drawbar presets: the factory list (the model's own, if it has one), then the user slots. Without a model
   they are the playing model's */
static uint32_t dbp_ndb(uint32_t man) { return man == MAN_PEDAL ? ORG_NDB_PEDAL : ORG_NDB; }
static const dbp_t *dbl_of(uint32_t man, uint32_t model)
{
    return man == MAN_PEDAL ? DBL_PED : DBL_OWN[dbl_own(model)];
}
static uint32_t dbp_nfact_m(uint32_t man, uint32_t model)
{
    return man == MAN_PEDAL ? DBL_PED_N : DBL_OWN_N[dbl_own(model)];
}
static uint32_t dbp_count_m(uint32_t man, uint32_t model) { return dbp_nfact_m(man, model) + (man == MAN_PEDAL ? NUSER_PED : NUSER); }
static uint32_t dbp_count(uint32_t man) { return dbp_count_m(man, org_par.model); }
static uint32_t dbp_nfact(uint32_t man) { return dbp_nfact_m(man, org_par.model); }
static uint32_t dbp_is_user(uint32_t man, uint32_t i) { return i >= dbp_nfact(man); }

static const uint8_t *dbp_get_m(uint32_t man, uint32_t model, uint32_t i)
{
    uint32_t nf = dbp_nfact_m(man, model);
    if (i >= dbp_count_m(man, model)) i = 0;
    if (i < nf) return dbl_of(man, model)[i].db;
    return man == MAN_PEDAL ? st1.user_ped[i - nf] : st1.user[i - nf];
}
static const uint8_t *dbp_get(uint32_t man, uint32_t i) { return dbp_get_m(man, org_par.model, i); }

/* the name, into s: the factory's, or USER n */
static char *dbp_name(char *s, uint32_t man, uint32_t i)
{
    const char *n;
    char *p = s;
    if (i >= dbp_count(man)) i = 0;
    if (dbp_is_user(man, i)) {
        uint32_t u = i - dbp_nfact(man) + 1u;
        for (n = "USER "; *n; ) *p++ = *n++;
        if (u >= 10u) *p++ = (char)('0' + u / 10u);
        *p++ = (char)('0' + u % 10u);
    } else {
        for (n = dbl_of(man, org_par.model)[i].name; *n; ) *p++ = *n++;
    }
    *p = 0;
    return p;
}

static uint32_t dbp_cur(void) { uint32_t i = dbp_sel(org_par.model)[mls.focus]; return i < dbp_count(mls.focus) ? i : 0u; }

/* the manual differs from its preset */
static uint32_t dbp_modified(uint32_t man)
{
    const uint8_t *p = dbp_get(man, dbp_sel(org_par.model)[man]);
    uint32_t d;
    for (d = 0; d < dbp_ndb(man); d++) if (org_par.drawbar[man][d] != p[d]) return 1;
    return 0;
}

static void dbp_recall(uint32_t man, uint32_t i)
{
    const uint8_t *p;
    uint32_t d;
    if (i >= dbp_count(man)) i = 0;
    dbp_sel(org_par.model)[man] = (uint8_t)i;
    p = dbp_get(man, i);
    for (d = 0; d < dbp_ndb(man); d++) org_par.drawbar[man][d] = p[d] > 8u ? 8u : p[d];
}

/* the entry that holds these drawbars (factory first, from the department's part), else the department's start */
static uint32_t dbp_match(uint32_t man, uint32_t model, const uint8_t *db)
{
    uint32_t n = dbp_count_m(man, model), st = dbp_start(model, man), k, d;
    for (k = 0; k < n; k++) {
        uint32_t i = (st + k) % n;
        const uint8_t *p = dbp_get_m(man, model, i);
        for (d = 0; d < dbp_ndb(man) && p[d] == db[d]; d++) { }
        if (d == dbp_ndb(man)) return i;
    }
    return st;
}

/* SAVE: into the user slot shown, or the first free one; the slot (1..), 0 = all user slots are taken */
static uint32_t dbp_store(uint32_t man)
{
    uint32_t i = dbp_sel(org_par.model)[man], nf = dbp_nfact(man), n = dbp_count(man), d;
    uint8_t *p;
    if (i >= n || !dbp_is_user(man, i)) {
        for (i = nf; i < n; i++) {
            const uint8_t *q = dbp_get(man, i);
            for (d = 0; d < dbp_ndb(man) && !q[d]; d++) { }
            if (d == dbp_ndb(man)) break;               /* all zero: free */
        }
        if (i >= n) return 0;
    }
    p = man == MAN_PEDAL ? st1.user_ped[i - nf] : st1.user[i - nf];
    for (d = 0; d < dbp_ndb(man); d++) p[d] = org_par.drawbar[man][d];
    dbp_sel(org_par.model)[man] = (uint8_t)i;
    st_dirty |= 3u;
    return i - nf + 1u;
}

static void dbp_factory(void)           /* the user slots empty */
{
    memset(&st1, 0, sizeof st1);
    st1.ver = STORE_VER;
}

/* ---- setups: the live state <-> a setup_t */
static int32_t ui_oct_get(void);        /* orgigs_ctl.c */
static void ui_oct_set(int32_t o);

static void setup_capture(setup_t *s)
{
    uint32_t m, d;
    reg_t *r = &s->r;
    r->model = org_par.model;
    for (m = 0; m < ORG_NMAN; m++) for (d = 0; d < ORG_NDB; d++) r->db[m][d] = org_par.drawbar[m][d];
    r->character = org_par.character; r->perc = org_par.perc; r->rev_mode = org_par.rev_mode;
    r->delay_time = org_par.delay_time; r->delay_fb = org_par.delay_fb; r->coupler = org_par.coupler;
    for (m = 0; m < ORG_NMAN; m++) {
        fxman_t *f = &r->fx[m];
        f->drive = org_par.drive[m]; f->vib = org_par.vib[m]; f->trem = org_par.trem[m];
        f->les = org_par.les_on[m]; f->dsend = org_par.dsend[m]; f->rsend = org_par.rsend[m];
    }
    s->focus = mls.focus;
    s->oct = (uint8_t)(ui_oct_get() + 2);
    for (m = 0; m < ORG_NMAN; m++) s->dbp[m] = dbp_sel(org_par.model)[m];
    for (d = 0; d < 16u; d++) s->mix[d] = ml_mix[d];
    s->zn = zn;
}

static void setup_apply(const setup_t *s)
{
    uint32_t m, d;
    const reg_t *r = &s->r;
    ml_release_all();
    org_par.model = r->model < ORG_NMODEL ? r->model : 0u;
    for (m = 0; m < ORG_NMAN; m++) for (d = 0; d < ORG_NDB; d++) org_par.drawbar[m][d] = r->db[m][d] > 8u ? 8u : r->db[m][d];
    org_par.character = r->character & 127u; org_par.perc = r->perc < ORG_NPERC_ALL ? r->perc : 0u;
    org_par.rev_mode = r->rev_mode < ORG_NREV ? r->rev_mode : 0u;
    org_par.delay_time = r->delay_time & 127u; org_par.delay_fb = r->delay_fb & 127u;
    org_par.coupler = r->coupler & 1u;
    for (m = 0; m < ORG_NMAN; m++) {
        const fxman_t *f = &r->fx[m];
        org_par.drive[m] = f->drive & 127u; org_par.vib[m] = f->vib < ORG_NVIB ? f->vib : 0u;
        org_par.trem[m] = f->trem & 127u; org_par.les_on[m] = f->les & 1u;
        org_par.dsend[m] = f->dsend & 127u; org_par.rsend[m] = f->rsend & 127u;
    }
    mls.focus = s->focus < ORG_NMAN ? s->focus : 0u;
    ui_oct_set(clampi((int32_t)s->oct - 2, -2, 3));
    for (m = 0; m < ORG_NMAN; m++)              /* (the baroque and the cathedral had the shared list in OrGigs 1) */
        dbp_sel(org_par.model)[m] = org_par.model < ORG_NMODEL1 && dbl_own(org_par.model) && m != MAN_PEDAL
                                  ? (uint8_t)dbp_match(m, org_par.model, r->db[m])
                                  : s->dbp[m] < dbp_count(m) ? s->dbp[m] : 0u;
    for (d = 0; d < 16u; d++) ml_mix[d] = s->mix[d] & 127u;
    zn = s->zn;
    zones_sanitise(&zn);
    mls.learn = 0;
    ml_harm_resend();
}

static void setup_recall(uint32_t i)
{
    i &= NSETUP - 1u;
    st0.setup = (uint8_t)i;
    setup_apply(&st0.setups[i]);
    ml_mix_send_used();                 /* the mixer's volumes to the channels this setup plays */
    st_dirty |= 1u;
}
static void setup_store(uint32_t i)
{
    i &= NSETUP - 1u;
    st0.setup = (uint8_t)i;
    setup_capture(&st0.setups[i]);
    st_dirty |= 1u;
}

/* ---- the flash: older versions */
typedef struct {                        /* versions 4 and 5: the sound of a setup, one effect chain */
    uint8_t model, db[ORG_NMAN][ORG_NDB];
    uint8_t character, vib, trem, perc, rev_mode, rev_mix, delay_mix, delay_time, delay_fb, leslie_bypass, coupler;
} reg5_t;
typedef struct {
    reg5_t r;
    uint8_t focus, oct, dbp[ORG_NMAN];
    zones_t zn;
} setup5_t;
typedef struct {                        /* version 5, sector 0 */
    uint8_t ver, setup, rate_safe, spare;
    setup5_t now;
    setup5_t setups[8];
    uint8_t dbp_sel[ORG_NMODEL1][ORG_NMAN];
} store5_t;
typedef struct {                        /* version 4 (four registrations) */
    uint16_t mask;
    uint8_t logic, local;
    int8_t ch_oct[16];
    int8_t transpose;
    uint8_t vel_level, vel_scaled, rate_safe, c_send, chord_n;
    int8_t chord[8];
} v4_ml_t;
typedef struct {
    uint8_t ver, snap, split, focus, split_key, oct;
    reg5_t snaps[4];
    v4_ml_t ml;
} v4_t;
static union { store5_t v5; v4_t v4; } st_old ORG_POOL;

/* one effect chain -> one per manual: the drive was the combo organs' character; the pedal gets no vibrato */
static void reg_from5(reg_t *r, const reg5_t *o)
{
    uint32_t m, vintage = dept_of(o->model) == DEPT_VINTAGE;
    memset(r, 0, sizeof *r);
    r->model = o->model;
    memcpy(r->db, o->db, sizeof r->db);
    r->character = vintage ? 64u : o->character;
    r->perc = o->perc; r->rev_mode = o->rev_mode; r->delay_time = o->delay_time; r->delay_fb = o->delay_fb;
    r->coupler = o->coupler;
    for (m = 0; m < ORG_NMAN; m++) {
        fxman_t *f = &r->fx[m];
        f->drive = vintage ? o->character : 0u;
        f->vib = m == MAN_PEDAL ? 0u : o->vib;
        f->trem = m == MAN_PEDAL ? 0u : o->trem;
        f->les = (uint8_t)(vintage && !o->leslie_bypass);
        f->dsend = o->delay_mix;
        f->rsend = o->rev_mix;
    }
}
static void setup_from5(setup_t *s, const setup5_t *o)
{
    uint32_t m;
    reg_from5(&s->r, &o->r);
    s->focus = o->focus; s->oct = o->oct;
    for (m = 0; m < ORG_NMAN; m++) s->dbp[m] = (uint8_t)dbp_match(m, s->r.model, s->r.db[m]);
    memset(s->mix, 100, sizeof s->mix);
    s->zn = o->zn;
}

static void store_factory0(void)
{
    uint32_t i, m;
    memset(&st0, 0, sizeof st0);
    st0.ver = STORE_VER;
    st0.lt_keys = LT_OFF; st0.lt_btns = LT_OFF; st0.lt_level = 3;
    for (i = 0; i < NSETUP; i++) {
        st0.setups[i] = SETUP_FACTORY[i];
        for (m = 0; m < ORG_NMAN; m++)
            st0.setups[i].dbp[m] = (uint8_t)dbp_match(m, st0.setups[i].r.model, st0.setups[i].r.db[m]);
    }
    for (i = 0; i < ORG_NMODEL; i++) for (m = 0; m < ORG_NMAN; m++) dbp_sel(i)[m] = (uint8_t)dbp_start(i, m);
    for (i = NSETUP; i-- > 0; )          /* each model shows what its first factory setup holds */
        for (m = 0; m < ORG_NMAN; m++) dbp_sel(st0.setups[i].r.model)[m] = st0.setups[i].dbp[m];
    st0.now = st0.setups[0];
}

static void store_apply_loaded(void);
static void store_load_all(void)        /* at power-on: what the flash holds, else the factory's */
{
    uint32_t i, m, ok0;
    for (i = ORG_NMODEL1; i < ORG_NMODEL; i++) for (m = 0; m < ORG_NMAN; m++) dbp_sel(i)[m] = (uint8_t)dbp_start(i, m);
    if (store_load(1, &st1, sizeof st1) != 0 || st1.ver != STORE_VER) dbp_factory();
    ok0 = store_load(0, &st0, sizeof st0) == 0 && st0.ver == STORE_VER;
    if (!ok0) {
        store_factory0();
        if (store_load(0, &st_old.v5, sizeof st_old.v5) == 0 && st_old.v5.ver == 5u) {   /* 8 setups, one effect chain */
            for (i = 0; i < 8u; i++) setup_from5(&st0.setups[i], &st_old.v5.setups[i]);
            setup_from5(&st0.now, &st_old.v5.now);
            st0.setup = st_old.v5.setup & 7u;
            st0.rate_safe = st_old.v5.rate_safe & 1u;
            for (i = 0; i < ORG_NMODEL; i++)
                for (m = 0; m < ORG_NMAN; m++) dbp_sel(i)[m] = (uint8_t)dbp_start(i, m);
            for (m = 0; m < ORG_NMAN; m++) dbp_sel(st0.now.r.model)[m] = st0.now.dbp[m];
            st_dirty |= 3u;
        } else if (settings_load(&st_old.v4, sizeof st_old.v4) == 0 && st_old.v4.ver == 4u) {   /* four registrations */
            for (i = 0; i < 4u; i++) {
                reg_from5(&st0.setups[i].r, &st_old.v4.snaps[i]);
                st0.setups[i].oct = st_old.v4.oct;
                for (m = 0; m < ORG_NMAN; m++)
                    st0.setups[i].dbp[m] = (uint8_t)dbp_match(m, st0.setups[i].r.model, st0.setups[i].r.db[m]);
            }
            st0.setup = st_old.v4.snap & 3u;
            st0.rate_safe = st_old.v4.ml.rate_safe & 1u;
            st0.now = st0.setups[st0.setup];
            st_dirty |= 3u;
        }
    }
    store_apply_loaded();
}

static void store_apply_loaded(void)    /* st0 / st1 just read (flash or a backup): checked, then they play */
{
    if (st0.lt_keys >= LT_N) st0.lt_keys = LT_OFF;
    if (st0.lt_btns >= LT_N) st0.lt_btns = LT_OFF;
    if (st0.lt_level >= LT_LEVELS) st0.lt_level = 3;
    ml_rate_safe = st0.rate_safe & 1u;
    org_par.equal = st0.rate_safe >> 1 & 1u;   /* (bit 1: a byte OrGigs 1 reads as bit 0 only) */
    ml_ctl_mode = st0.ctl_mode < CTL_N ? st0.ctl_mode : CTL_PITCH;
    org_par.bend_all = ml_ctl_mode == CTL_PITCH;
    setup_apply(&st0.now);
}

static void store_capture_now(void)     /* the panel as it is, into sector 0 (to save it or to send it) */
{
    setup_capture(&st0.now);
    st0.rate_safe = (uint8_t)(ml_rate_safe | org_par.equal << 1);
    st0.ctl_mode = ml_ctl_mode;
}

static void store_save_due(void)        /* writes what changed (the caller checks that nothing sounds) */
{
    if (st_dirty & 1u) {
        store_capture_now();
        store_save(0, &st0, sizeof st0);
    }
    if (st_dirty & 2u) store_save(1, &st1, sizeof st1);
    st_dirty = 0;
}
