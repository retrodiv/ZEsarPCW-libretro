/* ZEsarPCW: modified by retrodiv <retrodiv@proton.me>; recorded 2026-09-08.
 * Port modifications: Copyright (c) 2026 retrodiv <retrodiv@proton.me>; GNU GPL v3.
 * Integrate embedded OpenPCW-OS, autorun, diagnostics and DSK format fixes.
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

/*

DSK emulation

*/


#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>


#include "dsk.h"
#include "libretro/pcw_debug.h"
#include "libretro/pcw_only_impl.h"
#include "pd765.h"
#include "settings.h"

//Embedded OpenPCW-OS helper disk, so the boot works with no external file.
#include "openpcw_os_disk_embed.h"


char dskplusthree_file_name[PATH_MAX]="";

z80_bit dskplusthree_write_protection={0};



//Si cambios en escritura se hace flush a disco. Por defecto no


z80_byte p3dsk_buffer_disco[DSK_MAX_BUFFER_DISCO];
int p3dsk_buffer_disco_size=DSK_MAX_BUFFER_DISCO; //Tamanyo del dsk leido. De momento establecemos en maximo

void dsk_insert_disk(char *nombre)
{
    strcpy(dskplusthree_file_name,nombre);

    if (noautoload.v==0) {

        debug_printf (VERBOSE_INFO,"Restarting autoload");

        debug_printf (VERBOSE_INFO,"Reset cpu due to autoload");
        reset_cpu();

	}

}


z80_bit dskplusthree_emulation={0};

//Zona de memoria dsk sector. Offset inicio, tamanyo y si esta activo






const int dsk_sector_sizes_numbers[]={
    0,    //0: TODO: no tengo claro que 0 sea tal cual sector size 0
    256,  //1
    512,  //2
    1024, //3
    2048, //4
    4096, //5
    8192, //6
    16384 //7
};






void dskplusthree_disable(void)
{

	if (dskplusthree_emulation.v==0) return;

	DBG_PRINT_DSK VERBOSE_INFO,"Disabling DSK emulation");

	dskplusthree_emulation.v=0;

}


const char *dsk_signature_basic=   "MV - CPC";
//Usada en captain blood: "MV - CPC format Disk Image (DU54)\r\nDisk-Info"

const char *dsk_signature_extended="EXTENDED";

int dsk_file_type_extended=0;

void dsk_get_string_protected(int offset,char *buffer_signature,int total_bytes)
{
    int i;
    for (i=0;i<total_bytes;i++) {
        z80_byte c=p3dsk_buffer_disco[i+offset];
        if (c<32 || c>126) c='.';

        buffer_signature[i]=c;
    }

    buffer_signature[i]=0;
}



void dsk_get_signature(char *buffer)
{
    //Mostrar firma ocultando caracteres no validos
    dsk_get_string_protected(0,buffer,DSK_SIGNATURE_LENGTH);
}

void dsk_get_creator(char *buffer)
{
    dsk_get_string_protected(0x22,buffer,DSK_CREATOR_LENGTH);
}


