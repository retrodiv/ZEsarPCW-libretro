/* ZEsarPCW: modified by retrodiv <retrodiv@proton.me>; recorded 2026-09-07.
 * Port modifications: Copyright (c) 2026 retrodiv <retrodiv@proton.me>; GNU GPL v3.
 * Retain PCW memory/port operations, diagnostics and concise CPU notes.
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

#include "cpu.h"
#include "operaciones.h"
#include "libretro/pcw_debug.h"
#include "ay38912.h"
#include "contend.h"
#include "pd765.h"
#include "pcw.h"




z80_byte lee_puerto_spectrum_no_time(z80_byte puerto_h,z80_byte puerto_l);

void set_value_beeper (int v);

z80_byte idle_bus_port_atribute(void);

//Tablas para instrucciones adc, sbc y otras
z80_byte overflow_add_table[] = { 0, 0, 0, FLAG_PV, FLAG_PV, 0, 0, 0 };

z80_byte halfcarry_add_table[] ={ 0, FLAG_H, FLAG_H, FLAG_H, 0, 0, 0, FLAG_H };
z80_byte halfcarry_sub_table[] = { 0, 0, FLAG_H, 0, FLAG_H, 0, FLAG_H, FLAG_H };

z80_byte overflow_sub_table[] = { 0, FLAG_PV, 0, 0, 0, 0, FLAG_PV, 0 };

z80_byte parity_table[256];
z80_byte sz53_table[256];
z80_byte sz53p_table[256];







//La mayoria de veces se llama aqui desde set_flags_carry_, dado que casi todas las operaciones que tocan el carry tocan el halfcarry
//hay algunas operaciones, como inc8, que tocan el halfcarry pero no el carry
void set_flags_halfcarry_suma(z80_byte antes,z80_byte result)
{
        antes=antes & 0xF;
        result=result & 0xF;

        if (result<antes) Z80_FLAGS |=FLAG_H;
        else Z80_FLAGS &=(255-FLAG_H);
}

//La mayoria de veces se llama aqui desde set_flags_carry_, dado que casi todas las operaciones que tocan el carry tocan el halfcarry
//hay algunas operaciones, como inc8, que tocan el halfcarry pero no el carry
void set_flags_halfcarry_resta(z80_byte antes,z80_byte result)
{
        antes=antes & 0xF;
        result=result & 0xF;

        if (result>antes) Z80_FLAGS |=FLAG_H;
        else Z80_FLAGS &=(255-FLAG_H);
}


// Signed overflow is the XOR of the carries into and out of the sign bit.
/*
void set_flags_overflow_inc(z80_byte antes,z80_byte result)
{

	if (result==128) Z80_FLAGS |=FLAG_PV;
	else Z80_FLAGS &=(255-FLAG_PV);

}
*/

/*
void set_flags_overflow_dec(z80_byte antes,z80_byte result)
// Signed overflow is the XOR of the carries into and out of the sign bit.
{
	if (result==127) Z80_FLAGS |=FLAG_PV;
	else Z80_FLAGS &=(255-FLAG_PV);
}
*/

void set_flags_overflow_suma(z80_byte antes,z80_byte result)

//Siempre llamar a esta funcion despues de haber actualizado el Flag de Carry, pues lo utiliza

// Signed overflow is the XOR of the carries into and out of the sign bit.
{
	//127+127=254 ->overflow    01111111 01111111 = 11111110    NC   67=y    xor=1
	//-100-100=-200 -> overflow 10011100 10011100 = 00111000     C    67=n   xor=1

	//-2+127=125 -> no overflow 11111110 01111111 = 11111101     C    67=y   xor=0
	//127-2=125 -> no overlow   01111111 11111110 = 11111101     C    67=y   xor=0

	//10-100=-90 -> no overflow 00001010 10011100 = 10100110    NC    67=n   xor=0

	z80_byte overflow67;

        if ( (result & 127) < (antes & 127) ) overflow67=FLAG_C;
        else overflow67=0;

	if ( (Z80_FLAGS & FLAG_C ) ^ overflow67) Z80_FLAGS |=FLAG_PV;
	else Z80_FLAGS &=(255-FLAG_PV);

}

void set_flags_overflow_resta(z80_byte antes,z80_byte result)

//Siempre llamar a esta funcion despues de haber actualizado el Flag de Carry, pues lo utiliza

// Signed overflow is the XOR of the carries into and out of the sign bit.
{
        //127+127=254 ->overflow    01111111 01111111 = 11111110    NC   67=y    xor=1
        //-100-100=-200 -> overflow 10011100 10011100 = 100111000    C    67=n   xor=1

        //-2+127=125 -> no overflow 11111110 01111111 = 11111101     C    67=y   xor=0
        //127-2=125 -> no overlow   01111111 11111110 = 11111101     C    67=y   xor=0

        //10-100=-90 -> no overflow 00001010 10011100 = 10100110    NC    67=n   xor=0

        z80_byte overflow67;


        if ( (result & 127) > (antes & 127) ) overflow67=FLAG_C;
        else overflow67=0;

	if ( (Z80_FLAGS & FLAG_C ) ^ overflow67) Z80_FLAGS |=FLAG_PV;
	else Z80_FLAGS &=(255-FLAG_PV);

}






//activa flags segun instruccion in r,(c)
void set_flags_in_reg(z80_byte value)
{
	Z80_FLAGS=( Z80_FLAGS & FLAG_C) | sz53p_table[value];
}

void set_flags_parity(z80_byte value)
{
	Z80_FLAGS=(Z80_FLAGS & (255-FLAG_PV) ) | parity_table[value];
}



