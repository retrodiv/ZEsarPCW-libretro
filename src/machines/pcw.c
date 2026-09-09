/* ZEsarPCW: modified by retrodiv <retrodiv@proton.me>; recorded 2026-09-09.
 * Port modifications: Copyright (c) 2026 retrodiv <retrodiv@proton.me>; GNU GPL v3.
 * Integrate video modes, input, status, loader handoff and FDC behaviour.
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

#include <stdio.h>
#include <string.h>

#include "pcw.h"
#include "libretro/openpcw_os_integration.h"
#include "libretro/pcw_screen_api.h"
#include "libretro/pcw_debug.h"
#include "operaciones.h"
#include "audio.h"

#include "dsk.h"
#include "pd765.h"
#include "ay38912.h"
#include "settings.h"
#include "libretro/pcw_keyboard.h"

/* PCW hardware references: John Elliott, PCW Hardware;
 * Richard Fairhurst, Amstrad PCW hardware notes.
 * https://www.seasip.info/Unix/Joyce/hardware.pdf
 * https://www.systemed.net/pcw/hardware.html
 * Upstream also references Amstrad-PCW_MiSTer/boot_loader.sv and:
 * https://www.habisoft.com/pcwwiki/doku.php?id=en:sistema:indice
 * See licenses/COMMENT-SOURCES.md for the scope of the comment replacements.
 */

//Direcciones donde estan cada pagina de ram. 128 paginas de 16 kb cada una
//asignamos 2 MB para tener el máximo de memoria ya disponible
z80_byte *pcw_ram_mem_table[PCW_MAX_RAM_PAGES];





//Direcciones actuales mapeadas para lectura, bloques de 16 kb
z80_byte *pcw_memory_paged_read[4];

//Direcciones actuales mapeadas para lectura, bloques de 16 kb
z80_byte *pcw_memory_paged_write[4];


//Paginas mapeadas para lectura en los 4 segmentos, usado para lectura de teclado y en debug
z80_byte pcw_banks_paged_read[4];

//Contador de linea para lanzar interrupcion.
z80_byte pcw_scanline_counter;

z80_bit pcw_pending_interrupt={0};


z80_byte pcw_keyboard_table[16]={
    255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255
};

z80_byte pcw_interrupt_from_pd765_type=0;

//Some PCW titles are CP/M applications whose on-disc self-boot loader never completes under
//emulation: it polls the floppy controller at $003D (BIT 7,(HL)) for a hardware timing/interrupt
//condition the deterministic emulation does not reproduce, so it spins there forever. For a known
//set of such titles (identified by exact whole-image hash in dsk.c) the core boots CP/M from the
//embedded helper disc, re-inserts the user's disc and types the application's start command -- i.e.
//it loads the title exactly as it would from the CP/M prompt. Per-disc, so inert for any other disc.
int pcw_disc_cpm_autorun=0;            //set per-disc in dsk.c (1 = use the CP/M-boot path)
char pcw_disc_cpm_autorun_cmd[16]="";  //CP/M command to type once at the prompt (e.g. "head")
int pcw_cpm_autorun_pending=0;         //failback fired; waiting for the CP/M boot to finish
int pcw_cpm_autorun_keydelay=0;        //frames (50Hz) left before typing the command
int pcw_cpm_autolaunch_enabled=1;      //user toggle (libretro core option); 0 = leave the disc raw

//Total ram. Para un 8256, 256k. Para un 8512, 512k
int pcw_total_ram=256*1024;


//Determina la paleta del mode1 (la que es como cga)
int pcw_mode1_palette=0;


//
// Inicio de variables necesarias para preservar el estado (o sea las que tienen que ir en un snapshot)
//

//Registros de bancos para F0,F1,F2,F3
z80_byte pcw_bank_registers[4];

//Port F4 If a memory range is set as “locked”, then the “block to read” bits are ignored; memory is read from the “block to write”.
//Bit 7: lock C000..FFFF
//Bit 6: lock 8000..BFFF
//Bit 5: lock 4000..7FFF
//Bit 4: lock 0..3FFF
//Bits 3-0: unused
z80_byte pcw_port_f4_value;

//Address of roller RAM. b7-5: bank (0-7). b4-1: address / 512.
z80_byte pcw_port_f5_value;

//Vertical screen position
z80_byte pcw_port_f6_value;

//b7: reverse video. b6: screen enable.
z80_byte pcw_port_f7_value;

z80_byte pcw_port_f8_value;

//z80_byte pcw_interrupt_counter;

//
// Fin de variables necesarias para preservar el estado (o sea las que tienen que ir en un snapshot)
//

//Cambio de modos de video en los modelos ficticios Plus inventados por habisoft
//Out puerto 80h,0. 81h, modo (0,1,2,3)
//Out puerto 80h,20h+indice. 81h, color R,G,B (24 bits) en esa paleta. incrementando indice a cada escritura (cuando haya escrito los 3 RGB)
z80_bit pcw_allow_videomode_change={1};

z80_byte pcw_last_port_80_value=0;
z80_byte pcw_last_port_81_value=0;
int pcw_last_index_color_change=0;
int pcw_last_index_color_change_component=0;

z80_byte *pcw_get_memory_offset_read(z80_int dir)
{
    int segmento=dir/16384;

    int offset=dir & 16383;

    z80_byte *puntero;

    puntero=pcw_memory_paged_read[segmento];

    puntero +=offset;

    return puntero;
}

z80_byte *pcw_get_memory_offset_write(z80_int dir)
{
    int segmento=dir/16384;

    int offset=dir & 16383;

    z80_byte *puntero;

    puntero=pcw_memory_paged_write[segmento];

    puntero +=offset;

    return puntero;
}

z80_byte pcw_get_mask_bank_ram(void)
{
    //Si pcw_total_ram=256*1024, mascara 15
    //Si pcw_total_ram=512*1024, mascara 31

    int max_banks=pcw_total_ram/16384;

    return max_banks-1;
}