static void dsk_enable_image(int embedded)
{
	if (dskplusthree_emulation.v) return;

	DBG_PRINT_DSK VERBOSE_INFO,"Enabling DSK emulation");
	DBG_PRINT_DSK VERBOSE_INFO,"Opening DSK File %s",dskplusthree_file_name);

	long long int tamanyo;
    p3dsk_buffer_disco_size=0;

    //The OpenPCW-OS helper disk is embedded in the core, so
    //the fail-back to CP/M works with no external file (as standalone ZEsarUX does).
    if (embedded) {
        memcpy(p3dsk_buffer_disco,openpcw_os_dsk,sizeof openpcw_os_dsk);
        tamanyo=sizeof openpcw_os_dsk;
    }
    else
    {
	tamanyo=get_file_size(dskplusthree_file_name);

	if (tamanyo>DSK_MAX_BUFFER_DISCO) {
		debug_printf(VERBOSE_ERR,"DSK size too big");
		return;
	}

        FILE *ptr_dskfile;
        ptr_dskfile=fopen(dskplusthree_file_name,"rb");

        if (!ptr_dskfile) {
                debug_printf(VERBOSE_ERR,"Unable to open disk %s",dskplusthree_file_name);
                return;
        }

        int _dsk_read=fread(p3dsk_buffer_disco,1,DSK_MAX_BUFFER_DISCO,ptr_dskfile);
        int read_error=ferror(ptr_dskfile);
        fclose(ptr_dskfile);

        /* Both DSK formats require a complete 256-byte Disk Information Block.
           Reject short/failed reads before inspecting bytes left by a previous
           disk. Also reject a file that changed size between stat and read. */
        if (read_error || _dsk_read<0x100 || _dsk_read!=tamanyo) {
            debug_printf(VERBOSE_ERR,"Incomplete or unreadable DSK image: %s",dskplusthree_file_name);
            return;
        }

        //Per-disc detection: some PCW discs are CP/M applications whose on-disc self-boot loader
        //never completes under emulation (it spins at $003D polling the floppy controller for a
        //hardware timing/interrupt condition the deterministic emulation does not reproduce). For
        //those, the core boots CP/M and types the application's start command (see machines/pcw.c).
        //Identify them by an exact whole-image hash (FNV-1a) so the behaviour applies to ONLY those
        //titles and nothing else, and carry the start command per title.
        {
            extern int pcw_disc_cpm_autorun; extern char pcw_disc_cpm_autorun_cmd[16];
            static const struct { unsigned int hash; const char *cmd; } cpm_autorun_table[] = {
                { 0xBD78AB02u, "head" },   //Head Over Heels
                { 0x61AA9708u, "ballyhoo" },   //Ballyhoo
                { 0xEE9F0DF7u, "deadline" },   //Deadline
                { 0x74B54F3Fu, "dtp" },   //The Desktop Publisher
                { 0x8CB8B812u, "enchante" },   //Enchanter
                { 0x69CB741Du, "hitchhik" },   //Hitchhiker's Guide
                { 0xDDF9BB39u, "hollywoo" },   //Hollywood Hijinx
                { 0x106A33C8u, "infidel" },   //Infidel
                { 0xB7BDC620u, "leatherg" },   //Leather Goddesses
                { 0xD7D1ACB9u, "lurkingh" },   //The Lurking Horror
                { 0xAB6B9567u, "office" },   //Mini Office Pro
                { 0xB441D7C2u, "moonmist" },   //Moonmist
                { 0x479F3206u, "planetfa" },   //Planetfall
                { 0xAA248C7Du, "hearts" },   //Plundered Hearts
                { 0xBBFA34B8u, "scrabble" },   //Computer Scrabble
                { 0x57E41BB4u, "seastalk" },   //Seastalker
                { 0x7EEEB3F2u, "sorcerer" },   //Sorcerer
                { 0xDB34E4E4u, "spellbre" },   //Spellbreaker
                { 0x80C0825Au, "starcros" },   //Starcross
                { 0x449305A3u, "stationf" },   //Stationfall
                { 0x162A1AC3u, "suspect" },   //Suspect
                { 0xCE421816u, "suspend" },   //Suspended
                { 0x31118E13u, "tet" },   //Tetris
                { 0x19EB1F4Cu, "verbiage" },   //Verbiage
                { 0x29EFFD31u, "wishbrin" },   //Wishbringer
                { 0xADF4AA6Du, "witness" },   //The Witness
                { 0xF1EFA452u, "zork1" },   //Zork I
                { 0x63685277u, "zork2" },   //Zork II
                { 0x9454C30Cu, "zork3" },   //Zork III
                { 0xD7AD5D0Du, "bob" },   //Bob Winner
                // Additional supported titles and colour editions.
                //--- A>-autolaunch: plain CP/M game discs that reach A> natively; type their own .COM ---
                { 0x8F808D31u, "bounder" },   //bounder_pcw.dsk  [A>-autolaunch]
                { 0x1C036568u, "load" },   //ACE.dsk  [A>-autolaunch]
                { 0x6C0CAC9Cu, "ash" },   //After Shock A.dsk  [A>-autolaunch]
                { 0xC695CECCu, "ballyhoo" },   //Ballyhoo.dsk  [A>-autolaunch]
                { 0x4BBC9238u, "football" },   //Brian Cloughs Football Fortunes.dsk  [A>-autolaunch]
                { 0x7A64E643u, "disk" },   //Complete Home Entertainment Centre.dsk  [A>-autolaunch]
                { 0x45D746E1u, "enchante" },   //Enchanter.dsk  [A>-autolaunch]
                { 0x238A4D1Bu, "f" },   //Fairlight.dsk  [A>-autolaunch]
                { 0x9FB31F5Du, "game" },   //Guild Of Thieves A.dsk  [A>-autolaunch]
                { 0x244AF7E5u, "hitchhik" },   //Hithhikers Guide To The Galaxy.dsk  [A>-autolaunch]
                { 0x6B689727u, "hollywoo" },   //Hollywood Hijinx.dsk  [A>-autolaunch]
                { 0x683D0B9Cu, "menu" },   //Jewels Of Darkness.dsk  [A>-autolaunch]
                { 0x04F6D46Eu, "game" },   //Jinxter A.dsk  [A>-autolaunch]
                { 0x99EF7290u, "lor" },   //Lord of the Rings.dsk  [A>-autolaunch]
                { 0x4605B5DAu, "magic" },   //Magic Way.dsk  [A>-autolaunch]
                { 0x6AC57EF9u, "st" },   //Sporting Triangles A.dsk  [A>-autolaunch]
                { 0x14FE19FEu, "sglider" },   //Starglider.dsk  [A>-autolaunch]
                { 0xB5BC8270u, "poker" },   //Strip Poker.dsk  [A>-autolaunch]
                { 0x2D0977E8u, "terracom" },   //Terracom.dsk  [A>-autolaunch]
                { 0x6DB8E6CEu, "ajacka" },   //applejack.dsk  [A>-autolaunch]
                { 0xB11FCFA1u, "brick" },   //brick.dsk  [A>-autolaunch]
                { 0x9956126Au, "c23" },   //catch23_pcw.dsk  [A>-autolaunch]
                { 0x102359CDu, "games" },   //classic1_pcw.dsk  [A>-autolaunch]
                { 0x6CB42C40u, "gamesii" },   //classic_game2_pcw.dsk  [A>-autolaunch]
                { 0xEAB50C53u, "chess" },   //colossus4chess_pcw.dsk  [A>-autolaunch]
                { 0x1A2B1D5Cu, "conecta4" },   //conecta4.dsk  [A>-autolaunch]
                { 0x4441828Bu, "cyrus" },   //cyrus2chess.dsk  [A>-autolaunch]
                { 0x3819E726u, "f" },   //fairlight2.dsk  [A>-autolaunch]
                { 0x18263C34u, "golf" },   //golf.dsk  [A>-autolaunch]
                { 0xE4C8DB66u, "herbert" },   //herbert.dsk  [A>-autolaunch]
                { 0x9E58E506u, "ladder" },   //ladder.dsk  [A>-autolaunch]
                { 0xBC653FA4u, "menu" },   //magic_maths_a.dsk  [A>-autolaunch]
                { 0x2E2D5F55u, "master" },   //master.dsk  [A>-autolaunch]
                { 0x7C60FA53u, "morfi1" },   //mortadeloydilemon2_pcw.dsk  [A>-autolaunch]
                { 0x2349C91Cu, "office" },   //office2.dsk  [A>-autolaunch]
                { 0xCCA39D2Au, "office" },   //office3.dsk  [A>-autolaunch]
                { 0x0C003886u, "paint" },   //paint.dsk  [A>-autolaunch]
                { 0x8FD26511u, "ps" },   //prospel1.dsk  [A>-autolaunch]
                { 0x63574300u, "sargon" },   //sargon.dsk  [A>-autolaunch]
                { 0xF318ED6Cu, "slider" },   //slider.dsk  [A>-autolaunch]
                { 0xA2C3F4C5u, "solitar" },   //solitario.dsk  [A>-autolaunch]
                { 0x5F7B14C6u, "snooker" },   //stevedsn.dsk  [A>-autolaunch]
                { 0x8D52CEA6u, "sc2" },   //supercalc2a.dsk  [A>-autolaunch]
                { 0xE6B882E2u, "symbos" },   //sym-pcw.dsk  [A>-autolaunch]
                { 0x8A009D9Eu, "tp" },   //triviala.dsk  [A>-autolaunch]
                { 0xB896F984u, "tuma71" },   //tuma7.dsk  [A>-autolaunch]
                { 0x5B1B62E5u, "chess" },   //3dclockchess_pcw.dsk
                { 0xBBF9FB42u, "bridge" },   //colos4br.dsk
                { 0xEFA164D9u, "bridge" },   //Colossus 4 Bridge.dsk
                { 0xD6D5A524u, "scrabble" },   //Computer Scrabble Deluxe.dsk
                { 0x8C6F4FC8u, "elf" },   //Elf.dsk
                { 0x09DE349Bu, "menu" },   //gnome ranger - side a.dsk
                { 0x7AF023EEu, "menu" },   //Gnome Ranger A.dsk
                { 0xBEBFD949u, "menu" },   //gr1.dsk
                { 0xB0A9B951u, "menu" },   //ingrid's back (uk) (1988) (disk 
                { 0xA1CCD090u, "menu" },   //Ingrid's Back A.dsk
                { 0x7E640DDEu, "menu" },   //jewelsdarkness.dsk
                { 0x216131C6u, "menu" },   //Knight Orc A.dsk
                { 0xE3D1F840u, "menu" },   //knightorc1.dsk
                { 0xAE3F78A7u, "menu" },   //lancelot - side a.dsk
                { 0x4FA6235Eu, "menu" },   //Lancelot A.dsk
                { 0xCB0B2DEEu, "office" },   //office1b.dsk
                { 0x3F21887Bu, "office" },   //office2a.dsk
                { 0x258C0CF8u, "pawn" },   //Pawn A.dsk
                { 0x1709F908u, "menu" },   //scapeghost - side a.dsk
                { 0xB4520077u, "menu" },   //Silicon Dreams.dsk
                { 0x86BCCFEDu, "sfh" },   //Strike Force Harrier.dsk
                { 0xA895F618u, "ad" },   //Templos Sagrados A.dsk
                { 0xA8B57C5Cu, "menu" },   //Time And Magic A.dsk
                { 0x41475B17u, "menu" },   //time and magik - side a.dsk
                { 0x51490CF3u, "turbo" },   //turbo_pa.dsk
                { 0xA0F38DECu, "verbiage" },   //Verbiage.dsk
                //--- black-hang-with-.COM: self-boot loader stalls at $003D but the disc holds a
                //    real game .COM; the $003D fail-back path boots CP/M and types the disc's own .COM ---
                { 0xB825CC27u, "bob" },        //Bob Winner A.dsk
                { 0x01875F25u, "cutthroa" },   //Cutthroats.dsk
                { 0x7B9DF0E7u, "menu" },       //Scapeghost A.dsk
                { 0x000C36D4u, "suspend" },    //Suspended.dsk
                { 0x81009DF4u, "tp" },         //Trivial Pursuit Genus A.dsk
            };
            unsigned int h=2166136261u; int i;
            for (i=0;i<_dsk_read;i++){ h ^= p3dsk_buffer_disco[i]; h = h*16777619u; }
            pcw_disc_cpm_autorun=0; pcw_disc_cpm_autorun_cmd[0]=0;
            for (i=0;i<(int)(sizeof(cpm_autorun_table)/sizeof(cpm_autorun_table[0]));i++) {
                if (h==cpm_autorun_table[i].hash) {
                    pcw_disc_cpm_autorun=1;
                    int j; for(j=0;cpm_autorun_table[i].cmd[j] && j<15;j++) pcw_disc_cpm_autorun_cmd[j]=cpm_autorun_table[i].cmd[j];
                    pcw_disc_cpm_autorun_cmd[j]=0;
                }
            }
            if (getenv("ZPCW_DISCID")) fprintf(stderr,"[discid] FNV-1a=%08X read=%d cpm_autorun=%d cmd=%s\n",
                h,_dsk_read,pcw_disc_cpm_autorun,pcw_disc_cpm_autorun_cmd);
        }
    }

    //Detect signature
    const int signature_check_length=8;

    if (!memcmp(dsk_signature_basic,p3dsk_buffer_disco,signature_check_length)) {
        DBG_PRINT_DSK VERBOSE_INFO,"Detected Basic DSK");
        dsk_file_type_extended=0;
    }

    else if (!memcmp(dsk_signature_extended,p3dsk_buffer_disco,signature_check_length)) {
        DBG_PRINT_DSK VERBOSE_INFO,"Detected Extended DSK");
        dsk_file_type_extended=1;
    }

    else {
        debug_printf(VERBOSE_ERR,"Unknown DSK file format");
        return;
    }

    p3dsk_buffer_disco_size=tamanyo;

    //Mostrar firma ocultando caracteres no validos
    char buffer_signature[DSK_SIGNATURE_LENGTH+1];
    dsk_get_signature(buffer_signature);
    DBG_PRINT_DSK VERBOSE_INFO,"DSK signature: %s",buffer_signature);

    char buffer_creator[DSK_CREATOR_LENGTH+1];
    dsk_get_creator(buffer_creator);
    DBG_PRINT_DSK VERBOSE_INFO,"DSK creator: %s",buffer_creator);

    DBG_PRINT_DSK VERBOSE_INFO,"DSK total tracks: %d total sides: %d",dsk_get_total_tracks(),dsk_get_total_sides());

    char buffer_esquema_proteccion[DSK_MAX_PROTECTION_SCHEME+1];
    int protegido_no_soportado=dsk_get_protection_scheme(buffer_esquema_proteccion);
    if (protegido_no_soportado) {
        DBG_PRINT_DSK VERBOSE_ERR,"This disk is protected with method: %s. It's not fully supported and might be not readable",buffer_esquema_proteccion);
    }

    DBG_PRINT_DSK VERBOSE_INFO,"Protection system: %s",buffer_esquema_proteccion);

        dskplusthree_emulation.v=1;

}