//Parity set if even number of bits set
//Paridad si numero par de bits
z80_byte get_flags_parity(z80_byte value)
{
	z80_byte result;

        result=FLAG_PV;
        z80_byte mascara=1;
        z80_byte bit;

        for (bit=0;bit<8;bit++) {
                if ( (value) & mascara) result = result ^ FLAG_PV;
                mascara = mascara << 1;
        }

	return result;
}



//Inicializar algunas tablas para acelerar cpu
void init_cpu_tables(void)
{
	z80_byte value=0;

	int contador=0;

	debug_printf (VERBOSE_INFO,"Initializing cpu flags tables");

#define BYTETOBINARYPATTERN "%d%d%d%d%d%d%d%d"
#define BYTETOBINARY(byte)  \
  (byte & 0x80 ? 1 : 0), \
  (byte & 0x40 ? 1 : 0), \
  (byte & 0x20 ? 1 : 0), \
  (byte & 0x10 ? 1 : 0), \
  (byte & 0x08 ? 1 : 0), \
  (byte & 0x04 ? 1 : 0), \
  (byte & 0x02 ? 1 : 0), \
  (byte & 0x01 ? 1 : 0)


	//Tabla paridad, sz53
	for (contador=0;contador<256;contador++,value++) {
		parity_table[value]=get_flags_parity(value);
		debug_printf (VERBOSE_PARANOID,"Parity table: value: %3d (" BYTETOBINARYPATTERN ") parity: %d",value,BYTETOBINARY(value),parity_table[value]);


		sz53_table[value]=value & ( FLAG_3|FLAG_5|FLAG_S );

		if (value==0) sz53_table[value] |=FLAG_Z;

		debug_printf (VERBOSE_PARANOID,"SZ53 table: value: %3d (" BYTETOBINARYPATTERN ") flags: (" BYTETOBINARYPATTERN ") ",value,BYTETOBINARY(value),BYTETOBINARY(sz53_table[value])) ;


		sz53p_table[value]=sz53_table[value] | parity_table[value];
		debug_printf (VERBOSE_PARANOID,"SZ53P table: value: %3d (" BYTETOBINARYPATTERN ") flags: (" BYTETOBINARYPATTERN ") ",value,BYTETOBINARY(value),BYTETOBINARY(sz53p_table[value])) ;

	}
}

void neg(void)
{
/*
        z80_byte result,antes;

        antes=0;

        result=antes-reg_a;
        reg_a=result;

        set_flags_carry_resta(antes,result);
        set_flags_overflow_resta(antes,result);
        Z80_FLAGS=(Z80_FLAGS & (255-FLAG_3-FLAG_5-FLAG_Z-FLAG_S)) | FLAG_N | sz53_table[result];

*/

        z80_byte tempneg=reg_a;
        reg_a=0;
	sub_a_reg(tempneg);
}



//Comun al generar interrupcion en im0/1
void cpu_common_jump_im01(void)
{
	//if (im_mode==0 || im_mode==1) {
	reg_pc=56;
	t_estados += 7;


	//Im modo 0 la interrupción es un t-estado más rápido que con Im modo 1, en cpu mostek

}


//Rutinas de cpu core vacias para que, al parsear breakpoints del config file, donde aun no hay inicializada maquina,
//funciones como opcode1=XX , peek(x), etc no peten porque utilizan funciones peek. Inicializar también las de puerto por si acaso
























//Poke en Inves afecta a los 64 kb de RAM









//Quicksilva QS Sound board






/* Legacy RAM mirroring notes: 0x2000 -> 0x2400, 0x2800 -> 0x2c00,
 * and 0x3000/0x3400/0x3800 -> 0x3c00, in 1 KiB blocks.
 * These notes describe a foreign-machine mapping, not PCW bank switching.
 */



















z80_byte fetch_opcode_pcw(void)
{
    /*
        set_visualmemopcodebuffer(reg_pc);
*/
//printf("antes %p\n",peek_byte_no_time);
z80_byte value= peek_byte_no_time (reg_pc);

        //printf("despues %p\n",peek_byte_no_time);
        return value;
}








//Si dir>16383, leemos RAM. Sino, leemos ROM siempre que la ram oculta este oculta










//Funcion usada en la emulacion de refresco de RAM

//Puntero a la funcion original























































































































































































































void poke_byte_no_time_pcw(z80_int dir,z80_byte valor)
{

    z80_byte *puntero;

    puntero=pcw_get_memory_offset_write(dir);

    *puntero=valor;

/*#ifdef EMULATE_VISUALMEM

set_visualmembuffer(dir);

#endif*/

}

void poke_byte_pcw(z80_int dir,z80_byte valor)
{


        //Y sumamos estados normales
        t_estados += 3;


	poke_byte_no_time_pcw(dir,valor);


}

void out_port_pcw_no_time(z80_int puerto,z80_byte value)
{
    z80_byte puerto_l=puerto&0xFF;

    if (puerto_l==0x01) {
        //printf("OUT FDC data register %02XH\n",value);
        pd765_out_port_data_register(value);
    }

    /*

    &F0 O   Select bank for &0000
    &F1 O   Select bank for &4000
    &F2 O   Select bank for &8000
    &F3 O   Select bank for &C000. Usually &87.
    */

    if (puerto_l>=0xF0 && puerto_l<=0xF3) {
        pcw_out_port_bank(puerto_l,value);
    }

    if (puerto_l==0xF4) {
        pcw_out_port_f4(value);
    }

    if (puerto_l==0xF5) {
        pcw_out_port_f5(value);
    }

    if (puerto_l==0xF6) {
        pcw_out_port_f6(value);
    }

    if (puerto_l==0xF7) {
        pcw_out_port_f7(value);
    }

    if (puerto_l==0xF8) {
        pcw_out_port_f8(value);
    }

    //13.1 DKTronics sound generator
    //TODO: Registro 0E para lectura de joystick dk tronics
    //
    /* DKTronics joystick inputs are active-low: fire/up/down/right/left
     * occupy bits 6/5/4/3/2 of AY register 14. Reference: John Elliott,
     * PCW Hardware, DKTronics interface (link in licenses/COMMENT-SOURCES.md).
     */
   /*
   Usado en:
   Abadia del Crimen
   Head Over Heels
   */

    if (puerto_l==0xAA && ay_chip_present.v) out_port_ay(65533,value);
    if (puerto_l==0xAB && ay_chip_present.v) out_port_ay(49149,value);

    if (pcw_allow_videomode_change.v) {
        if (puerto_l==0x80 || puerto_l==0x81) pcw_out_port_video(puerto_l,value);
    }


    if (puerto_l!=0x01 && puerto_l!=0xF4 && puerto_l!=0xf8 && puerto_l!=0xAA && puerto_l!=0xAB && (puerto_l<0xf0 || puerto_l>0xf8)) {
        //printf("Out port UNKNOWN %02XH value %02XH\n",puerto_l,value);
        //sleep(3);
    }

}