void pcw_set_memory_pages(void)
{


    z80_byte bank;

    int i;

    //z80_byte port_f4_mask=0x10;

    for (i=0;i<4;i++) {

        //Sending the bank number (with b7 set) to one of ports &F0-&F3 selects that bank for reading and writing.
        //Sending the bank number for writing to b0-2 of a port and the bank for reading to b4-b6 (with b7 reset)
        //maps separate banks in for reading and writing: this can only be used for the first 8 banks.

        bank=pcw_bank_registers[i];

        //TODO. algo no he entendido bien....  gonzalezz y otros de opera
        //hacen out F3,3, cuando realmente lo que quieren hacer es paginar en el modo extendido (sin tener el bit 7 alzado)
        //Con eso cargan los de opera, batman y demas...
        //bank |=128;


        if (bank & 128) {
            //PCW (“extended”) paging mode
            bank &=pcw_get_mask_bank_ram();
            //printf("mask %d\n",pcw_get_mask_bank_ram());
            pcw_memory_paged_read[i]=pcw_ram_mem_table[bank];
            pcw_memory_paged_write[i]=pcw_ram_mem_table[bank];

            pcw_banks_paged_read[i]=bank;
        }
        else {
            //CPC (“standard”) paging mode
            //Juegos de Opera Soft, Batman etc usan este modo
            z80_byte bank_write=bank & 7;
            z80_byte bank_read=(bank >> 4) & 7;

            //Port F4: If a memory range is set as “locked”, then the “block to read” bits are ignored; memory is read from the “block to write”.
            /*
                    D7              RW3  (CPU bank C000h to FFFFh)
                    D6              RW0  (CPU bank 0000h to 3FFFh)
                    D5              RW2  (CPU hank B000h to BFFFh)
	                D4		RW1  (CPU bank 4000h to 7FFFh)
            */
            z80_byte port_f4_mask;
            switch(i) {
                case 0:
                    port_f4_mask=0x40;
                break;

                case 1:
                    port_f4_mask=0x10;
                break;

                case 2:
                    port_f4_mask=0x20;
                break;

                default:
                    port_f4_mask=0x80;
                break;
            }
            if (pcw_port_f4_value & port_f4_mask) {
                //printf("memory locked\n");
                //sleep(2);
                bank_read=bank_write;
            }

            pcw_memory_paged_read[i]=pcw_ram_mem_table[bank_read];
            pcw_memory_paged_write[i]=pcw_ram_mem_table[bank_write];

            pcw_banks_paged_read[i]=bank_read;

        }

        //port_f4_mask=port_f4_mask<<1;

    }


}

//Tabla de colores en rgb24, para modos 0, 1, 2, 3
int pcw_rgb24_full_table[PCW_ALL_COLOURS_MODES]={

    //
    // 2 colores para modo 0
    //
    0x000000, //negro
    0x41FF00,  //verde. Tipico color de green "P1" phosphor

    //
    // 16 colores para modo 1, realmente son 4 paletas diferentes, los 4 colores tipicos de CGA
    //

    //Paleta 0 low intensity
    //negro, green, red, brown
    0x000000, //negro
    0x00AA00, //green
    0xAA0000, //red
    0xAA5500, //brown

    //Paleta 0 high intensity
    //negro, light green, light red, yellow
    0x000000, //negro
    0x55FF55, //light green,
    0xFF5555, //light red
    0xFFFF55, //yellow

    //Paleta 1 low intensity
    //negro, cyan, magenta, light gray
    0x000000, //negro
    0x00AAAA, //cyan
    0xAA00AA, //magenta
    0xAAAAAA, //light gray

    //Paleta 1 high intensity
    0x000000, //negro
    0x55FFFF, //light cyan
    0xFF55FF, //light magenta
    0xFFFFFF, //white


    //
    // 16 colores para modo 2, 3
    //
    0x000000,
    0x0000AA,
    0x00AA00,
    0x00AAAA,
    0xAA0000,
    0xAA00AA,
    0xAA5500,
    0xAAAAAA,
    0x555555,
    0x5555FF,
    0x55FF55,
    0x55FFFF,
    0xFF5555,
    0xFF55FF,
    0xFFFF55,
    0xFFFFFF
};


//Retorna rgb15
int pcw_convert_rgb_24_to_rgb_15(int rgb24)
{
    int r8=(rgb24>>16) & 0xFF;
    int g8=(rgb24>>8) & 0xFF;
    int b8=(rgb24   ) & 0xFF;

    int r5=(r8>>3) & 0x1F;
    int g5=(g8>>3) & 0x1F;
    int b5=(b8>>3) & 0x1F;

    int rgb15=(r5<<10) | (g5<<5) | b5;

    //printf("RGB24 %06X = RGB15 %06X  R %02X G %02X B %02X ,  R %02X G %02X B %02X\n",rgb24,rgb15,r8,g8,b8,r5,g5,b5);

    return rgb15;
}

static int pcw_convert_rgb_15_to_rgb_24(int rgb15)
{
    int r=(rgb15>>10) & 0x1F;
    int g=(rgb15>>5) & 0x1F;
    int b=rgb15 & 0x1F;
    return (r<<19) | (g<<11) | (b<<3);
}

static int pcw_convert_rgb_8_to_rgb_24(z80_byte color)
{
    static const z80_byte component[8]={0,36,73,109,146,182,219,255};
    int r=(color>>5)&7;
    int g=(color>>2)&7;
    int b=(color&3)<<1;
    if (b) b|=1;
    return (component[r]<<16) | (component[g]<<8) | component[b];
}

#define PCW_COLORS_IN_MODE0 2
#define PCW_COLORS_IN_MODE1 (4*4)
#define PCW_COLORS_IN_MODE2_3 16

#define PCW_COLOUR_NEW_START_MODE1_RGB15 PCW_COLORS_IN_MODE0
#define PCW_COLOUR_NEW_START_MODE2_RGB15 (PCW_COLOUR_NEW_START_MODE1_RGB15+PCW_COLORS_IN_MODE1)

typedef struct {
    int offset,total_colores;
} pcw_colores_paletas;

pcw_colores_paletas lista_pcw_colores_paletas[3]={
    {0,PCW_COLORS_IN_MODE0},
    {PCW_COLOUR_NEW_START_MODE1_RGB15,PCW_COLORS_IN_MODE1},
    {PCW_COLOUR_NEW_START_MODE2_RGB15,PCW_COLORS_IN_MODE2_3}
};

//La paleta de trabajo es esta, en 15 bits, que al final se obtendran colores de paleta de tsconf de 15 bits
//Cuando el pcw modifica algun color de la paleta, se hace sobre esta paleta de 15 bits
//Son 16 colores, todos compartidos entre todos modos
//Por ejemplo los 2 colores del modo 0 son los mismos 2 colores iniciales del modo 1, PERO,
//habitualmente al cambiar el modo de video (dependiendo del bit 7 del valor) se sobreescriben los colores que la componen llamando a pcw_init_colour_palette_mode,
//y dicha funcion asigna los colores de pcw_rgb_table_15_bits segun pcw_rgb24_full_table, con los colores que tiene de cada modo
int pcw_rgb_table_15_bits[PCW_TOTAL_PALETTE_COLOURS];

//Resetear paleta asociada a un modo concreto
//Modos 2 y 3 comparten misma paleta
void pcw_init_colour_palette_mode(int mode)
{
    if (mode>=4) mode=0;
    if (mode==3) mode=2;

    int colores=lista_pcw_colores_paletas[mode].total_colores;
    int offset_origen=lista_pcw_colores_paletas[mode].offset;

    int i;
    for (i=0;i<colores;i++) {
        int rgb_24=pcw_rgb24_full_table[offset_origen+i];
        int rgb_15=pcw_convert_rgb_24_to_rgb_15(rgb_24);
        pcw_rgb_table_15_bits[i]=rgb_15;
    }
}

//Convertir los colores rgb24 a rgb15
/*
void pcw_init_colour_palette(void)
{

    int i;

    for (i=0;i<PCW_TOTAL_PALETTE_COLOURS;i++) {
        int rgb_24=pcw_rgb_table[i];
        int rgb_15=pcw_convert_rgb_24_to_rgb_15(rgb_24);
        pcw_rgb_table_15_bits[i]=rgb_15;
    }

}
*/