/* External paths always mean external content, even if their filename or
   parent directory happens to contain the helper disk's name. */
void dskplusthree_enable(void)
{
    dsk_enable_image(0);
}

void dskplusthree_enable_openpcw(void)
{
    dsk_enable_image(1);
}

int dsk_get_protection_scheme_aux_longitud(char *esquema,int longitud)
{
    int i;

    for (i=0;i<p3dsk_buffer_disco_size-longitud;i++) {
        if (!memcmp(&p3dsk_buffer_disco[i],esquema,longitud)) return 1;
    }

    return 0;
}

int dsk_get_protection_scheme_aux(char *esquema)
{
    int longitud=strlen(esquema);
    return dsk_get_protection_scheme_aux_longitud(esquema,longitud);
}

//Realmente es: SPEEDLOCK +3 DISC PROTECTION SYSTEM COPYRIGHT 1988 SPEEDLOCK ASSOCIATES FOR MORE DETAILS, PHONE (0734) 470303
//Ejemplo Batman The Caped Crusader.dsk
char *dsk_protection_scheme_speedlock_p3="SPEEDLOCK +3 DISC PROTECTION SYSTEM COPYRIGHT";

//Realmente es: SPEEDLOCK DISC PROTECTION SYSTEMS (C) 1989 SPEEDLOCK ASSOCIATES FOR MORE DETAILS, PHONE (0734) 470303
//Ejemplo Chuck Yeager's Advanced Flight Trainer.dsk
char *dsk_protection_scheme_speedlock_disc="SPEEDLOCK DISC PROTECTION SYSTEMS";

