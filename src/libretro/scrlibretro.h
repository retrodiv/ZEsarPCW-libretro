/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Native libretro video driver -- header.

    This file is part of the ZEsarPCW libretro core (derived from ZEsarUX) and is distributed under
    the GNU General Public License v3 (the same licence as ZEsarUX).
*/

#ifndef SCRLIBRETRO_H
#define SCRLIBRETRO_H

#include <stdint.h>

/* Driver entry points (mirror the other scr* drivers). */
extern int scrlibretro_init(void);
extern void scrlibretro_end(void);
extern void scrlibretro_refresca_pantalla(void);

/* --- bridge surface consumed by libretro.c --------------------------------- */

/* The persistent XRGB8888 framebuffer the emulator renders into. */
extern uint32_t *scrlibretro_framebuffer;

/* Allocated buffer geometry (a generous fixed maximum). */
extern int scrlibretro_max_width;
extern int scrlibretro_max_height;

/* Set to 1 by scrlibretro_refresca_pantalla() when a full video frame has been
   composed; retro_run() polls + clears it to know one frame elapsed. */
extern volatile int scrlibretro_frame_ready;

/* The drawable size for the current machine state (includes border), in pixels.
   Reported to the frontend as base_width/base_height each frame. */
extern int scrlibretro_cur_width(void);
extern int scrlibretro_cur_height(void);

#endif
