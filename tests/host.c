/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs on a computer: the app (firmware/src/orgigs.c) runs unchanged on a fake FM-1 (sim/sim_platform.h).
 * A script plays it over time; out come the sound (WAV, as the USB audio would record it), the screen
 * (PPM at "shot" lines) and the MIDI it sends (printed).
 *
 *   tests/run_host.sh [script.txt]
 *
 * Script lines, T in ms:   T keys MASK       the 27 keys held (bit 0 = F3), hex or decimal
 *                          T btn ID          a button tapped (40 ms); T down ID / T up ID: held
 *                          T enc ID STEPS    an encoder turned (0 SELECT 1 ALGORITHM 2..5 KNOB1..4 6 PRESETS)
 *                          T midi ST D1 D2   USB-MIDI in
 *                          T sysex HEX       a SysEx in: the bytes between F0 and F7 (out: "SYSEX out F0..F7")
 * The SMK-37 (BOARD=smk37): keys MASK = all 37 (bit 0 = C3), and
 *                          T pad P V         a pad hit (V velocity; 0: let go), P 0..7 top row, 8..15 bottom
 *                          T fader F V       fader F (0..3) to V (0..127)
 *                          T wheel W RAW     the pitch (0) or mod (1) wheel, raw 0..1023
 *                          T vel V           the velocity of the keys pressed from now on
 *                          T batt RAW, T pedal RAW        the battery divider's, the pedal jack's raw ADC value (0 by default)
 *                          T usb 0 / 1       off / on a USB host (1 by default: the battery charges)
 *                          a shot also prints the pads' colours (PADS r,g,b ..)
 *                          T mod V / T bend V    the touch strips (0..127 / -8192..8191)
 *                          T shot            the screen to OUT-T.ppm
 *                          T end
 * ORGIGS_BLOB_LOG=1 prints each save of a flash blob ("blob N saved"); --params prints the parameter table
 * (core/orgigs_params.c, tools/params_doc.py) */
#include "../sim/sim_platform.h"
#ifdef ORG_LEVEL_DEBUG
static int32_t lv_dbg[3];               /* (orgigs_fx.c fills it) */
#endif

static void par_dump(void);            /* core/orgigs_params.c: the parameter table (--params) */
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { printf("%6u ms  MIDI out %02X %3u %3u\n", fm1_ms, st, d1, d2); }
static void host_settings_saved(void) { printf("%6u ms  settings saved\n", fm1_ms); }
static int sysex_send(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    printf("%6u ms  SYSEX out ", fm1_ms);
    for (i = 0; i < n; i++) printf("%02X", p[i]);
    printf("\n");
    return 0;
}

enum { EV_KEYS, EV_BTN, EV_DOWN, EV_UP, EV_ENC, EV_MIDI, EV_MOD, EV_BEND, EV_SHOT, EV_SYSEX, EV_PAD, EV_FADER, EV_WHEEL, EV_VEL, EV_BATT, EV_PEDAL, EV_USB };
typedef struct { uint32_t t, kind; int32_t a, b, c; uint8_t *sx; unsigned long long m; } host_ev_t;
static host_ev_t ev[8192];
static uint32_t ev_n, end_ms = 5000;
static const char *host_out = "build/host/orgigs";
static int16_t *wav;
static uint32_t wav_n;
static uint32_t btn_release_at[FM1_NBTN];

static void read_script(const char *path)
{
    static char line[4096];
    char what[16];
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f) && ev_n < 8192u) {
        host_ev_t e = {0, 0, 0, 0, 0, 0, 0};
        unsigned t;
        int a = 0, b = 0, c = 0;
        if (line[0] == '#' || sscanf(line, "%u %15s", &t, what) != 2) continue;
        e.t = t;
        sscanf(line, "%*u %*s %i %i %i", &a, &b, &c);
        e.a = a; e.b = b; e.c = c;
        if (!strcmp(what, "end")) { end_ms = t; continue; }
        else if (!strcmp(what, "keys")) { e.kind = EV_KEYS; sscanf(line, "%*u %*s %lli", (long long *)&e.m); }
        else if (!strcmp(what, "pad")) e.kind = EV_PAD;
        else if (!strcmp(what, "fader")) e.kind = EV_FADER;
        else if (!strcmp(what, "wheel")) e.kind = EV_WHEEL;
        else if (!strcmp(what, "vel")) e.kind = EV_VEL;
        else if (!strcmp(what, "batt")) e.kind = EV_BATT;
        else if (!strcmp(what, "pedal")) e.kind = EV_PEDAL;
        else if (!strcmp(what, "usb")) e.kind = EV_USB;
        else if (!strcmp(what, "btn")) e.kind = EV_BTN;
        else if (!strcmp(what, "down")) e.kind = EV_DOWN;
        else if (!strcmp(what, "up")) e.kind = EV_UP;
        else if (!strcmp(what, "enc")) e.kind = EV_ENC;
        else if (!strcmp(what, "midi")) e.kind = EV_MIDI;
        else if (!strcmp(what, "mod")) e.kind = EV_MOD;
        else if (!strcmp(what, "bend")) e.kind = EV_BEND;
        else if (!strcmp(what, "shot")) e.kind = EV_SHOT;
        else if (!strcmp(what, "sysex")) {
            char hex[4096];
            uint32_t i, n;
            if (sscanf(line, "%*u %*s %4095s", hex) != 1) continue;
            n = (uint32_t)strlen(hex) / 2u;
            e.kind = EV_SYSEX; e.a = (int32_t)n; e.sx = malloc(n ? n : 1u);
            for (i = 0; i < n; i++) { unsigned v; sscanf(hex + 2u * i, "%2x", &v); e.sx[i] = (uint8_t)v; }
        }
        else continue;
        ev[ev_n++] = e;
    }
    fclose(f);
}

