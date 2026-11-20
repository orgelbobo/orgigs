/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs on the M-VAVE FM-1: what the core needs to know about the board. */
#define BOARD_NAME "FM-1"
#define BOARD_NKEYS 27u                 /* the panel's keys: F3 .. G5 */
#define BOARD_KEY0 53                   /* key 0 = F3 */
#define BOARD_SETUP_K0 0u               /* the 16 white keys of the setups start at key 0 (F3) */
#define BOARD_SAVE_NAME "SAVE"          /* the SAVE button, as it is printed */
#define BOARD_LIGHT_HINT "SEL+GLO OR HOME: BACK"
#define BOARD_YST_ON "ENV"              /* YST: the buttons that switch it on, and read the SEQTRAK again */
#define BOARD_YST_REF "LFO"
#define BOARD_YST_HINT "SELECT: TRACK  PRESETS: SOUND"
#define BOARD_FXI_PICK "EDIT: PICK ONE"   /* INSERT (FX twice): a knob turned with no effect chosen */
#define BOARD_CC1_WAH 1                 /* while the insert is the WAH, CC 1 (the mod strip's) is its pedal */
#define BOARD_BAT_SCALE 6810u           /* the battery divider (core/orgigs_bat.c): a full cell reads 624, 4.15 V */
typedef uint32_t board_keys_t;          /* the keys held, bit k = key k */
