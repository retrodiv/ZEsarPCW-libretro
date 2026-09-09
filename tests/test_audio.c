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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audiolibretro.h"
#include "audio.h"
#include "ay38912.h"
#include "pcw_fdc_sound.h"

extern int8_t audio_buffer_assigned[];
extern short ultimo_valor_tono_A[], ultimo_valor_tono_B[], ultimo_valor_tono_C[];
int pcw_libretro_audio_rate = 15600;
int pd765_motor_speed, pd765_pcn, pd765_read_stats_bytes_sec_acumulated;
static int16_t delivered[960*2];
static unsigned delivered_frames, batches;
void debug_printf(int level, const char *format, ...) { (void)level; (void)format; }
void libretro_audio_batch(const int16_t *data, unsigned frames)
{
    delivered_frames = frames;
    memcpy(delivered, data, frames*2*sizeof *data);
    batches++;
}

static int failures;

static void expect(int condition, const char *message, int sample)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s (sample=%d)\n", message, sample);
        failures++;
    }
}

int main(void)
{
    int8_t input[4];
    int16_t output[16];

    for (int value = -128; value <= 127; value++) {
        for (int i = 0; i < 4; i++) input[i] = (int8_t)value;

        int frames = audiolibretro_convert_s8(input, 2, 15600, 15600,
                                               output, 8);
        expect(frames == 2, "native frame count", value);
        for (int i = 0; i < frames * 2; i++)
            expect(output[i] == value * 256, "native int8 conversion", value);

        frames = audiolibretro_convert_s8(input, 2, 15600, 48000,
                                          output, 8);
        expect(frames == 6, "48 kHz frame count", value);
        for (int i = 0; i < frames * 2; i++)
            expect(output[i] == value * 256, "48 kHz int8 conversion", value);
    }

    input[0] = input[1] = -128;
    input[2] = input[3] = 127;
    int frames = audiolibretro_convert_s8(input, 2, 15600, 48000, output, 8);
    expect(frames == 6, "extreme interpolation frame count", 0);
    for (int i = 1; i < frames; i++)
        expect(output[i * 2] >= output[(i - 1) * 2],
               "extreme interpolation is monotonic", i);

    expect(audiolibretro_convert_s8(NULL, 2, 15600, 15600, output, 8) == 0,
           "NULL input rejected", 0);
    expect(audiolibretro_convert_s8(input, 2, 0, 15600, output, 8) == 0,
           "zero source rate rejected", 0);

    audio_buffer = audio_buffer_assigned;
    for (int rate = 0; rate < 2; rate++) {
        pcw_libretro_audio_rate = rate ? 48000 : 15600;
        unsigned wanted = rate ? 960 : 312;
        for (int value = -128; value <= 127; value++) {
            audio_buffer_indice = 0;
            audio_send_stereo_sample((int8_t)value, (int8_t)(-value-1));
            unsigned before = batches;
            envio_audio();
            expect(batches == before, "delivery deferred until engine returns", value);
            audiolibretro_flush_frame();
            expect(batches == before+1 && delivered_frames == wanted,
                   "complete audio frame delivered once", value);
            for (unsigned i = 0; i < wanted; i++) {
                expect(delivered[i*2] == value*256, "signed left buffer/padding", value);
                expect(delivered[i*2+1] == (-value-1)*256, "signed right buffer/padding", value);
            }
            audiolibretro_flush_frame();
            expect(batches == before+1, "no duplicate audio delivery", value);
        }
        audio_buffer_indice = 0;
        audio_send_mono_sample(-100);
        envio_audio();
        unsigned before = batches;
        audiolibretro_discard_frame();
        audiolibretro_flush_frame();
        expect(batches == before, "failed frame audio discarded", rate);
    }

    /* The real AY generator must preserve the negative half of its waveform
       when plain char is unsigned. Three loud channels sum to +/-72. */
    total_ay_chips = 1;
    ay_chip_present.v = 1;
    ay_3_8912_registros[0][7] = 0x38; /* tones enabled, noise disabled */
    ay_3_8912_registros[0][8] = ay_3_8912_registros[0][9] = ay_3_8912_registros[0][10] = 15;
    for (int sign = -1; sign <= 1; sign += 2) {
        int8_t left, right;
        ultimo_valor_tono_A[0] = ultimo_valor_tono_B[0] = ultimo_valor_tono_C[0] = (short)(sign*32767);
        da_output_ay_izquierdo_derecho(&left, &right);
        expect(left == sign*72 && right == sign*72, "signed AY tone/mix", sign);
    }

    /* Mix the real FDC samples into opposite int16 rails. A positive mix must
       clamp left; a negative mix must clamp right, without wrapping sign. */
    int16_t loud[960*2];
    pcw_fdc_sound_set_level(3);
    pcw_fdc_sound_reset();
    pd765_motor_speed = 100;
    pcw_fdc_sound_poll();
    for (int i = 0; i < 960; i++) { loud[i*2] = 32767; loud[i*2+1] = -32768; }
    pcw_fdc_sound_mix(loud, 960, 48000);
    int positive = 0, negative = 0;
    for (int i = 0; i < 960; i++) {
        expect(loud[i*2] == 32767 || loud[i*2+1] == -32768, "FDC saturation", i);
        positive |= loud[i*2+1] > -32768;
        negative |= loud[i*2] < 32767;
    }
    expect(positive && negative, "FDC contains both sample signs", 0);

    if (failures) return 1;
    puts("PASS: signed PCM/AY, deferred frame delivery and FDC saturation at 15600/48000 Hz");
    return 0;
}
