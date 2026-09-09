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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libretro.h"
#include "pcw_osk.h"
#include "pcw_keyboard.h"
#include "pcw.h"
#include "pcw_osk_skin_types.h"
#include "pcw_osk_skin_uk.h"
#include "pcw_osk_skin_us.h"

z80_byte pcw_keyboard_table[16];

static int buttons[20];

static int poll_button(int id)
{
    return id >= 0 && id < (int)(sizeof buttons / sizeof buttons[0]) && buttons[id];
}

static void set_button(int id, int down)
{
    buttons[id] = down;
    pcw_osk_handle_input(poll_button);
}

static void tap_button(int id)
{
    set_button(id, 1);
    set_button(id, 0);
}

static int matrix_down(const pcw_osk_skin_key_t *key)
{
    return (pcw_keyboard_table[key->prow] & key->pmask) == 0;
}

static void fresh_osk(void)
{
    memset(buttons, 0, sizeof buttons);
    memset(pcw_keyboard_table, 0xff, sizeof pcw_keyboard_table);
    pcw_osk_reset();
    pcw_osk_set_options(100, 0, 0); /* five frames at 50 Hz; UK default */
    pcw_osk_toggle();
}

static int check_return_focus(int locale, int transparency, int scale_x)
{
    const pcw_osk_skin_t *skin = locale ? &pcw_osk_skin_us : &pcw_osk_skin_uk;
    const pcw_osk_skin_key_t *neighbour = &skin->keys[48];
    const pcw_osk_skin_key_t *ret = &skin->keys[49];
    static uint32_t before[2 * PCW_OSK_SKIN_UK_W * PCW_OSK_SKIN_UK_H];
    static uint32_t after[2 * PCW_OSK_SKIN_UK_W * PCW_OSK_SKIN_UK_H];
    int width = scale_x * skin->width;
    int i, x, y, failures = 0;

    fresh_osk();
    pcw_osk_set_options(100, transparency, locale);
    for (i = 0; i < width * skin->height; i++)
        before[i] = after[i] = 0xFF203040u;
    pcw_osk_render(before, width, skin->height, width);
    for (i = 0; i < 12; i++) tap_button(RETRO_DEVICE_ID_JOYPAD_RIGHT);
    pcw_osk_render(after, width, skin->height, width);

    /* Check the actual neighbour, independently of RET's generated sprite. */
    for (y = neighbour->y; y < neighbour->y + neighbour->h; y++) {
        for (x = scale_x * neighbour->x;
             x < scale_x * (neighbour->x + neighbour->w); x++) {
            if (before[y * width + x] != after[y * width + x]) failures++;
        }
    }
    /* The artwork's upper arm is 16 pixels tall; the lower stem starts four
       pixels to its right. Every pixel of both must retain focus, including
       the upper-left extension. The cutout must remain untouched. */
    for (y = 0; y < ret->h; y++) {
        for (x = 0; x < scale_x * ret->w; x++) {
            int pos = (ret->y + y) * width + scale_x * ret->x + x;
            int in_return = y < 16 || x >= scale_x * 4;
            if ((before[pos] != after[pos]) != in_return) failures++;
        }
    }
    if (failures)
        fprintf(stderr, "RET focus: %s, transparency=%d, scale_x=%d: %d failures\n",
                locale ? "US" : "UK", transparency, scale_x, failures);
    return failures;
}

