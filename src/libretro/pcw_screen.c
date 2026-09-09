/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>. Distributed under the GNU GPL v3.
*/
/* PCW-only replacement for ZEsarUX's standalone video/application layer.
   The libretro driver renders the complete PCW raster at 1:1 on every frame;
   it has no window zoom, putpixel cache, ZX Desktop, rainbow buffers, footer
   or cross-thread resize path. */

#include "cpu.h"
#include "pcw_screen_api.h"
#include "scrlibretro.h"

void (*scr_putpixel)(int x,int y,unsigned int color);

int screen_testados_linea;
int screen_testados_total;

void screen_set_video_params_indices(void)
{
    screen_testados_total=screen_testados_linea*312;
}

void cpu_loop_refresca_pantalla(void)
{
    scrlibretro_refresca_pantalla();
}

void set_t_scanline_draw_zero(void)
{
    t_scanline_draw=0;
}

void t_scanline_next_line(void)
{
    t_scanline_draw++;
    t_scanline++;
}