//Retorna el color de 15 bits de un indice a paleta
int pcw_get_color_palette(int index)
{
    return pcw_rgb_table_15_bits[index];
}

//Retorna el color en modo 0
int pcw_get_rgb_color_mode0(int i)
{
    /* libretro "Phosphor" core option: a real PCW monitor is a fixed 1-bit
       on/off phosphor, so emit a fixed colour regardless of the palette the
       program last wrote. This also makes a forced Monochrome mode look right
       over games that natively run in a colour mode (whose palette would
       otherwise bleed through pcw_get_color_palette). 0 green, 1 white,
       2 amber -- amber is this core's addition (ZEsarUX has only green/white).
       Colours go through the same 15-bit -> spectrum_colortable expansion the
       native green path uses. */
    extern int pcw_libretro_phosphor;
    {
        int on_rgb24;
        switch (pcw_libretro_phosphor) {
            case 1:  on_rgb24 = 0xFFFFFF; break; /* white */
            case 2:  on_rgb24 = 0xFFB000; break; /* amber */
            default: on_rgb24 = 0x41FF00; break; /* green (PCW phosphor) */
        }
        return pcw_convert_rgb_15_to_rgb_24(
            pcw_convert_rgb_24_to_rgb_15(i ? on_rgb24 : 0x000000));
    }
}

//Retorna el color en modo 1
int pcw_get_rgb_color_mode1(int i)
{
    int indice_color=i+pcw_mode1_palette*4;
    int color_rgb15=pcw_get_color_palette(indice_color);

    int indice_color_rgb24=pcw_convert_rgb_15_to_rgb_24(color_rgb15);
    //printf("%d %d\n",i,indice_color_rgb24);
    return indice_color_rgb24;
}

//Retorna el color en modo 2 y 3
int pcw_get_rgb_color_mode2_3(int i)
{
    int indice_color=i;
    int color_rgb15=pcw_get_color_palette(indice_color);

    int indice_color_rgb24=pcw_convert_rgb_15_to_rgb_24(color_rgb15);
    //printf("%d %d\n",i,indice_color_rgb24);
    return indice_color_rgb24;
}

/*
libretro native render: each mode writes its real pixel grid 1:1 -- one pixel per
logical pixel, no vertical doubling and no border offset (the frontend scales the
native frame to the PCW monitor's 4:3 aspect). x arrives on the 720-dot scan grid,
so the lower-resolution modes divide it down to their native width: mode 0 = 720,
mode 1 = 360, mode 2 = 180, mode 3 = 360. scr_putpixel writes straight to the
libretro framebuffer (origin 0,0), unlike scr_putpixel_zoom which added the border
margin and the vertical 2x of the old fixed 784x560 frame.
*/

//Putpixel para modo 0 (720x256 nativo)
void pcw_refresca_putpixel_mode0(int x,int y,int color)
{
    int color_final=pcw_get_rgb_color_mode0(color);
    scr_putpixel(x,y,color_final);
}

//Putpixel para modo 1 (360x256 nativo: dos dots por pixel logico)
void pcw_refresca_putpixel_mode1(int x,int y,int color)
{
    int color_final=pcw_get_rgb_color_mode1(color);
    scr_putpixel(x>>1,y,color_final);
}

//Putpixel para modo 2 y 3. Modo 2 = 180x256 (4 dots/pixel), modo 3 = 360x256 (2 dots/pixel)
void pcw_refresca_putpixel_mode2(int x,int y,int color)
{
    int color_final=pcw_get_rgb_color_mode2_3(color);
    int nx = (pcw_video_mode==3) ? (x>>1) : (x>>2);
    scr_putpixel(nx,y,color_final);
}



int pcw_get_palette_colour_indexfinal(int indice_color)
{
    int color_rgb15,color_rgb24;

    color_rgb15=pcw_get_color_palette(indice_color);
    color_rgb24=pcw_convert_rgb_15_to_rgb_24(color_rgb15);
    return color_rgb24;
}

//Retorna rgb24
int pcw_get_palette_colour(int indice_color)
{

    //printf("pcw_get_palette_colour mode %d\n",pcw_video_mode);

    switch (pcw_video_mode) {
        case 0:
            indice_color=indice_color % 2;
            return pcw_get_palette_colour_indexfinal(indice_color);
        break;

        case 1:
        case 2:
        default:
            indice_color=indice_color % 16;
            return pcw_get_palette_colour_indexfinal(indice_color);
        break;

    }
}

/* libretro: set once the running program has programmed a colour-palette entry
   beyond the 2 monochrome (mode-0) colours -- i.e. it provides its own mode-1+
   palette (like Coliseum) rather than relying on the default (like the monochrome
   game Livingstone). The libretro core uses this to decide whether a FORCED colour
   mode (Allow I/O = OFF) should load the default CGA/EGA palette or leave the
   program's. Reset on machine reset (pcw_reset). */
int pcw_libretro_program_set_palette = 0;

void pcw_change_palette_colour_indexfinal(int indice_color,int rgb_color)
{
    int rgb15;

    rgb15=pcw_convert_rgb_24_to_rgb_15(rgb_color);
    pcw_rgb_table_15_bits[indice_color]=rgb15;

    if (indice_color >= 2) pcw_libretro_program_set_palette = 1;


}

void pcw_change_palette_colour(int indice_color,int rgb_color)
{

    //printf("pcw_change_palette_colour mode %d\n",pcw_video_mode);


    switch (pcw_video_mode) {
        case 0:
            indice_color=indice_color % 2;
            pcw_change_palette_colour_indexfinal(indice_color,rgb_color);
        break;

        case 1:
        case 2:
        default:
            indice_color=indice_color % 16;
            pcw_change_palette_colour_indexfinal(indice_color,rgb_color);
        break;

    }

    //printf("indice %d color %d\n",indice_color,rgb15);

}



void pcw_reset(void)
{
    pcw_libretro_program_set_palette = 0;        /* a fresh machine has not set a palette yet */
    //pcw_bank_registers[0]=pcw_bank_registers[1]=pcw_bank_registers[2]=pcw_bank_registers[3]=0;

    pcw_bank_registers[0]=0x80;
    pcw_bank_registers[1]=0x81;
    pcw_bank_registers[2]=0x82;
    pcw_bank_registers[3]=0x83;

    //Importante esto en arranque, si no, no van juegos de Opera por ejemplo
    pcw_port_f4_value=0xF1;

    pcw_port_f5_value=0;
    pcw_port_f6_value=0;
    pcw_port_f7_value=0;
    pcw_port_f8_value=0;

    pcw_interrupt_from_pd765_type=0;

    //pcw_interrupt_counter=0;

    //En reset deberia activar la señal de Terminal Count, pero, tal
    //y como gestionamos esa señal, en que lanzamos una accion al recibirla, y no se mantiene una linea de señal, tal cual,
    //esto aqui no hay que lanzarlo. Ademas al hacer reset de una maquina ya se hace reset de la controladora de disco
    //pd765_set_terminal_count_signal();

    pcw_scanline_counter=0;
    pcw_pending_interrupt.v=0;

    pcw_set_memory_pages();

    pcw_boot_timer=10;

    //Rearm the native sector bootstrap
    rom_load(NULL);

    pcw_video_mode=0;

    //pcw_init_colour_palette();
    pcw_init_colour_palette_mode(pcw_video_mode);
}

