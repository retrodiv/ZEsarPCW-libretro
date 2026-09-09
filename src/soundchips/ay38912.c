/* ZEsarPCW: modified by retrodiv <retrodiv@proton.me>; recorded 2026-09-07.
 * Port modifications: Copyright (c) 2026 retrodiv <retrodiv@proton.me>; GNU GPL v3.
 * Retain PCW AY emulation and replace imported prose with concise notes.
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
 * AY register layout used by this implementation:
 *   R0..R5: three 12-bit tone periods; R6: 5-bit noise period.
 *   R7: tone/noise gates and I/O direction; R8..R10: channel amplitudes.
 *   R11/R12: envelope period, low/high byte; R13: envelope shape.
 *   R14/R15: I/O registers, subject to the chip variant's available ports.
 * PCW port decoding and the clock constant are supplied by the machine layer.
 * Reference: General Instrument, AY-3-8910/8912 Data Manual (1979),
 * register descriptions. See licenses/COMMENT-SOURCES.md for the reference URL.
 */

#include <stdio.h>
//#include <math.h>


#include "ay38912.h"
#include "audio.h"
#include "libretro/pcw_debug.h"


//Indica si esta presente el chip o no
z80_bit ay_chip_present;


int ay_chip_frequency=FRECUENCIA_PCW_AY;



//Valores de volumen
char volume_table[16]={0,0,1,1,
               1,2,2,3,
               4,6,8,10,
               14,16,20,24};


//Este es bit enviado cuando tanto tono como ruido son 0.... Esto permite speech, como en chase hq
//si lo ponemos a 0, no se oye nada

//una onda oscila 2 veces de signo en su frecuencia
#define TEMP_MULTIPLICADOR 1




//valores de 16 bits con signo
//tempp  short sine_table[FRECUENCIA_CONSTANTE_NORMAL_SONIDO];


//tabla suficientemente grande como para que al aumentar la frecuencia del sonido no se salga de rango
//soportar hasta cpu 1000% (10 veces mayor)
//short sine_table[15600*10];



int total_ay_chips=1;

//Chip de sonido activo (0 o 1)
int ay_chip_selected=0;

//
//Variables que dependen del chip activo
//
//16 BYTES Contenido de los registros del chip de sonido
z80_byte ay_3_8912_registros[MAX_AY_CHIPS][16];

//Ultimo registro seleccionado por el puerto 65533
z80_byte ay_3_8912_registro_sel[MAX_AY_CHIPS];

//frecuencia de canal de envelope
int freq_envelope[MAX_AY_CHIPS];

//contador de canal de ruido .... (FRECUENCIA_CONSTANTE_NORMAL_SONIDO/freq_ruido)
int contador_envelope[MAX_AY_CHIPS];

int ciclo_envolvente[MAX_AY_CHIPS];

int ciclo_envolvente_10_14_signo[MAX_AY_CHIPS];

//frecuencia de cada canal
int freq_tono_A[MAX_AY_CHIPS],freq_tono_B[MAX_AY_CHIPS],freq_tono_C[MAX_AY_CHIPS];

//contador de cada canal... (FRECUENCIA_CONSTANTE_NORMAL_SONIDO/freq_tono)
int contador_tono_A[MAX_AY_CHIPS],contador_tono_B[MAX_AY_CHIPS],contador_tono_C[MAX_AY_CHIPS];

//ultimo valor enviado para cada canal, valores con signo:
short ultimo_valor_tono_A[MAX_AY_CHIPS];
short ultimo_valor_tono_B[MAX_AY_CHIPS];
short ultimo_valor_tono_C[MAX_AY_CHIPS];

char ultimo_valor_envolvente[MAX_AY_CHIPS];

//frecuencia de canal de ruido
int freq_ruido[MAX_AY_CHIPS];

//contador de canal de ruido .... (FRECUENCIA_CONSTANTE_NORMAL_SONIDO/freq_ruido)
int contador_ruido[MAX_AY_CHIPS];

//ultimo valor enviado para canal de ruido. valor con signo:
short ultimo_valor_ruido[MAX_AY_CHIPS];

//valor randomize
z80_int randomize_noise[MAX_AY_CHIPS];

