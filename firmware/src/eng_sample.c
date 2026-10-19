/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAMPLE: IMA ADPCM sample sets.
 *
 * FELUCCA_SAMPLE=0 (the FeluccaSEQ default) builds without the DSP: index 4 stays reserved (ENG_SAMPLE_GONE,
 * never selectable, silent); a stored SAMPLE track keeps engine 4 and comes back whole with FELUCCA_SAMPLE=1.
 * The user-slot layer below (upload, scan, backup: editor.c, project.c) is built either way — the stored slots
 * outlive any one engine build — while the decode helpers it shares with GRAIN / SLICE follow their flags. */
#if FELUCCA_SAMPLE
static const char *const N_ONOFF_E[] = {"OFF", "ON"};
#endif
/* IMA ADPCM sample sets (tools/gen_samples.py), native rates, zones across
 * the keyboard, loops with the ADPCM state stored at the loop start.
 * SET is P_E0 (EDIT 1, KNOB 1). SET 4 was PERC, the GM drum kit: retired, an alias of PIANO; its sounds
 * load as the DRUM engine (core.h drum_from_perc). */
typedef struct {
    uint32_t off, n, ls, le;     /* byte offset, samples, loop start / end (sample index) */
    uint32_t rate;               /* source rate / 44100, Q16 */
    int16_t root16;              /* root pitch in 1/16 semitone */
    int16_t pred;                /* ADPCM state at the loop start */
    uint8_t idx, lo, hi, looped;
} smp_zone_t;
typedef struct {
    const char *name;
    uint16_t z0, nz;
} smp_set_t;
#include "felucca_samples.h"
#if defined(SMP_PERC_SLOT) && SMP_PERC_SLOT != SMP_SET_PERC
#error "tools/gen_samples.py PERC_SLOT != core.h SMP_SET_PERC"
#endif

#if FELUCCA_SAMPLE || FELUCCA_GRAIN || FELUCCA_SLICE   /* ADPCM decode: SAMPLE voices, GRAIN grains, SLICE's src, the
                                                        * SAMPLE wave display (ui_graph.c) */
static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (16 octaves down) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;     /* no loop for negative d16 */
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);   /* 2^(d/192) via the pitch table */
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}
#endif

/* FeluccaSEQ: the three user sample slots (Felucca's data region 0xA0000..0xDBFFF, the FSMP
 * headers uploaded by the web editor) are gone -- that flash is the pattern library (patlib.c,
 * PLAN.md FSQ1). Stored SET / SRC values keep their numbers; a sound that named USR1..3 mods
 * into the built-in range and, with no data, plays the sine below. */
#define SMP_NALL SMP_NSETS
static const char *const SMP_ALL_NAMES[SMP_NALL] = {SMP_SET_NAMES_INIT};
                                                /* (params.c: a SET / SRC edit desc's names are these) */

/* A missing sample (SAMPLE, GRAIN, SLICE: an empty or invalid user slot, a set this build has no data for) plays
 * this instead, like a hardware sampler with its sample gone: a plain sine at the note's pitch (+ d16, 1/16
 * semitones), at half the peak of a full sample, through the engine's one-pole low-pass (lp Q15, state *y) and the
 * voice's amp (the ADSR: no click at the start, phase 0, or the end). *ph its phase. The UI says so once
 * (engines.c snd_missing, ui_input.c sample_notice). */
#if FELUCCA_SAMPLE || FELUCCA_GRAIN || FELUCCA_SLICE
static void smp_sine(int32_t *out, uint32_t n, const vmod_t *m, int32_t d16, int32_t lp, uint32_t *ph, int32_t *y)
{
    uint32_t i, p = *ph, inc = pitch_inc((uint32_t)clamp(m->pitch16 + d16, 0, 2047));
    int32_t s = *y;
    for (i = 0; i < n; i++, p += inc) {
        s += mulq15((sine_i(p) >> 1) - s, lp);
        out[i] += voice_amp(s, m, i) << 1;
    }
    *ph = p;
    *y = s;
}
#endif
#if FELUCCA_SAMPLE || FELUCCA_GRAIN
/* SET / SRC si (0..SMP_NALL - 1) has no sample data (a built-in set this build has none for) */
static int smp_set_missing(uint32_t si)
{
    return !SMP_ZONES[SMP_SETS[si % SMP_NSETS].z0].n;
}
#endif

#if FELUCCA_SAMPLE
/* zone index in a voice (a stored 0x8000 | slot << 5 | zone of a user slot that no longer exists: zone 0) */
static inline const smp_zone_t *smp_zone(uint32_t zi)
{
    return zi < 0x8000u ? &SMP_ZONES[zi] : &SMP_ZONES[0];
}

/* voice: ph[0] position (samples), ph[1] fraction Q16, s[0] predictor, s[1] step
 * index, s[2] previous sample, s[3] current sample, s[4] zone, s[5] step Q16 */