void pcw_init_memory_tables(void)
{
	DBG_PRINT_PCW VERBOSE_DEBUG,"Initializing pcw memory tables");

    z80_byte *puntero;
    puntero=memoria_spectrum;


    int i;
    for (i=0;i<PCW_MAX_RAM_PAGES;i++) {
            pcw_ram_mem_table[i]=puntero;
            puntero +=16384;
    }

}

void pcw_out_port_bank(z80_byte puerto_l,z80_byte value)
{
/*

&F0	O	Select bank for &0000
&F1	O	Select bank for &4000
&F2	O	Select bank for &8000
&F3	O	Select bank for &C000. Usually &87.
    */

    int bank=puerto_l-0xF0;



    pcw_bank_registers[bank]=value;

    //printf("PCW set bank %d value %02XH\n",bank,value);

    //if (bank==0) sleep(1);

    pcw_set_memory_pages();


}

void pcw_out_port_f4(z80_byte value)
{
    pcw_port_f4_value=value;
    //printf("PCW set port F4 value %02XH\n",value);

    pcw_set_memory_pages();
}

void pcw_out_port_f5(z80_byte value)
{
    pcw_port_f5_value=value;
    //printf("PCW set port F5 value %02XH\n",value);
}

void pcw_out_port_f6(z80_byte value)
{
    pcw_port_f6_value=value;
    //printf("PCW set port F6 value %02XH\n",value);
}


void pcw_out_port_f7(z80_byte value)
{
    /*if ((pcw_port_f7_value & 128) != (value&128)) {
        printf("Cambio inversion\n");
        sleep(1);
    }*/

    pcw_port_f7_value=value;
    //printf("PCW set port F7 value %02XH\n",value);



}


void pcw_interrupt_from_pd765(void)
{
    if (pcw_interrupt_from_pd765_type==1) {
        //printf("Generate NMI triggered from pd765\n");
        //
        interrupcion_non_maskable_generada.v=1;
        //sleep(2);
        //TODO: se supone que se desactivan todas al recibir una nmi
        /* Upstream describes a one-shot NMI route, but clearing it here
         * remains a TODO. The current implementation leaves the route set. */

        //TODO: No estoy seguro de lo siguiente
        //pcw_interrupt_from_pd765_type=0;
        //sleep(2);

    }

    //TODO Revisar esto. solo se genera cuando esta EI???
    if (pcw_interrupt_from_pd765_type==2) {
        //printf("Generate Maskable interrupt triggered from pd765\n");
        //If the Z80 has disabled interrupts, the interrupt line stays high until the Z80 enables them again.
        pcw_pending_interrupt.v=1;

        //temp
        //pcw_interrupt_from_pd765_type=0;
        //sleep(3);

    }
}

void pcw_out_port_f8(z80_byte value)
{
    //printf("PCW set port F8 value %02XH reg_pc %04XH\n",value,reg_pc);

    /* Port F8 dispatches bootstrap/reset, FDC routing and terminal count,
     * display gating, drive motor and beeper commands; see the cases below. */

    switch (value) {
        case 0:
            DBG_PRINT_PCW VERBOSE_DEBUG,"Out port F8. End bootstrap");
            //sleep(2);
        break;

        case 1:
            DBG_PRINT_PCW VERBOSE_DEBUG,"Out port F8. Reboot");
            //sleep(2);

            reg_pc=0;

            //Rearm the native sector bootstrap
            rom_load(NULL);
        break;

        /* Commands 2/3/4 select NMI, INT or disconnected FDC routing. */
        case 2:
            DBG_PRINT_PCW VERBOSE_PARANOID,"Out port F8. Connect FDC to NMI");
            pcw_interrupt_from_pd765_type=1;
            //sleep(2);
        break;

        case 3:
            DBG_PRINT_PCW VERBOSE_PARANOID,"Out port F8. Connect FDC to standard interrupts");
            pcw_interrupt_from_pd765_type=2;
            //sleep(2);
        break;

        case 4:
            DBG_PRINT_PCW VERBOSE_DEBUG,"Out port F8. Connect FDC to nothing");
            pcw_interrupt_from_pd765_type=0;
            //sleep(2);
        break;


        case 5:
            //printf("Set Terminal count\n");
            pd765_set_terminal_count_signal();
        break;

        case 6:
            //printf("Reset Terminal count\n");
            //Tal y como tenemos implementado el set, esto ya no tiene sentido
            //pd765_reset_terminal_count_signal();
        break;


        case 9:
            dsk_show_activity();
            pd765_motor_on();
        break;

        case 10:
            pd765_motor_off();
            DBG_PRINT_PCW VERBOSE_DEBUG,"Out port F8. Motor off on %04XH",reg_pc);
            //sleep(2);
        break;

        case 11:

            //Trivial pursuit por ejemplo reproduce musica con este beeper
            //printf("set beeper\n");
            value_beeper=100;
        break;

        case 12:

            //printf("reset beeper\n");
            value_beeper=0;
        break;

    }

}

void pcw_increment_interrupt_counter(void)
{
    //printf("scanline: %d F8 port: %02XH\n",t_scanline,pcw_port_f8_value);
    z80_byte pcw_interrupt_counter=pcw_port_f8_value & 0xF;

    if (pcw_interrupt_counter!=0x0F) pcw_interrupt_counter++;

    pcw_port_f8_value &=0xF0;
    pcw_port_f8_value |=pcw_interrupt_counter;

}

z80_byte pcw_get_port_f8_value(void)
{
    z80_byte return_value=pcw_port_f8_value;

    if (pd765_interrupt_pending) {

        //printf("Return F8 FDC interrupt\n");

        //printf("pd765_terminal_count_signal.v: %d\n",pd765_terminal_count_signal.v);
        return_value|=0x20;

        //sleep(1);
    }
    else {
        //printf("Return F8 FDC NO interrupt\n");
    }

    //bit 6 Frame flyback; this is set while the screen is not being drawn
    //TODO: de momento calculo chapucero
    //printf("t_scanline %d\n",t_scanline);
    if (t_scanline_draw<56) return_value |=0x40;

    //bit 4: screen-format status. On a standard 256-line PCW this bit reads as 1
    //(it is cleared only on the rare 200-line screen). Some titles read it to
    //choose their vertical screen position: e.g. Hundra/Coliseum/Phantis do
    //"IN A,(F4): AND 10h" and pick a centred vertical scroll (OUT F6,E0h) when it
    //is set, or a top-aligned one (OUT F6,FCh) when clear. Not reporting it left
    //the colour games' playfield shifted ~30 lines too high vs a real PCW.
    return_value |=0x10;

    return return_value;

}

z80_byte pcw_in_port_f8(void)
{
    //printf("LEE puerto F8H\n");
    return pcw_get_port_f8_value();


}


z80_byte pcw_in_port_f4(void)
{

    //printf("LEE puerto F4\n");

    z80_byte return_value=pcw_get_port_f8_value();

    pcw_port_f8_value &=0xF0;



    return return_value;

}

