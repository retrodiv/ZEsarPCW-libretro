/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 retrodiv <retrodiv@proton.me> */
#ifndef PCW_BOOT_H
#define PCW_BOOT_H

#include "pcw_state_io.h"

void pcw_boot_begin(void);
/* Consume one emulated time slice, or return false to execute the guest. */
bool pcw_boot_step(void);
bool pcw_boot_state(struct pcw_state_io *io);

#endif