//Ejemplo: Cabal.dsk
char *dsk_protection_scheme_paul_owen="OCEAN SOFTWARE LIMITED\x80PAUL OWENS\x80PROTECTION SYSTEM";

//Alkatraz
//THE ALKATRAZ PROTECTION SYSTEM   (C) 1987  Appleby Associates                    Antony Dunmore          James Wood            John Bayliffe
//Ejemplo: Echelon.dsk
char *dsk_protection_scheme_alkatraz="THE ALKATRAZ PROTECTION SYSTEM   (C) 1987";


//Esto no se si realmente es un sistema de proteccion
//Pero parece que da problemas al usar esos discos
//***Loader Copyright Three Inch Software 1988, All Rights Reserved. Three Inch Software, 73 Surbiton Road, Kingston upon Thames, KT1 2HG***
char *dsk_protection_scheme_three_inch="Loader Copyright Three Inch Software";

//Este viene en Norte y Sur, al parecer ejecuta comandos invalidos pero funciona en ZEsarUX
//NEW DISK PROTECTION SYSTEM. (C) 1990 BY NEW FRONTIER SOFT.
//SI TIENES CONCIMIENTOS DE CODIGO MAQUINA PUEDES TENER TRABAJO LLAMANDO AL NUMERO: (93) 485-11-57 O ESCRIBENDO A:
//C/FRANCISCO DE ARANDA, 45-47 ENTLO. 1 08005 BARCELONA.
/*
Sectores de 4kb
Parece que despues de un seek a pista 0, lanza otro a pista 1 y
si ese seek no finaliza en menos de 97 t-estados, empieza a enviar valores random (leidos de la rom) hasta que finaliza
el seek (durante el seek los valores random se ignoran)
*/
char *dsk_protection_scheme_new_frontier="NEW DISK PROTECTION SYSTEM. (C) 1990 BY NEW FRONTIER SOFT";


