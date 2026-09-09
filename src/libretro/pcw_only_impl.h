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

/* Small live helpers supplied by pcw_only_impl.c (GPLv3). */
#ifndef ZESARPCW_PCW_ONLY_IMPL_H
#define ZESARPCW_PCW_ONLY_IMPL_H

#include "cpu.h"

long long int get_file_size(char *name);
int util_parity(z80_byte value);

#endif
