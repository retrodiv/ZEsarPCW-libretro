/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>. Distributed under the GNU GPL v3.
*/
/* Explicit embedded-engine bootstrap.  ZEsarUX's start.c is the standalone
   application's command line, driver probing, UI and process lifecycle; none
   of those concepts exist inside a libretro core. */

#include "audio.h"
#include "audiolibretro.h"
#include "ay38912.h"
#include "pcw_debug.h"
#include "operaciones.h"
#include "scrlibretro.h"
#include "settings.h"
#include "core_pcw.h"
#include "pcw_engine.h"
#include "pcw.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void pcw_seed_ay_noise(void)
{
    int chip;
    z80_int seed=(z80_int)time(NULL);
    for (chip=0;chip<ay_retorna_numero_chips();chip++)
        randomize_noise[chip]=(z80_int)(seed+chip*0x9E37u);
}

void random_ram(z80_byte *memory,int length)
{
    /* Upstream's multi-machine helper advanced the AY LFSR for every byte but
       then selected zero for PCW RAM.  Preserve that observable result. */
    if (memory && length>0) memset(memory,0,(size_t)length);
}

/* All reachable CPU panics belong to these three synchronous engine calls.
   No panic returns to an instruction that expects usable memory/registers.
   The jumped-over C frames own no temporary allocations or open files: machine
   RAM and video allocations belong to the lifecycle owner, which cleans them up.
   Audio/video delivery happens after the frame boundary returns. A panic stores
   its message here; only the caller logs it or asks the frontend to shut down. */
static jmp_buf error_boundary;
static char error_message[256];

_Noreturn void cpu_panic(const char *message)
{
    snprintf(error_message, sizeof error_message, "%s", message ? message : "unknown");
    longjmp(error_boundary, 1);
}

const char *pcw_engine_error(void) { return error_message; }

int zesarpcw_libretro_init(int machine_type)
{
    if (setjmp(error_boundary)) return 1;
    pcw_boot_new_session();
    current_machine_type=machine_type;
    noautoload.v=0;
    memoria_spectrum=NULL;
    pcw_seed_ay_noise();
    init_cpu_tables();
    set_machine(NULL);
    cold_start_cpu_registers();
    reset_cpu();

    if (scrlibretro_init()) {
        strcpy(error_message, "Unable to initialize libretro video driver");
        return 1;
    }
    extern int8_t audio_buffer_assigned[];
    audio_buffer=audio_buffer_assigned;
    audio_buffer_indice=0;
    audio_empty_buffer();
    if (audiolibretro_init()) {
        strcpy(error_message, "Unable to initialize libretro audio driver");
        return 1;
    }
    init_chip_ay();

    reg_pc=0;
    interrupcion_maskable_generada.v=0;
    interrupcion_non_maskable_generada.v=0;
    z80_halt_signal.v=0;
    z80_wait_signal.v=0;
    return 0;
}

int pcw_engine_reset(void)
{
    if (setjmp(error_boundary)) return 1;
    reset_cpu();
    return 0;
}

int pcw_engine_run_frame(long *opcodes)
{
    if (setjmp(error_boundary)) return -1;
    *opcodes=0;
    scrlibretro_frame_ready=0;
    while (!scrlibretro_frame_ready && *opcodes<8000000L) {
        cpu_core_loop_pcw();
        (*opcodes)++;
    }
    return scrlibretro_frame_ready ? 1 : 0;
}
