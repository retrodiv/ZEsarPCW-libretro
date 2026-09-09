/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Native libretro video driver.

    ZEsarUX renders through a small driver abstraction (scr_putpixel,
    scr_refresca_pantalla, ...). This driver is the libretro back end: every
    pixel the Amstrad PCW renderer emits lands in a persistent XRGB8888
    framebuffer that retro_run() hands to the frontend's video callback. There is
    no SDL / X11 / framebuffer-device dependency; the frontend owns the window.

    Modelled on the upstream fbdev driver (video/scrfbdev.c), which likewise
    writes linear RGB into a memory buffer.

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#include <stdlib.h>

#include "scrlibretro.h"
#include "pcw_screen_api.h"
#include "pcw_debug.h"

/* Defined in machines/pcw.c -- composes the whole PCW screen + border into the
    driver via scr_putpixel. */
extern void scr_refresca_pantalla_y_border_pcw(void);

/* The framebuffer is the PCW's NATIVE display: the largest mode is the monochrome
   720x256 grid, so the buffer stride is 720 and we deliver each mode at its real
   per-mode size (720/360/180 x 256) with no border and no vertical doubling -- the
   frontend scales the native frame to the monitor's 4:3 aspect. SCRLIBRETRO_MAXW
   doubles as the row stride; the per-mode width reported each frame is <= it. */
#define SCRLIBRETRO_MAXW 720
#define SCRLIBRETRO_MAXH 256

uint32_t *scrlibretro_framebuffer = NULL;
int scrlibretro_max_width  = SCRLIBRETRO_MAXW;
int scrlibretro_max_height = SCRLIBRETRO_MAXH;
volatile int scrlibretro_frame_ready = 0;

/* The native size of the active PCW video mode (defined in machines/pcw.c). */
extern int pcw_get_native_width(void);
extern int pcw_get_native_height(void);

int scrlibretro_cur_width(void)  { return pcw_get_native_width(); }
int scrlibretro_cur_height(void) { return pcw_get_native_height(); }

/* --- putpixel: the only path that actually touches the framebuffer ---------- */

void scrlibretro_putpixel(int x, int y, unsigned int color)
{
    if (x < 0 || y < 0 || x >= SCRLIBRETRO_MAXW || y >= SCRLIBRETRO_MAXH) return;
    /* The PCW supplies RGB888 directly and has no ZEsarUX menu overlay. */
    scrlibretro_framebuffer[y * SCRLIBRETRO_MAXW + x] =
        0xFF000000u | (color & 0x00FFFFFFu);
}

/* --- per-frame compose ------------------------------------------------------ */

void scrlibretro_refresca_pantalla(void)
{
    /* PCW-only core: compose the Amstrad PCW screen + border into the buffer. */
    scr_refresca_pantalla_y_border_pcw();

    /* Signal retro_run() that exactly one video frame has been produced. */
    scrlibretro_frame_ready = 1;
}

int scrlibretro_init(void)
{
    debug_printf(VERBOSE_INFO, "Init libretro Video Driver");

    if (scrlibretro_framebuffer == NULL) {
        scrlibretro_framebuffer = calloc((size_t)SCRLIBRETRO_MAXW * SCRLIBRETRO_MAXH, sizeof(uint32_t));
        if (scrlibretro_framebuffer == NULL) {
            debug_printf(VERBOSE_ERR, "libretro video: out of memory allocating framebuffer");
            return 1;
        }
    }

    scr_putpixel           = scrlibretro_putpixel;

    /* 1:1 pixels -- the frontend scales; ZEsarUX never zooms internally here. */
    return 0;
}

void scrlibretro_end(void)
{
    debug_printf(VERBOSE_INFO, "Closing libretro video driver");
    if (scrlibretro_framebuffer != NULL) {
        free(scrlibretro_framebuffer);
        scrlibretro_framebuffer = NULL;
    }
}
