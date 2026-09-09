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

/* Minimal diagnostics API for the PCW-only libretro engine (GPLv3). */
#ifndef ZESARPCW_PCW_DEBUG_H
#define ZESARPCW_PCW_DEBUG_H

#define VERBOSE_ERR       0
#define VERBOSE_WARN      1
#define VERBOSE_INFO      2
#define VERBOSE_DEBUG     3
#define VERBOSE_PARANOID  4

#define VERBOSE_CLASS_DSK       (1 << 8)
#define VERBOSE_CLASS_PD765     (1 << 9)
#define VERBOSE_CLASS_PCW       (1 << 10)

void debug_printf(int debuglevel, const char *format, ...);
/* Engine-only: callers must run inside pcw_engine's init/reset/frame boundary. */
_Noreturn void cpu_panic(const char *message);

/* The core deliberately has no verbosity option. Remove fixed informational,
 * debug and paranoid calls (including their arguments and strings) at compile
 * time, while retaining errors and warnings. */
#if !defined(ZPCW_DEBUG_IMPLEMENTATION)
#define debug_printf(debuglevel, ...)                                      \
    do {                                                                   \
        if (((debuglevel) & 0xff) <= VERBOSE_WARN)                         \
            debug_printf((debuglevel), __VA_ARGS__);                       \
    } while (0)
#endif

#endif