//
//Fin variables que dependen del chip activo
//

//Mascara para bits no usados en registros de chip
z80_byte ay_mascara_registros[16]={
  0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff,
  0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff,
};



//Si hay que autoactivar chip AY en el caso que alguien lo use (lectura o escritura en el puerto AY)

void init_chip_ay(void)
{



    debug_printf (VERBOSE_INFO,"Initializing AY Chip");

    ay_chip_selected=0;


    //resetear valores de cada chip
    int chip;

    for (chip=0;chip<MAX_AY_CHIPS;chip++) {

        //resetear valores de puertos de sonido
        int r;
        for (r=0;r<16;r++) ay_3_8912_registros[chip][r]=255;

        ciclo_envolvente[chip]=0;

        ciclo_envolvente_10_14_signo[chip]=+1;

        //ultimo valor enviado para cada canal, valores con signo:
        ultimo_valor_tono_A[chip]=+32767;
        ultimo_valor_tono_B[chip]=+32767;
        ultimo_valor_tono_C[chip]=+32767;

        ultimo_valor_envolvente[chip]=0;

        ultimo_valor_ruido[chip]=+32767;
    }


    ay_chip_frequency=FRECUENCIA_PCW_AY;

    debug_printf (VERBOSE_INFO,"Setting AY chip frequency to %d HZ",ay_chip_frequency);




    //Onda senoidal. activar -lm en proceso de compilacion
    /*
    flotante sineval,radians;
        for (i=0;i<FRECUENCIA_CONSTANTE_NORMAL_SONIDO;i++) {
            radians=i;
            radians=radians*6.28318530718;

            radians=radians/((float)(FRECUENCIA_CONSTANTE_NORMAL_SONIDO));
            sineval=sin(radians);
            sine_table[i]=32767*sineval;

            debug_printf (VERBOSE_DEBUG,"i=%d radians=%f sine=%f value=%d",i,radians,sineval,sine_table[i]);
        }

    */


}




void ay_randomize(int chip)
{
    /*
    ;Seguimos la misma formula RND del spectrum:
    ;0..1 -> n=(75*(n+1)-1)/65536
    ;0..65535 -> n=65536/(75*(n+1)-1)
    generar_random_noise:
    */

    int resultado;
    int r;

    r=randomize_noise[chip];

    resultado=(75*(r+1)-1);

    randomize_noise[chip]=resultado & 0xFFFF;

    //printf ("randomize_noise: %d\n",randomize_noise);


}



//Generar salida aleatoria
void ay_chip_valor_aleatorio(int chip)
{

    ay_randomize(chip);

    /*
    ;Generar +1 o -1
    ;0..32767 -> +1
    ;32768..65535 -> -1
    */

    if (randomize_noise[chip]<32768) ultimo_valor_ruido[chip]=+32767;
    else ultimo_valor_ruido[chip]=-32767;

    //printf ("Cambio ruido a : %d\n",ultimo_valor_ruido);

}

char da_output_envolvente(int chip)
{
    return ultimo_valor_envolvente[chip];
}

