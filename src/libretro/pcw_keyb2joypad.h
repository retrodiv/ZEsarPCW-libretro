/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Per-game gamepad -> PCW-key mapping: pick a binding for the loaded disc by its
    .dsk sha1 / software-list shortname and expose, per RetroPad slot, the ZEsarUX
    key the core should inject. The binding database is compiled into the binary.

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#ifndef PCW_KEYB2JOYPAD_H
#define PCW_KEYB2JOYPAD_H

/* RetroPad slots we bind, in table-column order. The generated db table stores
   one token per slot; the left analog stick mirrors the d-pad at runtime. This
   is the single definition of the slot order -- the generated db header relies
   on it (it is included after this one). */
enum pcw_pad_slot {
    PCW_PAD_UP, PCW_PAD_DOWN, PCW_PAD_LEFT, PCW_PAD_RIGHT,
    PCW_PAD_B, PCW_PAD_A, PCW_PAD_Y, PCW_PAD_X,
    PCW_PAD_L, PCW_PAD_R, PCW_PAD_L2, PCW_PAD_R2,
    PCW_PAD_START, PCW_PAD_SELECT, PCW_PAD_R3,
    PCW_PAD_NSLOTS
};

/* Select the active per-game mapping for the loaded content. Resolution order:
   sha1 of the .dsk (canonical) -> software-list shortname derived from the
   filename -> a generic navigation profile. Logs which path matched. Call once
   per retro_load_game, before input is pumped. */
void pcw_keyb2joypad_select(const char *content_path);

/* The ZEsarUX key (a util_teclas value, or an ASCII code <128) bound to a
   RetroPad slot (a PCW_PAD_* index) in the active mapping; 0 if unbound. */
int  pcw_keyb2joypad_util_for_slot(int slot);

/* The token label bound to a slot ("SPACE", "q", ...); "" if none. For logging. */
const char *pcw_keyb2joypad_label_for_slot(int slot);

#endif /* PCW_KEYB2JOYPAD_H */