//Este sistema de proteccion consiste en pista 1, sector size 8192 pero solo 1 sector con size 6144, CRHN=1 0 1 6
//Bonanza Bros - Side B (Spectrum).dsk , X-Out.dsk
//Los bytes corresponden al track-info de pista 1, sector 0,1
char *dsk_protection_scheme_unknown1="\x01\x00\x00\x00\x06\x01\x4e\xe5\x01\x00\x01\x06\x20\x60\x00\x18";

//Este carga un bloque corto, luego una secuencia de colores en el border
//Esto es speedlock tambien
//Tai-Pan.dsk, Action Force.dsk
//Los bytes corresponden a track-info de pista 3, sectores 8,9
char *dsk_protection_scheme_unknown2="\x03\x00\x08\x02\x00\x00\x00\x02\x03\x00\x09\x02\x00\x40\x00\x02";

//Retorna diciendo si esta protegido y ademas sin soporte en emulacion, o no
int dsk_get_protection_scheme(char *buffer)
{
    if (dsk_get_protection_scheme_aux(dsk_protection_scheme_speedlock_p3)) {
        strcpy(buffer,"SPEEDLOCK +3 DISC 1988");
        return 1;
    }

    if (dsk_get_protection_scheme_aux(dsk_protection_scheme_speedlock_disc)) {
        strcpy(buffer,"SPEEDLOCK DISC 1989");
        return 1;
    }

    if (dsk_get_protection_scheme_aux(dsk_protection_scheme_paul_owen)) {
        strcpy(buffer,"Ocean Paul Owens");
        return 1;
    }

    if (dsk_get_protection_scheme_aux(dsk_protection_scheme_alkatraz)) {
        strcpy(buffer,"ALKATRAZ 1987");
        return 1;
    }

    if (dsk_get_protection_scheme_aux(dsk_protection_scheme_three_inch)) {
        strcpy(buffer,"Three Inch");
        return 1;
    }

    //Este si que esta soportado
    if (dsk_get_protection_scheme_aux(dsk_protection_scheme_new_frontier)) {
        strcpy(buffer,"New Frontier");
        return 0;
    }

    if (dsk_get_protection_scheme_aux_longitud(dsk_protection_scheme_unknown1,16)) {
        strcpy(buffer,"Unknown 1");
        return 1;
    }

    if (dsk_get_protection_scheme_aux_longitud(dsk_protection_scheme_unknown2,16)) {
        strcpy(buffer,"SPEEDLOCK +3 DISC 1988-2");
        return 1;
    }

    strcpy(buffer,"None");
    return 0;
}

z80_byte plus3dsk_get_byte_disk(int offset)
{

        if (dskplusthree_emulation.v==0) return 0;

        if (offset>=p3dsk_buffer_disco_size) {
                //debug_printf (VERBOSE_ERR,"Error. Trying to read beyond dsk. Size: %d Asked: %d. Disabling DSK",p3dsk_buffer_disco_size,offset);
                //no desactivamos disco
                //dskplusthree_disable();

                debug_printf (VERBOSE_ERR,"Error. Trying to read beyond dsk. Size: %d Asked: %d",p3dsk_buffer_disco_size,offset);
                return 0;
        }


        return p3dsk_buffer_disco[offset];
}

void plus3dsk_put_byte_disk(int offset,z80_byte value)
{
        if (dskplusthree_emulation.v==0) return;

        if (offset>=p3dsk_buffer_disco_size) {
                debug_printf (VERBOSE_ERR,"Error. Trying to write beyond dsk. Size: %d Asked: %d. Disabling DSK",p3dsk_buffer_disco_size,offset);
                dskplusthree_disable();
                return;
        }


	if (dskplusthree_write_protection.v) return;

        p3dsk_buffer_disco[offset]=value;

}

int dsk_get_total_tracks(void)
{
    return p3dsk_buffer_disco[0x30];
}

int dsk_get_total_sides(void)
{
    return p3dsk_buffer_disco[0x31];
}





//entrada: offset a track information block





//entrada: offset a track information block





//entrada: offset a track information block




//entrada: offset a track information block



//entrada: offset a track information block
int dsk_get_total_sectors_track_from_offset(int offset)
{
    int total_sectors=plus3dsk_get_byte_disk(offset+0x15);

    return total_sectors;
}


int dsk_get_total_sectors_track(int pista,int cara)
{
    int offset=dsk_get_start_track(pista,cara);
    return dsk_get_total_sectors_track_from_offset(offset);

}




int dsk_get_sector_size_from_n_value(int n_value)
{

    //It is assumed that sector sizes are defined as 3 bits only, so that a sector size of N="8" is equivalent to N="0".
    //Mot o Mundial de futbol tienen algunos sectores con tamaño 8
    n_value &=7;

    int sector_size=dsk_sector_sizes_numbers[n_value];

    return sector_size;
}

//entrada: offset a track information block
int dsk_get_sector_size_track_from_offset(int offset)
{
    int sector_size_byte=plus3dsk_get_byte_disk(offset+0x14);


    return dsk_get_sector_size_from_n_value(sector_size_byte);
}

int dsk_get_sector_size_track(int pista,int cara)
{
    int offset=dsk_get_start_track(pista,cara);
    return dsk_get_sector_size_track_from_offset(offset);
}



//const char *dsk_track_info_signature="Track-Info\r\n";
const char *dsk_track_info_signature="Track-Info";

