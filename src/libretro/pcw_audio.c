/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>. Distributed under the GNU GPL v3.
*/
/* PCW/libretro owns pacing and flushes exactly one 312-sample stereo block per
   emulated frame.  This replaces the standalone application's recording,
   MIDI/file export, menu-tone and threaded multi-frame buffer machinery. */

#include "audio.h"
#include "audiolibretro.h"

#include <string.h>

int8_t audio_buffer_assigned[AUDIO_BUFFER_SIZE*2];
int8_t *audio_buffer;
int audio_buffer_indice;

z80_bit beeper_enabled={1};
int8_t value_beeper;
int8_t audio_valor_enviar_sonido_izquierdo;
int8_t audio_valor_enviar_sonido_derecho;

void audio_empty_buffer(void)
{
    if (audio_buffer) memset(audio_buffer,0,AUDIO_BUFFER_SIZE*2);
}

void audio_send_stereo_sample(int8_t left,int8_t right)
{
    const int limit=AUDIO_BUFFER_SIZE*2;
    if (!audio_buffer || audio_buffer_indice>limit-2) return;
    audio_buffer[audio_buffer_indice++]=left;
    audio_buffer[audio_buffer_indice++]=right;
}

void audio_send_mono_sample(int8_t sample)
{
    audio_send_stereo_sample(sample,sample);
}

void envio_audio(void)
{
    if (!audio_buffer) return;

    /* A short frame is padded with its last complete stereo sample. */
    if (audio_buffer_indice>1) {
        int8_t left=audio_buffer[audio_buffer_indice-2];
        int8_t right=audio_buffer[audio_buffer_indice-1];
        while (audio_buffer_indice<AUDIO_BUFFER_SIZE*2) {
            audio_buffer[audio_buffer_indice++]=left;
            audio_buffer[audio_buffer_indice++]=right;
        }
    }
    else {
        memset(audio_buffer,0,AUDIO_BUFFER_SIZE*2);
    }

    audiolibretro_send_frame(audio_buffer);
    audio_buffer_indice=0;
}
