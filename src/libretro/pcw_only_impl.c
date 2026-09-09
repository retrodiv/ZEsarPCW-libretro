/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>. Distributed under the GNU General Public License v3
    (the same licence as ZEsarUX, Copyright (C) Cesar Hernandez Bano).
*/
//Implementaciones minimas REALES de simbolos que la ruta PCW si ejecuta pero cuyo
//fichero de origen (de otra maquina) se ha eliminado del arbol PCW-only.

#define ZPCW_DEBUG_IMPLEMENTATION

#include "pcw_debug.h"
#include "pcw_log.h"
#include "pcw_only_impl.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

/* PCM uses int8_t; the remaining upstream CPU code still needs signed char. */
_Static_assert(CHAR_MIN < 0, "The PCW engine requires -fsigned-char");

void debug_printf(int level, const char *format, ...)
{
    int priority=level & 0xff;
    va_list arguments;
    if (priority>VERBOSE_WARN) return;
    va_start(arguments,format);
    pcw_log_v(priority==VERBOSE_ERR ? RETRO_LOG_ERROR : RETRO_LOG_WARN,
              format,arguments);
    va_end(arguments);
}

z80_bit noautoload;

long long int get_file_size(char *name)
{
#if defined(_WIN32)
    struct __stat64 st;
    if (!name || _stat64(name,&st)!=0) return 0;
#else
    struct stat st;
    if (!name || stat(name,&st)!=0) return 0;
#endif
    return (long long int)st.st_size;
}

int util_parity(z80_byte value)
{
    value^=value>>4;
    value^=value>>2;
    value^=value>>1;
    return (value&1)^1;
}

/* PCW memory and ports have no emulated contention.  Keeping the five tiny
   timing callbacks here avoids linking contend.c and its 3.4 MiB of tables for
   machines which this core can never select. */
void contend_read_pcw(z80_int direccion GCC_UNUSED, int time)
{
    t_estados += time;
}

void contend_read_no_mreq_pcw(z80_int direccion GCC_UNUSED, int time)
{
    t_estados += time;
}

void contend_write_no_mreq_pcw(z80_int direccion GCC_UNUSED, int time)
{
    t_estados += time;
}

void ula_contend_port_early_pcw(z80_int port GCC_UNUSED)
{
    t_estados++;
}

void ula_contend_port_late_pcw(z80_int port GCC_UNUSED)
{
    t_estados += 2;
}