int main(void)
{
    int failures = 0;
    int i, lx, ly, locale, transparency, scale_x, changed_owned = 0;
    const pcw_osk_skin_key_t *a_key = &pcw_osk_skin_uk.keys[37]; /* initial selection */
    const pcw_osk_skin_key_t *ret = &pcw_osk_skin_uk.keys[49];
    const pcw_osk_skin_baked_t *ret_sprite = &pcw_osk_skin_uk.bakeds[ret->baked];
    static uint32_t before[PCW_OSK_SKIN_UK_W * PCW_OSK_SKIN_UK_H];
    static uint32_t after[PCW_OSK_SKIN_UK_W * PCW_OSK_SKIN_UK_H];
    static uint32_t switched[PCW_OSK_SKIN_US_W * PCW_OSK_SKIN_US_H];

    /* Closing belongs exclusively to the configurable core-level OSK button.
       B is not an implicit close action inside the virtual keyboard itself. */
    fresh_osk();
    tap_button(RETRO_DEVICE_ID_JOYPAD_B);
    if (!pcw_osk_is_active()) failures++;

    /* A physically held beyond the configured minimum must keep the selected
       PCW key down, then release it immediately after the physical release. */
    fresh_osk();
    set_button(RETRO_DEVICE_ID_JOYPAD_A, 1);
    for (i = 0; i < 20; i++) pcw_osk_tick();
    if (!matrix_down(a_key)) failures++;
    set_button(RETRO_DEVICE_ID_JOYPAD_A, 0);
    pcw_osk_tick();
    if (matrix_down(a_key)) failures++;

    /* A short tap still observes the configured minimum, so a frontend's
       one-frame button pulse cannot be missed by the emulated machine. */
    fresh_osk();
    set_button(RETRO_DEVICE_ID_JOYPAD_A, 1);
    pcw_osk_tick();
    set_button(RETRO_DEVICE_ID_JOYPAD_A, 0);
    pcw_osk_tick();
    if (!matrix_down(a_key)) failures++;
    for (i = 0; i < 3; i++) pcw_osk_tick();
    if (matrix_down(a_key)) failures++;

    /* The 60 ms default is exactly three 20 ms frames. */
    fresh_osk();
    pcw_osk_set_options(60, 0, 0);
    set_button(RETRO_DEVICE_ID_JOYPAD_A, 1);
    set_button(RETRO_DEVICE_ID_JOYPAD_A, 0);
    pcw_osk_tick();
    pcw_osk_tick();
    if (!matrix_down(a_key)) failures++;
    pcw_osk_tick();
    if (matrix_down(a_key)) failures++;

    /* The X convenience RETURN follows the same physical-hold contract. */
    fresh_osk();
    set_button(RETRO_DEVICE_ID_JOYPAD_X, 1);
    for (i = 0; i < 20; i++) pcw_osk_tick();
    if (pcw_keyboard_table[2] & 0x04) failures++;
    set_button(RETRO_DEVICE_ID_JOYPAD_X, 0);
    pcw_osk_tick();
    if (!(pcw_keyboard_table[2] & 0x04)) failures++;

    /* RET's baked 24x34 bounding box overlaps its left-hand neighbour. Changing
       selection from A to RET may tint non-transparent RET pixels only; every
       transparent pixel in RET's sprite must remain byte-identical. */
    fresh_osk();
    pcw_osk_render(before, PCW_OSK_SKIN_UK_W, PCW_OSK_SKIN_UK_H,
                   PCW_OSK_SKIN_UK_W);
    for (i = 0; i < 12; i++) tap_button(RETRO_DEVICE_ID_JOYPAD_RIGHT);
    pcw_osk_render(after, PCW_OSK_SKIN_UK_W, PCW_OSK_SKIN_UK_H,
                   PCW_OSK_SKIN_UK_W);
    for (ly = 0; ly < ret->h; ly++) {
        for (lx = 0; lx < ret->w; lx++) {
            int pos = (ret->y + ly) * PCW_OSK_SKIN_UK_W + ret->x + lx;
            uint32_t own = pcw_osk_skin_uk.baked_pix[
                ret_sprite->off + ly * ret_sprite->w + lx];
            if (!own && before[pos] != after[pos]) failures++;
            if (own && before[pos] != after[pos]) changed_owned++;
        }
    }
    if (!changed_owned) failures++;

    /* Changing the core option must invalidate the cached panel immediately.
       The locale art differs, and switching back must reproduce UK byte-for-byte. */
    pcw_osk_set_options(100, 0, 1);
    pcw_osk_render(switched, PCW_OSK_SKIN_US_W, PCW_OSK_SKIN_US_H,
                   PCW_OSK_SKIN_US_W);
    if (!memcmp(after, switched, sizeof after)) failures++;
    pcw_osk_set_options(100, 0, 0);
    pcw_osk_render(switched, PCW_OSK_SKIN_UK_W, PCW_OSK_SKIN_UK_H,
                   PCW_OSK_SKIN_UK_W);
    if (memcmp(after, switched, sizeof after)) failures++;

    for (locale = 0; locale < 2; locale++)
        for (transparency = 0; transparency < 3; transparency++)
            for (scale_x = 1; scale_x <= 2; scale_x++)
                failures += check_return_focus(locale, transparency, scale_x);

    pcw_osk_reset();
    if (failures) {
        fprintf(stderr, "FAIL: %d OSK close/hold/RET-mask/layout checks\n", failures);
        return 1;
    }
    puts("PASS: OSK has no implicit close, preserves holds/RET mask and switches UK/US");
    return 0;
}
