/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Floppy-drive sound: voices the mechanical noise of the PCW's 3" disc drive
    while the machine is loading -- the motor spin, the head read chatter and the
    head seek -- from real PCM recordings (pcw_fdc_samples.h), mixed straight into
    the libretro audio stream. It is off by default and selectable from the core
    options (Off / Quiet / Normal / Loud).

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#ifndef PCW_FDC_SOUND_H
#define PCW_FDC_SOUND_H

#include <stdint.h>

/* Set the drive-sound level: 0 = off, 1 = quiet, 2 = normal, 3 = loud. */
void pcw_fdc_sound_set_level(int level);

/* True while the synthesiser still has audible output (motor spinning or a step
   click playing). The audio path uses it to keep flushing frames during an
   otherwise-silent disc load so the drive noise is not swallowed by the beeper's
   silence detector. */
int  pcw_fdc_sound_wants_audio(void);

/* Sample the FDC's motor / head-position state and schedule any new step events.
   Call once per emulated video frame (50 Hz), just before mixing. */
void pcw_fdc_sound_poll(void);

/* Additively mix `frames` interleaved-stereo int16 samples, generated at `rate`
   Hz, into out[] (which already holds the beeper output). Clamps to int16. */
void pcw_fdc_sound_mix(int16_t *out, int frames, int rate);

/* Silence all voices and re-baseline the head position (machine reset / disc
   change), so a reset does not emit a spurious seek train. */
void pcw_fdc_sound_reset(void);

#endif /* PCW_FDC_SOUND_H */
