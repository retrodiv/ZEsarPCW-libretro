/* SPDX-License-Identifier: GPL-3.0-only */
/* ZEsarPCW -- Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GNU GPL v3; see LICENSE. */
#ifndef ZESARPCW_ENGINE_H
#define ZESARPCW_ENGINE_H

int zesarpcw_libretro_init(int machine_type);
int pcw_engine_reset(void);
/* -1: fatal error; 0: frame limit; 1: completed frame. */
int pcw_engine_run_frame(long *opcodes);
const char *pcw_engine_error(void);

#endif
