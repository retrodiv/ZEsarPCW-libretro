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

/* PCW save-state blocks in a ZSF snapshot container. */
#ifndef ZESARPCW_PCW_ZSF_H
#define ZESARPCW_PCW_ZSF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool pcw_zsf_save(uint8_t *destination, size_t capacity, size_t *length);
bool pcw_zsf_load(const uint8_t *source, size_t length);

#endif
