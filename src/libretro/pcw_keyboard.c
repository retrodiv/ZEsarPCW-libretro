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
#include <string.h>

#include "pcw_keyboard.h"
#include "libretro.h"
#include "pcw.h"

/* Keep this mapper byte-for-byte compatible with the original keyboard
 * passthrough. It remains the complete implementation of the Default mode. */
int pcw_keyboard_map_retro_keycode(unsigned keycode)
{
    if (keycode >= RETROK_SPACE && keycode <= RETROK_TILDE) return (int)keycode;
    if (keycode >= RETROK_F1 && keycode <= RETROK_F15)
        return UTIL_KEY_F1 + (int)(keycode - RETROK_F1);
    if (keycode >= RETROK_KP0 && keycode <= RETROK_KP9)
        return UTIL_KEY_KP0 + (int)(keycode - RETROK_KP0);
    switch (keycode) {
        case RETROK_RETURN:      return UTIL_KEY_ENTER;
        case RETROK_BACKSPACE:   return UTIL_KEY_BACKSPACE;
        case RETROK_TAB:         return UTIL_KEY_TAB;
        case RETROK_ESCAPE:      return UTIL_KEY_ESC;
        case RETROK_DELETE:      return UTIL_KEY_DEL;
        case RETROK_UP:          return UTIL_KEY_UP;
        case RETROK_DOWN:        return UTIL_KEY_DOWN;
        case RETROK_RIGHT:       return UTIL_KEY_RIGHT;
        case RETROK_LEFT:        return UTIL_KEY_LEFT;
        case RETROK_HOME:        return UTIL_KEY_HOME;
        case RETROK_END:         return UTIL_KEY_END;
        case RETROK_PAGEUP:      return UTIL_KEY_PAGE_UP;
        case RETROK_PAGEDOWN:    return UTIL_KEY_PAGE_DOWN;
        case RETROK_LSHIFT:      return UTIL_KEY_SHIFT_L;
        case RETROK_RSHIFT:      return UTIL_KEY_SHIFT_R;
        case RETROK_LCTRL:       return UTIL_KEY_CONTROL_L;
        case RETROK_RCTRL:       return UTIL_KEY_CONTROL_R;
        case RETROK_LALT:        return UTIL_KEY_ALT_L;
        case RETROK_RALT:        return UTIL_KEY_ALT_R;
        case RETROK_CAPSLOCK:    return UTIL_KEY_CAPS_LOCK;
        case RETROK_NUMLOCK:     return UTIL_KEY_KP_NUMLOCK;
        case RETROK_KP_PERIOD:   return UTIL_KEY_KP_COMMA;
        case RETROK_KP_DIVIDE:   return UTIL_KEY_KP_DIVIDE;
        case RETROK_KP_MULTIPLY: return UTIL_KEY_KP_MULTIPLY;
        case RETROK_KP_MINUS:    return UTIL_KEY_KP_MINUS;
        case RETROK_KP_PLUS:     return UTIL_KEY_KP_PLUS;
        case RETROK_KP_ENTER:    return UTIL_KEY_KP_ENTER;
        case RETROK_LMETA:
        case RETROK_LSUPER:      return UTIL_KEY_WINKEY_L;
        case RETROK_RMETA:
        case RETROK_RSUPER:      return UTIL_KEY_WINKEY_R;
        default:                 return 0;
    }
}

/* UK ISO host keys. `alternate` covers libretro aliases used by different
 * frontends for the same physical key (Super/Meta, Print/SysRq, Pause/Break,
 * and the ISO backslash key/OEM 102). Values are stable config-file strings. */
struct host_key {
    const char *value;
    const char *label;
    unsigned keycode;
    unsigned alternate;
};

