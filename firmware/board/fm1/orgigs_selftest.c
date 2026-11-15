/* SPDX-License-Identifier: GPL-3.0-only */
/* Self-test build (tools/build.py --selftest, never for release): the firmware plays itself, so the FM-1
 * emulator runs every code path without anyone at the panel. Every 120 ms one step; 64 steps per model,
 * all 8 models (~61 s), then again with the settings reloaded from flash. Each model gets other MIDI zones
 * (channels, ranges, overlap, the FM-1's keys by zone, auto-pedal, thru, clone, harmonic, chord, filters,
 * output pacing), chords on all three zones over MIDI and on the panel keys, drawbar CCs, the knobs of
 * every screen with and without SEL, the screens' switches, rotary, bend, expression, reverb spaces, chord
 * capture, range learning, drawbar presets, performance setups, then 2.7 s of silence (reverb tail, save). */

static uint32_t st_t, st_i;

static void st_notes(uint32_t ch, uint32_t root, uint32_t on)
{
    static const uint8_t CHORD[4] = {0, 4, 7, 12};
    uint32_t j;
    for (j = 0; j < 4u; j++) ml_midi_in((uint8_t)((on ? 0x90u : 0x80u) | ch), (uint8_t)(root + CHORD[j]), on ? (uint8_t)(60u + 20u * j) : 0u);
}