//Devuelve la salida del canal indicado, devuelve valor con signo
int8_t da_output_canal(z80_byte mascara,short ultimo_valor_tono,z80_byte volumen,int chip)
{

    //valor con signo
    int8_t valor8;
    int valor;

/*
 * R7 has active-low tone/noise gates. Select the enabled signed sample;
 * when both are enabled this implementation averages them. With neither
 * enabled, retain a constant level so amplitude writes can produce audio.
 */

    z80_bit tone,noise;

//	printf ("ultimo_valor_tono: %d\n",ultimo_valor_tono);

    tone.v=!(ay_retorna_mixer_register(chip) & mascara & 7);
    noise.v=!(ay_retorna_mixer_register(chip) & mascara & (8+16+32));

    if (tone.v==1 && noise.v==0)  {
        valor=ultimo_valor_tono;
        //ay_player_silence_detection_counter=0;
    }

    else if (tone.v==0 && noise.v==1)  {
                valor=ultimo_valor_ruido[chip];
                //ay_player_silence_detection_counter=0;
        }

    else if (tone.v==1 && noise.v==1)  {
        /* Keep the combined sample within the same range as a single source. */
        //Valor combinado ruido y tono
        //en version 1.0 este /2 no estaba, era un error, por tanto se generaba al final un volumen mayor de lo normal,
        //cosa que podia hacer que el valor final del sonido cambiase de signo,
        //provocando ruido mas alto de lo normal
        valor=(ultimo_valor_ruido[chip]+ultimo_valor_tono)/2;

        //ay_player_silence_detection_counter=0;
        //printf ("tone y noise. valor:%d\n",valor);

        }

    else {
        //Canales desactivados
        //Parece que deberiamos devolver 0, pero no, devuelve 1
        //esto permite generar sintetizacion de voz/sonido, etc en juegos como Chase HQ por ejemplo, Dizzy III
        //valor=1;
        valor=32767;
    }



    if (volumen & 16) {
        //printf ("Hay envolvente activo tipo : %d\n",ay_3_8912_registros[13]&15);
        volumen=da_output_envolvente(chip);
    }

    volumen=volumen & 15; //Evitar valores de volumen fuera de rango que vengan de los registros de volumen

    //if (volumen>15) printf ("  Error volumen >15 : %d\n",volumen);
    valor=valor*volume_table[volumen];
    valor=valor/32767;
    valor8=valor;
    //printf ("valor final tono: %d\n",valor8);

    //if (valor8>24) printf ("valor final tono: %d\n",valor8);
    //if (valor8<-24) printf ("valor final tono: %d\n",valor8);

    return valor8;


}

//Devuelve el sonido de salida del chip de los 3 canales

/*
0=Mono
1=ACB Stereo (Canal A=Izq,Canal C=Centro,Canal B=Der)
2=ABC Stereo (Canal A=Izq,Canal B=Centro,Canal C=Der)
3=BAC Stereo (Canal A=Centro,Canal B=Izquierdo,Canal C=Der)
4=Custom. Depende de variables
ay3_custom_stereo_A
ay3_custom_stereo_B
ay3_custom_stereo_C
En cada una de esas 3, si vale 0=Left. Si 1=Center, Si 2=Right
*/

void da_output_ay_3_canales(int8_t *canal_A,int8_t *canal_B, int8_t *canal_C)
{
    //char valor_enviar_ay=0;
    int valor_enviar_ay_canal_A=0;
    int valor_enviar_ay_canal_B=0;
    int valor_enviar_ay_canal_C=0;
    if (ay_chip_present.v==1) {

        //Hacerlo para cada chip
        int chips=ay_retorna_numero_chips();

        int i;

        for (i=0;i<chips;i++) {

            valor_enviar_ay_canal_A +=da_output_canal(1+8,ultimo_valor_tono_A[i],ay_3_8912_registros[i][8],i);
            valor_enviar_ay_canal_B +=da_output_canal(2+16,ultimo_valor_tono_B[i],ay_3_8912_registros[i][9],i);
            valor_enviar_ay_canal_C +=da_output_canal(4+32,ultimo_valor_tono_C[i],ay_3_8912_registros[i][10],i);


        }

        //Dividir valor restante entre numero de chips
        valor_enviar_ay_canal_A /=chips;
        valor_enviar_ay_canal_B /=chips;
        valor_enviar_ay_canal_C /=chips;
    }


    /*
    if (valor_enviar_ay==0x10) {
        printf ("A %d B %d C %d\n",da_output_canal(1+8,ultimo_valor_tono_A,ay_3_8912_registros[8]),da_output_canal(2+16,ultimo_valor_tono_B,ay_3_8912_registros[9]),da_output_canal(4+32,ultimo_valor_tono_C,ay_3_8912_registros[10]) );
    }
    */

    *canal_A=valor_enviar_ay_canal_A;
    *canal_B=valor_enviar_ay_canal_B;
    *canal_C=valor_enviar_ay_canal_C;
}