static void write_ppm(uint32_t t)
{
    char name[512];
    FILE *f;
    uint32_t i;
    snprintf(name, sizeof name, "%s-%05u.ppm", host_out, t);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
    printf("%6u ms  screen -> %s\n", t, name);
}

static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
}

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t step, owed;
    static int32_t blk[2u * HALF_FRAMES];
    uint32_t i;
    fm1_ms++;
    for (i = 0; i < FM1_NBTN; i++)
        if (btn_release_at[i] && btn_release_at[i] <= fm1_ms) { host_button(i, 0); btn_release_at[i] = 0; }
    while (step < ev_n && ev[step].t <= fm1_ms) {
        host_ev_t *e = &ev[step++];
        switch (e->kind) {
        case EV_KEYS: host_keys(e->m); break;
#if BOARD_SMK37
        case EV_PAD: host_pad((uint32_t)e->a, (uint32_t)e->b); break;
        case EV_FADER: if (e->a >= 0 && e->a < 4) fm1_in.fader[e->a] = (uint8_t)e->b; break;
        case EV_WHEEL: if (e->a >= 0 && e->a < 2) fm1_in.wheel[e->a] = (uint16_t)e->b; break;
        case EV_PEDAL: fm1_in.pedal = (uint16_t)e->a; break;
        case EV_VEL: host_vel = (uint32_t)e->b ? (uint32_t)e->b : (uint32_t)e->a; break;
#endif
        case EV_BATT: sim_batt = (uint32_t)e->a; break;
        case EV_USB: sim_usb_up = (uint32_t)e->a; break;
        case EV_BTN: host_button((uint32_t)e->a, 1); if ((uint32_t)e->a < FM1_NBTN) btn_release_at[e->a] = fm1_ms + 40u; break;
        case EV_DOWN: host_button((uint32_t)e->a, 1); break;
        case EV_UP: host_button((uint32_t)e->a, 0); break;
        case EV_ENC: if (e->a >= 0 && (uint32_t)e->a < FM1_NENC) host_enc[e->a] += e->b; break;
        case EV_MIDI: host_midi_in((uint8_t)e->a, (uint8_t)e->b, (uint8_t)e->c); break;
        case EV_MOD: ui_strip_mod_local((uint32_t)e->a); break;
        case EV_BEND: ui_strip_pitch_local(e->a); break;
        case EV_SYSEX: app_sysex(e->sx, (uint32_t)e->a); break;
        case EV_SHOT:
            write_ppm(fm1_ms);
#if BOARD_SMK37
            {
                uint32_t p;
                printf("%6u ms  PADS", fm1_ms);
                for (p = 0; p < 16u; p++) printf(" %u,%u,%u", host_padrgb[p][0], host_padrgb[p][1], host_padrgb[p][2]);
                printf("\n");
            }
#endif
            break;
        default: break;
        }
    }
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        wav = realloc(wav, (wav_n + HALF_FRAMES) * 4u);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += HALF_FRAMES;
    }
#ifdef ORG_LEVEL_DEBUG
    if (fm1_ms % 100u == 0u) {
        fprintf(stderr, "LV %u %d %d %d\n", fm1_ms, lv_dbg[0], lv_dbg[1], lv_dbg[2]);
        lv_dbg[0] = lv_dbg[1] = lv_dbg[2] = 0;
    }
#endif
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--params")) { par_dump(); return 0; }
    if (argc < 3) { fprintf(stderr, "usage: %s OUT script.txt | --params\n", argv[0]); return 1; }
    host_out = argv[1];
    host_blob_log = getenv("ORGIGS_BLOB_LOG") != 0;
#if BOARD_SMK37
    fm1_in.wheel[0] = 512;              /* the pitch wheel rests in the middle */
    fm1_in.wheel[1] = 1023;             /* the mod wheel down (the Elite reads it backwards) */
#endif
    read_script(argv[2]);
    app_main();
    return 0;
}