void out_port_pcw(z80_int puerto,z80_byte value)
{
  ula_contend_port_early( puerto );
  out_port_pcw_no_time(puerto,value);
  ula_contend_port_late( puerto ); t_estados++;
}



z80_byte lee_puerto_pcw_no_time(z80_byte puerto_h GCC_UNUSED,z80_byte puerto_l)
{
    //printf("LEE puerto %02XH\n",puerto_l);

    if (puerto_l==0x00) {
        //printf("IN FDC status register\n");
        return pd765_read_status_register();
    }

    if (puerto_l==0x01) {
        //printf("IN FDC data register\n");
        return pd765_read();
    }
    if (puerto_l==0xF4) {
        return pcw_in_port_f4();
    }

    if (puerto_l==0xF8) {
        return pcw_in_port_f8();


    }

    if (puerto_l==0xFD) {
        return pcw_in_port_fd();
    }

    if (ay_chip_present.v) {
        if (puerto_l==0xA9) {

            //Registro 14, dktronics joystick
            if ( (ay_3_8912_registro_sel[ay_chip_selected] & 15) == 14) {
                return pcw_in_port_dktronics_joystick();
    		}

            return in_port_ay(0xFF);
        }
    }

    if (puerto_l==0x9f) {
        return pcw_in_port_9f();
    }

    if (puerto_l==0xe0) {
        return pcw_in_port_e0();
    }

    if (pcw_allow_videomode_change.v) {
        if (puerto_l==0x80 || puerto_l==0x81) return pcw_in_port_video(puerto_l);
    }



    //printf ("In Port %x unknown asked, PC after=0x%x\n",puerto_l+256*puerto_h,reg_pc);
    return 255;


}

z80_byte lee_puerto_pcw(z80_byte puerto_h,z80_byte puerto_l)
{
  z80_int port=value_8_to_16(puerto_h,puerto_l);
  ula_contend_port_early( port );
  ula_contend_port_late( port );
  z80_byte valor = lee_puerto_pcw_no_time( puerto_h, puerto_l );

  t_estados++;

  return valor;

}

z80_byte peek_byte_no_time_pcw(z80_int dir)
{
/*
	set_visualmemreadbuffer(dir);
*/
    //Lectura de teclado
    int offset=dir & 16383;
    if (offset>=0x3FF0) {
        int segmento=dir/16384;

        if (pcw_banks_paged_read[segmento]==3) {
            return pcw_read_keyboard(offset);
        }

    }


    z80_byte *puntero;

    puntero=pcw_get_memory_offset_read(dir);

    return *puntero;

}


z80_byte peek_byte_pcw(z80_int dir)
{

        t_estados +=3;

        return peek_byte_no_time_pcw(dir);

}

z80_int peek_word(z80_int dir)
{
        z80_byte h,l;

        l=peek_byte(dir);
        h=peek_byte(dir+1);

        return (h<<8) | l;
}




z80_int lee_word_pc(void)
{
        z80_int valor;

        valor=peek_word(reg_pc);
        reg_pc +=2;
        return valor;

	//este reg_pc+=2 es incorrecto, no funciona
	//return peek_word(reg_pc+=2);
}

void poke_word(z80_int dir,z80_int valor)
{
        poke_byte(dir,valor & 0xFF);
        poke_byte(dir+1, (valor >> 8) & 0xFF );
}



z80_byte peek_byte_desp(z80_int dir,z80_byte desp)
{


//RETURN (dir+desp)

        z80_int desp16,puntero;

        desp16=desp8_to_16(desp);

        puntero=dir + desp16;

        set_memptr(puntero);

        return peek_byte(puntero);

}

void poke_byte_desp(z80_int dir,z80_byte desp,z80_byte valor)
{
        z80_int desp16,puntero;

        desp16=desp8_to_16(desp);

        puntero=dir + desp16;

        set_memptr(puntero);

	poke_byte(puntero,valor);
}





//Esto no es macro porque se usa en sitios como cp(hl) donde se hace cp_reg(peek_byte(hl)) y provocaria error de timing
void cp_reg(z80_byte value)
{

        z80_byte result,antes;

        set_undocumented_flags_bits(value);

        result=reg_a;
        antes=reg_a;

        result -=value;

        set_flags_zero_sign(result);
        set_flags_carry_resta(antes,result);
        set_flags_overflow_resta(antes,result);
        Z80_FLAGS |=FLAG_N;

}






