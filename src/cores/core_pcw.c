/* ZEsarPCW: modified by retrodiv <retrodiv@proton.me>; updated 2026-09-09.
 * Port modifications: Copyright (c) 2026 retrodiv <retrodiv@proton.me>; GNU GPL v3.
 * Integrate frame pacing, input, loader progress and interrupt/FDC fixes.
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

#include "pcw_boot.h"
#include <stdlib.h>
#include <stdio.h>

#include <time.h>
#include <sys/time.h>
#include <errno.h>


#include "core_pcw.h"
#include "cpu.h"
#include "audio.h"
#include "libretro/pcw_screen_api.h"
#include "ay38912.h"
#include "operaciones.h"
#include "contend.h"

#include "pcw.h"

#include "pd765.h"


z80_byte byte_leido_core_pcw;

void core_pcw_final_frame(void)
{
    t_scanline=0;
    set_t_scanline_draw_zero();

    //TODO: controlar si t_scanline_draw se va "por debajo" del borde inferior
    //tampoco deberia pasar nada porque al hacer render rainbow ya se controla que sea superior y en ese caso no renderiza nada
    //printf ("End video frame en pcw_scanline_counter: %d t: %d scanline_draw: %d\n",pcw_scanline_counter,t_estados,t_scanline_draw);


    //Aqui no se deberia resetear, solo cuando hay vsync, pero algo hay erroneo en mi codigo que si no pongo esto,
    //hay "parpadeos" de cambio de modo en prince of persia, ianna (en menus), dizzy 5 no va, etc
    //if (pcw_endframe_workaround.v) {
    //    t_scanline_draw=0;
    //}



    //Parche para maquinas que no generan 312 lineas, porque si enviamos menos sonido se escuchara un click al final
    //Es necesario que cada frame de pantalla contenga 312 bytes de sonido
    //Igualmente en la rutina de envio_audio se vuelve a comprobar que todo el sonido a enviar
    //este completo; esto es necesario para Z88


    int linea_estados=t_estados/screen_testados_linea;

    while (linea_estados<312) {
        audio_send_mono_sample(0);
        linea_estados++;
    }

    //Frame-lock the audio: flush exactly one emulated frame's worth of samples
    //here (this frame now holds its full 312 samples). Upstream drives envio_audio
    //from the wall-clock timer thread, which drifts against the frontend-paced
    //emulated frame and makes the sound run slow / desync. The timer-thread calls
    //are disabled under COMPILE_LIBRETRO (see timer.c) so this is the only caller.
    envio_audio();

    //Frame-lock the keyboard the same way: the live key-matrix refresh, the PCW
    //keyboard ticker (auto-type pacing + the &3FFF transmit heartbeat) and the
    //real-joystick poll, once per EMULATED frame. Upstream runs these off the
    //wall-clock 1/50 interrupt; on a frontend running faster than real time
    //(fast-forward, headless) that fires only every Nth emulated frame, starving
    //the ticker -- every auto-typed key stays held across those N frames and the
    //CP/M autolaunch command comes out garbled by key repeats. Frame-locked, the
    //keyboard behaves identically at any host speed.
    pcw_keyboard_ticker_update();

    t_estados -=screen_testados_total;


    cpu_loop_refresca_pantalla();

    //libretro owns frame timing: the frontend paces retro_run() to 50 fps. The
    //emulator must NOT also wait for real-time here, or the two throttles stack
    //and graphics + sound run below real speed. Never wait -- emulate one frame
    //flat-out and let retro_run() return to the frontend.
}


void core_pcw_end_scanline_stuff(void)
{


    //audio_valor_enviar_sonido=0;

    int8_t ay_left;
    int8_t ay_right;

    da_output_ay_izquierdo_derecho(&ay_left,&ay_right);
    audio_valor_enviar_sonido_izquierdo=ay_left;
    audio_valor_enviar_sonido_derecho=ay_right;

    //audio_valor_enviar_sonido +=da_output_ay();

    //TODO real beeper
    if (beeper_enabled.v) {
        //if (beeper_real_enabled==0) {
            audio_valor_enviar_sonido_izquierdo += value_beeper;
            audio_valor_enviar_sonido_derecho += value_beeper;
        //}

        /*else {
            char suma_beeper=get_value_beeper_sum_array();
            audio_valor_enviar_sonido_izquierdo += suma_beeper;
            audio_valor_enviar_sonido_derecho += suma_beeper;
            beeper_new_line();
        }*/


    }

    audio_send_stereo_sample(audio_valor_enviar_sonido_izquierdo,audio_valor_enviar_sonido_derecho);

    ay_chip_siguiente_ciclo();


    //printf("Llega Info %d t: %d pcw_crtc_contador_scanline %d t_scanline_draw %d\n",
    //    pcw_scanline_counter,t_estados,pcw_crtc_contador_scanline,t_scanline_draw);

    //final de linea
    //copiamos contenido linea y border a buffer rainbow

    /*
    if (rainbow_enabled.v==1) {
        //printf ("render core scanline draw: %d\n",t_scanline_draw);
        screen_store_scanline_rainbow_pcw_border_and_display();
    }
    */

    t_scanline_next_line();

    pcw_scanline_counter++;

    //pcw_handle_vsync_state();






    //pcw genera interrupciones a 300 hz
    //Esto supone lanzar 6  (50*6=300) interrupciones en cada frame
    //al final de un frame ya va una interrupcion
    //generar otras 5
    //tenemos unas 300 scanlines en cada pantalla
    //generamos otras 5 interrupciones en cada scanline: 50,100,150,200,250

    //Esto tiene que ir antes de pcw_handle_vsync_state
    //pcw_scanline_counter++;

    //printf ("crtc counter: %d t: %d scanline_draw: %d\n",pcw_scanline_counter,t_estados,t_scanline_draw);



    //Con ay player, interrupciones a 50 Hz


    if (pcw_scanline_counter>=52) {
        pcw_pending_interrupt.v=1;

        //printf ("Llega interrupcion crtc del Z80 en counter: %d pcw_crtc_contador_scanline: %d t: %d scanline_draw: %d\n",
        //pcw_scanline_counter,pcw_crtc_contador_scanline,t_estados,t_scanline_draw);


        if (iff1.v==1) {
            //printf ("Llega interrupcion crtc con interrupciones habilitadas del Z80 en counter: %d t: %d t_scanline_draw %d\n",pcw_scanline_counter,t_estados,t_scanline_draw);

        }

        else {
            //printf ("Llega interrupcion crtc con interrupciones DESHABILITADAS del Z80 en counter: %d t: %d\n",pcw_scanline_counter,t_estados);
        }
        pcw_scanline_counter=0;

        pcw_increment_interrupt_counter();
    }


