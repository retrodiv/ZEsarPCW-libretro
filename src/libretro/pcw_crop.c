/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Smart-crop decision -- see pcw_crop.h.

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#include "pcw_crop.h"

/* A few px of slack so content that is a hair over 256x192 still snaps. */
#define PCW_CROP_TOL 4

void pcw_crop_state_reset(pcw_crop_state *st)
{
    st->slid = 0;
}

void pcw_crop_smart_decide(int W, int H, int mnx, int mny, int mxx, int mxy,
                           pcw_crop_state *st)
{
    /* the central 256x192 game area for this mode (width scales with the mode). */
    int fw = (int)((long)W * 256 / 360);   if (fw > W) fw = W;
    int fh = (H < 192) ? H : 192;
    int fx = (W - fw) / 2, fy = (H - fh) / 2;
    int ew = mxx - mnx + 1, eh = mxy - mny + 1;
    int fits_centred = (mnx >= fx && mxx < fx + fw && mny >= fy && mxy < fy + fh);
    int fits_size    = (ew <= fw + PCW_CROP_TOL && eh <= fh + PCW_CROP_TOL);

    if (st->slid && fits_size) {
        /* STICKY: already showing an off-centre 256x192 crop and the content still
           fits 256x192 -- STAY slid, do NOT snap back to the centred box. Nudge the
           box only if the content would otherwise spill out of the current one;
           otherwise leave it exactly where it is (no drift). */
        int contained = (st->w == fw && st->h == fh &&
                         mnx >= st->x && mxx < st->x + fw &&
                         mny >= st->y && mxy < st->y + fh);
        if (!contained) {
            int cx = (mnx + mxx + 1 - fw) / 2;
            int cy = (mny + mxy + 1 - fh) / 2;
            if (cx < 0) cx = 0; else if (cx + fw > W) cx = W - fw;
            if (cy < 0) cy = 0; else if (cy + fh > H) cy = H - fh;
            st->x = cx; st->y = cy; st->w = fw; st->h = fh;
        }
    } else if (fits_centred) {
        /* DEFAULT: content sits inside the centred box -> the plain central 256x192,
           NO re-centring. Stable for every game that draws centred. */
        st->x = fx; st->y = fy; st->w = fw; st->h = fh; st->slid = 0;
    } else if (fits_size) {
        /* content is <=256x192 but drawn off-centre so it spills the centred box ->
           slide the box to contain it, and latch the slid state. From here it STAYS
           slid (the branch above) until the content no longer fits 256x192 at all. */
        int cx = (mnx + mxx + 1 - fw) / 2;
        int cy = (mny + mxy + 1 - fh) / 2;
        if (cx < 0) cx = 0; else if (cx + fw > W) cx = W - fw;
        if (cy < 0) cy = 0; else if (cy + fh > H) cy = H - fh;
        st->x = cx; st->y = cy; st->w = fw; st->h = fh; st->slid = 1;
    } else {
        /* content fills more than 256x192 even centred -> full raster. */
        st->x = 0; st->y = 0; st->w = W; st->h = H; st->slid = 0;
    }
}

void pcw_crop_latch_reset(pcw_crop_latch *L)
{
    L->latched = 0;
    L->frames = 0;
    L->box_since = -1;
}

int pcw_crop_latch_step(pcw_crop_latch *L, int decided_full, long grace)
{
    if (L->latched) return 1;              /* already engaged -> stay full */
    L->frames++;
    /* the grace clock starts the first time the game shows the 256x192 view */
    if (!decided_full && L->box_since < 0) L->box_since = L->frames;
    /* after the grace, the first switch to full raster latches it for good */
    if (L->box_since >= 0 && L->frames - L->box_since >= grace && decided_full)
        L->latched = 1;
    return L->latched;
}