z80_int sbc_16bit(z80_int reg, z80_int value)
{

	z80_int result;
        z80_byte h;
        int result_32bit;

        set_memptr(reg+1);



        result_32bit=reg-value-( Z80_FLAGS & FLAG_C );

        z80_int lookup =      ( (  (reg) & 0x8800 ) >> 11 ) | \
                            ( (  (value) & 0x8800 ) >> 10 ) | \
                            ( ( result_32bit & 0x8800 ) >>  9 );  \


        result=result_32bit & 65535;
        h=value_16_to_8h(result);
        set_undocumented_flags_bits(h);


        if (result_32bit & 0x10000) Z80_FLAGS |=FLAG_C;
        else Z80_FLAGS &=(255-FLAG_C);

        set_flags_zero_sign_16(result);

	Z80_FLAGS=(Z80_FLAGS & (255-FLAG_H-FLAG_PV)) | halfcarry_sub_table[lookup&0x07] | overflow_sub_table[lookup >> 4] | FLAG_N;

	return result;


}





z80_int add_16bit(z80_int reg, z80_int value)
{
        z80_int result,antes;
        z80_byte h;

	set_memptr(reg+1);


        antes=reg;
        result=reg;

        result +=value;

	z80_int lookup = ( (       (reg) & 0x0800 ) >> 11 ) | \
                            ( (  (value) & 0x0800 ) >> 10 ) | \
                            ( (   result & 0x0800 ) >>  9 );  \


        h=value_16_to_8h(result);
        set_undocumented_flags_bits(h);

        set_flags_carry_16_suma(antes,result);


	Z80_FLAGS=(Z80_FLAGS & (255-FLAG_H-FLAG_N)) | halfcarry_add_table[lookup];


        return result;
}

z80_int adc_16bit(z80_int reg, z80_int value)
{

        z80_int result;
        z80_byte h;
	int result_32bit;

	set_memptr(reg+1);


        result_32bit=reg+value+( Z80_FLAGS & FLAG_C );

	z80_int lookup =      ( (  (reg) & 0x8800 ) >> 11 ) | \
                            ( (  (value) & 0x8800 ) >> 10 ) | \
                            ( ( result_32bit & 0x8800 ) >>  9 );  \


	result=result_32bit & 65535;
        h=value_16_to_8h(result);
        set_undocumented_flags_bits(h);


        if (result_32bit & 0x10000) Z80_FLAGS |=FLAG_C;
        else Z80_FLAGS &=(255-FLAG_C);

        set_flags_zero_sign_16(result);

        Z80_FLAGS=(Z80_FLAGS & (255-FLAG_H-FLAG_PV-FLAG_N)) | halfcarry_add_table[lookup&0x07] | overflow_add_table[lookup >> 4];


        return result;


}



z80_int desp8_to_16(z80_byte desp)
{

	z80_int desp16;

        if (desp>127) {
		desp=256-desp;
                desp16=-desp;
	}
        else
                desp16=desp;

	return desp16;

}



z80_int pop_valor()
{
	z80_int valor;

        valor=peek_word(reg_sp);
        reg_sp +=2;
        return valor;

}

//Tener en cuenta los valores de tipo para los strings:
/*enum push_value_type {
	PUSH_VALUE_TYPE_DEFAULT=0,
	PUSH_VALUE_TYPE_CALL,
	PUSH_VALUE_TYPE_RST,
	PUSH_VALUE_TYPE_PUSH,
	PUSH_VALUE_TYPE_MASKABLE_INTERRUPT,
        PUSH_VALUE_TYPE_NON_MASKABLE_INTERRUPT
};
*/




//En la funcion por defecto no usamos el tipo
//El tipo realmente sera un valor en el rango del enum de push_value_type
//lo pongo como z80_byte y no como enum porque luego al activar extended_stack, las funciones de nested
//requieren que ese parametro sea z80_byte
void push_valor_default(z80_int valor,z80_byte tipo GCC_UNUSED)
{
        reg_sp -=2;
        poke_word(reg_sp,valor);
}



z80_byte rlc_valor_comun(z80_byte value)
{
        if (value & 128) Z80_FLAGS |=FLAG_C;
        else Z80_FLAGS &=(255-FLAG_C);

        value=value << 1;
        value |= ( Z80_FLAGS & FLAG_C );

        set_undocumented_flags_bits(value);
	Z80_FLAGS &=(255-FLAG_N-FLAG_H);

        return value;

}

z80_byte rlc_valor(z80_byte value)
{
	value=rlc_valor_comun(value);
	set_flags_zero_sign(value);
	set_flags_parity(value);
	return value;
}


void rlc_reg(z80_byte *reg)
{
        z80_byte value=*reg;

        *reg=rlc_valor(value);

}

void rlca(void)
{
        //RLCA no afecta a S, Z ni P
        reg_a=rlc_valor_comun(reg_a);
}

void ex_af_af(void)
{
    z80_byte valor;

	valor=reg_a;
	reg_a=reg_a_shadow;
	reg_a_shadow=valor;

	valor=Z80_FLAGS;
	Z80_FLAGS=Z80_FLAGS_SHADOW;
	Z80_FLAGS_SHADOW=valor;

}

//rutina comun a rl y rla
z80_byte rl_valor_comun(z80_byte value)
{
        z80_bit flag_C_antes;
        flag_C_antes.v=( Z80_FLAGS & FLAG_C );

        if (value & 128) Z80_FLAGS |=FLAG_C;
        else Z80_FLAGS &=(255-FLAG_C);

        value=value << 1;
        value |= flag_C_antes.v;

        set_undocumented_flags_bits(value);

        Z80_FLAGS &=(255-FLAG_N-FLAG_H);

        return value;

}

z80_byte rl_valor(z80_byte value)
{
	value=rl_valor_comun(value);
	set_flags_zero_sign(value);
	set_flags_parity(value);
        return value;

}

void rl_reg(z80_byte *reg)
{
        z80_byte value=*reg;

        *reg=rl_valor(value);

}


void rla(void)
{
	//RLA no afecta a S, Z ni P
        reg_a=rl_valor_comun(reg_a);
}