z80_byte pcw_in_port_fd(void)
{
    //printf("LEE puerto FDH on PC=%04XH\n",reg_pc);
    //Impresora. TODO completar
    return 0x40; //bit 6: If 1, printer has finished.
}

int pcw_keyboard_ticker_update_counter;

//Type the CP/M start command (the command followed by RETURN) for a CP/M-autorun title, once
//CP/M is up and the user's disc has been re-inserted. Uses the standard send-text-as-keystrokes
//player, which presses/releases each key with proper inter-key timing.
void pcw_cpm_send_autorun_keys(void)
{
    /* The generic keystroke spooler also owns the keyboard matrices for every
       other ZEsarUX machine.  Keep the tiny CP/M autorun queue local to PCW. */
    extern void pcw_cpm_begin_autorun_keys(const char *command);
    pcw_cpm_begin_autorun_keys(pcw_disc_cpm_autorun_cmd);
}

static unsigned char pcw_cpm_autorun_keys[16];
static unsigned pcw_cpm_autorun_key_count;
static unsigned pcw_cpm_autorun_key_index;
static unsigned pcw_cpm_autorun_key_delay;
static z80_bit pcw_cpm_autorun_key_down;

void pcw_cpm_begin_autorun_keys(const char *command)
{
    unsigned count = 0;
    while (command[count] && count < sizeof pcw_cpm_autorun_keys - 1) {
        pcw_cpm_autorun_keys[count] = (unsigned char)command[count];
        count++;
    }
    pcw_cpm_autorun_keys[count++] = '\r';
    pcw_cpm_autorun_key_count = count;
    pcw_cpm_autorun_key_index = 0;
    pcw_cpm_autorun_key_delay = 0;
    pcw_cpm_autorun_key_down.v = 0;
}

static void pcw_cpm_advance_autorun_keys(void)
{
    if (pcw_cpm_autorun_key_index >= pcw_cpm_autorun_key_count) return;
    if (++pcw_cpm_autorun_key_delay < 3) return;
    pcw_cpm_autorun_key_delay = 0;
    pcw_keyboard_ascii_event(
        pcw_cpm_autorun_keys[pcw_cpm_autorun_key_index],
        !pcw_cpm_autorun_key_down.v);
    pcw_cpm_autorun_key_down.v ^= 1;
    if (!pcw_cpm_autorun_key_down.v) pcw_cpm_autorun_key_index++;
}

void pcw_keyboard_ticker_update(void)
{
	pcw_keyboard_ticker_update_counter++;

    //CP/M-autorun: once armed (the failback CP/M boot has finished and re-inserted the user's
    //disc), count down the settle delay then type the application's start command.
    if (pcw_cpm_autorun_keydelay>0) {
        pcw_cpm_autorun_keydelay--;
        if (pcw_cpm_autorun_keydelay==0) pcw_cpm_send_autorun_keys();
    }

    //Drive the send-text-as-keystrokes player once per frame. The PCW reads its keyboard from
    //memory (&3FF0-&3FFF, see pcw_read_keyboard), not from an I/O port, so the generic port-read
    //driver in operaciones.c never advances the player for the PCW. Also advance the pacing here
    //(set pending_next / toggle the press-release phase every 'delay' frames): the 20 ms tick that
    //normally does this is not guaranteed to run in the single-threaded libretro core, and without
    //it the auto-typed command would never move past its first key. Call before the &3FFF ticker
    //update below, because get_key resets the whole key matrix.
    pcw_cpm_advance_autorun_keys();


    //3FFFh bit 6 toggles with each update from the keyboard to the PCW.
    //3FFFh bit 7 is 1 if the keyboard is currently transmitting its state to the PCW, 0 if it is scanning its keys.
	pcw_keyboard_table[15] &= 0x3F;
    pcw_keyboard_table[15] |=0x40;
    pcw_keyboard_table[15] |=0x80;
	if (pcw_keyboard_ticker_update_counter & 1) pcw_keyboard_table[15] &= (255-0x80);
	if (pcw_keyboard_ticker_update_counter & 2) pcw_keyboard_table[15] &= (255-0x40);
}

z80_byte pcw_read_keyboard(z80_int dir)
{
    /* The keyboard occupies bank 3 offsets 0x3ff0..0x3fff.
     * pcw_keyboard_table supplies the matrix rows; the extra status bytes are
     * assembled below. See Fairhurst's keyboard notes and Elliott's PCW Hardware
     * references at the top of this file for the hardware layout.
     */

    //Quedarnos con ultimo byte de la direccion
    int fila=dir & 0xF;

    //de momento
    z80_byte return_value=255;

    //Teclas al pulsar activan bit


    /* Offset 0x3fff, bit 7 reports the keyboard's transmission phase. */

    //temp
    //if (fila==8) return_value=temp_row_8;
    //if (fila==5) return_value=temp_row_5;


    //si estamos en el menu, no devolver tecla
    return_value=pcw_keyboard_table[fila];
    if (fila==0xD) return_value &=(255-128);

    //if (fila==0xF) sleep(1);

    //3FFDh bit 7 is 0 if LK2 is present, 1 if not.

    if (fila==0xD) return_value &=(255-128);

    //Si hay algun tipo de joystick de pcw habilitado, las teclas de cursor no retornarlas

    //Soporte joystick opqa space. Muchos (la mayoria?) de opera soft



    //printf("PCW return read row %XH value %02XH reg_pc=%04XH\n",fila,return_value,reg_pc);
    //sleep(1);



    return return_value ^ 255;
}

//Puerto joystick kempston
z80_byte pcw_in_port_9f(void)
{
    return 255;
}

//Puerto joystick cascade
z80_byte pcw_in_port_e0(void)
{
    return 255;
}

z80_byte pcw_in_port_dktronics_joystick(void)
{
    return in_port_ay(0xFF);
}






void scr_refresca_pant_pcw_return_line_pointer(z80_byte roller_ram_bank,z80_int roller_ram_offset,
    z80_byte *address_block, z80_int *address)
{
    z80_byte *puntero_roller_ram;

    puntero_roller_ram=pcw_ram_mem_table[roller_ram_bank];
    puntero_roller_ram +=roller_ram_offset;

    z80_int valor=*puntero_roller_ram+256*puntero_roller_ram[1];

    //descomponer en bloque y addres

    *address_block=(valor>>13) & 0x07;

    //15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0
    //block   |  offset /16                 | offset

    *address=(valor & 7) +2 *(valor & 0x1FF8);
}



/* Video modes implemented here:
 * 0: 720x256 monochrome; 1: 360x256 with four colours;
 * 2: 180x256 with sixteen colours; 3: 360x256 with two colours per 8x1 cell.
 * Mode 3 pairs an attribute byte with eight pixel bits. The colour extension
 * uses the palette and port handlers in this file.
 */


int pcw_video_mode=0;