int dsk_check_track_signature(int offset)
{
    int i;
    //La teoria dice que serian 12 contando \r y \n final pero veo varios dsk (de cpc por ejemplo zaptballs) que acaban con dos espacios
    for (i=0;i<10;i++) {
        int leido_firma=dsk_track_info_signature[i];
        int leido_pista=plus3dsk_get_byte_disk(offset+i);
        if (leido_firma!=leido_pista) return -1;
    }
    return 0;
}

int dsk_basic_get_start_track(int pista_encontrar,int cara_encontrar)
{
    int pista;
    int offset=0x100;
    /* A standard CPCEMU DSK publishes one fixed Track-Info-plus-data size in
       the disk header.  Some legitimate PCW images leave the redundant N byte
       in an individual Track-Info block at zero while every sector descriptor
       still says 512 bytes. Deriving the next offset from that redundant byte
       desynchronises all following tracks. Prefer the authoritative global
       size, retaining the old calculation only for malformed legacy images
       which omit it. */
    int basic_track_size=plus3dsk_get_byte_disk(0x32) |
                         (plus3dsk_get_byte_disk(0x33)<<8);

    for (pista=0;pista<dsk_get_total_tracks();pista++) {
        //Validar que estemos en informacion de pista realmente mirando la firma
        //TODO: quiza esta validacion se pueda quitar y/o hacerla al abrir el dsk
        if (dsk_check_track_signature(offset)) {
            debug_printf(VERBOSE_ERR,"DSK: Basic DSK, track signature not found on track %XH offset %XH",pista,offset);
        }

        z80_byte track_number=plus3dsk_get_byte_disk(offset+0x10);
        z80_byte side_number=plus3dsk_get_byte_disk(offset+0x11);

        if (track_number==pista_encontrar && side_number==cara_encontrar) {
            return offset;
        }

        int saltar=basic_track_size;
        if (saltar<256) {
            int sector_size=dsk_get_sector_size_track_from_offset(offset);
            if (sector_size<0) {
                debug_printf(VERBOSE_ERR,"DSK Basic: Sector size not supported on track %d",pista);
                return -1;
            }
            int total_sectors=dsk_get_total_sectors_track_from_offset(offset);
            saltar=total_sectors*sector_size+256; //256 ocupa el sector block
        }

        offset +=saltar;
    }

    return -1;

}

//Retorna numero de pista.
//Entrada: offset: offset a track-info
int dsk_get_track_number_from_offset(int offset)
{
    z80_byte track_number=plus3dsk_get_byte_disk(offset+0x10);
    return track_number;
}




//Retorna numero de cata.
//Entrada: offset: offset a track-info
int dsk_get_track_side_from_offset(int offset)
{
    z80_byte side_number=plus3dsk_get_byte_disk(offset+0x11);
    return side_number;
}



int dsk_extended_get_start_track(int pista_encontrar,int cara_encontrar)
{
    int pista,cara;
    int offset=0x100;
    int offset_track_table=0x34;

    for (pista=0;pista<dsk_get_total_tracks();pista++) {
        for (cara=0;cara<dsk_get_total_sides();cara++) {
            //Validar que estemos en informacion de pista realmente mirando la firma
            //TODO: quiza esta validacion se pueda quitar y/o hacerla al abrir el dsk
            if (dsk_check_track_signature(offset)) {
                debug_printf(VERBOSE_ERR,"DSK: Extended DSK, track signature not found on track %XH size %d offset %XH",pista,cara,offset);
            }

            z80_byte track_number=dsk_get_track_number_from_offset(offset);
            z80_byte side_number=dsk_get_track_side_from_offset(offset);

            //printf("dsk_extended_get_start_track: pista: %d current_track: %d offset: %XH buscar pista: %d\n",
            //    pista,track_number,offset,pista_encontrar);

            if (track_number==pista_encontrar && side_number==cara_encontrar) {
                //printf("dsk_extended_get_start_track: return %X\n",offset);
                return offset;
            }

            int sector_size=dsk_get_sector_size_track_from_offset(offset);
            if (sector_size<0) {
                debug_printf(VERBOSE_ERR,"DSK Extended: Sector size not supported on track %d side %d",pista,cara);
                return -1;
            }


            int saltar=plus3dsk_get_byte_disk(offset_track_table)*256;
            offset +=saltar;


            offset_track_table++;


        }
    }

    return -1;

}


//Retorna -1 si pista no encontrada
//Retorna offset al Track information block
int dsk_get_start_track(int pista,int cara)
{
    //Hacerlo diferente si dsk basico o extendido
    if (dsk_file_type_extended) return dsk_extended_get_start_track(pista,cara);
    else return dsk_basic_get_start_track(pista,cara);
}

int dsk_extended_get_track_size(int pista,int cara)
{
    int offset_track_table=0x34;

    //este incremento sobre la tabla es el doble cuando el disco tiene dos caras
    int incremento=pista*dsk_get_total_sides();

    offset_track_table +=incremento;

    if (cara==1) offset_track_table++;

    return plus3dsk_get_byte_disk(offset_track_table)*256;

}