static void selftest_step(void)
{
    uint32_t i, ph, m, k, round, z;
    if (fm1_ms - st_t < 120u) return;
    st_t = fm1_ms;
    i = st_i++;
    ph = i % 64u;
    m = (i / 64u) % ORG_NMODEL;
    round = i / (64u * ORG_NMODEL);
    if (i && ph == 0 && m == 0) store_load_all();   /* a new round: what the last one saved */

    switch (ph) {
    case 0:
        set_model((int32_t)m);
        set_mode(MODE_PLAY);
        zn = ZONES_DEFAULT;
        for (z = 0; z < ORG_NMAN; z++) {
            zone_t *q = &zn.z[z];
            q->flags = (uint8_t)(((m + round + z) & 1u ? Z_VELTHRU : Z_VELEXPR) | (m & 2u ? Z_NOCC : 0u) | (m & 4u ? Z_NOPC : 0u));
            q->flags |= (uint8_t)((m + z) % 4u == 1u ? Z_CLONE : (m + z) % 4u == 2u ? Z_HARM : (m + z) % 4u == 3u ? Z_CHORD : 0u);
            if (m == 5u && z == 1u) q->flags |= Z_NOINT;
            q->clone_mask = (uint16_t)(0x0F0u << (m & 3u));
            q->clone_oct[5] = -1; q->clone_oct[6] = 1;
            q->harm_base = (uint8_t)(m % 8u);
            q->harm_send = (uint8_t)(m % ML_NCSEND);
            q->transpose = (int8_t)((int32_t)m - 4);
            q->octave = (int8_t)(z == 2u ? -1 : 0);
            q->chord_n = 3; q->chord[0] = 0; q->chord[1] = 4; q->chord[2] = 7;
            if (m & 1u) { q->lo = (uint8_t)(36u + 12u * z); q->hi = (uint8_t)(72u + 6u * z); }   /* overlapping ranges */
        }
        if (m == 6u) zn.z[2].in_ch = CH_OFF;
        if (m == 7u) zn.z[0].out_ch = CH_OFF;
        zn.keys = (uint8_t)(m & 1u);
        zn.auto_pedal = (uint8_t)(m >> 1 & 1u);
        zn.thru = (uint8_t)(m >> 2 & 1u);
        ml_rate_safe = (uint8_t)(m & 1u);
        mls.focus = (uint8_t)(m % 3u);
        ctl.oct = (int32_t)(m % 6u) - 2;
        ui.grp = m % 3u;
        org_par.mute = 0;
        break;
    case 1: case 2: case 3:
        st_notes(ph - 1u, 40u + 7u * ph, 1);
        break;
    case 4: case 5: case 6: case 7: case 8: case 9:
        for (k = (ph - 4u) * 4u; k < (ph - 4u) * 4u + 4u && k < BOARD_NKEYS; k += 2u)
            ml_local_key(k, BOARD_KEY0 + (int32_t)k + 12 * ctl.oct, 1);
        break;
    case 10:
        for (k = 0; k < 9u; k++) ml_midi_in(0xB0u, (uint8_t)(12u + k), (uint8_t)(127u - 14u * k));
        ml_midi_in(0xC0u, 5u, 0u);                  /* program change */
        ml_midi_in(0xD1u, 50u, 0u);                 /* aftertouch */
        ml_midi_in(0xB9u, 7u, 99u);                 /* a channel no zone listens to */
        ml_midi_in(0x99u, 60u, 99u); ml_midi_in(0x89u, 60u, 0u);
        ml_midi_in(0xF8u, 0u, 0u);                  /* clock */
        break;
    case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18:
        /* the knobs of every screen, with and without SEL */
        if (ph < 13u) set_mode(MODE_PLAY);
        else if (ph < 15u) set_mode(MODE_FX);
        else { set_mode(MODE_MIDI); ui.ms = ph - 15u; }
        if (ph == 12u) { set_mode(MODE_MIX); ui.mix_row = round & 3u; }
        if (ph == 11u && (round & 1u)) set_mode(MODE_LIGHT);
        ui.shift = ph & 1u;
        for (k = 0; k < 4u; k++) knob(k, (ph & 2u) ? 3 : -2);
        ui_draw();
        ui.shift = 0;
        break;
    case 19: set_leslie(LESLIE_FAST); break;
    case 20: ml_midi_in(0xB0u, 1u, 127u); break;            /* the mod strip's job */
    case 21: ml_midi_in(0xE0u, 0x7Fu, 0x7Fu); ui_strip_pitch_local(3000); break;   /* bend up */
    case 22: ml_midi_in(0xE0u, 0x00u, 0x00u); ui_strip_mod_local(64); break;       /* bend down */
    case 23: ml_midi_in(0xE0u, 0x00u, 0x40u); ml_midi_in(0xB0u, 1u, 0u); ui_strip_pitch_local(0); break;
    case 24: ml_midi_in(0xB0u, 11u, 40u); break;
    case 25: set_leslie(LESLIE_BRAKE); break;
    case 26:
        org_par.rev_mode = (uint8_t)((m + round) % ORG_NREV);
        org_par.delay_fb = 100u; org_par.delay_time = (uint8_t)(m * 15u);
        org_par.perc = (uint8_t)(m % ORG_NPERC); org_par.character = 127u; org_par.coupler = (uint8_t)(m & 1u);
        for (z = 0; z < ORG_NMAN; z++) {
            org_par.rsend[z] = 90u; org_par.dsend[z] = 70u; org_par.vib[z] = (uint8_t)((m + z) % ORG_NVIB);
            org_par.trem[z] = 80u; org_par.drive[z] = (uint8_t)(127u - 40u * z); org_par.les_on[z] = (uint8_t)((m + z) % 3u != 0u);
        }
        st0.lt_keys = (uint8_t)(m % LT_N); st0.lt_btns = (uint8_t)((m + 1u) % LT_N); st0.lt_level = (uint8_t)((m + 3u * round) % LT_LEVELS);
        break;
    case 27: ml_chord_capture(); break;
    case 28: setup_store(m); touched(); break;
    case 29: set_leslie(LESLIE_SLOW); ml_midi_in(0xB0u, 11u, 127u); break;
    case 30: case 31: case 32: case 33:             /* the MIDI screens' switches, with and without SEL */
        set_mode(MODE_MIDI);
        ui.ms = ph - 30u;
        for (k = 0; k < 5u; k++) { ui.shift = round & 1u; midi_switch(k); ui.shift = 0; midi_switch(k); }
        ui_draw();
        break;
    case 34: mls.learn = 1; ml_local_key(20u, 70, 1); ml_local_key(20u, 0, 0); ml_midi_in((uint8_t)(0x90u | zn.z[mls.focus].in_ch % 16u), 40u, 90u); break;
    case 35: ml_midi_in((uint8_t)(0x80u | zn.z[mls.focus].in_ch % 16u), 40u, 0u); zones_sanitise(&zn); break;
    case 36: set_mode(MODE_FX); ui_draw(); break;
    case 37: dbp_recall(mls.focus, m * 2u); break;
    case 38: org_par.drawbar[mls.focus][0] = 5; dbp_store(mls.focus); break;
    case 39: ml_midi_in(0xB2u, 123u, 0u); break;   /* all notes off on the pedal's channel */
    case 40:
        for (k = 0; k < 3u; k++) st_notes(k, 40u + 7u * (k + 1u), 0);
        for (k = 0; k < BOARD_NKEYS; k++) ml_local_key(k, 0, 0);
        break;
    case 41: ml_release_all(); ml_midi_in(0xB0u, 123u, 0u); break;
    case 42: setup_recall((m + 3u) % NSETUP); touched(); break;
    case 43: st_notes(0, 60u, 1); ml_local_key(3u, 60, 1); ml_panic(); break;   /* panic over held notes */
    case 44: set_mode(MODE_PLAY); intro_start(); break;   /* the welcome screen (about 16 steps) */
    default: break;
    }
}

