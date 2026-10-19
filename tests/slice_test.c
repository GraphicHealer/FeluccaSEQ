/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE engine tests on the host (same sources as the firmware, through hostsim.c; run_tests.sh):
 *   build/host/slice_test DEMO_DIR
 * FeluccaSEQ has no user sample slots: the USR / MAN / store coverage went with them. What stays:
 * 1. BREAK's build-time table (gen_samples.py ima_states) == the firmware decoder's states.
 * 3. REV: a slice read backwards (64-sample windows from checkpoints) == the forward decode reversed.
 * 4. keys / steps -> slices (mod the count, START, ROOT, an empty source plays the sine); ONE / GATE / LOOP.
 * 5. demos into DEMO_DIR: both presets with their patterns, BREAK re-sequenced.
 * 8. PIANO (SRC 4, 1.0.4): the SAMPLE PIANO zone of middle C itself (no copy), its table, one AUTO slice, played;
 *    a missing sample (SLICE / SAMPLE / GRAIN on a gone source) plays a sine at the note's pitch: zero crossings,
 *    level, no click, no other sample, it ends after the note-off. */
#include <stdarg.h>
#include <stdint.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#if !FELUCCA_SLICE
#error "slice_test needs the SLICE engine (FELUCCA_SLICE=1, the default)"
#endif
#define SLC_ENG 13u                                      /* SLICE's engine number (engines.c, the protocol's) */


static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("slice: %-58s %s", what, ok ? "ok" : "FAIL");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  (");
        vprintf(fmt, ap);
        printf(")");
        va_end(ap);
    }
    printf("\n");
    fails += !ok;
}

/* 1: every stored state == the decoder's state there; returns the mismatches */
static uint32_t table_check(const slc_src_t *s)
{
    slc_dec_t d;
    uint32_t pos, k = 0, a = 0, bad = 0;
    slc_dec_at(&d, 0, 0);
    for (pos = 0; pos < s->len; pos++) {
        while (k < SLC_GRID && slc_gpos(s, k) == pos)
            bad += s->grid[k++] != slc_dec_st(&d);
        while (a < s->nauto && s->apos[a] == pos)
            bad += s->ast[a++] != slc_dec_st(&d);
        slc_dec_next(s, &d);
    }
    return bad + (SLC_GRID - k) + (s->nauto - a);
}

/* 6: MAN slices are sorted, keep SLC_MIN apart (and from their end), and each state == the decoder's there */


/* 3: slice j of DIV div read backwards == forwards reversed */
static uint32_t rev_check(uint32_t src, uint32_t div, uint32_t j)
{
    const slc_src_t *s = slc_get(src);
    static int32_t fw[1 << 18];
    static int16_t rb[SLC_RB];
    voice_t v;
    slc_dec_t d;
    uint32_t a, b, st, i, n, bad = 0;
    int32_t x;
    slc_bounds(s, div, j, &a, &b, &st);
    slc_dec_at(&d, a, st);
    for (n = 0; d.pos < b && n < (1u << 18); n++)
        fw[n] = slc_dec_next(s, &d);
    memset(&v, 0, sizeof v);
    v.ph[0] = b;
    v.ph[2] = a;
    v.s[0] = 0x7FFFFFFF;
    v.s[4] = (int32_t)(src | (st >> 24) << 3 | 1u << 7 | j << 8 | div << 16);
    for (i = 0; i < n; i++)
        bad += !slc_rev(s, &v, rb, 0, &x) || x != fw[n - 1u - i];
    bad += slc_rev(s, &v, rb, 0, &x) != 0;               /* and then it ends */
    return bad;
}

static uint32_t slice_of(const voice_t *v) { return ((uint32_t)v->s[4] >> 8) & 63u; }
static voice_t *voice_of(track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
    return 0;
}
static void blocks(uint32_t n)
{
    int32_t o[2 * CTL];
    while (n--)
        mix_block(o, CTL);
}

