/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    On-screen keyboard: a PCW keyboard drawn over the playfield, navigated with the
    pad, so the text adventures and CP/M programs in the catalogue are playable
    without a physical keyboard. Drawn by the core into its own framebuffer (no
    dependency on frontend overlays).

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#ifndef PCW_OSK_H
#define PCW_OSK_H

#include <stdint.h>

/* Open / close / query the on-screen keyboard. */
void pcw_osk_toggle(void);
void pcw_osk_close(void);
/* Unconditionally release pending/sticky keys and restore navigation/edge state.
   Unlike close(), this is also safe when a partially-initialised session is being
   torn down. Call on load, reset, unload and deinit. */
void pcw_osk_reset(void);
int  pcw_osk_is_active(void);

/* Configure from core options. release_ms = minimum time a short tap stays down;
   holding its physical A/X/Y button extends the PCW keypress until release.
   transparent selects the panel rendering mode. */
/* layout_us: 0 = UK (default), non-zero = US. */
void pcw_osk_set_options(int release_ms, int transparent, int layout_us);

/* Per-frame input. Called only while the OSK is active; `poll(id)` returns the
   current pressed state of a RetroPad button id. Drives navigation plus held or
   tapped keys (with sticky modifiers and minimum-duration auto-release) and
   consumes the pad so it does not reach the game. */
void pcw_osk_handle_input(int (*poll)(int id));

/* Per-frame release tick: must run every frame. A short tap releases after its
   minimum hold time; a physically held activator keeps the PCW key down beyond
   that minimum and releases it when the activator is released. */
void pcw_osk_tick(void);

/* Draw the keyboard over the native framebuffer (origin 0,0; `stride` is the row
   stride in pixels). Call after the game frame is composed, while active. */
void pcw_osk_render(uint32_t *fb, int width, int height, int stride);

#endif /* PCW_OSK_H */
