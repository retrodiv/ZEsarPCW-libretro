/* ZEsarPCW: modified by retrodiv <retrodiv@proton.me>; updated 2026-09-09.
 * Port modifications: Copyright (c) 2026 retrodiv <retrodiv@proton.me>; GNU GPL v3.
 * Start the native C bootstrap; retain PCW initialization and concise CPU notes.
 * See licenses/MODIFICATIONS.md for the scope and dating of this port.
 */

/*
    ZEsarUX  ZX Second-Emulator And Released for UniX
    Copyright (C) 2013 Cesar Hernandez Bano

    This file is part of ZEsarUX.

    ZEsarUX is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/* ZEsarPCW modification, 2026-09-07: replace imported documentation with
 * concise implementation notes; retain upstream and research credits.
 */

/*
   CPU related functions
*/


#include <stdlib.h>
#include <stdio.h>
#ifndef MINGW
    #include <unistd.h>
#endif
#include <string.h>

#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include <signal.h>

#ifdef MINGW
    //Para llamar a FreeConsole
    #include <windows.h>
#endif




#include "cpu.h"
#include "libretro/pcw_only_impl.h"
#include "start.h"
#include "libretro/pcw_debug.h"
#include "libretro/pcw_screen_api.h"
#include "ay38912.h"
#include "pd765.h"
#include "pcw.h"
#include "pcw_boot.h"



































//Maquina actual
z80_byte current_machine_type;








//parametro pasado por linea de comandos
//int initial_porcentaje_velocidad_emulador=100;








//T-estados totales del frame
int t_estados=0;


//Scan line actual. Este siempre indica la linea actual dentro del frame total. No alterable por vsync
int t_scanline=0;

//Scan line actual para dibujar en pantalla. En ZX spectrum siempre va adelante. En ZX80/81 lo altera el vsync
int t_scanline_draw=0;







z80_registro registro_hl;

z80_registro registro_de;

z80_registro registro_bc;

z80_byte reg_h_shadow,reg_l_shadow;
z80_byte reg_b_shadow,reg_c_shadow;
z80_byte reg_d_shadow,reg_e_shadow;
z80_byte reg_a_shadow;


z80_byte reg_i;
z80_byte reg_r,reg_r_bit7;


z80_int reg_pc;
z80_byte reg_a;
z80_int reg_sp;
z80_int reg_ix;
z80_int reg_iy;

//Nueva gestion de flags
z80_byte Z80_FLAGS;
z80_byte Z80_FLAGS_SHADOW;

//MEMPTR. Solo se usara si se ha activado en el configure
z80_int memptr;


int z80_ejecutada_instruccion_bloque_ld_cp=0;
int z80_ejecutada_instruccion_bloque_ot_in=0;
z80_byte z80_last_data_transferred_ot_in;


//A 0 si interrupts disabled
//A 1 si interrupts enabled
//z80_bit interrupts;
z80_bit iff1;
/* IFF1 gates maskable interrupts. IFF2 preserves the enable state across
 * NMI entry and supplies P/V for LD A,I and LD A,R; RETN restores IFF1.
 * EI/DI update both latches. See Zilog, Z80 CPU User Manual, CPU Control:
 * https://www.zilog.com/docs/z80/um0080.pdf
 */

z80_bit iff2;

z80_byte im_mode=0;

//border se ha modificado


/* With an interrupt bus byte of 0xff, IM2 reads its vector at (I << 8) | 0xff. */
z80_int get_im2_interrupt_vector(void)
{
    return reg_i*256+255;
}

//Inves. Contador ula delay. mas o menos exagerado
//maximo 1: a cada atributo
//z80_byte inves_ula_delay_factor=3;






//se ha generado interrupcion maskable de la cpu
//en spectrum pasa con el timer
//en zx80/zx81 pasa con el cambio de bit 6 en reg_r
z80_bit interrupcion_maskable_generada;


//se ha generado interrupcion non maskable de la cpu.
//pasa en zx81 cada 64 microsegundos y si el nmi generator esta activo
z80_bit interrupcion_non_maskable_generada;