/*
    //Ver si resetear t_scanline_draw
    int final_pantalla=pcw_get_crtc_final_display_zone();
    //printf("final pantalla: %d\n",final_pantalla);
    if (t_scanline_draw>=final_pantalla) {
        //printf("reseteando t_scanline_draw en %d\n",t_scanline_draw);
        set_t_scanline_draw_zero();
        //pcw_crtc_contador_scanline=0;
    }
*/

    //se supone que hemos ejecutado todas las instrucciones posibles de toda la pantalla. refrescar pantalla y
    //esperar para ver si se ha generado una interrupcion 1/50

    if (t_estados>=screen_testados_total) {
        core_pcw_final_frame();
    }

    //Fin final de frame



}



void core_pcw_handle_interrupts(void)
{

    z80_adjust_flags_interrupt_block_opcode();



    //if (interrupts.v==1) {   //esto ya no se mira. si se ha producido interrupcion es porque estaba en ei o es una NMI
    //ver si esta en HALT
    if (z80_halt_signal.v) {
        z80_halt_signal.v=0;
        //reg_pc++;
    }



    if (interrupcion_non_maskable_generada.v) {
        //printf ("generada nmi\n");
        interrupcion_non_maskable_generada.v=0;


        //NMI wait 14 estados
        t_estados += 14;



        push_valor(reg_pc,PUSH_VALUE_TYPE_NON_MASKABLE_INTERRUPT);


        reg_r++;
        iff1.v=0;
        //printf ("Calling NMI with pc=0x%x\n",reg_pc);

        //Otros 6 estados
        t_estados += 6;

        //Total NMI: NMI WAIT 14 estados + NMI CALL 12 estados
        reg_pc= 0x66;

        //temp

        t_estados -=15;



    }



    /* The pending latch can become asserted in the same scheduler turn as an
       NMI or a guest DI. Re-check IFF at the actual acceptance point: a Z80
       must never enter an ordinary interrupt after IFF1 has been cleared.
       Keep the PCW line pending so a later EI still observes it. */
    if (interrupcion_maskable_generada.v && !iff1.v) {
        interrupcion_maskable_generada.v=0;
        pcw_pending_interrupt.v=1;
    }

    //justo despues de EI no debe generar interrupcion
    //e interrupcion nmi tiene prioridad
    if (interrupcion_maskable_generada.v && iff1.v && byte_leido_core_pcw!=251) {
        //Tratar interrupciones maskable
        interrupcion_maskable_generada.v=0;



        push_valor(reg_pc,PUSH_VALUE_TYPE_MASKABLE_INTERRUPT);

        reg_r++;



        //desactivar interrupciones al generar una
        iff1.v=iff2.v=0;


        if (im_mode==0 || im_mode==1) {
            cpu_common_jump_im01();
        }
        else {
        //IM 2.

            z80_int temp_i;
            z80_byte dir_l,dir_h;
            temp_i=get_im2_interrupt_vector();
            dir_l=peek_byte(temp_i++);
            dir_h=peek_byte(temp_i);
            reg_pc=value_8_to_16(dir_h,dir_l);
            t_estados += 7;


        }

    }



}