void da_output_ay_izquierdo_derecho(int8_t *iz, int8_t *de)
{
    int8_t canal_A,canal_B,canal_C;

    da_output_ay_3_canales(&canal_A,&canal_B,&canal_C);

    int altavoz_izquierdo=0, altavoz_derecho=0;

    //Aplicar modo stereo AY
    //int ay3_stereo_mode=0;
    /*
    0=Mono
    1=ACB Stereo (Canal A=Izq,Canal C=Centro,Canal B=Der)
    2=ABC Stereo (Canal A=Izq,Canal B=Centro,Canal C=Der)
    3=BAC Stereo (Canal A=Centro,Canal B=Izquierdo,Canal C=Der)
    4=CBA Stereo (Canal A=Der,Canal B=Centro, Canal C=Izq)
    5=Custom. Depende de variables
    ay3_custom_stereo_A
    ay3_custom_stereo_B
    ay3_custom_stereo_C
    En cada una de esas 3, si vale 0=Left. Si 1=Center, Si 2=Right
    */
    altavoz_izquierdo=canal_A+canal_B+canal_C;
    altavoz_derecho=altavoz_izquierdo;

    *iz=altavoz_izquierdo;
    *de=altavoz_derecho;

}



char devuelve_volumen_ciclo_envolvente()
{

/*

Formas de envolventes:

0,1,2,3  \___________

4,5,6,7  /-----------

8        \|\|\|\|\|\|

9        \___________

10       \/\/\/\/\/\/

11       \|----------

12       /|/|/|/|/|/|

13       /-----------

14       /\/\/\/\/\/\

15       /|__________

formas 10 y 14, cuando cambian de subida o bajada (abajo del envolvente o arriba) se repite el ultimo valor pico, es decir, en la 10 los volumenes son
15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 15 14 13 12 11 ...

y en la 14 son:
0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0 0 1 2 3 4 5 ...

no hay que hacer un caso especial para esa "repeticion" del valor donde cambia la onda, tal y como esta hecha la funcion,
ese valor ya se repite por si solo

*/
    char volumen;

    //ciclos que han finalizado y van a 0
    if (ciclo_envolvente[ay_chip_selected]==256) {
        return 0;
    }

    //ciclos que han finalizado y van a 1
    if (ciclo_envolvente[ay_chip_selected]==512) {
        return 15;
    }


    z80_byte tipo_envolvente=ay_3_8912_registros[ay_chip_selected][13]&15;
    switch (tipo_envolvente) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 8:
        case 9:
        case 11:
            volumen=15-ciclo_envolvente[ay_chip_selected];
            break;

        case 4:
        case 5:
        case 6:
        case 7:
        case 12:
        case 13:
        case 15:
            volumen=ciclo_envolvente[ay_chip_selected];
            break;

        case 10: /*    \./  \./    El . significa un ciclo adicional repitiendo el 0      */
            if (ciclo_envolvente_10_14_signo[ay_chip_selected]==+1) volumen=15-ciclo_envolvente[ay_chip_selected];
            else volumen=ciclo_envolvente[ay_chip_selected];
            break;
        case 14: /*    /.\  /.\    El . significa un ciclo adicional repitiendo el 15     */
                        if (ciclo_envolvente_10_14_signo[ay_chip_selected]==+1) volumen=ciclo_envolvente[ay_chip_selected];
                        else volumen=15-ciclo_envolvente[ay_chip_selected];
            break;

        default:
        //Aqui no deberia llegar nunca
        volumen=15;
        //debug_printf (VERBOSE_ERR,"Envelope type %d not implemented",tipo_envolvente);
        break;
    }


    //debug_printf (VERBOSE_DEBUG,"Envelope Cycle: %d volume: %d envelope type: %d",ciclo_envolvente,volumen,tipo_envolvente);

    //Siguiente ciclo
    ciclo_envolvente[ay_chip_selected]++;

    if (ciclo_envolvente[ay_chip_selected]==16) {

        switch (tipo_envolvente) {
      //envolventes que se van a 0
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 9:
        case 15:
            //debug_printf (VERBOSE_DEBUG,"End envelope cycle. Going to 0");
            ciclo_envolvente[ay_chip_selected]=256;
        break;

      //envolventes que se van a 1
        case 11:
        case 13:
            //debug_printf (VERBOSE_DEBUG,"End envelope cycle. Going to 1");
            ciclo_envolvente[ay_chip_selected]=512;
        break;


      //envolventes que se repiten
        default:

            //debug_printf (VERBOSE_DEBUG,"End envelope cycle. Going to the beginning");
            ciclo_envolvente[ay_chip_selected]=0;
            if (tipo_envolvente==10 || tipo_envolvente==14) {
                //envolventes en v
                ciclo_envolvente_10_14_signo[ay_chip_selected]=-ciclo_envolvente_10_14_signo[ay_chip_selected];
            }

        break;

      }

    }


    return volumen;
}


