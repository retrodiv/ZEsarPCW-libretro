/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>. Distributed under the GNU General Public License v3
    (the same licence as ZEsarUX, Copyright (C) Cesar Hernandez Bano).

    Unit test for the Smart-crop decision (src/libretro/pcw_crop.c).

    Exercises the centred / sticky-slide / full state machine directly, with no
    emulator: each case feeds a content bounding box and asserts the resulting crop
    rectangle and slid flag. Build + run from the repository root:

        cc -Isrc/libretro tests/test_crop.c src/libretro/pcw_crop.c -o /tmp/test_crop
        /tmp/test_crop            # prints PASS/FAIL, exit code != 0 on any failure

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#include <stdio.h>
#include "pcw_crop.h"

static int g_fail = 0, g_total = 0;

static void chk(const char *name, const pcw_crop_state *st,
                int slid, int x, int y, int w, int h)
{
    g_total++;
    if (st->slid != slid || st->x != x || st->y != y || st->w != w || st->h != h) {
        g_fail++;
        printf("FAIL  %-38s got slid=%d (%d,%d %dx%d)  want slid=%d (%d,%d %dx%d)\n",
               name, st->slid, st->x, st->y, st->w, st->h, slid, x, y, w, h);
    } else {
        printf("ok    %-38s slid=%d (%d,%d %dx%d)\n",
               name, st->slid, st->x, st->y, st->w, st->h);
    }
}

static void chk_bool(const char *name, int ok)
{
    g_total++;
    if (!ok) { g_fail++; printf("FAIL  %s\n", name); }
    else     printf("ok    %s\n", name);
}

#define DECIDE(W,H,a,b,c,d) pcw_crop_smart_decide((W),(H),(a),(b),(c),(d),&st)

