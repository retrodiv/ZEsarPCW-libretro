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

/* Regression test for the rewind RAM encoder and unchanged-page cache. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pcw_state_rle.h"

static uint32_t random_state = UINT32_C(0x9e3779b9);

static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static int reference_rle(const uint8_t *source, uint8_t *output,
                         int length, uint8_t magic)
{
    uint8_t *destination = output;
    int previous_isolated_magic = 0;

    while (length > 0) {
        uint8_t repeated = source[0];
        int repetitions = 1;
        while (repetitions < length && source[repetitions] == repeated)
            repetitions++;
        source += repetitions;
        length -= repetitions;

        if (repetitions >= 5 || (repeated == magic && repetitions > 1)) {
            while (repetitions > 0) {
                if (previous_isolated_magic) {
                    previous_isolated_magic = 0;
                    destination[0] = magic;
                    destination[1] = magic;
                    destination[2] = 1;
                    destination += 3;
                }
                destination[0] = magic;
                destination[1] = magic;
                destination[2] = repeated;
                destination[3] = (uint8_t)(repetitions > 255 ? 0 : repetitions);
                destination += 4;
                repetitions -= 256;
            }
        } else {
            previous_isolated_magic = repetitions == 1 && repeated == magic;
            memset(destination, repeated, (size_t)repetitions);
            destination += repetitions;
        }
    }
    return (int)(destination - output);
}

static int check_case(const uint8_t *source, int length)
{
    uint8_t expected[PCW_STATE_RLE_PAGE_SIZE * 2];
    uint8_t actual[PCW_STATE_RLE_PAGE_SIZE * 2];
    int expected_length = reference_rle(source, expected, length, 0xdd);
    int actual_length = pcw_state_rle_compress(source, actual, length, 0xdd);
    return expected_length == actual_length &&
           memcmp(expected, actual, (size_t)expected_length) == 0;
}

int main(void)
{
    uint8_t page[PCW_STATE_RLE_PAGE_SIZE];
    uint8_t expected[PCW_STATE_RLE_PAGE_SIZE * 2];
    uint8_t actual[PCW_STATE_RLE_PAGE_SIZE * 2];

    for (unsigned test = 0; test < 10000; test++) {
        int length = (int)(next_random() % (PCW_STATE_RLE_PAGE_SIZE + 1));
        for (int i = 0; i < length; i++)
            page[i] = (uint8_t)next_random();
        /* Exercise long runs, the 256-byte split and isolated 0xdd boundaries. */
        if (length > 700 && (test & 1))
            memset(page + 113, (test & 2) ? 0xdd : 0, 513);
        if (length > 20 && (test & 4)) {
            page[7] = 0xdd;
            memset(page + 8, 0x42, 7);
        }
        if (!check_case(page, length)) {
            fprintf(stderr, "RLE mismatch in fuzz case %u (length %d)\n", test, length);
            return 1;
        }
    }

    for (int i = 0; i < PCW_STATE_RLE_PAGE_SIZE; i++)
        page[i] = (uint8_t)next_random();
    memset(page + 2048, 0, 4096);
    int expected_length = reference_rle(page, expected, sizeof page, 0xdd);
    int expected_compressed = expected_length <= PCW_STATE_RLE_PAGE_SIZE;
    if (!expected_compressed) {
        memcpy(expected, page, sizeof page);
        expected_length = sizeof page;
    }

    int compressed = -1;
    pcw_state_rle_reset_cache();
    int actual_length = pcw_state_rle_compress_page(page, actual, 7, 0, &compressed);
    if (actual_length != expected_length || compressed != expected_compressed ||
        memcmp(actual, expected, (size_t)expected_length) != 0) return 2;
    memset(actual, 0xa5, sizeof actual);
    actual_length = pcw_state_rle_compress_page(page, actual, 7, 0, &compressed);
    if (actual_length != expected_length || compressed != expected_compressed ||
        memcmp(actual, expected, (size_t)expected_length) != 0) return 3;

    page[12345] ^= 0x80;
    expected_length = reference_rle(page, expected, sizeof page, 0xdd);
    expected_compressed = expected_length <= PCW_STATE_RLE_PAGE_SIZE;
    if (!expected_compressed) {
        memcpy(expected, page, sizeof page);
        expected_length = sizeof page;
    }
    actual_length = pcw_state_rle_compress_page(page, actual, 7, 0, &compressed);
    if (actual_length != expected_length || compressed != expected_compressed ||
        memcmp(actual, expected, (size_t)expected_length) != 0) return 4;

    actual_length = pcw_state_rle_compress_page(page, actual, 7, 1, &compressed);
    if (actual_length != PCW_STATE_RLE_PAGE_SIZE || compressed != 0 ||
        memcmp(actual, page, sizeof page) != 0) return 5;

    pcw_state_rle_free_cache();
    actual_length = pcw_state_rle_compress_page(page, actual, 7, 0, &compressed);
    if (actual_length != expected_length || compressed != expected_compressed ||
        memcmp(actual, expected, (size_t)expected_length) != 0) return 6;
    pcw_state_rle_free_cache();

    puts("PASS: byte-identical state RLE and unchanged-page cache");
    return 0;
}