#define HK(v, l, k)       { (v), (l), (k), RETROK_UNKNOWN }
#define HKA(v, l, k, alt) { (v), (l), (k), (alt) }
static const struct host_key host_keys[] = {
    HK("Escape", "Escape", RETROK_ESCAPE),
    HK("F1", "F1", RETROK_F1), HK("F2", "F2", RETROK_F2),
    HK("F3", "F3", RETROK_F3), HK("F4", "F4", RETROK_F4),
    HK("F5", "F5", RETROK_F5), HK("F6", "F6", RETROK_F6),
    HK("F7", "F7", RETROK_F7), HK("F8", "F8", RETROK_F8),
    HK("F9", "F9", RETROK_F9), HK("F10", "F10", RETROK_F10),
    HK("F11", "F11", RETROK_F11), HK("F12", "F12", RETROK_F12),
    HKA("Print Screen", "Print Screen", RETROK_PRINT, RETROK_SYSREQ),
    HK("Scroll Lock", "Scroll Lock", RETROK_SCROLLOCK),
    HKA("Pause", "Pause", RETROK_PAUSE, RETROK_BREAK),

    HK("Grave", "`  ¬  ¦", RETROK_BACKQUOTE),
    HK("1", "1", RETROK_1), HK("2", "2", RETROK_2),
    HK("3", "3", RETROK_3), HK("4", "4", RETROK_4),
    HK("5", "5", RETROK_5), HK("6", "6", RETROK_6),
    HK("7", "7", RETROK_7), HK("8", "8", RETROK_8),
    HK("9", "9", RETROK_9), HK("0", "0", RETROK_0),
    HK("Minus", "-  _", RETROK_MINUS),
    HK("Equals", "=  +", RETROK_EQUALS),
    HK("Backspace", "Backspace", RETROK_BACKSPACE),

    HK("Tab", "Tab", RETROK_TAB),
    HK("Q", "Q", RETROK_q), HK("W", "W", RETROK_w),
    HK("E", "E", RETROK_e), HK("R", "R", RETROK_r),
    HK("T", "T", RETROK_t), HK("Y", "Y", RETROK_y),
    HK("U", "U", RETROK_u), HK("I", "I", RETROK_i),
    HK("O", "O", RETROK_o), HK("P", "P", RETROK_p),
    HK("Left bracket", "[  {", RETROK_LEFTBRACKET),
    HK("Right bracket", "]  }", RETROK_RIGHTBRACKET),
    HK("Return", "Return", RETROK_RETURN),

    HK("Caps Lock", "Caps Lock", RETROK_CAPSLOCK),
    HK("A", "A", RETROK_a), HK("S", "S", RETROK_s),
    HK("D", "D", RETROK_d), HK("F", "F", RETROK_f),
    HK("G", "G", RETROK_g), HK("H", "H", RETROK_h),
    HK("J", "J", RETROK_j), HK("K", "K", RETROK_k),
    HK("L", "L", RETROK_l),
    HK("Semicolon", ";  :", RETROK_SEMICOLON),
    HK("Apostrophe", "'  @", RETROK_QUOTE),
    HK("Hash", "#  ~", RETROK_HASH),

    HK("Left Shift", "Left Shift", RETROK_LSHIFT),
    HKA("UK ISO extra", "UK ISO \\  |", RETROK_OEM_102, RETROK_BACKSLASH),
    HK("Z", "Z", RETROK_z), HK("X", "X", RETROK_x),
    HK("C", "C", RETROK_c), HK("V", "V", RETROK_v),
    HK("B", "B", RETROK_b), HK("N", "N", RETROK_n),
    HK("M", "M", RETROK_m),
    HK("Comma", ",  <", RETROK_COMMA),
    HK("Period", ".  >", RETROK_PERIOD),
    HK("Slash", "/  ?", RETROK_SLASH),
    HK("Right Shift", "Right Shift", RETROK_RSHIFT),

    HK("Left Ctrl", "Left Ctrl", RETROK_LCTRL),
    HKA("Left Super", "Left Super", RETROK_LSUPER, RETROK_LMETA),
    HK("Left Alt", "Left Alt", RETROK_LALT),
    HK("Space", "Space", RETROK_SPACE),
    HK("Right Alt", "Right Alt (AltGr)", RETROK_RALT),
    HKA("Right Super", "Right Super", RETROK_RSUPER, RETROK_RMETA),
    HK("Menu", "Menu", RETROK_MENU),
    HK("Right Ctrl", "Right Ctrl", RETROK_RCTRL),

    HK("Insert", "Insert", RETROK_INSERT), HK("Home", "Home", RETROK_HOME),
    HK("Page Up", "Page Up", RETROK_PAGEUP), HK("Delete", "Delete", RETROK_DELETE),
    HK("End", "End", RETROK_END), HK("Page Down", "Page Down", RETROK_PAGEDOWN),
    HK("Up", "Up arrow", RETROK_UP), HK("Left", "Left arrow", RETROK_LEFT),
    HK("Down", "Down arrow", RETROK_DOWN), HK("Right", "Right arrow", RETROK_RIGHT),

    HK("Num Lock", "Num Lock", RETROK_NUMLOCK),
    HK("Keypad Divide", "Keypad /", RETROK_KP_DIVIDE),
    HK("Keypad Multiply", "Keypad *", RETROK_KP_MULTIPLY),
    HK("Keypad Minus", "Keypad -", RETROK_KP_MINUS),
    HK("Keypad 7", "Keypad 7", RETROK_KP7),
    HK("Keypad 8", "Keypad 8", RETROK_KP8),
    HK("Keypad 9", "Keypad 9", RETROK_KP9),
    HK("Keypad Plus", "Keypad +", RETROK_KP_PLUS),
    HK("Keypad 4", "Keypad 4", RETROK_KP4),
    HK("Keypad 5", "Keypad 5", RETROK_KP5),
    HK("Keypad 6", "Keypad 6", RETROK_KP6),
    HK("Keypad 1", "Keypad 1", RETROK_KP1),
    HK("Keypad 2", "Keypad 2", RETROK_KP2),
    HK("Keypad 3", "Keypad 3", RETROK_KP3),
    HK("Keypad 0", "Keypad 0", RETROK_KP0),
    HK("Keypad Period", "Keypad .", RETROK_KP_PERIOD),
    HK("Keypad Enter", "Keypad Enter", RETROK_KP_ENTER),
};
#undef HK
#undef HKA