//bucle principal de ejecucion de la cpu de pcw
void cpu_core_loop_pcw(void)
{





        //Eventos de la controladora de disco
        pd765_next_event_from_core();

        //Eventos de boot disco, volver a disco anterior cuando se haya iniciado CP/M
        pcw_handle_end_boot_disk();

        pcw_boot_check_dsk_not_bootable();

#ifdef DEBUG_SECOND_TRAP_STDOUT

    //Para poder debugar rutina que imprima texto. Util para aventuras conversacionales
    //hay que definir este DEBUG_SECOND_TRAP_STDOUT manualmente en compileoptions.h despues de ejecutar el configure

        scr_stdout_debug_print_char_routine();

#endif


        if (!pcw_boot_step()) {
        //PCW disc-loader forward-progress guard.
        //Some PCW disc loaders contain a self-modifying routine that gates on the exact
        //cycle timing of the uPD765 floppy controller. The emulated controller is timed
        //deterministically and need not reproduce the original drive's exact phase, so on a
        //cold start that routine can take a path that neither returns nor advances: the
        //loader ends up looping/overwriting low RAM and never reaches the program entry
        //point, and the disc never finishes loading (the CPU makes no forward progress).
        //This guard recognises the exact instruction sequence that arms and invokes such a
        //non-terminating timing-check call, and lets execution continue past it. The routine's
        //payload has already been transferred by the preceding loader call, so advancing past
        //only the timing call leaves the loaded data intact and lets the load complete. It is
        //gated on an exact byte-pattern match at this PC, so it triggers only for this specific
        //code shape and is completely inert for everything else.
        {
            if (reg_pc==0xD465){
                if (peek_byte_no_time(0xD45C)==0x3E && peek_byte_no_time(0xD45D)==0x8A &&
                    peek_byte_no_time(0xD45E)==0x32 && peek_byte_no_time(0xD45F)==0x9C &&
                    peek_byte_no_time(0xD460)==0xF0 && peek_byte_no_time(0xD461)==0xAF &&
                    peek_byte_no_time(0xD462)==0x32 && peek_byte_no_time(0xD465)==0xCD &&
                    peek_byte_no_time(0xD466)==0x00 && peek_byte_no_time(0xD467)==0x00){
                    reg_pc=0xD468;
                }
            }
        }
        contend_read( reg_pc, 4 );
        byte_leido_core_pcw=fetch_opcode();



        //Si la cpu está detenida por señal HALT, reemplazar opcode por NOP
        if (z80_halt_signal.v) {
            byte_leido_core_pcw=0;
        }
        else {
            reg_pc++;
        }

        reg_r++;

        z80_no_ejecutado_block_opcodes();
        codsinpr[byte_leido_core_pcw]  () ;
        }
    //A final de cada scanline
    if ( (t_estados/screen_testados_linea)>t_scanline  ) {

        core_pcw_end_scanline_stuff();

    }



    //Si habia interrupcion pendiente  y están las interrupciones habilitadas

    if (pcw_pending_interrupt.v && iff1.v==1) {


        pcw_pending_interrupt.v=0;

        interrupcion_maskable_generada.v=1;


        pcw_scanline_counter=0;

        //printf("Generada interrupcion maskable y atendida en core\n");
        //sleep(2);

    }

    //TODO si habia interrupcion pendiente y no se atiende por estar en DI, la perdemos??
    //if (pcw_pending_interrupt.v) pcw_pending_interrupt.v=0;

    //Interrupcion de cpu. gestion im0/1/2. Esto se hace al final de cada frame en pcw o al cambio de bit6 de R en zx80/81
    if (interrupcion_maskable_generada.v || interrupcion_non_maskable_generada.v) {
        core_pcw_handle_interrupts();

    }
	//Fin gestion interrupciones

}
