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

/* Fast PCW RAM encoder for libretro save states. */
#ifndef ZESARPCW_STATE_RLE_H
#define ZESARPCW_STATE_RLE_H

#include <stdint.h>

#define PCW_STATE_RLE_PAGE_SIZE 16384
#define PCW_STATE_RLE_MAX_PAGES 32

int pcw_state_rle_compress(const uint8_t *source, uint8_t *output,
                           int length, uint8_t magic);
int pcw_state_rle_compress_page(const uint8_t *source, uint8_t *output,
                                unsigned page, int force_uncompressed,
                                int *is_compressed);
void pcw_state_rle_reset_cache(void);
void pcw_state_rle_free_cache(void);

#endif