/* The generic PCW-key names follow the current UK PCW physical keyboard/OSK,
 * not the legends produced by a selected CP/M keyboard locale. Two separately
 * drawn keys may intentionally share a matrix cell (both Shift keys, and ':' is
 * Shift+';'). `extra_*` expresses such a chord, plus the lock indicator. */
struct pcw_target {
    const char *option_key;
    const char *label;
    unsigned char row, mask;
    unsigned char extra_row, extra_mask;
    unsigned default_host;
};

#define T(id, label, row, mask, host) \
    { "zesarpcw_key_" id, (label), (row), (mask), 0xff, 0, (host) }
#define TX(id, label, row, mask, erow, emask, host) \
    { "zesarpcw_key_" id, (label), (row), (mask), (erow), (emask), (host) }
#define U RETROK_UNKNOWN
static const struct pcw_target targets[] = {
    T("stop", "STOP", 8,0x04, RETROK_ESCAPE),
    T("1", "1", 8,0x01, RETROK_1), T("2", "2", 8,0x02, RETROK_2),
    T("3", "3", 7,0x02, RETROK_3), T("4", "4", 7,0x01, RETROK_4),
    T("5", "5", 6,0x02, RETROK_5), T("6", "6", 6,0x01, RETROK_6),
    T("7", "7", 5,0x02, RETROK_7), T("8", "8", 5,0x01, RETROK_8),
    T("9", "9", 4,0x02, RETROK_9), T("0", "0", 4,0x01, RETROK_0),
    T("minus", "-", 3,0x02, RETROK_MINUS), T("equals", "=", 3,0x01, RETROK_EQUALS),
    T("delete_right", "DEL right", 2,0x01, RETROK_DELETE),
    T("delete_left", "DEL left", 9,0x80, RETROK_BACKSPACE),
    T("can", "CAN", 10,0x04, U), T("cut", "CUT", 1,0x04, U),
    T("copy", "COPY", 1,0x08, U), T("paste", "PASTE", 0,0x08, U),

    T("tab", "TAB", 8,0x10, RETROK_TAB),
    T("q", "Q", 8,0x08, RETROK_q), T("w", "W", 7,0x08, RETROK_w),
    T("e", "E", 7,0x04, RETROK_e), T("r", "R", 6,0x04, RETROK_r),
    T("t", "T", 6,0x08, RETROK_t), T("y", "Y", 5,0x08, RETROK_y),
    T("u", "U", 5,0x04, RETROK_u), T("i", "I", 4,0x08, RETROK_i),
    T("o", "O", 4,0x04, RETROK_o), T("p", "P", 3,0x08, RETROK_p),
    T("left_bracket", "[", 3,0x04, RETROK_LEFTBRACKET),
    T("right_bracket", "]", 2,0x02, RETROK_RIGHTBRACKET),
    T("f8_f7", "f8/f7", 10,0x10, RETROK_F7),
    T("exch_find", "EXCH/FIND", 2,0x10, RETROK_KP7),
    T("doc_page", "DOC/PAGE", 1,0x10, RETROK_KP8),
    T("unit_para", "UNIT/PARA", 0,0x10, RETROK_KP9),

    TX("lock", "LOCK", 8,0x40, 13,0x40, RETROK_CAPSLOCK),
    T("a", "A", 8,0x20, RETROK_a), T("s", "S", 7,0x10, RETROK_s),
    T("d", "D", 7,0x20, RETROK_d), T("f", "F", 6,0x20, RETROK_f),
    T("g", "G", 6,0x10, RETROK_g), T("h", "H", 5,0x10, RETROK_h),
    T("j", "J", 5,0x20, RETROK_j), T("k", "K", 4,0x20, RETROK_k),
    T("l", "L", 4,0x10, RETROK_l),
    T("semicolon", ";", 3,0x20, RETROK_SEMICOLON),
    TX("colon", ":", 3,0x20, 2,0x20, U),
    T("pound_currency", "Pound/Currency", 3,0x10, U),
    T("return", "RET", 2,0x04, RETROK_RETURN),
    T("f6_f5", "f6/f5", 10,0x01, RETROK_F5),
    T("line_eol", "LINE/EOL", 1,0x20, RETROK_KP4),
    T("up", "UP", 1,0x40, RETROK_UP),
    T("word_char", "WORD/CHAR", 0,0x20, RETROK_KP6),

    T("left_shift", "Left SHIFT", 2,0x20, RETROK_LSHIFT),
    T("z", "Z", 8,0x80, RETROK_z), T("x", "X", 7,0x80, RETROK_x),
    T("c", "C", 7,0x40, RETROK_c), T("v", "V", 6,0x80, RETROK_v),
    T("b", "B", 6,0x40, RETROK_b), T("n", "N", 5,0x40, RETROK_n),
    T("m", "M", 4,0x40, RETROK_m),
    T("comma", ",", 4,0x80, RETROK_COMMA), T("period", ".", 3,0x80, RETROK_PERIOD),
    T("slash", "/", 3,0x40, RETROK_SLASH), T("half_at", "1/2 @", 2,0x40, U),
    T("right_shift", "Right SHIFT", 2,0x20, RETROK_RSHIFT),
    T("f4_f3", "f4/f3", 0,0x01, RETROK_F3),
    T("left", "LEFT", 1,0x80, RETROK_LEFT),
    T("centre", "CENTRE", 0,0x80, RETROK_KP2),
    T("right", "RIGHT", 0,0x40, RETROK_RIGHT),

    T("alt", "ALT", 10,0x80, RETROK_LALT), T("extra", "EXTRA", 10,0x02, RETROK_LCTRL),
    T("keypad_plus", "Keypad +", 2,0x80, RETROK_KP_PLUS),
    T("space", "SPACE", 5,0x80, RETROK_SPACE),
    T("keypad_minus", "Keypad -", 10,0x08, RETROK_KP_MINUS),
    T("ptr", "PTR", 1,0x02, RETROK_RALT), T("exit", "EXIT", 1,0x01, RETROK_RCTRL),
    T("f2_f1", "f2/f1", 0,0x04, RETROK_F1),
    T("rel", "REL", 0,0x02, RETROK_KP0),
    T("down", "DOWN", 10,0x40, RETROK_DOWN),
    T("keypad_enter", "ENT", 10,0x20, RETROK_KP_ENTER),
};
#undef U
#undef T
#undef TX

