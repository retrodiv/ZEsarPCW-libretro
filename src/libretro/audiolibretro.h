/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Native libretro audio driver -- header.

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#ifndef AUDIOLIBRETRO_H
#define AUDIOLIBRETRO_H

#include <stdint.h>

extern int  audiolibretro_init(void);
extern void audiolibretro_send_frame(const int8_t *buffer);
extern void audiolibretro_flush_frame(void);
extern void audiolibretro_discard_frame(void);
extern void audiolibretro_silence_frame(void);
/* Pure conversion helper, exported inside the core only (hidden visibility).
   Kept separate from delivery/mixing so every int8 value can be unit-tested. */
extern int audiolibretro_convert_s8(const int8_t *input, int input_frames,
                                    int source_rate, int output_rate,
                                    int16_t *output, int output_capacity);

#endif