int main(void)
{
    /* Mode 1 colour raster: 360x256 -> box 256x192 centred at (52,32). */

    /* A) a centred game stays the plain centred box, forever, never slides. */
    { pcw_crop_state st = {0,0,0,0,0};
      DECIDE(360,256, 52,32,307,223); chk("centred",          &st, 0, 52,32,256,192);
      DECIDE(360,256, 54,34,305,221); chk("centred (smaller)", &st, 0, 52,32,256,192);
      DECIDE(360,256, 52,32,307,223); chk("centred stable",    &st, 0, 52,32,256,192);
    }

    /* B) off-centre 256x192 -> slides, and STAYS slid even when a later frame would
          fit the centred box (sticky). */
    { pcw_crop_state st = {0,0,0,0,0};
      DECIDE(360,256, 52,0,307,191);  chk("off-centre top -> slide", &st, 1, 52,0,256,192);
      DECIDE(360,256, 60,40,300,210); chk("would fit centred -> STAY slid", &st, 1, 52,29,256,192);
      DECIDE(360,256, 52,0,307,191);  chk("off-centre again -> slid", &st, 1, 52,0,256,192);
    }

    /* C) a settled slid box does NOT drift while the content stays inside it. */
    { pcw_crop_state st = {0,0,0,0,0};
      DECIDE(360,256, 52,0,307,191);  chk("slide",          &st, 1, 52,0,256,192);
      DECIDE(360,256, 52,0,307,191);  chk("no drift (same)", &st, 1, 52,0,256,192);
      DECIDE(360,256, 60,8,299,183);  chk("no drift (inside)", &st, 1, 52,0,256,192);
    }

    /* D) slid -> content bigger than 256x192 even centred -> FULL (the only revert). */
    { pcw_crop_state st = {0,0,0,0,0};
      DECIDE(360,256, 52,0,307,191);  chk("slide",            &st, 1, 52,0,256,192);
      DECIDE(360,256, 0,0,359,255);   chk("too big -> full",  &st, 0, 0,0,360,256);
      DECIDE(360,256, 60,40,300,210); chk("then fits -> centred", &st, 0, 52,32,256,192);
    }

    /* E) off-centre content hard against the top-left -> box clamps to (0,0). */
    { pcw_crop_state st = {0,0,0,0,0};
      DECIDE(360,256, 0,0,255,191);   chk("top-left -> clamp", &st, 1, 0,0,256,192);
    }

    /* F) a screen change resets stickiness: a slid game then a centred screen
          decides afresh (-> centred, not stuck slid). */
    { pcw_crop_state st = {0,0,0,0,0};
      DECIDE(360,256, 0,0,255,191);   chk("slid",             &st, 1, 0,0,256,192);
      pcw_crop_state_reset(&st);
      DECIDE(360,256, 60,40,300,210); chk("after reset -> centred", &st, 0, 52,32,256,192);
    }

    /* G) content a hair over 256 wide (within tolerance) still snaps to 256x192. */
    { pcw_crop_state st = {0,0,0,0,0};
      DECIDE(360,256, 52,32,309,223); chk("258px wide -> snap (slide)", &st, 1, 53,32,256,192);
    }

    /* H) full-screen cover -> full. */
    { pcw_crop_state st = {0,0,0,0,0};
      DECIDE(360,256, 0,0,359,255);   chk("cover -> full",    &st, 0, 0,0,360,256);
    }

    /* I) mode 0 mono raster 720x256 -> box 512x192 centred at (104,32). */
    { pcw_crop_state st = {0,0,0,0,0};
      DECIDE(720,256, 104,32,615,223); chk("mono720 centred", &st, 0, 104,32,512,192);
    }

    /* ---- "once full, stay full" latch (grace = 5 frames for the test) ---- */
    #define GRACE 5
    int k;

    /* L1) within the grace, a full frame does NOT latch. */
    { pcw_crop_latch L; pcw_crop_latch_reset(&L);
      int r = 0;
      r |= pcw_crop_latch_step(&L, 0, GRACE);   /* box: starts the clock */
      r |= pcw_crop_latch_step(&L, 1, GRACE);   /* full inside grace */
      r |= pcw_crop_latch_step(&L, 0, GRACE);
      r |= pcw_crop_latch_step(&L, 1, GRACE);
      chk_bool("L1 grace ignores full", r == 0 && L.latched == 0);
    }

    /* L2) after the grace, the first full latches and stays forced (even if the
          content later fits 256x192 again). */
    { pcw_crop_latch L; pcw_crop_latch_reset(&L);
      for (k = 0; k < 6; k++) pcw_crop_latch_step(&L, 0, GRACE);   /* box past the grace */
      int r1 = pcw_crop_latch_step(&L, 1, GRACE);                  /* first full after grace */
      int r2 = pcw_crop_latch_step(&L, 0, GRACE);                  /* would be box -> forced full */
      chk_bool("L2 after grace full latches", r1 == 1 && r2 == 1 && L.latched == 1);
    }

    /* L3) a full-raster loader before any 256x192 view does NOT start the clock. */
    { pcw_crop_latch L; pcw_crop_latch_reset(&L);
      for (k = 0; k < 20; k++) pcw_crop_latch_step(&L, 1, GRACE); /* loader: full, no box yet */
      chk_bool("L3 loader does not start clock", L.latched == 0 && L.box_since == -1);
      pcw_crop_latch_step(&L, 0, GRACE);                          /* game box appears now */
      int during = pcw_crop_latch_step(&L, 1, GRACE);            /* full inside the fresh grace */
      chk_bool("L3 grace runs from the box", during == 0 && L.latched == 0);
    }

    /* L4) reset un-latches. */
    { pcw_crop_latch L; pcw_crop_latch_reset(&L);
      for (k = 0; k < 12; k++) pcw_crop_latch_step(&L, (k >= 6), GRACE); /* box then full */
      chk_bool("L4 latched before reset", L.latched == 1);
      pcw_crop_latch_reset(&L);
      chk_bool("L4 reset clears", L.latched == 0 && L.box_since == -1 && L.frames == 0);
    }

    printf("\n%s: %d/%d checks passed\n", g_fail ? "FAIL" : "PASS",
           g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
