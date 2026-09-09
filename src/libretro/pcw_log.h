/* SPDX-License-Identifier: GPL-3.0-only */
/* ZEsarPCW -- Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GNU GPL v3; see LICENSE. */
#ifndef ZESARPCW_LOG_H
#define ZESARPCW_LOG_H

#include <stdarg.h>
#include "libretro.h"

void pcw_log_set_environment(retro_environment_t environment);
void pcw_log(enum retro_log_level level, const char *format, ...);
void pcw_log_v(enum retro_log_level level, const char *format, va_list args);

#endif