int dsk_get_physical_sector(int pista,int sector)
{
        //if (condition_r_equals && sector>minimo_sector && match_condition) {
            //debug_printf(VERBOSE_DEBUG,"Found sector  ID track %d/sector %d at  pos track %d/sector %d",pista_buscar,sector_buscar,pista,sector);
            //printf("Found sector ID %02XH on track %d at pos sector %d\n",parametro_r,pista,sector);
            int iniciopista=dsk_get_start_track(pista,0); //TODO: de momento solo cara 0

            int offset=iniciopista+0x100;

            int sector_size;

            //Ejemplos en los que es necesario leer el tamanyo de esta manera: Riptoff Master Disk.dsk
            //En esos casos , el sector_size "normal" del dsk basico, esta a 0
            if (dsk_file_type_extended) {
                //TODO: cara 0 de momento solamente
                sector_size=dsk_get_real_sector_size_extended(pista,0,sector);
            }

            else {
                sector_size=dsk_get_sector_size_track_from_offset(iniciopista);
            }


            if (sector_size<0) {
                debug_printf(VERBOSE_ERR,"dsk_get_sector: Sector size not supported on track %d sector %d",pista,sector);
                return -1;
            }

            //int iniciopista=traps_plus3dos_getoff_start_track(pista);
            int offset_retorno=offset+sector_size*sector;
            //printf("Offset sector: %XH\n",offset_retorno);

            //*sector_fisico=sector;
            //printf("Found sector ID %02XH on track %d at offset in DSK: %XH\n",parametro_r,pista,offset_retorno);
            return offset_retorno;


}

//Retorna el offset al dsk segun la pista y sector id dados
//Retorna tambien el sector fisico: 0,1,2,3....
//Parametro minimo_sector permite escoger un sector mayor que dicho parametro
//search_deleted: si buscar sectores borrados
//skip_not_match corresponde a parametro SK
//check_r_parameter se pone a 0 en read_track
int dsk_get_sector(int pista,int parametro_r,z80_byte *sector_fisico,int minimo_sector,int search_deleted,int skip_not_match,int check_r_parameter)
{

    int iniciopista=dsk_get_start_track(pista,0); //TODO: de momento solo cara 0

    DBG_PRINT_DSK VERBOSE_PARANOID,"DSK Start track %d: %XH",pista,iniciopista);

    int total_sectors=dsk_get_total_sectors_track_from_offset(iniciopista);

    DBG_PRINT_DSK VERBOSE_PARANOID,"DSK Start track: %d",total_sectors);


    int sector_information_list=iniciopista+0x18;

    int sector;

    for (sector=0;sector<total_sectors;sector++) {

        DBG_PRINT_DSK VERBOSE_PARANOID,"Looking for sector ID %02XH on track %d we are in position sector %d",parametro_r,pista,sector);


        z80_byte sector_id=plus3dsk_get_byte_disk(sector_information_list+2);



        //Para obtener el tipo de sector (borrado si/no)
        z80_byte leido_id_st1=plus3dsk_get_byte_disk(sector_information_list+4);
        z80_byte leido_id_st2=plus3dsk_get_byte_disk(sector_information_list+5);

        //sector borrado
        int deleted_sector=0;
        if (leido_id_st2 & PD765_STATUS_REGISTER_TWO_CM_MASK) deleted_sector=1;

        int match_condition=0;

        if (search_deleted==deleted_sector) {
            //Encontramos el tipo de sector que buscamos (borrado si/no)
            match_condition=1;
        }
        else {
            //No lo encontramos. Si skip=0, no saltarlo
            if (!skip_not_match) match_condition=1;
        }


        //Sector no tiene marca ni de borrado ni de no borrado, por tanto no hay match
        //Aun no me he encontrado ningun disco con esto, pero por si acaso
        if ((leido_id_st2 & PD765_STATUS_REGISTER_TWO_MD_MASK) || (leido_id_st1 & PD765_STATUS_REGISTER_ONE_MA_MASK)) {
            match_condition=0;
            DBG_PRINT_DSK VERBOSE_DEBUG,"DSK: sector does not have deleted nor not deleted mask");
            //sleep(3);
        }


        //Condicion de que R tenga el valor esperado
        int condition_r_equals=0;

        if (check_r_parameter) {
            if (sector_id==parametro_r) {
                condition_r_equals=1;
            }
        }

        //No validamos que R tenga el valor esperado
        else {
            condition_r_equals=1;
        }



        if (condition_r_equals && sector>minimo_sector && match_condition) {
            //debug_printf(VERBOSE_DEBUG,"Found sector  ID track %d/sector %d at  pos track %d/sector %d",pista_buscar,sector_buscar,pista,sector);
            DBG_PRINT_DSK VERBOSE_PARANOID,"Found sector ID %02XH on track %d at pos sector %d",parametro_r,pista,sector);


            int offset=iniciopista+0x100;

            int sector_size;

            //Ejemplos en los que es necesario leer el tamanyo de esta manera: Riptoff Master Disk.dsk
            //En esos casos , el sector_size "normal" del dsk basico, esta a 0
            if (dsk_file_type_extended) {
                //TODO: cara 0 de momento solamente
                sector_size=dsk_get_real_sector_size_extended(pista,0,sector);
            }

            else {
                sector_size=dsk_get_sector_size_track_from_offset(iniciopista);
            }


            if (sector_size<0) {
                debug_printf(VERBOSE_ERR,"dsk_get_sector: Sector size not supported on track %d sector %d",pista,sector);
                return -1;
            }

            //int iniciopista=traps_plus3dos_getoff_start_track(pista);
            int offset_retorno=offset+sector_size*sector;
            //printf("Offset sector: %XH\n",offset_retorno);

            *sector_fisico=sector;
            DBG_PRINT_DSK VERBOSE_PARANOID,"Found sector ID %02XH on track %d at offset in DSK: %XH",parametro_r,pista,offset_retorno);
            return offset_retorno;
        }

        sector_information_list +=8;

    }



    DBG_PRINT_DSK VERBOSE_DEBUG,"NOT Found sector ID %02XH on track %d (max sectors: %d)",parametro_r,pista,total_sectors);

		return -1;

}