//Calcular e invertir , si conviene, salida de cada canal
void ay_chip_siguiente_ciclo_siguiente(int chip)
{

    if (ay_chip_present.v==0) return;

/*
                int r;
                for (r=0;r<15;r++)
                        printf ("R%d=%x ",r,ay_3_8912_registros[r]);
*/

    //actualizamos contadores de frecuencias
    ultimo_valor_tono_A[chip]=contador_tono_A[chip] < FRECUENCIA_CONSTANTE_NORMAL_SONIDO/2 ? 32767 : -32767;
    contador_tono_A[chip] +=freq_tono_A[chip];
    if (contador_tono_A[chip]>=FRECUENCIA_CONSTANTE_NORMAL_SONIDO) {
        contador_tono_A[chip] -=FRECUENCIA_CONSTANTE_NORMAL_SONIDO;
    }

    ultimo_valor_tono_B[chip]=contador_tono_B[chip] < FRECUENCIA_CONSTANTE_NORMAL_SONIDO/2 ? 32767 : -32767;
    contador_tono_B[chip] +=freq_tono_B[chip];
    if (contador_tono_B[chip]>=FRECUENCIA_CONSTANTE_NORMAL_SONIDO) {
        contador_tono_B[chip] -=FRECUENCIA_CONSTANTE_NORMAL_SONIDO;
    }

    ultimo_valor_tono_C[chip]=contador_tono_C[chip] < FRECUENCIA_CONSTANTE_NORMAL_SONIDO/2 ? 32767 : -32767;
    contador_tono_C[chip] +=freq_tono_C[chip];
    if (contador_tono_C[chip]>=FRECUENCIA_CONSTANTE_NORMAL_SONIDO) {
        contador_tono_C[chip] -=FRECUENCIA_CONSTANTE_NORMAL_SONIDO;
    }


    contador_ruido[chip] +=freq_ruido[chip];
    if (contador_ruido[chip]>=FRECUENCIA_CONSTANTE_NORMAL_SONIDO) {
        contador_ruido[chip] -=FRECUENCIA_CONSTANTE_NORMAL_SONIDO;
        ay_chip_valor_aleatorio(chip);
        //printf ("Conmutar ruido\n");
    }


    //Esto se ejecuta aunque desactivemos el envelope por linea de comandos... pero da igual, al final no se escuchara el envelope
    contador_envelope[chip] +=freq_envelope[chip];

    //*10 dado que hemos de ser capaz de generar frecuencias de 0.1
    //*16 dado que hay 16 divisiones en cada pulso de envolvente
    if (contador_envelope[chip]>=FRECUENCIA_CONSTANTE_NORMAL_SONIDO*10/16) {
        contador_envelope[chip] -=FRECUENCIA_CONSTANTE_NORMAL_SONIDO*10/16;
        ultimo_valor_envolvente[chip]=devuelve_volumen_ciclo_envolvente();
    }


}




int ay_retorna_numero_chips(void)
{

    return total_ay_chips;

}

void ay_chip_siguiente_ciclo(void)
{

    int chips=ay_retorna_numero_chips();
    int j;
    for (j=0;j<chips;j++) {
        ay_chip_siguiente_ciclo_siguiente(j);
    }

}


//leer puerto
z80_byte in_port_ay(z80_byte puerto_h)
{

    if (puerto_h==0xFF) {

        int r=ay_3_8912_registro_sel[ay_chip_selected] & 15;
        //evitamos valores fuera de rango

        z80_byte valor_retorno=ay_3_8912_registros[ay_chip_selected][r];
        //Aplicar mascara de bits. Bits no usados en registros AY se ponen a 0
        //printf ("Retornando valor registro chip AY %02XH\n",r);
        valor_retorno &=ay_mascara_registros[r];

        return valor_retorno;
    }

    return 255;
}


