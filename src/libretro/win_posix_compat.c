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
#ifdef MINGW

#include <stdlib.h>
#include <errno.h>
#include "win_compat.h"

static wchar_t *wide_utf8(const char *text)
{
    int count;
    wchar_t *wide;
    if (!text || !text[0]) {
        errno = EINVAL;
        return NULL;
    }
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count) {
        errno = EILSEQ;
        return NULL;
    }
    wide = malloc((size_t)count * sizeof(*wide));
    if (!wide) {
        errno = ENOMEM;
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) {
        free(wide);
        errno = EILSEQ;
        return NULL;
    }
    return wide;
}

FILE *pcw_fopen_utf8(const char *path, const char *mode)
{
    wchar_t *wide_path = wide_utf8(path);
    wchar_t *wide_mode;
    FILE *file;
    int saved_errno;
    if (!wide_path) return NULL;
    wide_mode = wide_utf8(mode);
    if (!wide_mode) {
        saved_errno = errno;
        free(wide_path);
        errno = saved_errno;
        return NULL;
    }
    /* Wide filenames do not change the byte encoding of the file contents. */
    file = _wfopen(wide_path, wide_mode);
    saved_errno = errno;
    free(wide_mode);
    free(wide_path);
    errno = saved_errno;
    return file;
}

int pcw_stat64_utf8(const char *path, struct __stat64 *info)
{
    wchar_t *wide_path;
    int result, saved_errno;
    if (!info) {
        errno = EINVAL;
        return -1;
    }
    wide_path = wide_utf8(path);
    if (!wide_path) return -1;
    result = _wstat64(wide_path, info);
    saved_errno = errno;
    free(wide_path);
    errno = saved_errno;
    return result;
}

#endif
