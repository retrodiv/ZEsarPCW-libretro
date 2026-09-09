/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Native libretro audio driver.

    ZEsarUX fills a stereo, 8-bit-signed buffer at FRECUENCIA_SONIDO (15600 Hz for
    the PCW: 312 samples/frame x 50 frames/s) and hands one AUDIO_BUFFER_SIZE block
    to audio_send_frame() per video frame. This driver converts
    that block to libretro's signed-16-bit interleaved stereo and pushes it to the
    frontend's audio batch callback (libretro_audio_batch, defined in libretro.c).

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/


#include "audiolibretro.h"
#include <string.h>
#include "audio.h"
#include "pcw_debug.h"
#include "pcw_fdc_sound.h"   /* mechanical disc-drive sound, mixed in below */

/* Implemented in libretro.c -- forwards converted samples to retro_audio_batch. */
extern void libretro_audio_batch(const int16_t *data, unsigned frames);

/* The output rate the frontend was told (libretro.c, "Audio sample rate" option).
   When it equals the native FRECUENCIA_SONIDO (15600) we pass the block straight
   through; when it is the standard 48000 we linearly resample so RetroArch never
   has to rate-convert the odd 15600 Hz stream (the source of the speed wobble). */
extern int pcw_libretro_audio_rate;

/* One 48000 Hz frame at 50 fps: this is the largest selectable output rate. */
#define AUDIO_OUT_MAX 960
static int16_t out[AUDIO_OUT_MAX * 2];
static unsigned pending_frames;

void audiolibretro_discard_frame(void) { pending_frames = 0; }

void audiolibretro_silence_frame(void)
{
    /* Discard any block staged by the incomplete frame; deliver exactly 20 ms
       at either supported output rate, without mixing drive noise into it. */
    unsigned frames = pcw_libretro_audio_rate == 48000 ? AUDIO_OUT_MAX : AUDIO_BUFFER_SIZE;
    pending_frames = 0;
    memset(out, 0, frames * 2 * sizeof *out);
    libretro_audio_batch(out, frames);
}

void audiolibretro_flush_frame(void)
{
    unsigned frames = pending_frames;
    pending_frames = 0;
    if (frames) libretro_audio_batch(out, frames);
}

int audiolibretro_init(void)
{
    audiolibretro_discard_frame();
    debug_printf(VERBOSE_INFO, "Init libretro Audio Driver, %d Hz", FRECUENCIA_SONIDO);
    return 0;
}

int audiolibretro_convert_s8(const int8_t *input, int input_frames,
                             int source_rate, int output_rate,
                             int16_t *output, int output_capacity)
{
    int output_frames;
    if (!input || !output || input_frames <= 0 || source_rate <= 0 ||
        output_rate <= 0 || output_capacity <= 0)
        return 0;

    if (output_rate == source_rate) {
        int i;
        output_frames = input_frames < output_capacity ? input_frames : output_capacity;
        for (i = 0; i < output_frames; i++) {
            output[i * 2]     = (int16_t)(input[i * 2] * 256);
            output[i * 2 + 1] = (int16_t)(input[i * 2 + 1] * 256);
        }
        return output_frames;
    }

    output_frames = (int)((long long)input_frames * output_rate / source_rate);
    if (output_frames > output_capacity) output_frames = output_capacity;
    {
        int j;
        for (j = 0; j < output_frames; j++) {
            long long pos = (long long)j * input_frames;
            int idx = (int)(pos / output_frames);
            int frac = (int)(pos - (long long)idx * output_frames);
            int idx1 = (idx + 1 < input_frames) ? idx + 1 : idx;
            int l0 = input[idx * 2],     l1 = input[idx1 * 2];
            int r0 = input[idx * 2 + 1], r1 = input[idx1 * 2 + 1];
            int lv = l0 + (int)((long long)(l1 - l0) * frac / output_frames);
            int rv = r0 + (int)((long long)(r1 - r0) * frac / output_frames);
            output[j * 2]     = (int16_t)(lv * 256);
            output[j * 2 + 1] = (int16_t)(rv * 256);
        }
    }
    return output_frames;
}

void audiolibretro_send_frame(const int8_t *buffer)
{
    /* buffer holds AUDIO_BUFFER_SIZE interleaved stereo frames, 8-bit signed
       per channel (L,R,L,R,...) -- AUDIO_BUFFER_SIZE*2 bytes total. */
    const int n_in = AUDIO_BUFFER_SIZE;            /* source frames @ FRECUENCIA_SONIDO */
    const int src  = FRECUENCIA_SONIDO;            /* native rate (15600 for the PCW) */
    int n_out;

    /* Sample the drive's motor/head state once per emulated frame (this driver is
       flushed exactly once per frame under libretro) before mixing its sound in. */
    pcw_fdc_sound_poll();

    n_out = audiolibretro_convert_s8(buffer, n_in, src,
                                     pcw_libretro_audio_rate, out, AUDIO_OUT_MAX);
    if (n_out <= 0) return;
    pcw_fdc_sound_mix(out, n_out, pcw_libretro_audio_rate);
    /* The bridge delivers this block only after guarded emulation returns. */
    pending_frames = (unsigned)n_out;
}