z80_byte rr_valor_comun(z80_byte value)
{
        z80_bit flag_C_antes;
        flag_C_antes.v=( Z80_FLAGS & FLAG_C );

        if (value & 1) Z80_FLAGS |=FLAG_C;
        else Z80_FLAGS &=(255-FLAG_C);

        value=value >> 1;
        value |= (flag_C_antes.v*128);

        set_undocumented_flags_bits(value);
	Z80_FLAGS &=(255-FLAG_N-FLAG_H);

        return value;

}

z80_byte rr_valor(z80_byte value)
{
	value=rr_valor_comun(value);
        set_flags_zero_sign(value);
	set_flags_parity(value);
	return value;
}


void rr_reg(z80_byte *reg)
{
        z80_byte value=*reg;

        *reg=rr_valor(value);

}


void rra(void)
{
        //RRA no afecta a S, Z ni P
        reg_a=rr_valor_comun(reg_a);

}

z80_byte rrc_valor_comun(z80_byte value)
{
        if (value & 1) Z80_FLAGS |=FLAG_C;
        else Z80_FLAGS &=(255-FLAG_C);

        value=value >> 1;
        value |= (( Z80_FLAGS & FLAG_C )*128);

        set_undocumented_flags_bits(value);
	Z80_FLAGS &=(255-FLAG_N-FLAG_H);

        return value;

}

z80_byte rrc_valor(z80_byte value)
{
	value=rrc_valor_comun(value);
	set_flags_zero_sign(value);
	set_flags_parity(value);
	return value;
}

void rrc_reg(z80_byte *reg)
{
        z80_byte value=*reg;

        *reg=rrc_valor(value);

}



void rrca(void)
{
        //RRCA no afecta a S, Z ni P
        reg_a=rrc_valor_comun(reg_a);
}





z80_byte sla_valor(z80_byte value)
{

	Z80_FLAGS=0;

        if (value & 128) Z80_FLAGS |=FLAG_C;

        value=value << 1;

	Z80_FLAGS |=sz53p_table[value];

        return value;

}

void sla_reg(z80_byte *reg)
{

        z80_byte value=*reg;

        *reg=sla_valor(value);

}

z80_byte sra_valor(z80_byte value)
{
        z80_byte value7=value & 128;

	Z80_FLAGS=0;

        if (value & 1) Z80_FLAGS |=FLAG_C;

        value=value >> 1;
        value |= value7;

	Z80_FLAGS |=sz53p_table[value];

        return value;

}

void sra_reg(z80_byte *reg)
{
        z80_byte value=*reg;

        *reg=sra_valor(value);

}

z80_byte srl_valor(z80_byte value)
{

	Z80_FLAGS=0;

        if (value & 1) Z80_FLAGS |=FLAG_C;

        value=value >> 1;

	Z80_FLAGS |=sz53p_table[value];

        return value;

}

void srl_reg(z80_byte *reg)
{
        z80_byte value=*reg;

        *reg=srl_valor(value);

}

z80_byte sls_valor(z80_byte value)
{

	Z80_FLAGS=0;

        if (value & 128) Z80_FLAGS |=FLAG_C;

        value=(value << 1) | 1;

	Z80_FLAGS |=sz53p_table[value];

        return value;

}

void sls_reg(z80_byte *reg)
{

        z80_byte value=*reg;

        *reg=sls_valor(value);

}


void add_a_reg(z80_byte value)
{

        z80_byte result,antes;

        result=reg_a;
        antes=reg_a;

        result +=value;
        reg_a=result;


        set_flags_carry_suma(antes,result);
        set_flags_overflow_suma(antes,result);
        Z80_FLAGS=(Z80_FLAGS & (255-FLAG_N-FLAG_Z-FLAG_S-FLAG_5-FLAG_3)) | sz53_table[result];


}


void adc_a_reg(z80_byte value)
{

	//printf ("flag_C antes: %d ",( Z80_FLAGS & FLAG_C ));

        z80_byte result,lookup;
	z80_int result_16bit;


        result_16bit=reg_a;
        result_16bit=result_16bit+value+( Z80_FLAGS & FLAG_C );

	lookup = ( (       reg_a & 0x88 ) >> 3 ) |
                     ( ( (value) & 0x88 ) >> 2 ) |
                ( ( result_16bit & 0x88 ) >> 1 );


	result=value_16_to_8l(result_16bit);
        reg_a=result;


	Z80_FLAGS = ( result_16bit & 0x100 ? FLAG_C : 0 );

	Z80_FLAGS=Z80_FLAGS | halfcarry_add_table[lookup & 0x07] | overflow_add_table[lookup >> 4] | sz53_table[result];

	//printf ("antes: %d value: %d result: %d flag_c: %d flag_pv: %d\n", antes, value, result, ( Z80_FLAGS & FLAG_C ),flag_PV.v);

}


//Devuelve el resultado de A-value sin alterar A
z80_byte sub_value(z80_byte value)
{
        z80_byte result,antes;

        result=reg_a;
        antes=reg_a;

        result -=value;

        set_flags_carry_resta(antes,result);
        set_flags_overflow_resta(antes,result);
        Z80_FLAGS=(Z80_FLAGS & (255-FLAG_Z-FLAG_S-FLAG_5-FLAG_3)) | FLAG_N | sz53_table[result];

	return result;


}

void sub_a_reg(z80_byte value)
{
	reg_a=sub_value(value);

}