//Calcular contadores de incremento
//void ay_establece_frecuencia_tono(z80_byte indice, int *freq_tono, int *contador_tono)
void ay_establece_frecuencia_tono(z80_byte indice, int *freq_tono)
{

    int freq_temp;
    freq_temp=ay_3_8912_registros[ay_chip_selected][indice]+256*(ay_3_8912_registros[ay_chip_selected][indice+1] & 0x0F);
    //printf ("Valor freq_temp : %d\n",freq_temp);

    //controlamos divisiones por cero
    if (!freq_temp) freq_temp++;

    freq_temp=freq_temp*AY_DIVISOR_FRECUENCIA;




    *freq_tono=FRECUENCIA_AY/freq_temp;

    //printf ("Valor freq_tono : %d\n",*freq_tono);

    /* Pruebas notas
    Octava 4. Nota C Valor registros: 424. Freq_tono=261 hz ok
            Nota D Valor registros: 377. Freq_tono=293 hz ok

    Maximo valor en registro=12 bit=4095. 4095*16=65520
    FRECUENCIA_AY=1773400
    1773400/65520=27 Hz

    Minimo valor en registro=0
    1773400/(0*16) infinito

    Si es 1 por ejemplo, 1*16=16
    1773400/16= 110.837 KHz

    */


    //freq_tono realmente tiene frecuencia*2... dice cada cuando se conmuta de signo
    //esto ya no hace falta con la tabla... multiplicador=1
    *freq_tono=(*freq_tono)*TEMP_MULTIPLICADOR;

    if (*freq_tono>FRECUENCIA_CONSTANTE_NORMAL_SONIDO) {
        //debug_printf (VERBOSE_DEBUG,"Frequency tone %d out of range",(*freq_tono)/TEMP_MULTIPLICADOR);
        *freq_tono=FRECUENCIA_CONSTANTE_NORMAL_SONIDO;
    }

    //si la frecuencia del tono es exactamente igual a la del sonido, pasara que siempre el valor de retorno
    //sera el primer valor en el array de sine_table.. seguramente +1 (32767)
    //alteramos un poco el valor
    if ( (*freq_tono)==FRECUENCIA_CONSTANTE_NORMAL_SONIDO) (*freq_tono)=FRECUENCIA_CONSTANTE_NORMAL_SONIDO-10;

    //de logica deberia resetear esto a 0.... pero si lo hago distorsiona el sonido
    //mejor dejar el contador con el valor actual
    //temp
    //*contador_tono=0;

    //printf ("Valor incremento frecuencia canal : %d contador_tono : %d\n",*freq_tono,*contador_tono);


}



void ay_establece_frecuencia_ruido(void)
{

    //Frecuencia ruido
    int freq_temp=ay_3_8912_registros[ay_chip_selected][6] & 31;
    //printf ("Valor registros ruido : %d Hz\n",freq_temp);

    //controlamos divisiones por cero
    if (!freq_temp) freq_temp++;

    freq_temp=freq_temp*AY_DIVISOR_FRECUENCIA;



    freq_ruido[ay_chip_selected]=FRECUENCIA_NOISE/freq_temp;
    //printf ("Frecuencia ruido: %d Hz\n",freq_ruido);

    //freq_ruido realmente tiene frecuencia*2... dice cada cuando se conmuta de signo
    //freq_ruido=freq_ruido*TEMP_MULTIPLICADOR;
    freq_ruido[ay_chip_selected]=freq_ruido[ay_chip_selected]*2;



    if (freq_ruido[ay_chip_selected]>FRECUENCIA_CONSTANTE_NORMAL_SONIDO) {
        //debug_printf (VERBOSE_DEBUG,"Frequency noise %d out of range",freq_ruido[ay_chip_selected]/2);
        freq_ruido[ay_chip_selected]=FRECUENCIA_CONSTANTE_NORMAL_SONIDO;
    }


    //si la frecuencia del ruido es exactamente igual a la del sonido
    //alteramos un poco el valor
    if ( freq_ruido[ay_chip_selected]==FRECUENCIA_CONSTANTE_NORMAL_SONIDO) freq_ruido[ay_chip_selected]=FRECUENCIA_CONSTANTE_NORMAL_SONIDO-10;



    //printf ("Frecuencia ruido final: %d Hz\n",freq_ruido);


}


