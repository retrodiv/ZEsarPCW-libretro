/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Floppy-drive sound -- see pcw_fdc_sound.h.

    The PCW drives its 3" disc through a NEC uPD765 FDC (storage/pd765.c). That
    emulation already exposes everything an audible drive needs:

       pd765_motor_speed                     0..100 relative spindle speed
       pd765_pcn                             present cylinder (the head's track)
       pd765_read_stats_bytes_sec_acumulated bytes read so far this second

    We voice the drive from three real PCM recordings (pcw_fdc_samples.h): a motor
    spin loop, a head read/load chatter, and a head seek. They play as the FDC moves:

      * the motor sample loops continuously while the spindle turns, its volume
        following pd765_motor_speed so it fades in on spin-up and coasts on stop;
      * the seek sample fires once each time the head changes track (pd765_pcn);
      * the read sample fires whenever the controller is streaming sector bytes,
        re-triggering so a long load keeps chattering.

    The samples are 44100 Hz; we resample them to the output rate on the fly and mix
    them additively into the libretro audio stream (audiolibretro.c).

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#include <string.h>

#include "pcw_fdc_sound.h"
#include "pcw_fdc_samples.h"

/* --- live FDC state we read (defined in storage/pd765.c) -------------------- */
extern int pd765_motor_speed;                    /* 0..100, smoothed by the FDC */
extern int pd765_pcn;                            /* present cylinder (track)     */
extern int pd765_read_stats_bytes_sec_acumulated;/* bytes read so far this sec   */

/* --- option ----------------------------------------------------------------- */
static int sound_level = 0;     /* 0 off, 1 quiet, 2 normal, 3 loud */

static float level_gain(void)
{
    /* The recordings are near full-scale; "Normal" plays them essentially
       unattenuated (as the reference does) so the drive sits up front, not far
       away. Loud pushes past that (peaks clamp). */
    switch (sound_level) {
        case 1:  return 0.55f;   /* quiet  */
        case 2:  return 1.00f;   /* normal (essentially unattenuated, like cap32) */
        case 3:  return 1.20f;   /* loud   (a touch hotter; peaks clamp) */
        default: return 0.0f;
    }
}

void pcw_fdc_sound_set_level(int level)
{
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    sound_level = level;
}

/* --- voices ----------------------------------------------------------------- */
/* One looping motor voice + a small pool of one-shots for seek / read. Each holds
   a fractional play position so it can be resampled to any output rate. */
typedef struct {
    const int16_t *pcm;
    int    len;
    double pos;       /* fractional sample index into pcm */
    int    active;
} voice;

#define OSN 4
static voice motor;          /* looping spindle hum  */
static voice readv;          /* looping read chatter */
static voice os[OSN];        /* seek one-shots        */

static float motor_amp = 0.0f;   /* smoothed spindle volume 0..1 */
static float read_amp  = 0.0f;   /* smoothed read-chatter volume 0..1 */
static int   read_hold = 0;      /* frames the read chatter stays on after the last read */

static int   last_pcn = -1;
static int   primed = 0;
static int   last_read_acc = 0;

/* The read chatter holds on for ~120 ms past the last byte moved, so a burst of
   sector reads sounds like one continuous load rather than chopped clicks. */
#define READ_HOLD_FRAMES 6

static void fire_seek(void)
{
    int i, slot = -1;
    for (i = 0; i < OSN; i++) if (!os[i].active) { slot = i; break; }
    if (slot < 0) { double best = -1; for (i = 0; i < OSN; i++) if (os[i].pos > best) { best = os[i].pos; slot = i; } }
    os[slot].pcm = pcw_fdc_seek_pcm; os[slot].len = pcw_fdc_seek_len;
    os[slot].pos = 0.0; os[slot].active = 1;
}

/* --- public ----------------------------------------------------------------- */

void pcw_fdc_sound_reset(void)
{
    memset(os, 0, sizeof os);
    motor.pcm = pcw_fdc_motor_pcm; motor.len = pcw_fdc_motor_len; motor.pos = 0.0; motor.active = 1;
    readv.pcm = pcw_fdc_read_pcm;  readv.len = pcw_fdc_read_len;  readv.pos = 0.0; readv.active = 1;
    motor_amp = read_amp = 0.0f;
    read_hold = 0;
    last_pcn = pd765_pcn;
    last_read_acc = pd765_read_stats_bytes_sec_acumulated;
    primed = 1;
}

int pcw_fdc_sound_wants_audio(void)
{
    if (sound_level == 0) return 0;
    if (motor_amp > 0.001f || pd765_motor_speed > 0) return 1;
    if (read_amp  > 0.001f || read_hold > 0) return 1;
    {
        int i;
        for (i = 0; i < OSN; i++) if (os[i].active) return 1;
    }
    return 0;
}

void pcw_fdc_sound_poll(void)
{
    if (sound_level == 0) return;
    if (!primed) pcw_fdc_sound_reset();

    /* head moved -> one seek sound per track change */
    if (pd765_pcn != last_pcn) {
        fire_seek();
        last_pcn = pd765_pcn;
    }

    /* controller streaming bytes -> keep the read chatter alive (a rise in the
       byte counter = activity; it resets to 0 every second, so only count rises). */
    int acc = pd765_read_stats_bytes_sec_acumulated;
    if (acc > last_read_acc) read_hold = READ_HOLD_FRAMES;
    else if (read_hold > 0)  read_hold--;
    last_read_acc = acc;
}

/* linear-interpolated sample fetch at fractional position pos */
static float voice_sample(const voice *v)
{
    int i = (int)v->pos;
    if (i < 0 || i >= v->len) return 0.0f;
    int j = (i + 1 < v->len) ? i + 1 : i;
    float frac = (float)(v->pos - (double)i);
    return (float)v->pcm[i] + ((float)v->pcm[j] - (float)v->pcm[i]) * frac;
}

void pcw_fdc_sound_mix(int16_t *out, int frames, int rate)
{
    if (sound_level == 0 || frames <= 0 || rate <= 0) return;
    if (!pcw_fdc_sound_wants_audio()) return;

    const double step = (double)PCW_FDC_SAMPLE_RATE / (double)rate;  /* src/dst */
    const float  gain = level_gain();
    const float  m_target = (float)pd765_motor_speed / 100.0f;       /* spindle */
    const float  r_target = (read_hold > 0) ? 1.0f : 0.0f;           /* reading? */
    /* one-pole smoothing (no expf: fixed coeffs are fine at the 48000 / 15600 output
       rates we use). The read fades a touch faster so bursts stay punchy. */
    const float  m_a = (rate >= 40000) ? 0.0016f : 0.0048f;
    const float  r_a = (rate >= 40000) ? 0.0040f : 0.0120f;

    int n, i;
    for (n = 0; n < frames; n++) {
        float s = 0.0f;

        /* motor (looping), volume follows the spindle speed */
        motor_amp += m_a * (m_target - motor_amp);
        s += voice_sample(&motor) * motor_amp;
        motor.pos += step;
        if (motor.pos >= (double)motor.len) motor.pos -= (double)motor.len;

        /* read chatter (looping), on while the controller streams bytes */
        read_amp += r_a * (r_target - read_amp);
        s += voice_sample(&readv) * read_amp;
        readv.pos += step;
        if (readv.pos >= (double)readv.len) readv.pos -= (double)readv.len;

        /* seek one-shots */
        for (i = 0; i < OSN; i++) {
            voice *v = &os[i];
            if (!v->active) continue;
            s += voice_sample(v);
            v->pos += step;
            if (v->pos >= (double)v->len) v->active = 0;
        }

        s *= gain;

        {
            int l = (int)out[n * 2]     + (int)s;
            int r = (int)out[n * 2 + 1] + (int)s;
            if (l >  32767) l =  32767; else if (l < -32768) l = -32768;
            if (r >  32767) r =  32767; else if (r < -32768) r = -32768;
            out[n * 2]     = (int16_t)l;
            out[n * 2 + 1] = (int16_t)r;
        }
    }
}
