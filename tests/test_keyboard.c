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

#include <stdio.h>
#include <string.h>

#include "libretro.h"
#include "pcw_keyboard.h"
#include "pcw.h"

/* pcw_keyboard.c normally gets this from machines/pcw.c. */
z80_byte pcw_keyboard_table[16];

struct key_pair { unsigned retro; int util; };

static int failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static int target(const char *suffix)
{
    char key[128];
    int i;
    snprintf(key, sizeof key, "zesarpcw_key_%s", suffix);
    for (i = 0; i < pcw_keyboard_target_count(); i++)
        if (!strcmp(pcw_keyboard_target_option_key(i), key)) return i;
    return -1;
}

static void clean_custom_map(void)
{
    int i;
    pcw_keyboard_set_custom_enabled(false);
    for (i = 0; i < pcw_keyboard_target_count(); i++)
        pcw_keyboard_set_binding_value(i, "Default");
    memset(pcw_keyboard_table, 0xff, sizeof pcw_keyboard_table);
    pcw_keyboard_reset_custom_state();
}

static void check_canonical_map(void)
{
    const struct key_pair pairs[] = {
        { RETROK_RETURN, UTIL_KEY_ENTER }, { RETROK_BACKSPACE, UTIL_KEY_BACKSPACE },
        { RETROK_TAB, UTIL_KEY_TAB }, { RETROK_ESCAPE, UTIL_KEY_ESC },
        { RETROK_DELETE, UTIL_KEY_DEL }, { RETROK_UP, UTIL_KEY_UP },
        { RETROK_DOWN, UTIL_KEY_DOWN }, { RETROK_LEFT, UTIL_KEY_LEFT },
        { RETROK_RIGHT, UTIL_KEY_RIGHT }, { RETROK_HOME, UTIL_KEY_HOME },
        { RETROK_END, UTIL_KEY_END }, { RETROK_PAGEUP, UTIL_KEY_PAGE_UP },
        { RETROK_PAGEDOWN, UTIL_KEY_PAGE_DOWN },
        { RETROK_LSHIFT, UTIL_KEY_SHIFT_L }, { RETROK_RSHIFT, UTIL_KEY_SHIFT_R },
        { RETROK_LCTRL, UTIL_KEY_CONTROL_L }, { RETROK_RCTRL, UTIL_KEY_CONTROL_R },
        { RETROK_LALT, UTIL_KEY_ALT_L }, { RETROK_RALT, UTIL_KEY_ALT_R },
        { RETROK_CAPSLOCK, UTIL_KEY_CAPS_LOCK }, { RETROK_NUMLOCK, UTIL_KEY_KP_NUMLOCK },
        { RETROK_KP_PERIOD, UTIL_KEY_KP_COMMA },
        { RETROK_KP_DIVIDE, UTIL_KEY_KP_DIVIDE },
        { RETROK_KP_MULTIPLY, UTIL_KEY_KP_MULTIPLY },
        { RETROK_KP_MINUS, UTIL_KEY_KP_MINUS }, { RETROK_KP_PLUS, UTIL_KEY_KP_PLUS },
        { RETROK_KP_ENTER, UTIL_KEY_KP_ENTER },
        { RETROK_LMETA, UTIL_KEY_WINKEY_L }, { RETROK_LSUPER, UTIL_KEY_WINKEY_L },
        { RETROK_RMETA, UTIL_KEY_WINKEY_R }, { RETROK_RSUPER, UTIL_KEY_WINKEY_R },
    };
    unsigned key, i;

    for (key = RETROK_SPACE; key <= RETROK_TILDE; key++)
        CHECK(pcw_keyboard_map_retro_keycode(key) == (int)key);
    for (key = RETROK_F1; key <= RETROK_F15; key++)
        CHECK(pcw_keyboard_map_retro_keycode(key) ==
              UTIL_KEY_F1 + (int)(key - RETROK_F1));
    for (key = RETROK_KP0; key <= RETROK_KP9; key++)
        CHECK(pcw_keyboard_map_retro_keycode(key) ==
              UTIL_KEY_KP0 + (int)(key - RETROK_KP0));
    for (i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
        CHECK(pcw_keyboard_map_retro_keycode(pairs[i].retro) == pairs[i].util);
    CHECK(pcw_keyboard_map_retro_keycode(RETROK_UNKNOWN) == 0);
    CHECK(pcw_keyboard_map_retro_keycode(RETROK_INSERT) == 0);
}

static void check_metadata(void)
{
    int i, j;
    CHECK(pcw_keyboard_target_count() == 82);
    CHECK(pcw_keyboard_host_key_count() == 105);
    CHECK(pcw_keyboard_host_key_count() + 2 <= RETRO_NUM_CORE_OPTION_VALUES_MAX);
    for (i = 0; i < pcw_keyboard_target_count(); i++) {
        unsigned row, mask;
        CHECK(pcw_keyboard_target_option_key(i) != NULL);
        CHECK(pcw_keyboard_target_label(i) != NULL);
        CHECK(pcw_keyboard_target_default_host_label(i) != NULL);
        CHECK(pcw_keyboard_target_matrix(i, &row, &mask));
        CHECK(row < 16 && mask && !(mask & (mask - 1)));
        for (j = i + 1; j < pcw_keyboard_target_count(); j++)
            CHECK(strcmp(pcw_keyboard_target_option_key(i),
                         pcw_keyboard_target_option_key(j)) != 0);
    }
    for (i = 0; i < pcw_keyboard_host_key_count(); i++) {
        CHECK(pcw_keyboard_host_key_value(i) != NULL);
        CHECK(pcw_keyboard_host_key_label(i) != NULL);
        for (j = i + 1; j < pcw_keyboard_host_key_count(); j++)
            CHECK(strcmp(pcw_keyboard_host_key_value(i),
                         pcw_keyboard_host_key_value(j)) != 0);
    }
    CHECK(!strcmp(pcw_keyboard_target_default_host_label(target("a")), "A"));
    CHECK(!strcmp(pcw_keyboard_target_default_host_label(target("colon")), "Unmapped"));
}

static void check_custom_events(void)
{
    int a = target("a"), s = target("s");
    int lshift = target("left_shift"), rshift = target("right_shift");
    int semicolon = target("semicolon"), colon = target("colon");
    int lock = target("lock");

    clean_custom_map();
    CHECK(pcw_keyboard_set_custom_enabled(true));
    CHECK(pcw_keyboard_custom_enabled());
    CHECK(pcw_keyboard_custom_event(true, RETROK_a));
    CHECK((pcw_keyboard_table[8] & 0x20) == 0);
    CHECK(pcw_keyboard_custom_event(false, RETROK_a));
    CHECK((pcw_keyboard_table[8] & 0x20) != 0);

    /* Duplicates are deliberate: one host key may press several PCW keys. */
    CHECK(pcw_keyboard_set_binding_value(a, "F12"));
    CHECK(pcw_keyboard_set_binding_value(s, "F12"));
    pcw_keyboard_custom_event(true, RETROK_F12);
    CHECK((pcw_keyboard_table[8] & 0x20) == 0);
    CHECK((pcw_keyboard_table[7] & 0x10) == 0);
    pcw_keyboard_custom_event(false, RETROK_F12);
    CHECK((pcw_keyboard_table[8] & 0x20) != 0);
    CHECK((pcw_keyboard_table[7] & 0x10) != 0);

    /* The two physical Shift keys share one PCW matrix cell. */
    pcw_keyboard_custom_event(true, RETROK_LSHIFT);
    pcw_keyboard_custom_event(true, RETROK_RSHIFT);
    CHECK((pcw_keyboard_table[2] & 0x20) == 0);
    pcw_keyboard_custom_event(false, RETROK_LSHIFT);
    CHECK((pcw_keyboard_table[2] & 0x20) == 0);
    pcw_keyboard_custom_event(false, RETROK_RSHIFT);
    CHECK((pcw_keyboard_table[2] & 0x20) != 0);
    CHECK(lshift >= 0 && rshift >= 0);

    /* ':' is the physical ';' matrix key plus PCW Shift. */
    CHECK(pcw_keyboard_set_binding_value(semicolon, "Unmapped"));
    CHECK(pcw_keyboard_set_binding_value(colon, "F12"));
    pcw_keyboard_custom_event(true, RETROK_F12);
    CHECK((pcw_keyboard_table[3] & 0x20) == 0);
    CHECK((pcw_keyboard_table[2] & 0x20) == 0);
    pcw_keyboard_custom_event(false, RETROK_F12);
    CHECK((pcw_keyboard_table[3] & 0x20) != 0);
    CHECK((pcw_keyboard_table[2] & 0x20) != 0);

    /* LOCK also owns the indicator cell used by the keyboard artwork. */
    pcw_keyboard_custom_event(true, RETROK_CAPSLOCK);
    CHECK((pcw_keyboard_table[8] & 0x40) == 0);
    CHECK((pcw_keyboard_table[13] & 0x40) == 0);
    pcw_keyboard_custom_event(false, RETROK_CAPSLOCK);
    CHECK((pcw_keyboard_table[8] & 0x40) != 0);
    CHECK((pcw_keyboard_table[13] & 0x40) != 0);
    CHECK(lock >= 0);

    /* Mapping changes and leaving Custom release anything held immediately. */
    CHECK(pcw_keyboard_set_binding_value(a, "A"));
    pcw_keyboard_custom_event(true, RETROK_a);
    CHECK((pcw_keyboard_table[8] & 0x20) == 0);
    CHECK(pcw_keyboard_set_binding_value(a, "Unmapped"));
    CHECK((pcw_keyboard_table[8] & 0x20) != 0);
    pcw_keyboard_custom_event(true, RETROK_F1);
    CHECK((pcw_keyboard_table[0] & 0x04) == 0);
    CHECK(pcw_keyboard_set_custom_enabled(false));
    CHECK((pcw_keyboard_table[0] & 0x04) != 0);
    CHECK(!pcw_keyboard_custom_enabled());

    /* Frontends use either BACKSLASH or OEM_102 for the same UK ISO key. */
    CHECK(pcw_keyboard_set_binding_value(a, "UK ISO extra"));
    pcw_keyboard_set_custom_enabled(true);
    pcw_keyboard_custom_event(true, RETROK_BACKSLASH);
    CHECK((pcw_keyboard_table[8] & 0x20) == 0);
    pcw_keyboard_custom_event(false, RETROK_BACKSLASH);
    CHECK((pcw_keyboard_table[8] & 0x20) != 0);

    CHECK(pcw_keyboard_custom_event(true, RETROK_UNKNOWN));
    CHECK(!pcw_keyboard_set_binding_value(a, "not a host key"));
    CHECK(!pcw_keyboard_target_matrix(-1, NULL, NULL));
    clean_custom_map();
}

static void check_pad_space(void)
{
    z80_byte physical[16];

    clean_custom_map();
    pcw_keyboard_release_all();
    CHECK(pcw_keyboard_default_event(true, RETROK_SPACE));
    CHECK(pcw_keyboard_table[5] == 0x7f);
    memcpy(physical, pcw_keyboard_table, sizeof physical);
    CHECK(pcw_keyboard_default_event(false, RETROK_SPACE));

    /* Pad profiles use the ZEsarUX SPACE token (128), while physical
       keyboard events and typed text use ASCII SPACE (32). */
    pcw_keyboard_set_util_key(UTIL_KEY_SPACE, true);
    CHECK(!memcmp(physical, pcw_keyboard_table, sizeof physical));
    pcw_keyboard_set_util_key(UTIL_KEY_SPACE, false);
    CHECK(pcw_keyboard_table[5] == 0xff);
    pcw_keyboard_ascii_event(' ', true);
    CHECK(!memcmp(physical, pcw_keyboard_table, sizeof physical));
    pcw_keyboard_ascii_event(' ', false);

    /* Releasing a physical key must not cancel a held pad button, and
       releasing one of two pad buttons bound to SPACE must keep it down. */
    pcw_keyboard_set_util_key(UTIL_KEY_SPACE, true);
    pcw_keyboard_default_event(true, RETROK_SPACE);
    pcw_keyboard_default_event(false, RETROK_SPACE);
    CHECK(!memcmp(physical, pcw_keyboard_table, sizeof physical));
    pcw_keyboard_set_util_key(UTIL_KEY_SPACE, true);
    pcw_keyboard_set_util_key(UTIL_KEY_SPACE, false);
    CHECK(!memcmp(physical, pcw_keyboard_table, sizeof physical));
    pcw_keyboard_set_util_key(UTIL_KEY_SPACE, false);
    for (unsigned row = 0; row < sizeof pcw_keyboard_table; row++)
        CHECK(pcw_keyboard_table[row] == 0xff);

    /* Custom host-keyboard bindings do not change the PCW pad action. */
    pcw_keyboard_set_binding_value(target("space"), "Unmapped");
    pcw_keyboard_set_custom_enabled(true);
    pcw_keyboard_set_util_key(UTIL_KEY_SPACE, true);
    CHECK(!memcmp(physical, pcw_keyboard_table, sizeof physical));
    pcw_keyboard_set_util_key(UTIL_KEY_SPACE, false);
    CHECK(pcw_keyboard_table[5] == 0xff);
    clean_custom_map();
}

static void check_direct_pcw_keys(void)
{
    clean_custom_map();
    for (int i = 0; i < pcw_keyboard_target_count(); i++) {
        unsigned row, mask;
        pcw_keyboard_release_all();
        CHECK(pcw_keyboard_target_matrix(i, &row, &mask));
        pcw_keyboard_set_target_key(i, true);
        CHECK(!(pcw_keyboard_table[row] & mask));
        pcw_keyboard_set_target_key(i, false);
        for (unsigned r = 0; r < sizeof pcw_keyboard_table; r++)
            CHECK(pcw_keyboard_table[r] == 0xff);
    }

    /* Editing the physical keyboard map must not lose a pad-held key's
       reference: its later release must still reach the PCW matrix. */
    pcw_keyboard_set_target_key(target("space"), true);
    pcw_keyboard_set_custom_enabled(true);
    pcw_keyboard_set_binding_value(target("a"), "F12");
    CHECK(!(pcw_keyboard_table[5] & 0x80));
    pcw_keyboard_set_target_key(target("space"), false);
    CHECK(pcw_keyboard_table[5] == 0xff);

    /* Physical and pad presses are independent owners of the same key. */
    pcw_keyboard_set_target_key(target("space"), true);
    pcw_keyboard_custom_event(true, RETROK_SPACE);
    pcw_keyboard_release_custom_keys();
    CHECK(!(pcw_keyboard_table[5] & 0x80));
    pcw_keyboard_set_target_key(target("space"), false);
    CHECK(pcw_keyboard_table[5] == 0xff);
    clean_custom_map();
}

static void check_native_keyboard(void)
{
    z80_byte expected[16];
    clean_custom_map();
    pcw_keyboard_release_all();
    CHECK(!pcw_keyboard_default_event(true, RETROK_UNKNOWN));
    CHECK(!pcw_keyboard_native_event(true, RETROK_UNKNOWN));
    for (int i = 0; i < pcw_keyboard_target_count(); i++) {
        unsigned key = pcw_keyboard_target_native_keycode(i);
        CHECK(key > RETROK_UNKNOWN && key < RETROK_LAST);
        for (int j = 0; j < i; j++)
            CHECK(key != pcw_keyboard_target_native_keycode(j));
        pcw_keyboard_set_target_key(i, true);
        memcpy(expected, pcw_keyboard_table, sizeof expected);
        pcw_keyboard_set_target_key(i, false);
        CHECK(pcw_keyboard_native_event(true, key));
        CHECK(!memcmp(expected, pcw_keyboard_table, sizeof expected));
        CHECK(pcw_keyboard_native_event(true, key)); /* repeat */
        CHECK(pcw_keyboard_native_event(false, key));
        for (unsigned row = 0; row < sizeof expected; row++)
            CHECK(pcw_keyboard_table[row] == 0xff);
    }
    CHECK(pcw_keyboard_target_native_keycode(-1) == RETROK_UNKNOWN);
    CHECK(pcw_keyboard_target_native_keycode(82) == RETROK_UNKNOWN);
    CHECK(!pcw_keyboard_default_event(true, RETROK_F9));
    CHECK(pcw_keyboard_native_event(true, RETROK_F9));
    CHECK(!(pcw_keyboard_table[10] & 4)); /* CAN */
    pcw_keyboard_release_all();
}

int main(void)
{
    check_canonical_map();
    check_metadata();
    check_custom_events();
    check_pad_space();
    check_direct_pcw_keys();
    check_native_keyboard();

    if (failures) {
        fprintf(stderr, "FAIL: %d keyboard mapping checks\n", failures);
        return 1;
    }
    puts("PASS: canonical map + 82-key physical/native keyboard maps + shared key ownership + pad SPACE");
    return 0;
}