#ifdef ORGIGS_TONETEST
/* tools/build.py --tonetest: for recording the emulator's audio (-audiodev wav) and reading its load.
 *   default            tonewheel 8' alone (a sine), no effects, A4 on 1 s / off 1 s: 440 Hz, period 2.000 s, no jumps
 *   ORGIGS_TONE_FULL   the heavy case: registration 1 (rotary, chorus, spring), chords on both manuals and the
 *                      pedal, on 2 s / off 2 s; TF_NOLES / TF_NOVIB / TF_NOREV switch one effect off */
static void tonetest_step(void)
{
    static uint32_t on = 2, t0;
    uint32_t want;
    if (on == 2) {
#ifdef ORGIGS_TONE_FULL
        setup_recall(0);
#ifdef TF_NOLES
        org_par.les_on[0] = org_par.les_on[1] = org_par.les_on[2] = 0;
#endif
#ifdef TF_NOVIB
        org_par.vib[0] = org_par.vib[1] = org_par.vib[2] = 0;
#endif
#ifdef TF_NOREV
        org_par.rsend[0] = org_par.rsend[1] = org_par.rsend[2] = 0;
#endif
#else
        uint32_t m, d;
        set_model(MODEL_B3);
        for (m = 0; m < ORG_NMAN; m++) for (d = 0; d < ORG_NDB; d++) org_par.drawbar[m][d] = 0;
        org_par.drawbar[0][2] = 8;
        org_par.perc = 0; org_par.character = 0; org_par.coupler = 0;
        for (m = 0; m < ORG_NMAN; m++) {
            org_par.vib[m] = 0; org_par.trem[m] = 0; org_par.drive[m] = 0; org_par.les_on[m] = 0;
            org_par.rsend[m] = 0; org_par.dsend[m] = 0;
        }
#endif
        org_par.mute = 0;
        zn = ZONES_DEFAULT; mls.focus = 0;
        t0 = fm1_ms; on = 0;
    }
#ifdef ORGIGS_TONE_FULL
    if (fm1_ms - t0 < 500u) return;
    want = ((fm1_ms - t0) / 2000u) & 1u ? 0u : 1u;
    if (want != on) {
        on = want;
        st_notes(0, 60u, on); st_notes(1, 48u, on);
        ml_midi_in(on ? 0x92u : 0x82u, 36u, on ? 100u : 0u);
    }
#else
    want = ((fm1_ms - t0) / 1000u) & 1u ? 0u : 1u;
    if (want != on) { on = want; ml_midi_in(on ? 0x90u : 0x80u, 69u, on ? 100u : 0u); }
#endif
}
#endif
