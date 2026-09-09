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

/*
 * Fast PCW RAM encoder for libretro rewind.
 *
 * RAM pages use ZSF's repetition encoding. Rewind snapshots arrive every frame
 * while most 16 KiB banks are unchanged; a raw shadow lets those banks reuse
 * their encoded form after a
 * vectorised libc memcmp instead of rescanning them byte by byte.
 */
#include "pcw_state_rle.h"

#include <stdlib.h>
#include <string.h>

struct pcw_state_rle_page {
    uint8_t shadow[PCW_STATE_RLE_PAGE_SIZE];
    uint8_t encoded[PCW_STATE_RLE_PAGE_SIZE];
    int encoded_length;
    uint8_t encoded_is_compressed;
    uint8_t valid;
};

static struct pcw_state_rle_page *page_cache;
static int page_cache_allocation_failed;

int pcw_state_rle_compress(const uint8_t *input, uint8_t *output,
                           int length, uint8_t magic)
{
    const uint8_t *source = input;
    const uint8_t *literal = input;
    const uint8_t *end = input + length;
    uint8_t *destination = output;

    while (source < end) {
        int remaining = (int)(end - source);
        uint8_t repeated = *source;
        int is_run = repeated == magic && remaining >= 2 && source[1] == repeated;
        if (!is_run && remaining >= 5 &&
            source[1] == repeated && source[2] == repeated &&
            source[3] == repeated && source[4] == repeated)
            is_run = 1;

        if (!is_run) {
            source++;
            continue;
        }

        const uint8_t *run_end = source + (repeated == magic ? 2 : 5);
        uint64_t repeated_word = UINT64_C(0x0101010101010101) * repeated;
        while (end - run_end >= 8) {
            uint64_t word;
            memcpy(&word, run_end, sizeof word);
            if (word != repeated_word) break;
            run_end += 8;
        }
        while (run_end < end && *run_end == repeated) run_end++;

        int literal_length = (int)(source - literal);
        if (literal_length) {
            memcpy(destination, literal, (size_t)literal_length);
            destination += literal_length;
            if (source[-1] == magic) {
                destination[0] = magic;
                destination[1] = magic;
                destination[2] = 1;
                destination += 3;
            }
        }

        int repetitions = (int)(run_end - source);
        while (repetitions > 0) {
            destination[0] = magic;
            destination[1] = magic;
            destination[2] = repeated;
            destination[3] = (uint8_t)(repetitions > 255 ? 0 : repetitions);
            destination += 4;
            repetitions -= 256;
        }

        source = run_end;
        literal = source;
    }

    if (literal < end) {
        size_t literal_length = (size_t)(end - literal);
        memcpy(destination, literal, literal_length);
        destination += literal_length;
    }
    return (int)(destination - output);
}

int pcw_state_rle_compress_page(const uint8_t *source, uint8_t *output,
                                unsigned page, int force_uncompressed,
                                int *is_compressed)
{
    struct pcw_state_rle_page *cached;
    int encoded_length;

    if (force_uncompressed || page >= PCW_STATE_RLE_MAX_PAGES) {
        memcpy(output, source, PCW_STATE_RLE_PAGE_SIZE);
        *is_compressed = 0;
        return PCW_STATE_RLE_PAGE_SIZE;
    }

    if (!page_cache && !page_cache_allocation_failed) {
        page_cache = calloc(PCW_STATE_RLE_MAX_PAGES, sizeof *page_cache);
        if (!page_cache) page_cache_allocation_failed = 1;
    }
    cached = page_cache ? &page_cache[page] : NULL;
    if (cached && cached->valid &&
        memcmp(cached->shadow, source, PCW_STATE_RLE_PAGE_SIZE) == 0) {
        memcpy(output, cached->encoded, (size_t)cached->encoded_length);
        *is_compressed = cached->encoded_is_compressed;
        return cached->encoded_length;
    }

    encoded_length = pcw_state_rle_compress(source, output,
                                             PCW_STATE_RLE_PAGE_SIZE, 0xdd);
    if (encoded_length > PCW_STATE_RLE_PAGE_SIZE) {
        memcpy(output, source, PCW_STATE_RLE_PAGE_SIZE);
        encoded_length = PCW_STATE_RLE_PAGE_SIZE;
        *is_compressed = 0;
    } else {
        *is_compressed = 1;
    }

    if (cached) {
        memcpy(cached->shadow, source, PCW_STATE_RLE_PAGE_SIZE);
        memcpy(cached->encoded, output, (size_t)encoded_length);
        cached->encoded_length = encoded_length;
        cached->encoded_is_compressed = (uint8_t)*is_compressed;
        cached->valid = 1;
    }
    return encoded_length;
}

void pcw_state_rle_reset_cache(void)
{
    if (page_cache) {
        unsigned page;
        for (page = 0; page < PCW_STATE_RLE_MAX_PAGES; page++)
            page_cache[page].valid = 0;
    }
}

void pcw_state_rle_free_cache(void)
{
    free(page_cache);
    page_cache = NULL;
    page_cache_allocation_failed = 0;
}
