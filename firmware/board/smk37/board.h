/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs on the M-VAVE SMK-37 (Pro, Elite, MKE-P37: one keyboard): what the core needs to know about the board.
 * All 37 keys play (C3 .. C6, with velocity: fm1_in.keys, fm1_in.key_vel); the pads, the faders and the wheels are
 * the panel's (board.c). */
#define BOARD_NAME "SMK-37"
#define BOARD_NKEYS 37u                 /* the keybed: C3 .. C6 */
#define BOARD_KEY0 48                   /* key 0 = C3 */
#define BOARD_SAVE_NAME "BT"            /* the FM-1's SAVE is the button printed BT */
#define BOARD_LIGHT_HINT "SEQ: BACKUP  PATCH: BACK"
#define BOARD_YST_ON "CHORD"            /* YST: the buttons that switch it on, and read the SEQTRAK again */
#define BOARD_YST_REF "NOTE RPT"
#define BOARD_YST_HINT "PADS: TRACK  < >: SOUND"
typedef uint64_t board_keys_t;
#define BOARD_SPLASH 1                  /* a model or layout chosen: its name big over the whole screen (orgigs_ui.c) */
static void board_splash(const char *kind, const char *name, const char *more);
#define BOARD_BK_EXTRA_MAX 512u         /* the backup's extra part: the rotary cabinets, the insert effect, the master
                                           (board.c: smk_bk_put, smk_bk_get) */
