/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Common runtime view of a generated on-screen-keyboard skin. Individual
    locale headers contain only immutable pixel/key data and one descriptor of
    this type, allowing the renderer to switch layouts without duplicating code.

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#ifndef PCW_OSK_SKIN_TYPES_H
#define PCW_OSK_SKIN_TYPES_H

#include <stdint.h>

enum { SK_NORMAL=0, SK_SHIFT, SK_LOCK, SK_ALT, SK_EXTRA };
enum { SKF_NEEDS_SHIFT=1, SKF_BAKED=2, SKF_LOCKDOT=4 };

typedef struct {
    uint8_t w, h;
    uint16_t off;
} pcw_osk_skin_label_t;

typedef struct {
    uint8_t w, h;
    uint32_t off;
} pcw_osk_skin_baked_t;

typedef struct {
    int16_t x, y;
    uint8_t w, h;
    uint8_t prow, pmask;
    int16_t util;
    uint8_t kind, flags;
    int16_t label, baked;
    uint8_t navrow;
    int16_t dotx, doty;
    int16_t lx, ly;
} pcw_osk_skin_key_t;

typedef struct {
    int width, height, header_height;
    uint32_t c_bg, c_outer, c_inner, c_face, c_text, c_lockoff, c_lockon;
    const uint32_t *header;
    const uint8_t *label_bits;
    const pcw_osk_skin_label_t *labels;
    const uint32_t *baked_pix;
    const pcw_osk_skin_baked_t *bakeds;
    const pcw_osk_skin_key_t *keys;
    int nkeys;
} pcw_osk_skin_t;

#endif /* PCW_OSK_SKIN_TYPES_H */