#define TARGET_COUNT ((int)(sizeof targets / sizeof targets[0]))
#define HOST_COUNT   ((int)(sizeof host_keys / sizeof host_keys[0]))

_Static_assert(TARGET_COUNT == PCW_KEYBOARD_TARGET_COUNT,
               "update PCW_KEYBOARD_TARGET_COUNT when the PCW key table changes");
_Static_assert(HOST_COUNT + 3 <= RETRO_NUM_CORE_OPTION_VALUES_MAX,
               "physical-key options exceed libretro's value limit");

/* 0 = target default, 1 = unmapped, 2+n = explicit host_keys[n]. */
static unsigned char bindings[TARGET_COUNT];
static unsigned char held[TARGET_COUNT];
static unsigned char default_held[TARGET_COUNT];
static unsigned short matrix_refs[16][8];
static bool custom_enabled;

static int mask_bit(unsigned mask)
{
    int bit;
    for (bit = 0; bit < 8; bit++) if (mask == (1u << bit)) return bit;
    return -1;
}

static void matrix_key(unsigned row, unsigned mask, bool down)
{
    int bit = mask_bit(mask);
    unsigned short *refs;
    if (row >= 16 || bit < 0) return;
    refs = &matrix_refs[row][bit];
    if (down) {
        if ((*refs)++ == 0) pcw_keyboard_table[row] &= (z80_byte)~mask;
    } else if (*refs) {
        if (--(*refs) == 0) pcw_keyboard_table[row] |= (z80_byte)mask;
    }
}

