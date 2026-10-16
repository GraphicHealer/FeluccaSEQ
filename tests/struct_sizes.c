/* SPDX-License-Identifier: GPL-3.0-only
 * sizeof the sequencer and project data structures: tools/size_report.py compiles and runs this
 * (the sizes the capacity plans budget with: a step, a track, the song's pattern copies, motion).
 * song_chain.c is included for chain_pattern_t, private to it; its externs get stubs. */
#include <string.h>
#include "../firmware/src/core.h"
#include "../firmware/src/song_chain.c"
#include <stdio.h>

static void seq_release(track_t *t) { (void)t; }
static void seq_stop(void) {}
static uint32_t div_samples(uint32_t div) { (void)div; return 0; }
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx) { (void)t; (void)period; (void)idx; return 0; }

int main(void)
{
    printf("NSTEP              %4u   steps per track\n", NSTEP);
    printf("NTRK               %4u   tracks\n", NTRK);
    printf("NVOICE             %4u   voices, shared budget\n", NVOICE);
    printf("MOTION_MAX         %4u   automation + lock records, shared\n", MOTION_MAX);
    printf("CHAIN_ROWS         %4u   song sections\n", CHAIN_ROWS);
    printf("step_t             %4u B  one step\n", (unsigned)sizeof(step_t));
    printf("step8_t            %4u B  step, old formats\n", (unsigned)sizeof(step8_t));
    printf("track_t            %4u B  one track, steps + runtime (x %u = trk)\n", (unsigned)sizeof(track_t), NTRK);
    printf("  trk[NTRK]        %4u B\n", (unsigned)sizeof(trk));
    printf("song_t             %4u B  song state\n", (unsigned)sizeof(song_t));
    printf("motion_event_t     %4u B  one automation record\n", (unsigned)sizeof(motion_event_t));
    printf("motion_store_t     %4u B  the 128 records\n", (unsigned)sizeof(motion_store_t));
    printf("chain_row_t        %4u B  one song section\n", (unsigned)sizeof(chain_row_t));
    printf("chain_config_t     %4u B  the 16 sections\n", (unsigned)sizeof(chain_config_t));
    printf("chain_pattern_t    %4u B  a slot's snapshot in SONG (x 4 = chain.source)\n", (unsigned)sizeof(chain_pattern_t));
    printf("chain              %4u B  the whole chain state\n", (unsigned)sizeof(chain));
    printf("steps / track      %4u B  step_t x NSTEP (in track_t)\n", (unsigned)(sizeof(step_t) * NSTEP));
    printf("runtime / track    %4u B  track_t without the steps\n", (unsigned)(sizeof(track_t) - sizeof(step_t) * NSTEP));
    printf("steps @128         %4u B  trk + chain.source if NSTEP were 128\n",
           (unsigned)((sizeof(track_t) + sizeof(step_t) * NSTEP) * NTRK + 4u * sizeof(step_t) * NTRK * NSTEP));
    return 0;
}
