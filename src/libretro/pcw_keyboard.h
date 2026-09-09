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

/* ZEsarPCW host-keyboard mapping (GPLv3, same licence as ZEsarUX). */
#ifndef PCW_KEYBOARD_H
#define PCW_KEYBOARD_H

#include <stdbool.h>

/* Numeric keyboard codes used by the PCW input bridge. They match ZEsarUX
 * 13.0's util_teclas ABI, without importing the standalone utility/UI header. */
enum pcw_util_key {
    UTIL_KEY_NONE = 0,
    UTIL_KEY_SPACE = 128,
    UTIL_KEY_ENTER,
    UTIL_KEY_HOME,
    UTIL_KEY_END,
    UTIL_KEY_DEL,
    UTIL_KEY_SHIFT_L,
    UTIL_KEY_SHIFT_R,
    UTIL_KEY_CAPS_SHIFT,
    UTIL_KEY_ALT_L,
    UTIL_KEY_ALT_R,
    UTIL_KEY_CONTROL_L,
    UTIL_KEY_CONTROL_R,
    UTIL_KEY_BACKSPACE,
    UTIL_KEY_FIRE,
    UTIL_KEY_LEFT,
    UTIL_KEY_RIGHT,
    UTIL_KEY_DOWN,
    UTIL_KEY_UP,
    UTIL_KEY_TAB,
    UTIL_KEY_CAPS_LOCK,
    UTIL_KEY_COMMA,
    UTIL_KEY_PERIOD,
    UTIL_KEY_F1,
    UTIL_KEY_F2,
    UTIL_KEY_F3,
    UTIL_KEY_F4,
    UTIL_KEY_F5,
    UTIL_KEY_F6,
    UTIL_KEY_F7,
    UTIL_KEY_F8,
    UTIL_KEY_F9,
    UTIL_KEY_F10,
    UTIL_KEY_F11,
    UTIL_KEY_F12,
    UTIL_KEY_F13,
    UTIL_KEY_F14,
    UTIL_KEY_F15,
    UTIL_KEY_ESC,
    UTIL_KEY_PAGE_UP,
    UTIL_KEY_PAGE_DOWN,
    UTIL_KEY_KP_PLUS,
    UTIL_KEY_KP_NUMLOCK,
    UTIL_KEY_KP_DIVIDE,
    UTIL_KEY_KP_MULTIPLY,
    UTIL_KEY_KP_MINUS,
    UTIL_KEY_KP0,
    UTIL_KEY_KP1,
    UTIL_KEY_KP2,
    UTIL_KEY_KP3,
    UTIL_KEY_KP4,
    UTIL_KEY_KP5,
    UTIL_KEY_KP6,
    UTIL_KEY_KP7,
    UTIL_KEY_KP8,
    UTIL_KEY_KP9,
    UTIL_KEY_KP_COMMA,
    UTIL_KEY_KP_ENTER,
    UTIL_KEY_WINKEY_L,
    UTIL_KEY_WINKEY_R
};


/* Kept in the public metadata header so the core-options tables can be sized
 * exactly instead of reserving space for an arbitrary upper bound. The static
 * assertion next to the target table catches additions that forget to update
 * this value. */
#define PCW_KEYBOARD_TARGET_COUNT 82

/* The canonical passthrough used by Physical keyboard mapping = Default. */
int pcw_keyboard_map_retro_keycode(unsigned keycode);
bool pcw_keyboard_default_event(bool down, unsigned keycode);
/* Native frontend keyboard remaps: default host keys plus aliases for all
 * seven PCW keys without a default physical host key. */
bool pcw_keyboard_native_event(bool down, unsigned keycode);
unsigned pcw_keyboard_target_native_keycode(int index);
void pcw_keyboard_set_util_key(int util_key, bool down);
/* Direct PCW key input, including keys with no default host-key equivalent.
 * Call on press/release edges; independent sources may hold the same key. */
void pcw_keyboard_set_target_key(int index, bool down);
void pcw_keyboard_ascii_event(unsigned char character, bool down);
void pcw_keyboard_release_all(void);

/* Metadata used to build the per-PCW-key Core Options. */
int pcw_keyboard_target_count(void);
const char *pcw_keyboard_target_option_key(int index);
const char *pcw_keyboard_target_label(int index);
const char *pcw_keyboard_target_default_host_label(int index);

int pcw_keyboard_host_key_count(void);
const char *pcw_keyboard_host_key_value(int index);
const char *pcw_keyboard_host_key_label(int index);

/* Live custom-map state. Values are "Default", "Unmapped", or one of the
 * strings returned by pcw_keyboard_host_key_value(). */
bool pcw_keyboard_custom_enabled(void);
bool pcw_keyboard_set_custom_enabled(bool enabled);
bool pcw_keyboard_set_binding_value(int target, const char *value);

/* Returns true when Custom owns the event (including an unbound key). */
bool pcw_keyboard_custom_event(bool down, unsigned keycode);
void pcw_keyboard_release_custom_keys(void);
void pcw_keyboard_reset_custom_state(void);

/* Test/introspection helper: the primary PCW matrix cell for a target. */
bool pcw_keyboard_target_matrix(int index, unsigned *row, unsigned *mask);

#endif