static void target_key(int index, bool down)
{
    const struct pcw_target *t = &targets[index];
    if (down && t->extra_mask) matrix_key(t->extra_row, t->extra_mask, true);
    matrix_key(t->row, t->mask, down);
    if (!down && t->extra_mask) matrix_key(t->extra_row, t->extra_mask, false);
}

void pcw_keyboard_set_target_key(int index, bool down)
{
    if (index >= 0 && index < TARGET_COUNT) target_key(index, down);
}

static int host_index_for_keycode(unsigned keycode)
{
    int i;
    for (i = 0; i < HOST_COUNT; i++)
        if (host_keys[i].keycode == keycode || host_keys[i].alternate == keycode)
            return i;
    return -1;
}

static bool binding_matches(int target, unsigned keycode)
{
    unsigned char binding = bindings[target];
    if (binding == 1) return false;
    if (binding == 0) return targets[target].default_host != RETROK_UNKNOWN &&
                              targets[target].default_host == keycode;
    binding -= 2;
    return binding < HOST_COUNT &&
           (host_keys[binding].keycode == keycode || host_keys[binding].alternate == keycode);
}

int pcw_keyboard_target_count(void) { return TARGET_COUNT; }
int pcw_keyboard_host_key_count(void) { return HOST_COUNT; }

const char *pcw_keyboard_target_option_key(int index)
{
    return index >= 0 && index < TARGET_COUNT ? targets[index].option_key : NULL;
}

const char *pcw_keyboard_target_label(int index)
{
    return index >= 0 && index < TARGET_COUNT ? targets[index].label : NULL;
}

const char *pcw_keyboard_target_default_host_label(int index)
{
    int host;
    if (index < 0 || index >= TARGET_COUNT || targets[index].default_host == RETROK_UNKNOWN)
        return "Unmapped";
    host = host_index_for_keycode(targets[index].default_host);
    return host >= 0 ? host_keys[host].label : "Unmapped";
}

const char *pcw_keyboard_host_key_value(int index)
{
    return index >= 0 && index < HOST_COUNT ? host_keys[index].value : NULL;
}

const char *pcw_keyboard_host_key_label(int index)
{
    return index >= 0 && index < HOST_COUNT ? host_keys[index].label : NULL;
}

bool pcw_keyboard_custom_enabled(void) { return custom_enabled; }

void pcw_keyboard_release_custom_keys(void)
{
    int i;
    for (i = 0; i < TARGET_COUNT; i++) {
        if (held[i]) target_key(i, false);
        held[i] = 0;
    }
    /* Other sources (physical keys, RetroPad) still own their references. */
}

void pcw_keyboard_reset_custom_state(void)
{
    memset(held, 0, sizeof held);
    memset(matrix_refs, 0, sizeof matrix_refs);
}