/* 5: a pattern on track 1 (SLICE preset pi, then edits), the sequencer for `bars` bars, a tail */
typedef struct {
    const char *file;
    uint32_t preset, bpm, nsteps, bars;
    const uint8_t *notes;
    int16_t src, div;                            /* -1 = the preset's */
} demo_t;
static int demo(const char *dir, const demo_t *dm)
{
    char path[512];
    FILE *w;
    track_t *t = &trk[0];
    uint32_t f, i, frames, peak = 0, bar = 0;
    snprintf(path, sizeof path, "%s/%s", dir, dm->file);
    if (!(w = fopen(path, "wb")))
        return 1;
    host_tracks_init();
    song.g[G_BPM] = (int16_t)dm->bpm;
    host_preset(t, SLC_ENG, dm->preset);
    if (dm->src >= 0)
        t->p[P_E0] = dm->src;
    if (dm->div >= 0)
        t->p[P_E1] = dm->div;
    for (i = 0; i < dm->nsteps; i++) {
        uint8_t n = dm->notes[i];
        put_step(t, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
    t->p[P_SLEN] = (int16_t)dm->nsteps;
    bar = (uint32_t)(4.0 * 60.0 / dm->bpm * FS);
    frames = dm->bars * bar + FS;
    wav_hdr(w, frames);
    transport_req = 1;
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        if (f >= dm->bars * bar && f < dm->bars * bar + CTL)
            transport_req = 2;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t a = (uint32_t)abs(o[2 * i]);
            peak = a > peak ? a : peak;
            wav_put(w, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(w);
    printf("slice: demo %-30s %u bars at %u BPM, peak %u\n", dm->file, dm->bars, dm->bpm, peak);
    return peak < 2000u || peak > 32767u;
}

static int in_child(int (*fn)(const char *, const demo_t *), const char *dir, const demo_t *dm)
{
    pid_t pid;
    int st = 0;
    fflush(stdout);
    if (!(pid = fork())) {
        int rc = fn(dir, dm);
        fflush(stdout);
        _exit(rc);
    }
    waitpid(pid, &st, 0);
    return !WIFEXITED(st) || WEXITSTATUS(st);
}

/* 8: track 1 on engine eng (preset 0, SRC / SET src, no sends, ADSR 0 / 127 / 127 / 10) plays note for 0.5 s, then
 * lets go: the steady part's pitch (rising zero crossings over 0.4 s), its peak, the largest step between samples
 * (a click: more than a sine of that peak and pitch can move), the voice gone 0.3 s after the note-off */
static void voices_off(void)                               /* nothing sounding */
{
    uint32_t i, k;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            trk[k].v[i].active = 0;
}
typedef struct {
    double hz;
    uint32_t peak, step, ended, sine;
} sine_run_t;
static sine_run_t sine_run(uint32_t eng, int16_t src, uint32_t note)
{
    static int32_t x[FS];
    track_t *t = &trk[0];
    voice_t *v;
    sine_run_t r = {0, 0, 0, 0, 0};
    uint32_t i, k, n = 0, up = 0, first = 0, last = 0;
    voices_off();
    host_tracks_init();
    host_preset(t, eng, 0);
    t->p[P_E0] = src;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = 0;
    t->p[P_ATK] = 0;
    t->p[P_DEC] = 127;
    t->p[P_SUS] = 127;
    t->p[P_REL] = 10;
    trk_note_on(t, note, 100);
    v = voice_of(t, note);
    r.sine = v && (eng == SLC_ENG ? ((uint32_t)v->s[4] & 7u) == SLC_SINE : v->s[6] == 2);
#if FELUCCA_GRAIN
    r.sine = r.sine || (v && eng == 8u && v->s[0] == GR_SINE);
#endif
    for (k = 0; k < FS / 2u / CTL; k++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            x[n++] = o[2 * i];
    }
    trk_note_off(t, note);
    blocks(FS * 3u / 10u / CTL);
    r.ended = !(v && v->active);
    for (i = 1; i < n; i++) {
        uint32_t d = (uint32_t)abs(x[i] - x[i - 1u]), a = (uint32_t)abs(x[i]);
        r.step = d > r.step ? d : r.step;
        if (i >= n - FS * 2u / 5u) {                       /* the last 0.4 s held */
            r.peak = a > r.peak ? a : r.peak;
            if (x[i - 1u] < 0 && x[i] >= 0) {
                if (!up++)
                    first = i;
                last = i;
            }
        }
    }
    r.hz = up > 1u ? (double)(up - 1u) * FS / (double)(last - first) : 0.0;
    return r;
}
static int sine_ok(const char *what, sine_run_t r, double hz)
{
    char m[120];
    /* a sine of peak A at f moves at most 2 pi f A / FS a sample (+ a margin for the fade-in ramp) */
    uint32_t lim = (uint32_t)(6.2832 * hz * r.peak / FS * 1.25) + 64u;
    int ok = r.sine && r.hz > hz * 0.99 && r.hz < hz * 1.01 && r.peak > 4000u && r.peak < 30000u && r.step <= lim && r.ended;
    snprintf(m, sizeof m, "%s plays a sine (%.0f Hz), ends after the note-off", what, hz);
    check(m, ok, "%s, %.1f Hz, peak %u, largest step %u of %u, %s", r.sine ? "sine" : "NOT the sine", r.hz, r.peak,
          r.step, lim, r.ended ? "ended" : "still sounding");
    return ok;
}


int main(int argc, char **argv)
{
    static const uint8_t CHOP[16] = {60, 61, 62, 67, 64, 65, 60, 69, 68, 70, 62, 67, 72, 72, 74, 64};   /* ui.c 9..11 */
    static const uint8_t STUTTER[16] = {60, 60, 61, 61, 62, 0, 63, 63, 64, 65, 65, 0, 66, 66, 66, 67};
    static const uint8_t SLICES[16] = {60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75};
    static const uint8_t RESEQ[32] = {60, 0, 62, 60, 64, 0, 67, 62, 60, 69, 70, 0, 72, 0, 64, 72,   /* 2 bars */
                                      60, 61, 60, 61, 64, 0, 66, 67, 68, 64, 70, 71, 72, 72, 72, 72};
    const char *dir = argc > 1 ? argv[1] : "build/slice_demo";
    char msg[160];
    uint32_t i, bad;

    /* 1 */
    check("BREAK: build-time table == decoder states", !(bad = table_check(&SLC_BREAK)), "%u of %u differ", bad,
          SLC_GRID + SLC_BREAK.nauto);

    check("USR1..3: gone with the slots, no material", !slc_get(1) && !slc_get(2) && !slc_get(3), 0);

    /* 3 */
    for (bad = 0, i = 0; i < 16u; i++)
        bad += rev_check(0, 2, i);
    for (i = 0; i < 4u; i++)
        bad += rev_check(0, 0, i);
    for (i = 0; i < SLC_BREAK.nauto; i++)
        bad += rev_check(0, SLC_DIV_AUTO, i);
    check("REV: backwards == forwards reversed (BREAK; 4 / 16 / AUTO)", !bad, "%u samples differ", bad);

    /* 4: keys / steps -> slices */
    {
        track_t *t = &trk[0];
        voice_t *v;
        uint32_t a, b, st, ok = 1;
        host_tracks_init();
        host_preset(t, SLC_ENG, 0);                              /* BREAK 16 */
        trk_note_on(t, 65, 100);
        v = voice_of(t, 65);
        slc_bounds(&SLC_BREAK, 2, 5, &a, &b, &st);
        ok &= v && slice_of(v) == 5u && v->ph[0] == a && v->ph[2] == b;
        trk_note_on(t, 59, 100);
        ok &= (v = voice_of(t, 59)) && slice_of(v) == 15u;
        trk_note_on(t, 76, 100);
        ok &= (v = voice_of(t, 76)) && slice_of(v) == 0u;
        t->p[P_E2] = 3;                                    /* START */
        trk_note_on(t, 60, 100);
        ok &= (v = voice_of(t, 60)) && slice_of(v) == 3u;
        t->p[P_E2] = 0;
        t->p[P_ROOT] = 2;                                  /* ROOT D: D4 is slice 0 */
        trk_note_on(t, 62, 100);
        ok &= (v = voice_of(t, 62)) && slice_of(v) == 0u;
        check("keys: note - C4 - ROOT + START mod 16", ok, 0);
        ok = 1;
        t->p[P_ROOT] = 0;
        t->p[P_E1] = SLC_DIV_AUTO;
        trk_note_on(t, 72, 100);                           /* 12 mod 10 hits */
        ok &= (v = voice_of(t, 72)) && slice_of(v) == 12u % SLC_BREAK.nauto && v->ph[0] == SLC_BREAK.apos[12u % SLC_BREAK.nauto];
        t->p[P_E0] = 2;                                    /* USR2: empty -> the sine */
        trk_note_on(t, 61, 100);
        ok &= (v = voice_of(t, 61)) && ((uint32_t)v->s[4] & 7u) == SLC_SINE;
        check("keys: AUTO count, an empty source plays the sine", ok, 0);
        ok = 1;
        song.octave = 0;
        t->p[P_E0] = 0;
        ok &= kb_map(t, 0) == 60u && kb_map(t, 9) == 69u;
        check("keys: the lowest key is slice 0 (no scale)", ok, 0);
    }
    {   /* ONE / GATE / LOOP: a 125 ms slice, the note released after 10 ms */
        static const char *const MN[3] = {"ONE", "GATE", "LOOP"};
        uint32_t m;
        for (m = 0; m < 3u; m++) {
            track_t *t = &trk[0];
            voice_t *v;
            uint32_t held, after, k;
            host_tracks_init();
            host_preset(t, SLC_ENG, 0);
            t->p[P_E4] = (int16_t)m;
            t->p[P_REL] = 10;
            trk_note_on(t, 60, 100);
            v = voice_of(t, 60);
            if (m == SLC_LOOP) {
                blocks(FS / CTL);                          /* 1 s held: still looping */
                held = v && v->active;
            } else {
                held = 1;
            }
            blocks(FS / 100u / CTL);
            trk_note_off(t, 60);
            blocks(FS / 20u / CTL);                        /* 50 ms after the note-off */
            after = v && v->active;
            for (k = 0; k < FS / 4u / CTL && v && v->active; k++)
                blocks(1);
            snprintf(msg, sizeof msg, "MODE %s: %s", MN[m], m == SLC_ONE ? "plays on after the note-off, ends at the slice end" :
                     m == SLC_GATE ? "stops at the note-off" : "loops while held, ends after the note-off");
            check(msg, held && (m == SLC_ONE ? after : !after) && !(v && v->active), 0);
        }
    }

    {   /* engine 13 is SLICE (the protocol's number), two factory presets; DIV MAN over a built-in
         * source reads as AUTO (the MAN tables were per user slot: none left) */
        uint32_t a2, b2, st2;
        check("engine 13 is SLICE (the protocol's number), two factory presets",
              ENGINES[SLC_ENG] == &ENG_SLICE && ENG_SLICE.npresets == 2u && str_eq(ENG_SLICE.presets[0].name, "CHOP") &&
              str_eq(ENG_SLICE.presets[1].name, "STUTTER") && ENG_SLICE.presets[0].pat == 9u && ENG_SLICE.presets[1].pat == 10u, 0);
        slc_bounds(&SLC_BREAK, SLC_DIV_MAN, 3, &a2, &b2, &st2);
        check("MAN: none set = the AUTO slices (BREAK always)",
              slc_count(&SLC_BREAK, SLC_DIV_MAN) == SLC_BREAK.nauto && a2 == SLC_BREAK.apos[3] &&
              b2 == SLC_BREAK.apos[4] && !slc_man_begin(SLC_NUSR), 0);
    }

    /* 5 */
    {
        const demo_t D[] = {
            {"preset_chop.wav", 0, 120, 16, 4, CHOP, -1, -1},
            {"preset_stutter.wav", 1, 120, 16, 4, STUTTER, -1, -1},
            {"break_in_order.wav", 0, 120, 16, 2, SLICES, -1, -1},
            {"break_resequenced.wav", 0, 120, 32, 4, RESEQ, -1, -1},
            {"break_resequenced_32.wav", 0, 120, 32, 4, RESEQ, -1, 3},
            {"usr_empty_plays_sine.wav", 0, 120, 16, 2, SLICES, 2, SLC_DIV_AUTO},    /* USR2 empty: a sine per key */
        };
        int df = 0;
        for (i = 0; i < sizeof D / sizeof D[0]; i++)
            df += in_child(demo, dir, &D[i]);
        check("demos rendered (peak above -24 dBFS, no clipping)", !df, "%s", dir);
    }

    /* 8: PIANO (SRC 4), a missing sample's sine */
#ifdef SLC_PIANO_NOTE
    check("PIANO: build-time table == decoder states", !(bad = table_check(&SLC_PIANO)), "%u of %u differ", bad,
          SLC_GRID + SLC_PIANO.nauto);
    for (i = 0; i < NELEM(SMP_ZONES) && !(SMP_ZONES[i].root16 == SLC_PIANO_NOTE * 16 &&
                                          SMP_ZONES[i].off == SLC_PIANO.seg[0].off); i++)
        ;
    check("PIANO (SRC 4): the SAMPLE PIANO zone itself (no copy), one AUTO slice at 0",
          slc_get(SLC_SRC_PIANO) == &SLC_PIANO && i < NELEM(SMP_ZONES) && SLC_PIANO.nseg == 1u &&
          SLC_PIANO.len == SMP_ZONES[i].n && SLC_PIANO.seg[0].n == SLC_PIANO.len && SLC_PIANO.rate == SMP_ZONES[i].rate &&
          SLC_PIANO.nauto == 1u && SLC_PIANO.apos[0] == 0u && SLC_PIANO.ast[0] == 0u,
          "%u samples, %u AUTO", SLC_PIANO.len, SLC_PIANO.nauto);
    {   /* SRC 4 plays the zone from its start */
        track_t *t = &trk[0];
        voice_t *v;
        uint32_t ok;
        int32_t o[2 * CTL], pk = 0;
        voices_off();
        host_tracks_init();
        host_preset(t, SLC_ENG, 0);
        t->p[P_E0] = (int16_t)SLC_SRC_PIANO;
        t->p[P_E1] = SLC_DIV_AUTO;
        trk_note_on(t, 60, 100);
        v = voice_of(t, 60);
        ok = v && ((uint32_t)v->s[4] & 7u) == SLC_SRC_PIANO && v->ph[0] == 0u && v->ph[2] == SLC_PIANO.len;
        for (i = 0; i < 40u; i++) {
            mix_block(o, CTL);
            for (n = 0; n < (long)CTL; n++)
                pk = abs(o[2 * n]) > pk ? abs(o[2 * n]) : pk;
        }
        check("PIANO (SRC 4): a key plays the zone (AUTO: the whole note), sounding", ok && pk > 2000, "peak %d", pk);
    }
#else
    check("PIANO (SRC 4): none in a build without the CC0 samples", !slc_get(SLC_SRC_PIANO), 0);
#endif
    check("SRC: BREAK USR1 USR2 USR3 PIANO (append-only: 1.0.3's numbers kept)",
          ENG_SLICE.edit[0].max == 4 && str_eq(N_SLC_SRC[0], "BREAK") && str_eq(N_SLC_SRC[1], "USR1") &&
          str_eq(N_SLC_SRC[3], "USR3") && str_eq(N_SLC_SRC[4], "PIANO") && SLC_BREAK.len != 0u, 0);
    sine_ok("SLICE on an empty USR2: A4", sine_run(SLC_ENG, 2, 69), 440.0);
    sine_ok("SLICE on an empty USR2: A3", sine_run(SLC_ENG, 2, 57), 220.0);
#if FELUCCA_SAMPLE && !SMP_BUILTINS            /* (with built-ins a stored USR index mods onto a real set) */
    sine_ok("SAMPLE on a gone source: A4", sine_run(4u, (int16_t)(SMP_NSETS + 1), 69), 440.0);
#endif
#if FELUCCA_GRAIN && !SMP_BUILTINS
    sine_ok("GRAIN on a gone source: C5", sine_run(8u, (int16_t)(SMP_NSETS + 2), 72), 523.25);
#endif
    check("SLICE on USR1 (gone): the sine", sine_run(SLC_ENG, 1, 60).sine, 0);
#ifdef SLC_PIANO_NOTE
    check("SAMPLE on PIANO: not the sine", !sine_run(4u, 0, 60).sine, 0);
#endif
    printf("slice: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
