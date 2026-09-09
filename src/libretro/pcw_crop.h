/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Smart-crop decision: given the raster size and the content's (windowed-max)
    bounding box, decide the crop rectangle for the "Smart" crop mode. Kept as a
    pure function (no globals, no framebuffer access) so it can be unit-tested
    directly -- libretro.c does the paper detection + windowing and feeds the bbox
    in here. See tests/test_crop.c.

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#ifndef PCW_CROP_H
#define PCW_CROP_H

/* The persistent Smart-crop state: the committed crop rectangle plus whether it is
   currently a slid (off-centre) 256x192 box. w == 0 means "nothing committed yet". */
typedef struct {
    int slid;          /* 1 = the current crop is a slid off-centre 256x192 box */
    int x, y, w, h;    /* committed crop rectangle */
} pcw_crop_state;

/* Reset to "decide afresh" -- clears the sticky-slide flag (call on a screen
   change). The rectangle is left as-is; the next decide() overwrites it. */
void pcw_crop_state_reset(pcw_crop_state *st);

/* Update *st to the Smart crop for raster W x H with content bbox
   [mnx..mxx] x [mny..mxy]:

     - content fits the central 256x192 box (and not already slid) -> that centred
       box, unchanged (stable -- no re-centring);
     - already slid and content still <= 256x192 -> STAY slid, nudging the box only
       if the content would otherwise spill out of it (never snaps back to centred);
     - content <= 256x192 but off-centre (spills the centred box) -> slide to
       contain it, and latch the slid state;
     - content larger than 256x192 even centred -> full raster (and unlatch).

   The 256x192 box width scales with the mode (W*256/360); a few px of tolerance
   lets a content that is a hair over still snap. */
void pcw_crop_smart_decide(int W, int H, int mnx, int mny, int mxx, int mxy,
                           pcw_crop_state *st);

/* "Once full, stay full" latch (on top of the Smart decision).

   The first `grace` frames from when the game FIRST shows the 256x192 view are
   ignored (a game can flip resolution rapidly right after starting). After that,
   the first frame the Smart decision is the full raster latches it: from then on
   the latch stays engaged and the crop never returns to 256x192. Counting from the
   first 256x192 view (not from reset) means a title that loads for a while -- its
   loader is full-raster, so the clock has not started -- still gets its full grace
   once the actual game screen appears. Reset per game with pcw_crop_latch_reset. */
typedef struct {
    int  latched;      /* 1 = force full raster from now on */
    long frames;       /* frames counted this session */
    long box_since;    /* frame the 256x192 view first appeared (-1 = not yet) */
} pcw_crop_latch;

void pcw_crop_latch_reset(pcw_crop_latch *L);

/* Advance the latch one frame. decided_full = 1 if the Smart decision this frame is
   the full raster (else it is a 256x192 box). Returns 1 if the full raster must be
   forced this frame (the latch is engaged). */
int  pcw_crop_latch_step(pcw_crop_latch *L, int decided_full, long grace);

#endif /* PCW_CROP_H */