static inline int32_t sample_next(const smp_zone_t *z, voice_t *v, int loop)
{
    uint32_t pos = v->ph[0], b = SMP_DATA[z->off + (pos >> 1)];
    uint32_t code = (pos & 1u) ? (b >> 4) : (b & 15u);
    int32_t step = IMA_STEP[(uint32_t)v->s[1] <= 88u ? v->s[1] : 88], vd = step >> 3;
    if (code & 4u)
        vd += step;
    if (code & 2u)
        vd += step >> 1;
    if (code & 1u)
        vd += step >> 2;
    v->s[0] = clamp(v->s[0] + ((code & 8u) ? -vd : vd), -32768, 32767);
    v->s[1] = clamp(v->s[1] + IMA_IDX[code & 7u], 0, 88);
    pos++;
    if (pos > z->le && z->looped && loop) {
        pos = z->ls;
        v->s[0] = z->pred;
        v->s[1] = z->idx;
    }
    v->ph[0] = pos;
    return v->s[0];
}

static void sample_note_on(track_t *t, voice_t *v)
{
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL, i, zi = 0xFFFFu;
    {                                               /* a built-in set: its zones split the keyboard */
        const smp_set_t *set = &SMP_SETS[si];
        for (i = 0; i < set->nz; i++)
            if (v->note >= SMP_ZONES[set->z0 + i].lo && v->note <= SMP_ZONES[set->z0 + i].hi) {
                zi = set->z0 + i;
                break;
            }
        v->s[4] = (int32_t)(zi == 0xFFFFu ? set->z0 : zi);
    }
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->s[0] = 0;
    v->s[1] = 0;
    v->s[2] = v->s[3] = 0;
    v->s[6] = zi == 0xFFFFu;     /* 1 = sample ended (one-shot); a kit key with no sound stays silent */
    v->s[7] = 0;                 /* lo-pass state */
    if (smp_set_missing(si))
        v->s[6] = 2;             /* 2 = no sample data: the sine (smp_sine; ph[0] its phase) */
}

static void sample_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const smp_zone_t *z = smp_zone((uint32_t)v->s[4]);
    uint32_t i, frac = v->ph[1];
    int32_t d16 = clamp(m->pitch16 + p[P_E1] * 16 - z->root16, -1536, 576);   /* <= 3 octaves up: bounded decode load */
    uint32_t r = pow2_q16(d16), stepq = (r >> 8) * (z->rate >> 8);   /* Q16 samples per output */
    int32_t bits = p[P_E2], lp = 4000 + ((clamp((p[P_E4] << 8) + m->cutoff, 0, 127 << 8) * 28767) >> 15);
    int32_t drv = p[P_E6], sh = bits / 10;
    if (v->s[6] == 2) {                               /* no sample data: the sine at the note's pitch + TUNE */
        smp_sine(out, n, m, p[P_E1] * 16, lp, &v->ph[0], &v->s[7]);
        return;
    }
    if (v->s[6] || !z->n) {
        v->active = 0;
        return;
    }
    if (v->ph[0] == 0 && v->ph[1] == 0 && v->s[3] == 0)
        v->s[3] = sample_next(z, v, p[P_E3]);         /* prime the interpolator */
    for (i = 0; i < n; i++) {
        int32_t s;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            v->s[2] = v->s[3];
            if (v->ph[0] >= z->n) {                   /* one-shot reached its end */
                v->s[6] = 1;
                v->s[3] = 0;
                if (frac >= 65536u) {                /* past the last sample-to-zero interval */
                    v->s[2] = 0;
                    frac &= 65535u;
                }
                break;
            }
            v->s[3] = sample_next(z, v, p[P_E3]);
        }
        s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
        if (sh)                                       /* BITS: 0 = clean, up to 12 bits removed */
            s = (s >> sh) << sh;
        if (drv)
            s = softclip(s + (((s >> 2) * (drv * 150)) >> 11));   /* = s * drv * 600 / 32768, no overflow */
        v->s[7] += mulq15(s - v->s[7], lp);           /* gentle tone control (CUT) */
        out[i] += voice_amp(v->s[7], m, i) << 1;
        if (v->s[6])
            break;
    }
    v->ph[1] = frac;
}

static const engine_t ENG_SAMPLE = {
    .name = "SAMPLE",
    .page_title = {"SET", "TONE"},
    .edit = {
        {"SET", F_ENUM, 0, SMP_NALL - 1, 0, SMP_ALL_NAMES, 0},
        {"TUNE", F_SEMI, -24, 24, 0, 0, 0},
        {"BITS", F_INT, 0, 127, 0, 0, 0},
        {"LOOP", F_ENUM, 0, 1, 1, N_ONOFF_E, 0},
        {"CUT", F_INT, 0, 127, 127, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    .presets = SMP_PRESET_TABLE,
    .npresets = SMP_NPRESETS,
    .note_on = sample_note_on,
    .render = sample_render,
    .knob = {P_E0, P_E4, P_ATK, P_REL},
    .sampled = 1,
};
#else
/* engine 4 without the DSP (as engine 1's ENG_FM4_GONE, 9's ENG_PHYS_GONE): never selectable, no presets, silent;
 * a stored SAMPLE track keeps engine 4 (its sounds come back whole with FELUCCA_SAMPLE=1) */
static void sample_gone_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void sample_gone_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)out; (void)n; (void)m;
}
static const engine_t ENG_SAMPLE_GONE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = {
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
    },
    .note_on = sample_gone_note_on,
    .render = sample_gone_render,
    .knob = {P_E0, P_E4, P_ATK, P_REL},
};
#endif