/*
libretro: the NATIVE pixel size of the active PCW video mode. The core renders and
reports this exact size each frame (the frontend scales it to the monitor's real
4:3 aspect); it never pads into a larger fixed buffer. Width follows the colour
mode -- mode 0 is the full 720-dot 1bpp grid; the colour modes pack 2 or 4 dots per
logical pixel, so their real widths are 360/180/360. Height is always 256 lines
(the old 512 was a display-only vertical doubling).
*/
int pcw_get_native_width(void)
{
    switch (pcw_video_mode) {
        case 1: return 360;
        case 2: return 180;
        case 3: return 360;
        default: return 720;
    }
}
int pcw_get_native_height(void) { return 256; }

//Refresco sin rainbow
void scr_refresca_pantalla_pcw(void)
{


    //Address of roller RAM. b7-5: bank (0-7). b4-1: address / 512.
    z80_byte roller_ram_bank=(pcw_port_f5_value >> 5) & 0x07;

    z80_int roller_ram_offset=(pcw_port_f5_value & 0x1F) * 512;

    //printf("Roller ram: bank: %02XH Offset: %02XH\n",roller_ram_bank,roller_ram_offset);


    int x,y,scanline;

    for (y=0;y<256;y+=8) {
        z80_byte address_block;
        z80_int address;


        //roller_ram_offset+=2;
        for (x=0;x<720;x+=8) {

            for (scanline=0;scanline<8;scanline++) {
                int yfinal=y+scanline;

                //Tener en cuenta scroll vertical
                // puerto F6
                //Ejemplo de juego que usa scroll vertical: skywar.dsk

                z80_byte linea_scroll=pcw_port_f6_value;

                int linea_roller_bank_elegir=(yfinal+linea_scroll) % 256;

                int offset_a_linea_roller_bank=linea_roller_bank_elegir*2; //*2 porque hay punteros de 16 bits


                scr_refresca_pant_pcw_return_line_pointer(roller_ram_bank,roller_ram_offset+offset_a_linea_roller_bank,&address_block,&address);

                address +=x;


                z80_byte *puntero_byte=pcw_ram_mem_table[address_block]+address;

                z80_byte byte_leido=*puntero_byte;

                //Reverse video
                //Ejemplo de juego que usa reverse video: skywar.dsk
                if (pcw_port_f7_value & 0x80) {
                    byte_leido ^=255;
                }

                //Si pantalla no activa
                if (!(pcw_port_f7_value & 0x40)) byte_leido=0;

                int bit;
                int pixel_color;


                //Modo 0 (720x256x2), el nativo del PCW: negro y verde, o Negro y blanco, a elegir vía puente.
                if (pcw_video_mode==0) {
                    for (bit=0;bit<8;bit++) {

                        if (byte_leido & 128) pixel_color=1;
                        else pixel_color=0;

                        pcw_refresca_putpixel_mode0(x+bit,y+scanline,pixel_color);

                        byte_leido=byte_leido<<1;
                    }
                }

                //Modo 1 (360x256x4): los colores de la paleta 1 con intensidad del modo de 4 colores de la CGA
                if (pcw_video_mode==1) {
                    for (bit=0;bit<8;bit+=2) {

                        pixel_color=(byte_leido>>6) & 3;

                        //Resolucion efectiva a mitad. en pantalla seguira siendo 720, solo que dos pixeles repetidos
                        pcw_refresca_putpixel_mode1(x+bit,y+scanline,pixel_color);
                        pcw_refresca_putpixel_mode1(x+bit+1,y+scanline,pixel_color);

                        byte_leido=byte_leido<<2;
                    }
                }

                //Modo 2 (180x256x16): los colores de la paleta de la CGA (los 16 clásicos que trae por defecto la EGA).
                if (pcw_video_mode==2) {
                    for (bit=0;bit<8;bit+=4) {

                        pixel_color=(byte_leido>>4) & 0xF;

                        //Resolucion efectiva a 1/4. en pantalla seguira siendo 720, solo que cuatro pixeles repetidos
                        pcw_refresca_putpixel_mode2(x+bit,y+scanline,pixel_color);
                        pcw_refresca_putpixel_mode2(x+bit+1,y+scanline,pixel_color);
                        pcw_refresca_putpixel_mode2(x+bit+2,y+scanline,pixel_color);
                        pcw_refresca_putpixel_mode2(x+bit+3,y+scanline,pixel_color);

                        byte_leido=byte_leido<<4;
                    }
                }

                /* In mode 3, each attribute/pixel pair describes an 8x1 cell:
                 * two four-bit palette indices followed by eight selectors.
                 * The resulting raster is 360x256. */
                if (pcw_video_mode==3) {

                    //Dado que nuestro bucle con x salta de 8 en 8 hasta 720, y este modo es de 360,
                    //tenemos que pillar cada x=0,x=16,etc
                    if ((x%16)==0) {
                        //Primer byte contiene los atributos
                        int papel=(byte_leido>>4)&15;
                        int tinta=byte_leido & 15;


                        //Obtener el siguiente byte, que contiene los pixeles
                        address +=8;

                        puntero_byte=pcw_ram_mem_table[address_block]+address;

                        byte_leido=*puntero_byte;


                        for (bit=0;bit<8;bit++) {

                            if (byte_leido & 128) pixel_color=tinta;
                            else pixel_color=papel;

                            //Doble de ancho
                            pcw_refresca_putpixel_mode2(x+bit*2,y+scanline,pixel_color);
                            pcw_refresca_putpixel_mode2(x+bit*2+1,y+scanline,pixel_color);

                            byte_leido=byte_leido<<1;
                        }
                    }



                }
            }
        }
    }
}

void scr_refresca_pantalla_y_border_pcw_no_rainbow(void)
{
    /*
    libretro native render: the core delivers only the active display at its real
    per-mode resolution, so the PCW border/overscan is not drawn (the frontend
    supplies any surround). Drawing it here would land inside the native frame
    anyway, now that the active area starts at origin 0,0.
    */
    scr_refresca_pantalla_pcw();
}



void scr_refresca_pantalla_y_border_pcw(void)
{

    //TODO : de momento sin rainbow
    scr_refresca_pantalla_y_border_pcw_no_rainbow();

    /*
    if (rainbow_enabled.v) {
        scr_refresca_pantalla_y_border_pcw_rainbow();
    }
    else {
        scr_refresca_pantalla_y_border_pcw_no_rainbow();
    }
    */
}

int pcw_was_booting_disk_enabled=0;
z80_int pcw_was_booting_disk_address=0;
//Nombre anterior que habia antes de insertar disco boot
char dskplusthree_before_boot_file_name[PATH_MAX]="";

//Si antes de insertar habia disco
z80_bit dskplusthree_emulation_before_boot={0};


//Comprueba cuando se ha iniciado del todo el disco de CP/M y reinserta el disco anterior que habia
void pcw_handle_end_boot_disk(void)
{
    if (!pcw_was_booting_disk_enabled) return;

    if (reg_pc==pcw_was_booting_disk_address) {
        DBG_PRINT_PCW VERBOSE_DEBUG,"Reached end of boot");
        pcw_was_booting_disk_enabled=0;


        //Si habia disco insertado antes, reinsertar
        if (dskplusthree_emulation_before_boot.v) {
            DBG_PRINT_PCW VERBOSE_DEBUG,"Reinserting disk before boot: %s",dskplusthree_before_boot_file_name);

            //Desactivamos autoload para que no haga reset
            int antes_noautoload=noautoload.v;
            noautoload.v=1;
            dskplusthree_disable();

    		dsk_insert_disk(dskplusthree_before_boot_file_name);

	    	dskplusthree_enable();

            noautoload.v=antes_noautoload;
        }

        //If this CP/M boot was the autorun failback, the user's disc is back in the drive now;
        //arm the start-command type-in after a short settle delay (let CP/M reach the prompt).
        if (pcw_cpm_autorun_pending) {
            pcw_cpm_autorun_pending=0;
            pcw_cpm_autorun_keydelay=50*4;   //~4 seconds at 50 Hz
        }

    }
}