//se ha generado interrupcion de timer 1/50s


z80_bit z80_halt_signal;

z80_bit z80_wait_signal;

z80_byte *memoria_spectrum;






//Aqui solo se llama posteriormente a haber inicializado la maquina, nunca antes

void z80_no_ejecutado_block_opcodes(void)
{
    z80_ejecutada_instruccion_bloque_ld_cp=0;
    z80_ejecutada_instruccion_bloque_ot_in=0;
}

void z80_adjust_flags_interrupt_block_opcode(void)
{

    //Si estabamos en una instruccion de bloque
    /* Hardware research: David Banks (hoglet67), Undocumented Flags:
     * https://github.com/hoglet67/Z80Decoder/wiki/Undocumented-Flags
     */
    //Esto comun para ld_cp ot_in
    if (z80_ejecutada_instruccion_bloque_ld_cp || z80_ejecutada_instruccion_bloque_ot_in) {
        /* On an interrupted repeat, flags 5/3 use bits 13/11 of the
         * instruction address (the ED prefix), held here in reg_pc. */
        Z80_FLAGS &=(255-FLAG_3-FLAG_5);

        z80_byte high_pc=(reg_pc>>8);
        z80_byte final_35=high_pc & (8+32);
        Z80_FLAGS |=final_35;

    }

    if (z80_ejecutada_instruccion_bloque_ot_in) {
        /* Repeating block I/O also adjusts P/V and H using carry, the
         * transferred byte's sign and the low bits of B. The code below
         * implements the cases described in Banks's research linked above. */


        z80_byte PF,HF;

        PF=(Z80_FLAGS & FLAG_PV ? 1 : 0);
        HF=(Z80_FLAGS & FLAG_H ? 1 : 0);

        Z80_FLAGS &=(255-FLAG_PV-FLAG_H);

        if (Z80_FLAGS & FLAG_C) {
            if (z80_last_data_transferred_ot_in & 0x80) {
                PF = PF ^ util_parity((reg_b - 1) & 0x7) ^ 1;
                HF = (reg_b & 0x0F) == 0x00;
            } else {
                PF = PF ^ util_parity((reg_b + 1) & 0x7) ^ 1;
                HF = (reg_b & 0x0F) == 0x0F;
            }
        } else {
            PF = PF ^ util_parity(reg_b & 0x7) ^ 1;
        }

        if (PF) Z80_FLAGS |=FLAG_PV;
        if (HF) Z80_FLAGS |=FLAG_H;

    }

}

//Establecer la mayoria de registros a valores indefinidos (a 255), que asi se establecen al arrancar una maquina
//al pulsar el boton de reset aqui no se llama
void cold_start_cpu_registers(void)
{
    reg_a=0xff;
    Z80_FLAGS=0xff;
    BC=HL=DE=0xffff;
    reg_ix=reg_iy=reg_sp=0xffff;
    reg_h_shadow=reg_l_shadow=reg_b_shadow=reg_c_shadow=0xff;
    reg_d_shadow=reg_e_shadow=reg_a_shadow=Z80_FLAGS_SHADOW=0xff;
    reg_i=0;
    reg_r=reg_r_bit7=0;
    return;
}


//Para maquinas Z88 y zxuno y prism

void reset_cpu(void)
{

    debug_printf(VERBOSE_INFO,"Reset PCW cpu");

    reg_pc=0;
    reg_i=0;
    reg_a=0xff;
    Z80_FLAGS=0xff;
    reg_sp=0xffff;
    iff1.v=iff2.v=0;
    im_mode=0;
    z80_halt_signal.v=0;
    z80_wait_signal.v=0;
    interrupcion_maskable_generada.v=0;
    interrupcion_non_maskable_generada.v=0;

    pcw_reset();
    t_estados=0;
    t_scanline=0;
    t_scanline_draw=0;
    init_chip_ay();
    if (pd765_enabled.v) pd765_reset();
    return;

}


//Patron de llenado para inves: FF,00,FF,00, etc...