bool pcw_keyboard_set_custom_enabled(bool enabled)
{
    if (custom_enabled == enabled) return false;
    pcw_keyboard_release_custom_keys();
    custom_enabled = enabled;
    return true;
}

bool pcw_keyboard_set_binding_value(int target, const char *value)
{
    int i;
    unsigned char next;
    if (target < 0 || target >= TARGET_COUNT || !value) return false;
    if (!strcmp(value, "Default")) next = 0;
    else if (!strcmp(value, "Unmapped")) next = 1;
    else {
        for (i = 0; i < HOST_COUNT; i++)
            if (!strcmp(value, host_keys[i].value)) break;
        if (i == HOST_COUNT) return false;
        next = (unsigned char)(i + 2);
    }
    if (bindings[target] == next) return false;
    pcw_keyboard_release_custom_keys();
    bindings[target] = next;
    return true;
}

bool pcw_keyboard_custom_event(bool down, unsigned keycode)
{
    int i;
    if (!custom_enabled) return false;
    for (i = 0; i < TARGET_COUNT; i++) {
        if (!binding_matches(i, keycode)) continue;
        if (down && !held[i]) {
            held[i] = 1;
            target_key(i, true);
        } else if (!down && held[i]) {
            target_key(i, false);
            held[i] = 0;
        }
    }
    return true;
}

unsigned pcw_keyboard_target_native_keycode(int index)
{
    /* RetroArch owns the native keyboard selector's PC labels. Keep the usual
       equivalents and give the otherwise-unreachable PCW keys unused F keys.
       These aliases apply only to the native keyboard device. */
    static const char *const aliases[] = {
        "CAN", "CUT", "COPY", "PASTE", "Pound/Currency", "1/2 @", ":"
    };
    if (index < 0 || index >= TARGET_COUNT) return RETROK_UNKNOWN;
    if (targets[index].default_host != RETROK_UNKNOWN)
        return targets[index].default_host;
    for (unsigned i = 0; i < sizeof aliases / sizeof aliases[0]; i++)
        if (!strcmp(targets[index].label, aliases[i])) return RETROK_F9 + i;
    return RETROK_UNKNOWN;
}

static bool default_event(bool down, unsigned keycode, bool native)
{
    int i;
    if (keycode == RETROK_UNKNOWN) return false;
    for (i = 0; i < TARGET_COUNT; i++) {
        unsigned host = native ? pcw_keyboard_target_native_keycode(i) : targets[i].default_host;
        if (host != keycode) continue;
        if (down && !default_held[i]) {
            default_held[i] = 1;
            target_key(i, true);
        } else if (!down && default_held[i]) {
            target_key(i, false);
            default_held[i] = 0;
        }
        return true;
    }
    return false;
}

bool pcw_keyboard_default_event(bool down, unsigned keycode)
{
    return default_event(down, keycode, false);
}

bool pcw_keyboard_native_event(bool down, unsigned keycode)
{
    return default_event(down, keycode, true);
}

void pcw_keyboard_set_util_key(int util_key, bool down)
{
    int i;
    if (!util_key) return;
    /* Pad profiles retain ZEsarUX's SPACE token; host keys use ASCII. */
    if (util_key == UTIL_KEY_SPACE) util_key = RETROK_SPACE;
    for (i = 0; i < TARGET_COUNT; i++) {
        unsigned host = targets[i].default_host;
        if (host != RETROK_UNKNOWN && pcw_keyboard_map_retro_keycode(host) == util_key)
            target_key(i, down);
    }
}

void pcw_keyboard_ascii_event(unsigned char character, bool down)
{
    pcw_keyboard_set_util_key(character == '\r' ? UTIL_KEY_ENTER : character,
                              down);
}

void pcw_keyboard_release_all(void)
{
    memset(pcw_keyboard_table, 0xff, 16);
    memset(held, 0, sizeof held);
    memset(default_held, 0, sizeof default_held);
    memset(matrix_refs, 0, sizeof matrix_refs);
}

bool pcw_keyboard_target_matrix(int index, unsigned *row, unsigned *mask)
{
    if (index < 0 || index >= TARGET_COUNT || !row || !mask) return false;
    *row = targets[index].row;
    *mask = targets[index].mask;
    return true;
}