void sbc_a_reg(z80_byte value)
{
        z80_byte result,lookup;
        z80_int result_16bit;

        result_16bit=reg_a;
	result_16bit=result_16bit-value-( Z80_FLAGS & FLAG_C );


        lookup = ( (       reg_a & 0x88 ) >> 3 ) |
                     ( ( (value) & 0x88 ) >> 2 ) |
                ( ( result_16bit & 0x88 ) >> 1 );

	result=value_16_to_8l(result_16bit);
        reg_a=result;


	Z80_FLAGS = ( result_16bit & 0x100 ? FLAG_C : 0 );

        Z80_FLAGS=Z80_FLAGS | halfcarry_sub_table[lookup & 0x07] | overflow_sub_table[lookup >> 4] | FLAG_N | sz53_table[result];


}




void rl_ixiy_desp_reg(z80_byte desp,z80_byte *registro)
{
        z80_byte valor_leido;
        z80_int desp16,puntero;

	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

	set_memptr(puntero);

        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
        valor_leido = rl_valor(valor_leido);
        poke_byte(puntero,valor_leido);

        if (registro!=0) *registro=valor_leido;
}

void rr_ixiy_desp_reg(z80_byte desp,z80_byte *registro)
{
        z80_byte valor_leido;
        z80_int desp16,puntero;

	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
        valor_leido = rr_valor(valor_leido);
        poke_byte(puntero,valor_leido);

        if (registro!=0) *registro=valor_leido;
}



void rlc_ixiy_desp_reg(z80_byte desp,z80_byte *registro)
{
	z80_byte valor_leido;
	z80_int desp16,puntero;

	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
        valor_leido = rlc_valor(valor_leido);
        poke_byte(puntero,valor_leido);

        if (registro!=0) *registro=valor_leido;
}

void rrc_ixiy_desp_reg(z80_byte desp,z80_byte *registro)
{
        z80_byte valor_leido;
        z80_int desp16,puntero;

	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
        valor_leido = rrc_valor(valor_leido);
        poke_byte(puntero,valor_leido);

        if (registro!=0) *registro=valor_leido;
}

void sla_ixiy_desp_reg(z80_byte desp,z80_byte *registro)
{
        z80_byte valor_leido;
        z80_int desp16,puntero;


	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
        valor_leido = sla_valor(valor_leido);
        poke_byte(puntero,valor_leido);

        if (registro!=0) *registro=valor_leido;
}

void sra_ixiy_desp_reg(z80_byte desp,z80_byte *registro)
{
        z80_byte valor_leido;
        z80_int desp16,puntero;

	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
        valor_leido = sra_valor(valor_leido);
        poke_byte(puntero,valor_leido);

        if (registro!=0) *registro=valor_leido;
}

void srl_ixiy_desp_reg(z80_byte desp,z80_byte *registro)
{
        z80_byte valor_leido;
        z80_int desp16,puntero;

	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
        valor_leido = srl_valor(valor_leido);
        poke_byte(puntero,valor_leido);

        if (registro!=0) *registro=valor_leido;
}

void sls_ixiy_desp_reg(z80_byte desp,z80_byte *registro)
{
        z80_byte valor_leido;
        z80_int desp16,puntero;

	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
        valor_leido = sls_valor(valor_leido);
        poke_byte(puntero,valor_leido);

        if (registro!=0) *registro=valor_leido;
}


void res_bit_ixiy_desp_reg(z80_byte numerobit, z80_byte desp, z80_byte *registro)
{

	z80_byte valor_and,valor_leido;
	z80_int desp16,puntero;

	//printf ("res bit=%d desp=%d reg=%x   \n",numerobit,desp,registro);

	valor_and=1;

	if (numerobit) valor_and = valor_and << numerobit;

	//cambiar 0 por 1
	valor_and = valor_and ^ 0xFF;

	desp16=desp8_to_16(desp);
	puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


	valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
	valor_leido = valor_leido & valor_and;
	poke_byte(puntero,valor_leido);


	if (registro!=0) *registro=valor_leido;
	//printf (" valor_and = %d valor_final = %d \n",valor_and,valor_leido);

}

void set_bit_ixiy_desp_reg(z80_byte numerobit, z80_byte desp, z80_byte *registro)
{

        z80_byte valor_or,valor_leido;
        z80_int desp16,puntero;

        //printf ("set bit=%d desp=%d reg=%x   \n",numerobit,desp,registro);

        valor_or=1;

        if (numerobit) valor_or = valor_or << numerobit;

	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );
        valor_leido = valor_leido | valor_or;
        poke_byte(puntero,valor_leido);

        if (registro!=0) *registro=valor_leido;
        //printf (" valor_set = %d valor_final = %d \n",valor_or,valor_leido);

}

//void bit_bit_ixiy_desp_reg(z80_byte numerobit, z80_byte desp, z80_byte *registro)
void bit_bit_ixiy_desp_reg(z80_byte numerobit, z80_byte desp)
{

        z80_byte valor_and,valor_leido;
        z80_int desp16,puntero;

        //printf ("bit bit=%d desp=%d reg=%x   \n",numerobit,desp,registro);

        valor_and=1;

        if (numerobit) valor_and = valor_and << numerobit;

	desp16=desp8_to_16(desp);
        puntero=*registro_ixiy + desp16;

        set_memptr(puntero);


        valor_leido=peek_byte(puntero);
	contend_read_no_mreq( puntero, 1 );

//                                        ;Tambien hay que poner el flag P/V con el mismo valor que
//                                        ;coge el flag Z, y el flag S debe tener el valor del bit 7


	Z80_FLAGS=(Z80_FLAGS & (255-FLAG_N-FLAG_Z-FLAG_PV-FLAG_S)) | FLAG_H;

	if (!(valor_leido & valor_and) ) {
		Z80_FLAGS |=FLAG_Z|FLAG_PV;
	}

        if (numerobit==7 && (valor_leido & 128) ) Z80_FLAGS |=FLAG_S;

        //En teoria el BIT no tiene este comportamiento de poder asignar el resultado a un registro
        //if (registro!=0) *registro=valor_leido;

        //printf (" valor_bit = %d valor_final = %d \n",valor_or,valor_leido);

        set_undocumented_flags_bits(value_16_to_8h(puntero));

}