//int maquina_anterior_cambio_cpu_speed=-1;

//ajustar timer de final de cada frame de pantalla. En vez de lanzar un frame cada 20 ms, hacerlo mas o menos rapido

/*
void set_emulator_speed(void)
{
    //ajuste mediante t estados por linea. requiere desactivar realvideo
    //ademas la tabla de memoria contended se saldra de rango...
    screen_testados_linea=screen_testados_linea*porcentaje_velocidad_emulador/100;

    printf ("t estados por linea: %d\n",screen_testados_linea);

}
*/


//ajusta max_cpu_cycles segun porcentaje cpu
//desactivado de momento


//asignar memoria de maquina, liberando memoria antes si conviene
void malloc_machine(int tamanyo)
{
    if (memoria_spectrum!=NULL) {
        debug_printf(VERBOSE_INFO,"Freeing previous Machine memory");
        free(memoria_spectrum);
    }

    debug_printf(VERBOSE_INFO,"Allocating %d bytes for Machine memory",tamanyo);
    memoria_spectrum=malloc(tamanyo);

    if (memoria_spectrum==NULL) {
        cpu_panic ("Error. Cannot allocate Machine memory");
    }


}

void malloc_mem_machine(void) {

    if (!MACHINE_IS_PCW) cpu_panic("PCW libretro core selected a non-PCW machine");

    pcw_total_ram=MACHINE_IS_PCW_8256 ? 256*1024 : 512*1024;
    /* Preserve the upstream two-megabyte backing allocation: its PCW bank
       layout stays stable while the selected model exposes 256 or 512 KiB. */
    malloc_machine(2*1024*1024);
    random_ram(memoria_spectrum,2*1024*1024);
    pcw_init_memory_tables();
    pcw_set_memory_pages();
    return;

}



void set_machine_params(void)
{

    if (!MACHINE_IS_PCW) cpu_panic("PCW libretro core selected a non-PCW machine");

    ay_chip_present.v=1;

    screen_testados_linea=224;

    pd765_enabled.v=0;
    pd765_enable();
    screen_set_video_params_indices();
    return;


}





//Reabrir ventana en caso de que maquina seleccionada sea diferente a la anterior



void post_set_machine_no_rom_load(void)
{

        screen_set_video_params_indices();
        return;



}

void post_set_machine(char *romfile)
{

    //leer rom
    debug_printf (VERBOSE_INFO,"Loading ROM");
    rom_load(romfile);

    post_set_machine_no_rom_load();
}


void set_machine(char *romfile)
{

    //Si estaba divmmc o divide activo, desactivarlos
    //if (divmmc_enabled.v) divmmc_disable();
    //if (divide_enabled.v) divide_disable();

                //Si se cambia de maquina zxuno a otra no zxuno, desactivar divmmc
        /*
                if (last_machine_type!=255) {
                        if (last_machine_type==14 && machine_type!=14 && divmmc_enabled.v) {
                                debug_printf (VERBOSE_INFO,"Disabling divmmc because it was enabled on ZX-Uno");
                                divmmc_disable();
                        }
                }
        */



    set_machine_params();
    malloc_mem_machine();

    //Si hay activo divmmc o divide, reactivar. Se pone aqui porque en caso de zxuno es necesario que esten inicializadas las paginas de memoria
    //if (divmmc_enabled.v) divmmc_enable();
    //if (divide_enabled.v) divide_enable();

    post_set_machine(romfile);
}



//Punto de entrada para error al cargar rom de cualquier modelo de spectrum, dado que se lee longitud de la rom diferente de la esperada
//panic
//void rom_load_cpu_panic(char *romfilename,int leidos)
//{
//	debug_printf(VERBOSE_ERR,"Error loading ROM");
//}


void rom_load(char *romfilename)
{
    (void)romfilename;
    if (!MACHINE_IS_PCW) cpu_panic("PCW libretro core selected a non-PCW ROM");
    pcw_boot_begin();
    return;

}




//char *param_custom_romfile=NULL;





//cuantos botones-joystick a teclas definidas
