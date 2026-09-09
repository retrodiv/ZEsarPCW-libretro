/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * ZEsarPCW -- Amstrad PCW libretro core.
 * Copyright (c) 2026 retrodiv <retrodiv@proton.me>
 *
 * Distributed under the GNU General Public License, version 3.
 * This program comes WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE
 * file at the repository root for the full licence terms.
 */

/* Video/timing surface used by the PCW-only engine (GPLv3). */
#ifndef ZESARPCW_PCW_SCREEN_API_H
#define ZESARPCW_PCW_SCREEN_API_H

extern void (*scr_putpixel)(int x, int y, unsigned int color);
extern int screen_testados_linea;
extern int screen_testados_total;

void screen_set_video_params_indices(void);
void cpu_loop_refresca_pantalla(void);
void set_t_scanline_draw_zero(void);
void t_scanline_next_line(void);

#endif