z80_byte *devuelve_reg_offset(z80_byte valor)
{

	//printf ("devuelve_reg de: %d\n",valor);
	switch (valor) {
		case 0:
			return &reg_b;
		;;
		case 1:
			return &reg_c;
		;;
		case 2:
			return &reg_d;
		;;
		case 3:
			return &reg_e;
		;;
		case 4:
			return &reg_h;
		;;
		case 5:
			return &reg_l;
		;;
		case 6:
			return 0;
		;;
		case 7:
			return &reg_a;
		;;

		default:
			cpu_panic("Critical Error devuelve_reg_offset valor>7");
			//aqui no deberia llegar nunca
			return 0;
		break;
	}

}



void rl_cb_reg(z80_byte *registro)
{
        z80_byte valor_leido;

	if (registro==0) {
		//(HL)
		valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
		valor_leido=rl_valor(valor_leido);
        	poke_byte(HL,valor_leido);
	}

	else rl_reg(registro);

}

void rr_cb_reg(z80_byte *registro)
{
        z80_byte valor_leido;

        if (registro==0) {
                //(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
                valor_leido=rr_valor(valor_leido);
                poke_byte(HL,valor_leido);
        }

        else rr_reg(registro);
}



void rlc_cb_reg(z80_byte *registro)
{
        z80_byte valor_leido;

        if (registro==0) {
                //(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
                valor_leido=rlc_valor(valor_leido);
                poke_byte(HL,valor_leido);
        }

        else rlc_reg(registro);
}

void rrc_cb_reg(z80_byte *registro)
{
        z80_byte valor_leido;

        if (registro==0) {
                //(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
                valor_leido=rrc_valor(valor_leido);
                poke_byte(HL,valor_leido);
        }

        else rrc_reg(registro);
}

void sla_cb_reg(z80_byte *registro)
{
        z80_byte valor_leido;

        if (registro==0) {
                //(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
                valor_leido=sla_valor(valor_leido);
                poke_byte(HL,valor_leido);
        }

        else sla_reg(registro);
}

void sra_cb_reg(z80_byte *registro)
{
        z80_byte valor_leido;

        if (registro==0) {
                //(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
                valor_leido=sra_valor(valor_leido);
                poke_byte(HL,valor_leido);
        }

        else sra_reg(registro);
}

void srl_cb_reg(z80_byte *registro)
{
        z80_byte valor_leido;

        if (registro==0) {
                //(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
                valor_leido=srl_valor(valor_leido);
                poke_byte(HL,valor_leido);
        }

        else srl_reg(registro);

}

void sls_cb_reg(z80_byte *registro)
{
        z80_byte valor_leido;

        if (registro==0) {
                //(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
                valor_leido=sls_valor(valor_leido);
                poke_byte(HL,valor_leido);
        }

        else sls_reg(registro);
}


void res_bit_cb_reg(z80_byte numerobit, z80_byte *registro)
{

	z80_byte valor_and,valor_leido;

	//printf ("res bit=%d reg=%x  \n",numerobit,registro);

	valor_and=1;

	if (numerobit) valor_and = valor_and << numerobit;

	//cambiar 0 por 1
	valor_and = valor_and ^ 0xFF;

	if (registro==0) {
		//(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
		valor_leido=valor_leido & valor_and;
                poke_byte(HL,valor_leido);
	}
	else {
		valor_leido = (*registro) & valor_and;
		*registro = valor_leido;
	}

	//printf (" valor_and = %d valor_final = %d \n",valor_and,valor_leido);

}

void set_bit_cb_reg(z80_byte numerobit, z80_byte *registro)
{

        z80_byte valor_or,valor_leido;

	//printf ("set bit=%d reg=%x  \n",numerobit,registro);

        valor_or=1;

        if (numerobit) valor_or = valor_or << numerobit;

        if (registro==0) {
                //(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
                valor_leido = valor_leido | valor_or;
                poke_byte(HL,valor_leido);
        }
        else {

                valor_leido = (*registro) | valor_or;
                *registro = valor_leido;
        }

        //printf (" valor_set = %d valor_final = %d \n",valor_or,valor_leido);

}

void bit_bit_cb_reg(z80_byte numerobit, z80_byte *registro)
{

        z80_byte valor_or,valor_leido;

	//printf ("bit bit=%d reg=%x ",numerobit,registro);

        valor_or=1;

        if (numerobit) valor_or = valor_or << numerobit;

	if (registro==0) {
		//(HL)
                valor_leido=peek_byte(HL);
		contend_read_no_mreq( HL, 1 );
		set_undocumented_flags_bits_memptr();

	}
	else {
		valor_leido=*registro;
	        if (numerobit==5 && (valor_leido & 32 )) Z80_FLAGS |=FLAG_5;
	        else Z80_FLAGS &=(255-FLAG_5);

        	if (numerobit==3 && (valor_leido & 8 )) Z80_FLAGS |=FLAG_3;
	        else Z80_FLAGS &=(255-FLAG_3);

	}

//                                        ;Tambien hay que poner el flag P/V con el mismo valor que
//                                        ;coge el flag Z, y el flag S debe tener el valor del bit 7


	Z80_FLAGS=(Z80_FLAGS & (255-FLAG_N-FLAG_Z-FLAG_PV-FLAG_S)) | FLAG_H;

	if (!(valor_leido & valor_or) ) {
		Z80_FLAGS |=FLAG_Z|FLAG_PV;
	}

	if (numerobit==7 && (valor_leido & 128) ) Z80_FLAGS |=FLAG_S;

        //printf (" valor_bit = %d valor_final = %d \n",valor_or,valor_leido);


}


