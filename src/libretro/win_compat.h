/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * ZEsarPCW -- Amstrad PCW libretro core.
 * Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GNU GPL version 3; see LICENSE.
 * No warranty, including merchantability or fitness for a particular purpose.
 *
 * Force-included for mingw-w64 so all core file opens use the UTF-8 paths
 * supplied by libretro, independently of Windows' active ANSI code page.
 */
#ifndef ZESARUX_PCW_WIN_COMPAT_H
#define ZESARUX_PCW_WIN_COMPAT_H

#ifdef MINGW
#include <windows.h>
#include <stdio.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

FILE *pcw_fopen_utf8(const char *path, const char *mode);
int pcw_stat64_utf8(const char *path, struct __stat64 *info);
#define fopen pcw_fopen_utf8
#define _stat64(path, info) pcw_stat64_utf8(path, info)
#endif

#endif
