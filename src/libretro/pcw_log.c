/* SPDX-License-Identifier: GPL-3.0-only */
/* ZEsarPCW -- Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GNU GPL v3; see LICENSE. */
#include "pcw_log.h"

#include <stdio.h>
#include <string.h>

static retro_log_printf_t frontend_log;

void pcw_log_set_environment(retro_environment_t environment)
{
    struct retro_log_callback callback = { NULL };
    frontend_log = NULL;
    if (environment && environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &callback))
        frontend_log = callback.log;
}

void pcw_log_v(enum retro_log_level level, const char *format, va_list args)
{
    /* Logging must remain available during allocation failures. Reserve room for
       a terminating newline, including when a diagnostic is truncated. */
    char message[2048];
    if (vsnprintf(message, sizeof message - 1, format, args) < 0) return;
    size_t length = strlen(message);
    if (!length || message[length - 1] != '\n') message[length++] = '\n';
    message[length] = 0;
    if (frontend_log) frontend_log(level, "%s", message);
    else fputs(message, stderr);
}

void pcw_log(enum retro_log_level level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    pcw_log_v(level, format, args);
    va_end(args);
}