//;                    Bits:  4    3    2    1    0     ;desplazamiento puerto
//puerto_65278   db    255  ; V    C    X    Z    Sh    ;0
//puerto_65022   db    255  ; G    F    D    S    A     ;1
//puerto_64510    db              255  ; T    R    E    W    Q     ;2
//puerto_63486    db              255  ; 5    4    3    2    1     ;3
//puerto_61438    db              255  ; 6    7    8    9    0     ;4
//puerto_57342    db              255  ; Y    U    I    O    P     ;5
//puerto_49150    db              255  ; H                J         K      L    Enter ;6
//puerto_32766    db              255  ; B    N    M    Simb Space ;7









z80_byte envia_load_pp_spectrum(z80_byte puerto_h);
z80_byte envia_load_spectrum_nextos(z80_byte puerto_h);



//Enviar L O A D " " para spectrum 128k spanish




//Enviar Espacio, cursor arriba una vez, enter dos veces para nextos









//Enviamos Enter y F8
/*
z80_byte envia_load_f8_sam(z80_byte puerto_h,z80_byte puerto_l)
{

#define DURA_SAM_TECLA 60
#define DURA_SAM_SILENCIO 44

#define SEQUENCE_SAM_ENTER_MIN DURA_SAM_SILENCIO
#define SEQUENCE_SAM_ENTER_MAX SEQUENCE_SAM_ENTER_MIN+DURA_SAM_TECLA

#define SEQUENCE_SAM_F8_MIN SEQUENCE_SAM_ENTER_MAX+DURA_SAM_SILENCIO
#define SEQUENCE_SAM_F8_MAX SEQUENCE_SAM_F8_MIN+DURA_SAM_TECLA


                        printf ("initial_tap_sequence SAM: %d\n",initial_tap_sequence);

                        if (initial_tap_sequence>SEQUENCE_SAM_ENTER_MIN && initial_tap_sequence<SEQUENCE_SAM_ENTER_MAX && puerto_l==254 && puerto_h==0xBF)  {
				printf ("Sending enter\n");
                                return 255-1; //ENTER
                        }

			if (initial_tap_sequence>SEQUENCE_SAM_F8_MIN && initial_tap_sequence<SEQUENCE_SAM_F8_MAX) {
				printf ("puerto_h %02X puerto_l %02X\n",puerto_h,puerto_l);
			}

                        //if (initial_tap_sequence>SEQUENCE_SAM_F8_MIN && initial_tap_sequence<SEQUENCE_SAM_F8_MAX && puerto_l==0xF9 && puerto_h==0xFB)  {
			//Manera peculiar de leer teclas sam coupe
                        if (initial_tap_sequence>SEQUENCE_SAM_F8_MIN && initial_tap_sequence<SEQUENCE_SAM_F8_MAX && puerto_l==0xF9 && (puerto_h&4)==0)  {
				printf ("Sending F8\n");
				initial_tap_sequence++;
                                return 255-64; //F8
                        }



                        if (initial_tap_sequence<SEQUENCE_SAM_F8_MAX) initial_tap_sequence++;
                        else {
                                envia_jload_desactivar();



				debug_printf (VERBOSE_INFO,"End sending F8. Releasing all keys");
                        }

                        return 255;

}

*/









//Teclado en Spectrum y Jupiter Ace es casi igual excepto en la fila inferior de teclas





/* Legacy keyboard ghosting: keys sharing a column can join selected
 * rows electrically, exposing additional pressed keys on a matrix read.
 * This describes the upstream row-scanned keyboards, not the PCW interface.
 */


//Para una fila que vamos a leer, comparamos si con el resto de filas, coinciden teclas en misma columna
//si es asi, se agregara esa fila para leer en el puerto final

//Para saber si se pulsan teclas de misma columna, hacer xor a nivel de bit


//Que puerto o puertos se leeran finalmente aplicando bug de matrix error

//comun para spectrum y zx80/81 y sam y ace


//Devuelve valor para el puerto de kempston (joystick o gunstick kempston)

//Ultimo valor de dicho bit








//Devuelve valor puerto para maquinas Spectrum











void cpi_cpd_common(void)
{

	z80_byte antes,result;

	antes=reg_a;
        result=reg_a-peek_byte(HL);

        contend_read_no_mreq( HL, 1 ); contend_read_no_mreq( HL, 1 );
        contend_read_no_mreq( HL, 1 ); contend_read_no_mreq( HL, 1 );
        contend_read_no_mreq( HL, 1 );



        set_undocumented_flags_bits(result);
        set_flags_zero_sign(result);
	set_flags_halfcarry_resta(antes,result);
        Z80_FLAGS |=FLAG_N;


        BC--;

        if (!BC) Z80_FLAGS &=(255-FLAG_PV);
        else Z80_FLAGS |=FLAG_PV;

	if ((Z80_FLAGS & FLAG_H)) result--;

        if (result & 8 ) Z80_FLAGS |=FLAG_3;
        else Z80_FLAGS &=(255-FLAG_3);

        if (result & 2 ) Z80_FLAGS |=FLAG_5;
        else Z80_FLAGS &=(255-FLAG_5);


}











//Devuelve valor puerto para maquinas MSX1







//Devuelve valor puerto para maquinas SVI1







//Devuelve valor puerto para maquinas Coleco








//Devuelve valor puerto para maquinas SG1000





//Devuelve valor puerto para maquinas SMS





//Extracted from Fuse emulator


//Alterar el bit 6 de lectura segun los bits 3 y 4 en escritura
//TODO: realmente ese cambio de 1 a 0 va retrasado entre 50 microsegundos y 800 microsegundos,
//pero nosotros estamos haciendo el cambio al momento

















//Pokea parte de la ram oculta del inves con valor