void pcw_boot_cpm(void)
{
    /* The helper is selected through an internal API, never a magic filename.
       Keep the user's path for the handoff after the native shell is ready. */
    strcpy(dskplusthree_before_boot_file_name, dskplusthree_file_name);
    dskplusthree_emulation_before_boot.v = dskplusthree_emulation.v;
    dskplusthree_disable();
    dskplusthree_enable_openpcw();
    pd765_enable();
    reset_cpu();
    pcw_was_booting_disk_enabled = 1;
    pcw_was_booting_disk_address = OPENPCW_OS_SHELL_READY_PC;
}

/* A new content session cannot inherit an unfinished helper substitution. */
void pcw_boot_new_session(void)
{
    pcw_was_booting_disk_enabled=0;
    pcw_was_booting_disk_address=0;
    dskplusthree_before_boot_file_name[0]=0;
    dskplusthree_emulation_before_boot.v=0;
    pcw_cpm_autorun_pending=0;
    pcw_cpm_autorun_keydelay=0;
    pcw_cpm_autorun_key_count=pcw_cpm_autorun_key_index=0;
    pcw_cpm_autorun_key_delay=0;
    pcw_cpm_autorun_key_down.v=0;
}

/* Native bootstrap reports failure directly; no firmware-PC heuristic. */
void pcw_boot_failed(void)
{
    if (pcw_was_booting_disk_enabled) return;
    if (pcw_disc_cpm_autorun && pcw_cpm_autolaunch_enabled)
        pcw_cpm_autorun_pending=1;
    pcw_boot_cpm();
    pcw_boot_timer=0;
}

//Cuenta segundos desde el boot
int pcw_boot_timer=0;

//Llamado desde el timer cada segundo

//Comprueba que el disco no sea arrancable (cuando llega a ciertas direcciones y ejecuta opcode concreto)
//Y en ese caso puede hacer fallback a CP/M
//Tambien detectar si no hay disco insertado
void pcw_boot_check_dsk_not_bootable(void)
{
    //Si ha pasado ya el tiempo desde el boot, no comprobar mas

    if (pcw_boot_timer==0) return;

    //CP/M-application titles (see note at pcw_disc_cpm_autorun): their on-disc self-boot loader
    //spins forever at $003D (CB 7E = BIT 7,(HL)) waiting for an FDC timing condition the emulation
    //does not reproduce. For the known set (flagged per-disc by hash in dsk.c) take the CP/M-boot
    //path instead: boot CP/M, re-insert the user's disc, and arm the start-command type-in. Gated
    //on the per-disc flag, so it never affects any other disc (including legitimate protected boots).
    if (pcw_disc_cpm_autorun && pcw_cpm_autolaunch_enabled && reg_pc==0x003D &&
        peek_byte_no_time(0x003D)==0xCB && peek_byte_no_time(0x003E)==0x7E) {
        pcw_boot_timer=0;
        pcw_cpm_autorun_pending=1;
        pcw_boot_cpm();
        return;
    }

    //NOTE: $003D is a PROTECTED disk's loader poll (e.g. Batman's $0000 protection
    //routine waits for the FDC there with BIT 7,(HL)); it is NOT a non-bootable
    //indicator. Triggering CP/M fail-back there aborts a legitimate protected boot.
    //The real non-bootable give-up is the F000 loader's poll at $F13B.
    if (reg_pc!=0xF13B && reg_pc!=0xF9) return;

    int autoboot=0;

    //Cuando no hay disco insertado
    //OUT (C),A, cuando hace el recalibrate
    /*if (reg_pc==0xF9 && peek_byte_no_time(reg_pc)==0xED && peek_byte_no_time(reg_pc+1)==0x79) {
        pcw_boot_timer=0;
        printf("Seems you do not have selected any DSK\n");
        autoboot=1;
    }*/

    /*
    3DH BIT 7,(HL)
    CB 7E
    */

   if (peek_byte_no_time(reg_pc)==0xCB && peek_byte_no_time(reg_pc+1)==0x7E) {
        pcw_boot_timer=0;
        DBG_PRINT_PCW VERBOSE_INFO,"Seems you have selected a non bootable disk");

        autoboot=1;
   }

    //Si hay que autoinsertar cpm
    if (autoboot) {
        DBG_PRINT_PCW VERBOSE_INFO,"Autobooting CP/M");
        //CP/M-autorun titles that are plain (non-bootable) CP/M data discs reach A> via this
        //fail-back path (they never stall at $003D). Arm the start-command type-in here too, so
        //the disc runs its OWN .COM at the prompt (same per-disc hash gate as the $003D path).
        if (pcw_disc_cpm_autorun && pcw_cpm_autolaunch_enabled) pcw_cpm_autorun_pending=1;
        //sleep(2);
        pcw_boot_cpm();

        //Y no autodetectar de nuevo si disco no botable,
        //Esto no deberia suceder, pues CP/M es botable,
        //pero si por algo fallase, nos quedariamos en un bucle continuo de reinicios
        pcw_boot_timer=0;
    }

}