//Retorna el offset al dsk segun la pista y sector fisico
int dsk_get_sector_fisico(int pista,int cara,int sector_fisico)
{

    int iniciopista=dsk_get_start_track(pista,cara);

    //printf("Inicio pista %d: %XH\n",pista,iniciopista);


    int offset=iniciopista+0x100;

    int sector_size;

    //Ejemplos en los que es necesario leer el tamanyo de esta manera: Riptoff Master Disk.dsk
    if (dsk_file_type_extended) {
        sector_size=dsk_get_real_sector_size_extended(pista,cara,sector_fisico);
    }

    else sector_size=dsk_get_sector_size_track_from_offset(iniciopista);


    if (sector_size<0) {
        debug_printf(VERBOSE_ERR,"dsk_get_sector: Sector size not supported on track %d sector %d",pista,sector_fisico);
        return -1;
    }

    //int iniciopista=traps_plus3dos_getoff_start_track(pista);
    int offset_retorno=offset+sector_size*sector_fisico;
    //printf("Offset sector: %XH\n",offset_retorno);


    //printf("Found sector ID %02XH on track %d at offset in DSK: %XH\n",parametro_r,pista,offset_retorno);
    return offset_retorno;





}


int dsk_get_start_sector_info(int pista,int cara,int sector_fisico)
{


    int iniciopista=dsk_get_start_track(pista,cara);

    //printf("En dsk_get_chrn Inicio pista %d: %XH\n",pista,iniciopista);

    //saltar 0x18
    iniciopista +=0x18;


    int offset_tabla_sector=sector_fisico*8;
    //z80_byte pista_id=plus3dsk_get_byte_disk(iniciopista+offset_tabla_sector); //Leemos pista id
    //z80_byte sector_id=plus3dsk_get_byte_disk(iniciopista+offset_tabla_sector+2); //Leemos c1, c2, etc

    //debug_printf(VERBOSE_DEBUG,"%02X ",sector_id);

    return iniciopista+offset_tabla_sector;



}

//Devolver CHRN de una pista y sector concreto
void dsk_get_chrn(int pista,int cara,int sector_fisico,z80_byte *parametro_c,z80_byte *parametro_h,z80_byte *parametro_r,z80_byte *parametro_n)
{


    int offset=dsk_get_start_sector_info(pista,cara,sector_fisico);

    *parametro_c=plus3dsk_get_byte_disk(offset);
    *parametro_h=plus3dsk_get_byte_disk(offset+1);
    *parametro_r=plus3dsk_get_byte_disk(offset+2);
    *parametro_n=plus3dsk_get_byte_disk(offset+3);


}

//Escribir CHRN en una pista y sector concreto
void dsk_put_chrn(int pista,int cara,int sector_fisico,z80_byte parametro_c,z80_byte parametro_h,z80_byte parametro_r,z80_byte parametro_n)
{


    int offset=dsk_get_start_sector_info(pista,cara,sector_fisico);

    plus3dsk_put_byte_disk(offset,parametro_c);
    plus3dsk_put_byte_disk(offset+1,parametro_h);
    plus3dsk_put_byte_disk(offset+2,parametro_r);
    plus3dsk_put_byte_disk(offset+3,parametro_n);


}



//Devolver st1,2 de una pista y sector concreto
void dsk_get_st12(int pista,int cara,int sector_fisico,z80_byte *parametro_st1,z80_byte *parametro_st2)
{

    int offset=dsk_get_start_sector_info(pista,cara,sector_fisico);

    *parametro_st1=plus3dsk_get_byte_disk(offset+4);
    *parametro_st2=plus3dsk_get_byte_disk(offset+5);

}

//Escribir st1,2 de una pista y sector concreto
void dsk_put_st12(int pista,int cara,int sector_fisico,z80_byte parametro_st1,z80_byte parametro_st2)
{

    int offset=dsk_get_start_sector_info(pista,cara,sector_fisico);

    plus3dsk_put_byte_disk(offset+4,parametro_st1);
    plus3dsk_put_byte_disk(offset+5,parametro_st2);

}


//Devolver tamaño real de una pista y sector concreto, para tipo extendido

int dsk_get_real_sector_size_extended(int pista,int cara,int sector_fisico)
{

    int iniciopista=dsk_get_start_track(pista,cara);

    //printf("En dsk_get_st12 Inicio pista %d: %XH\n",pista,iniciopista);

    //saltar 0x18
    iniciopista +=0x18;


    int offset_tabla_sector=sector_fisico*8;
    //z80_byte pista_id=plus3dsk_get_byte_disk(iniciopista+offset_tabla_sector); //Leemos pista id
    //z80_byte sector_id=plus3dsk_get_byte_disk(iniciopista+offset_tabla_sector+2); //Leemos c1, c2, etc

    //debug_printf(VERBOSE_DEBUG,"%02X ",sector_id);


    int tamanyo=plus3dsk_get_byte_disk(iniciopista+offset_tabla_sector+6)+256*plus3dsk_get_byte_disk(iniciopista+offset_tabla_sector+7);

    return tamanyo;


}


int dsk_is_track_formatted(int pista,int cara)
{
    int sinformatear=0;

    if (dsk_file_type_extended) {
        int track_size=dsk_extended_get_track_size(pista,cara);
        if (!track_size) sinformatear=1;
    }

    if (sinformatear) return 0;

    return 1;
}