void ay_establece_frecuencia_envelope(void)
{

    //debug_printf (VERBOSE_DEBUG,"Register Frequency Envelope ay");
    //contador de 16 bits?
    int freq_temp=ay_3_8912_registros[ay_chip_selected][11]+256*(ay_3_8912_registros[ay_chip_selected][12] & 0xFF);
    //debug_printf (VERBOSE_DEBUG,"Register counter envelope: %d",freq_temp);
    //freq_temp=freq_temp*256;
    //printf ("Valor frecuencia envelope : %d Hz\n",freq_temp);

    /* Envolvente: En teoria debe ir desde 0.1 Hz a 6 KHz

    SI X=6927

    Minimo valor(maxima frecuencia)=1.

    X/1= aprox 6000 Hz

    Maximo valor (menor frecuencia)=65536.  65536
    X/65536=0.09

    multiplicamos por 10

    X=69270

    */


    //controlamos divisiones por cero
    if (!freq_temp) freq_temp++;

    freq_envelope[ay_chip_selected]=FRECUENCIA_ENVELOPE/freq_temp;

    if (freq_envelope[ay_chip_selected]>FRECUENCIA_CONSTANTE_NORMAL_SONIDO) {
            //debug_printf (VERBOSE_DEBUG,"Frequency envelope %d out of range",freq_envelope[ay_chip_selected]);
            freq_envelope[ay_chip_selected]=FRECUENCIA_CONSTANTE_NORMAL_SONIDO;
    }


    //si la frecuencia del envelope es exactamente igual a la del sonido
    //alteramos un poco el valor
    //esto sucede con valores demasiado altos y quedan fijados a FRECUENCIA_CONSTANTE_NORMAL_SONIDO en el trozo de codigo anterior


    //si lo alterasemos solo un poquito, sucede que juegos como el Robocop 2 no se oyen los disparos
    //lo alteramos mas ( *2 / 3)
    if ( freq_envelope[ay_chip_selected]==FRECUENCIA_CONSTANTE_NORMAL_SONIDO) {
        //printf ("temp freq_envelope = FRECUENCIA_CONSTANTE_NORMAL_SONIDO : %d\n",freq_envelope);
        freq_envelope[ay_chip_selected] *=2;
        freq_envelope[ay_chip_selected] /=3;
    }



    //debug_printf (VERBOSE_DEBUG,"Frequency envelope *10 : %d Hz",freq_envelope[ay_chip_selected]);

}


// Emulación y gestión de los dos registros de conexión con el chip AY mediante protocolo serie
// Gracias a Miguel Angel Rodriguez Jodar por este código

// Nota: toda variable usada en esta emulación tiene prefijo aymidi_rs232_
// para distinguirla de la parte que envia a midi, o del exportador a archivos .mid


/*
 * The upstream AY serial/MIDI decoder was credited to Miguel Angel Rodriguez
 * Jodar. That decoder is excluded from the PCW build; the retained AY register
 * handling does not transmit MIDI. See licenses/COMMENT-SOURCES.md.
 */

// estados de mi FSM




// Fin emulación y gestión de los dos registros de conexión con el chip AY mediante protocolo serie