void pcw_out_port_video(z80_byte puerto_l,z80_byte value)
{

    /* Port 0x80 selects a function (high nibble) and index (low nibble),
     * resetting the component cursor. Port 0x81 accesses the selected data;
     * successive component writes build a 24-bit palette entry. */

    if (puerto_l==0x80) {
        pcw_last_port_80_value=value;

        if (value>=16) {
            pcw_last_index_color_change=value & 0x0F;
            pcw_last_index_color_change_component=0;

            //printf("Cambio indice a %d component %d\n",pcw_last_index_color_change,pcw_last_index_color_change_component);
        }
    }

    if (puerto_l==0x81) {
        pcw_last_port_81_value=value;

        z80_byte funcion=(pcw_last_port_80_value >>4) & 0xF;


        if (funcion==2) {
            //Cambio color paleta mediante indice a colores

            int indice_a_color;

            indice_a_color=pcw_last_index_color_change;

            //por si acaso
            if (indice_a_color>15) indice_a_color=0;

            int valor_a_cambiar=pcw_convert_rgb_8_to_rgb_24(value);

            //printf("Cambio color paleta con indice %d por RGB=%06X\n",indice_a_color,valor_a_cambiar);

            pcw_change_palette_colour(indice_a_color,valor_a_cambiar);


            pcw_last_index_color_change++;
            if (pcw_last_index_color_change>=16) pcw_last_index_color_change=0;
        }

        else if (funcion==1) {
            //Cambio color paleta mediante RGB


            int indice_a_color;
            int componente;


            indice_a_color=pcw_last_index_color_change;
            componente=pcw_last_index_color_change_component;

            //por si acaso
            if (indice_a_color>15) indice_a_color=0;
            if (componente>2) componente=0;

            //Llegan en orden B,G,R
            //Si componente=0, red. si 1, green, si 2, blue
            int valor_a_cambiar=pcw_get_palette_colour(indice_a_color);
            int rotaciones=componente;
            rotaciones *=8;
            //Si componente=0 (RED), rotaciones=2. si componente=1, rotaciones=8

            int mascara_quitar=0xFF << rotaciones;
            mascara_quitar ^=0xFFFFFF;

            int valor_aplicar=value << rotaciones;

            valor_a_cambiar &=mascara_quitar;
            valor_a_cambiar |=valor_aplicar;

            //printf("Cambio color paleta con rgb %d (component %d) component value=%02X por RGB=%06X\n",indice_a_color,pcw_last_index_color_change_component,value,valor_a_cambiar);

            pcw_change_palette_colour(indice_a_color,valor_a_cambiar);

            pcw_last_index_color_change_component++;

            if (pcw_last_index_color_change_component>=3) {
                pcw_last_index_color_change_component=0;

                pcw_last_index_color_change++;
                if (pcw_last_index_color_change>=16*3) pcw_last_index_color_change=0;
            }
        }

        else if (funcion==0) {
            //Cambio modo
            int modo=value & 0x7F;
            //printf("Cambio a modo %d\n",modo);
            if (modo>=4) modo=0;
            //7 bits indican el numero de modo. Aunque solo hay modos desde 0 a 4. 4 no soportado en ZEsarUX

            //bit 7 de value. Si a 0, se resetea paleta por defecto. A 1, no se resetea a paleta por defecto
            if ((value & 128)==0) {
                //printf("Reset paleta\n");

                pcw_init_colour_palette_mode(modo);
            }


            pcw_video_mode=modo;


        }

        else if (funcion==3) {
            //printf("Cambio de color en border no implementado\n");
        }

    }

}

z80_byte pcw_in_port_video(z80_byte puerto_l)
{
    if (puerto_l==0x80) return pcw_last_port_80_value;
    if (puerto_l==0x81) return pcw_last_port_81_value;

    //Aqui no deberia llegar
    return 255;
}

/* ZEsarPCW state extension, 2026-09-08. Restore the clocks, interrupt
   latches and helper-disk handoff with the controller, not its later state. */
#include "libretro/pcw_state_io.h"

bool pcw_machine_state(struct pcw_state_io *s)
{
    /* Frontend keys stay live across rewind. Only release/reapply the guest's
       autorun key; restoring a raw matrix would desynchronize host key refs. */
    if (s->apply && pcw_cpm_autorun_key_down.v &&
        pcw_cpm_autorun_key_index < pcw_cpm_autorun_key_count)
        pcw_keyboard_ascii_event(pcw_cpm_autorun_keys[pcw_cpm_autorun_key_index], false);
    PCW_STATE_FIELD(s, t_estados, 0, 1000000);
    PCW_STATE_FIELD(s, t_scanline, 0, 1024);
    PCW_STATE_FIELD(s, t_scanline_draw, 0, 1024);
    PCW_STATE_FIELD(s, memptr, 0, 65535);
    PCW_STATE_FIELD(s, iff2.v, 0, 1);
    PCW_STATE_FIELD(s, im_mode, 0, 2);
    PCW_STATE_FIELD(s, interrupcion_maskable_generada.v, 0, 1);
    PCW_STATE_FIELD(s, interrupcion_non_maskable_generada.v, 0, 1);
    PCW_STATE_FIELD(s, z80_wait_signal.v, 0, 1);
    PCW_STATE_FIELD(s, pcw_scanline_counter, 0, 255);
    PCW_STATE_FIELD(s, pcw_pending_interrupt.v, 0, 1);
    PCW_STATE_FIELD(s, pcw_interrupt_from_pd765_type, 0, 2);
    PCW_STATE_FIELD(s, pcw_keyboard_ticker_update_counter, INT32_MIN, INT32_MAX);
    PCW_STATE_FIELD(s, pcw_was_booting_disk_enabled, 0, 1);
    PCW_STATE_FIELD(s, pcw_was_booting_disk_address, 0, 65535);
    PCW_STATE_FIELD(s, dskplusthree_emulation_before_boot.v, 0, 1);
    PCW_STATE_FIELD(s, pcw_boot_timer, 0, 10);
    PCW_STATE_FIELD(s, pcw_disc_cpm_autorun, 0, 1);
    PCW_STATE_FIELD(s, pcw_cpm_autorun_pending, 0, 1);
    PCW_STATE_FIELD(s, pcw_cpm_autorun_keydelay, 0, 200);
    PCW_STATE_FIELD(s, pcw_cpm_autorun_key_count, 0, 16);
    PCW_STATE_FIELD(s, pcw_cpm_autorun_key_index, 0, 16);
    PCW_STATE_FIELD(s, pcw_cpm_autorun_key_delay, 0, 3);
    PCW_STATE_FIELD(s, pcw_cpm_autorun_key_down.v, 0, 1);
    PCW_STATE_FIELD(s, dsk_file_type_extended, 0, 1);
    PCW_STATE_FIELD(s, pcw_last_port_80_value, 0, 255);
    PCW_STATE_FIELD(s, pcw_last_port_81_value, 0, 255);
    PCW_STATE_FIELD(s, pcw_last_index_color_change, 0, 48);
    PCW_STATE_FIELD(s, pcw_last_index_color_change_component, 0, 2);
    int heartbeat = pcw_keyboard_table[15] & 0xc0;
    PCW_STATE_FIELD(s, heartbeat, 0, 0xc0);
    if (s->apply && s->ok)
        pcw_keyboard_table[15] = (pcw_keyboard_table[15] & 0x3f) | (heartbeat & 0xc0);
    pcw_state_bytes(s, pcw_cpm_autorun_keys, sizeof pcw_cpm_autorun_keys);
    /* Commands are fixed-size text, never paths or host pointers. */
    if (s->ok && s->input && (s->size - s->offset < sizeof pcw_disc_cpm_autorun_cmd ||
        !memchr(s->input + s->offset, 0, sizeof pcw_disc_cpm_autorun_cmd)))
        s->ok = false;
    pcw_state_bytes(s, pcw_disc_cpm_autorun_cmd, sizeof pcw_disc_cpm_autorun_cmd);
    if (s->apply && s->ok && pcw_cpm_autorun_key_down.v &&
        pcw_cpm_autorun_key_index < pcw_cpm_autorun_key_count)
        pcw_keyboard_ascii_event(pcw_cpm_autorun_keys[pcw_cpm_autorun_key_index], true);
    return s->ok;
}

void pcw_restore_boot_path(const char *path)
{
    /* Bind the saved handoff to this session's selected content. Save files
       must remain usable when the game directory or host platform changes. */
    if (pcw_was_booting_disk_enabled)
        snprintf(dskplusthree_before_boot_file_name,
                 sizeof dskplusthree_before_boot_file_name, "%s", path);
}