//Enviar valor a puerto
void out_port_ay(z80_int puerto,z80_byte value)
{

    //printf ("Out port ay chip. Puerto: %d Valor: %d\n",puerto,value);

    if (puerto==49149 && (ay_3_8912_registro_sel[ay_chip_selected]==14 || ay_3_8912_registro_sel[ay_chip_selected]==15) ) {
        //printf ("Out midi puerto: %d valor: %d\n",ay_3_8912_registro_sel[ay_chip_selected],value);
        //old_ay3_mid_handle(value);


    }
    //if (puerto==65533 && value>=14) printf("Out seleccion registro valor: %d\n",value);


    if (puerto==65533) {
        //printf("AY chip seleccion registro %d\n",value);
        //Ver si seleccion de chip turbosound o 3 canales AY

        int value_sin_mascara=value & 156; //10011100

        if (total_ay_chips>1 && value_sin_mascara==156) {

            /* Bits 1:0 select the AY: 3 -> first, 2 -> second, 1 -> third.
             * The PCW configuration uses one chip, so this branch is inactive. */


            //if (turbosound_enabled.v &&
            //   (value==255 || value==254 || value==253)
            //)
            //{

            int value_chip=value&3;

            //printf ("ay chip selection: %d\n",value);
            if (value_chip==3) ay_chip_selected=0;
            if (value_chip==2) ay_chip_selected=1;
            if (value_chip==1 && total_ay_chips>2) ay_chip_selected=2;
        }


        else {

            //seleccion de registro
            ay_3_8912_registro_sel[ay_chip_selected]=value & 15; //evitamos valores fuera de rango

        }
    }
    else if (puerto==49149) {
        //valor a registro
        ay_3_8912_registros[ay_chip_selected][ay_3_8912_registro_sel[ay_chip_selected]&15]=value;


        //Nota sobre registro 7 mixer:
        //Bit 6 controla la direccion del registro de I/O - registro R14 - de puerto paralelo
        //como no emulamos puerto paralelo, no nos debe preocupar esto
        //registro R15 en este chip ay3-8912 no se usa para nada


        if (ay_3_8912_registro_sel[ay_chip_selected] ==0 || ay_3_8912_registro_sel[ay_chip_selected] == 1) {
            //Canal A
            ay_establece_frecuencia_tono(0,&freq_tono_A[ay_chip_selected]);
        }

        if (ay_3_8912_registro_sel[ay_chip_selected] ==2 || ay_3_8912_registro_sel[ay_chip_selected] == 3) {
            //Canal B
            ay_establece_frecuencia_tono(2,&freq_tono_B[ay_chip_selected]);
        }


        if (ay_3_8912_registro_sel[ay_chip_selected] ==4 || ay_3_8912_registro_sel[ay_chip_selected] == 5) {
            //Canal C
            ay_establece_frecuencia_tono(4,&freq_tono_C[ay_chip_selected]);
        }

        if (ay_3_8912_registro_sel[ay_chip_selected] ==6) {
            //Frecuencia ruido
            ay_establece_frecuencia_ruido();

        }

        //Envelope
        //Esto se ejecuta aunque desactivemos el envelope por linea de comandos... pero da igual, al final no se escuchara el envelope
        if (ay_3_8912_registro_sel[ay_chip_selected] == 11 || ay_3_8912_registro_sel[ay_chip_selected] == 12) {
            ay_establece_frecuencia_envelope();
        }



        //Envelope
        //Esto se ejecuta aunque desactivemos el envelope por linea de comandos... pero da igual, al final no se escuchara el envelope
        if (ay_3_8912_registro_sel[ay_chip_selected] == 13) {
            //debug_printf (VERBOSE_DEBUG,"Register Envelope Type ay : %d",value);
            //Resetear el ciclo de envolvente
            ciclo_envolvente[ay_chip_selected]=0;
        }



    }
}

//Activa chip ay en el caso que la opcion de autoactivado este habilitada y el chip este desactivado


//Retorna la frecuencia del tono sobre un valor concreto del chip de sonido
//Se le pasa valores fino (8 bits bajos) y alto (8 bits altos)


//Retorna la frecuencia de un registro concreto del chip AY de sonido

/* R7 bits 0..2 disable tone and bits 3..5 disable noise for A, B and C.
 * A set bit closes its gate; the channel amplitudes are held separately.
 */

//A 0 todos para normal

//Retorna el registro del mezclador, pero aplicando filtro de canal activado/no, ruido si/no, tono si/no
//Usado en mid export, direct midi
z80_byte ay_retorna_mixer_register(int chip)
{
    z80_byte valor=ay_3_8912_registros[chip][7];


    return valor;
}



void ay_establece_frecuencias_todos_canales(void)
{

    int chips=ay_retorna_numero_chips();
    int j;
    for (j=0;j<chips;j++) {

        ay_chip_selected=j;

        ay_establece_frecuencia_tono(0,&freq_tono_A[j]);

        ay_establece_frecuencia_tono(2,&freq_tono_B[j]);

        ay_establece_frecuencia_tono(4,&freq_tono_C[j]);

        ay_establece_frecuencia_ruido();

        ay_establece_frecuencia_envelope();
    }

}
