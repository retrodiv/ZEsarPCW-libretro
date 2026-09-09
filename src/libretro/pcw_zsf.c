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

/*
 * Minimal ZSF reader/writer for the PCW libretro core.
 * Based on ZEsarUX src/snap/snap_zsf.c, Copyright (C) 2013 Cesar Hernandez
 * Bano, GNU GPL version 3 or later. PCW subset rewritten by retrodiv;
 * modification record dated 2026-09-07: see licenses/MODIFICATIONS.md.
 *
 * The standalone emulator's generic snapshot unit contains readers and writers
 * for every machine ZEsarUX supports.  Calling its two top-level functions kept
 * that entire dependency graph alive in an otherwise PCW-only binary.
 * This unit reads and writes only the ZSF blocks emitted by this core.
 */
#include "pcw_boot.h"
#include "pcw_zsf.h"

#include <string.h>

#include "operaciones.h"
#include "machines/pcw.h"
#include "soundchips/ay38912.h"
#include "pcw_state_rle.h"
#include "pcw_state_io.h"

#define ZSF_MACHINE_ID       1u
#define ZSF_Z80_REGS         2u
#define ZSF_AY_CHIP          7u
#define ZSF_Z80_HALT        43u
#define ZSF_PCW_CONF        57u
#define ZSF_PCW_RAM         58u
#define ZSF_PCW_RUNTIME  0x5043u
#define PCW_RUNTIME_CAPACITY 16384u

#define PCW_PAGE_SIZE    16384u
#define PCW_8256_PAGES      16u
#define PCW_8512_PAGES      32u

static const uint8_t zsf_magic[] = "ZSF ZEsarUX Snapshot File.";

struct zsf_writer {
    uint8_t *destination;
    size_t capacity;
    size_t length;
};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void wr32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static bool writer_copy(struct zsf_writer *writer, const void *source,
                        size_t length)
{
    if (length > writer->capacity - writer->length) return false;
    memcpy(writer->destination + writer->length, source, length);
    writer->length += length;
    return true;
}

static bool writer_block(struct zsf_writer *writer, uint16_t id,
                         const uint8_t *data, size_t length)
{
    uint8_t header[6];
    if (length > UINT32_MAX) return false;
    wr16(header, id);
    wr32(header + 2, (uint32_t)length);
    return writer_copy(writer, header, sizeof header) &&
           writer_copy(writer, data, length);
}

static void save_cpu_registers(uint8_t header[27])
{
    header[0] = reg_c;
    header[1] = reg_b;
    header[2] = reg_e;
    header[3] = reg_d;
    header[4] = reg_l;
    header[5] = reg_h;
    header[6] = get_flags();
    header[7] = reg_a;
    wr16(header + 8, reg_ix);
    wr16(header + 10, reg_iy);
    header[12] = reg_c_shadow;
    header[13] = reg_b_shadow;
    header[14] = reg_e_shadow;
    header[15] = reg_d_shadow;
    header[16] = reg_l_shadow;
    header[17] = reg_h_shadow;
    header[18] = get_flags_shadow();
    header[19] = reg_a_shadow;
    header[20] = (uint8_t)((reg_r & 127) | (reg_r_bit7 & 128));
    header[21] = reg_i;
    wr16(header + 22, reg_sp);
    wr16(header + 24, reg_pc);
    header[26] = (uint8_t)(iff1.v | (im_mode == 2 ? 2 : 0));
}

static void load_cpu_registers(const uint8_t header[27])
{
    reg_c = header[0];
    reg_b = header[1];
    reg_e = header[2];
    reg_d = header[3];
    reg_l = header[4];
    reg_h = header[5];
    store_flags(header[6]);
    reg_a = header[7];
    reg_ix = rd16(header + 8);
    reg_iy = rd16(header + 10);
    reg_c_shadow = header[12];
    reg_b_shadow = header[13];
    reg_e_shadow = header[14];
    reg_d_shadow = header[15];
    reg_l_shadow = header[16];
    reg_h_shadow = header[17];
    store_flags_shadow(header[18]);
    reg_a_shadow = header[19];
    reg_r = header[20];
    reg_r_bit7 = (uint8_t)(reg_r & 128);
    reg_i = header[21];
    reg_sp = rd16(header + 22);
    reg_pc = rd16(header + 24);
    im_mode = (uint8_t)(header[26] & 2);
    iff1.v = iff2.v = header[26] & 1;
}

static bool decode_page(const uint8_t *source, size_t source_length,
                        uint8_t *destination)
{
    size_t input = 0;
    size_t output = 0;
    while (input < source_length) {
        if (source[input] == 0xdd && input + 1 < source_length &&
            source[input + 1] == 0xdd) {
            size_t repetitions;
            if (source_length - input < 4) return false;
            repetitions = source[input + 3] ? source[input + 3] : 256u;
            if (repetitions > PCW_PAGE_SIZE - output) return false;
            if (destination)
                memset(destination + output, source[input + 2], repetitions);
            output += repetitions;
            input += 4;
        } else {
            if (output == PCW_PAGE_SIZE) return false;
            if (destination) destination[output] = source[input];
            output++;
            input++;
        }
    }
    return output == PCW_PAGE_SIZE;
}

static bool validate_ram_block(const uint8_t *data, size_t length,
                               unsigned pages)
{
    if (length < 6 || data[5] >= pages) return false;
    if (rd16(data + 3) != PCW_PAGE_SIZE) return false;
    if (data[0] & ~1u) return false;
    if (data[0] & 1u) return decode_page(data + 6, length - 6, NULL);
    return length == PCW_PAGE_SIZE + 6;
}

bool pcw_zsf_save(uint8_t *destination, size_t capacity, size_t *length)
{
    struct zsf_writer writer = { destination, capacity, 0 };
    uint8_t registers[27];
    uint8_t machine = (uint8_t)current_machine_type;
    uint8_t halt = (uint8_t)(z80_halt_signal.v & 1);
    uint8_t configuration[9];
    uint8_t page_block[PCW_PAGE_SIZE * 2 + 6];
    unsigned pages = MACHINE_IS_PCW_8512 ? PCW_8512_PAGES : PCW_8256_PAGES;
    unsigned page;

    if (!destination || !length || !MACHINE_IS_PCW) return false;
    if (!writer_copy(&writer, zsf_magic, sizeof zsf_magic - 1) ||
        !writer_block(&writer, ZSF_MACHINE_ID, &machine, 1)) return false;

    save_cpu_registers(registers);
    if (!writer_block(&writer, ZSF_Z80_REGS, registers, sizeof registers) ||
        !writer_block(&writer, ZSF_Z80_HALT, &halt, 1)) return false;

    memcpy(configuration, pcw_bank_registers, 4);
    configuration[4] = pcw_port_f4_value;
    configuration[5] = pcw_port_f5_value;
    configuration[6] = pcw_port_f6_value;
    configuration[7] = pcw_port_f7_value;
    configuration[8] = pcw_port_f8_value;
    if (!writer_block(&writer, ZSF_PCW_CONF, configuration,
                      sizeof configuration)) return false;

    for (page = 0; page < pages; page++) {
        int compressed;
        int encoded;
        page_block[0] = 0;
        wr16(page_block + 1, PCW_PAGE_SIZE);
        wr16(page_block + 3, PCW_PAGE_SIZE);
        page_block[5] = (uint8_t)page;
        encoded = pcw_state_rle_compress_page(pcw_ram_mem_table[page],
            page_block + 6, page, 0, &compressed);
        if (encoded < 0 || encoded > (int)(sizeof page_block - 6)) return false;
        if (compressed) page_block[0] = 1;
        if (!writer_block(&writer, ZSF_PCW_RAM, page_block,
                          (size_t)encoded + 6)) return false;
    }

    if (ay_chip_present.v) {
        int chip;
        if (total_ay_chips < 0 || total_ay_chips > MAX_AY_CHIPS) return false;
        for (chip = 0; chip < total_ay_chips; chip++) {
            uint8_t ay[19];
            ay[0] = (uint8_t)chip;
            ay[1] = (uint8_t)ay_chip_selected;
            ay[2] = ay_3_8912_registro_sel[chip];
            memcpy(ay + 3, ay_3_8912_registros[chip], 16);
            if (!writer_block(&writer, ZSF_AY_CHIP, ay, sizeof ay)) return false;
        }
    }

    uint8_t runtime[PCW_RUNTIME_CAPACITY];
    struct pcw_state_io io = { NULL, runtime, sizeof runtime, 0, false, true };
    if (!pcw_fdc_state(&io) || !pcw_machine_state(&io) || !pcw_boot_state(&io) ||
        !writer_block(&writer, ZSF_PCW_RUNTIME, runtime, io.offset)) return false;

    *length = writer.length;
    return true;
}

static bool validate_snapshot(const uint8_t *source, size_t length,
                              uint8_t *machine_out)
{
    size_t offset = sizeof zsf_magic - 1;
    uint8_t machine = 0;
    unsigned pages = 0;
    uint32_t seen_pages = 0;
    bool have_machine = false;
    bool have_registers = false;
    bool have_halt = false;
    bool have_configuration = false;
    bool have_runtime = false;

    if (!source || length < offset || memcmp(source, zsf_magic, offset))
        return false;
    while (offset < length) {
        uint16_t id;
        uint32_t block_length;
        const uint8_t *data;
        if (length - offset < 6) return false;
        id = rd16(source + offset);
        block_length = rd32(source + offset + 2);
        offset += 6;
        if (block_length > length - offset) return false;
        data = source + offset;
        offset += block_length;
        switch (id) {
        case ZSF_MACHINE_ID:
            if (block_length != 1 || have_machine ||
                (data[0] != MACHINE_ID_PCW_8256 &&
                 data[0] != MACHINE_ID_PCW_8512)) return false;
            machine = data[0];
            pages = machine == MACHINE_ID_PCW_8512 ?
                PCW_8512_PAGES : PCW_8256_PAGES;
            have_machine = true;
            break;
        case ZSF_Z80_REGS:
            if (block_length != 27 || have_registers) return false;
            have_registers = true;
            break;
        case ZSF_Z80_HALT:
            if (block_length != 1 || have_halt || (data[0] & ~1u)) return false;
            have_halt = true;
            break;
        case ZSF_PCW_CONF:
            if (block_length != 9 || have_configuration) return false;
            have_configuration = true;
            break;
        case ZSF_PCW_RAM:
            if (!have_machine || !validate_ram_block(data, block_length, pages) ||
                (seen_pages & (UINT32_C(1) << data[5]))) return false;
            seen_pages |= UINT32_C(1) << data[5];
            break;
        case ZSF_PCW_RUNTIME: {
            struct pcw_state_io io = { data, NULL, block_length, 0, false, true };
            if (have_runtime || !pcw_fdc_state(&io) || !pcw_machine_state(&io) || !pcw_boot_state(&io) ||
                io.offset != block_length) return false;
            have_runtime = true;
            break;
        }
        case ZSF_AY_CHIP:
            if (block_length != 19 || data[0] >= MAX_AY_CHIPS ||
                data[1] >= MAX_AY_CHIPS) return false;
            break;
        default:
            return false;
        }
    }
    if (!have_machine || !have_registers || !have_halt ||
        !have_configuration || !have_runtime || seen_pages !=
            (pages == 32 ? UINT32_MAX : (UINT32_C(1) << pages) - 1))
        return false;
    *machine_out = machine;
    return true;
}

bool pcw_zsf_load(const uint8_t *source, size_t length)
{
    size_t offset = sizeof zsf_magic - 1;
    uint8_t machine;
    const uint8_t *runtime = NULL;
    size_t runtime_length = 0;

    if (!validate_snapshot(source, length, &machine)) return false;
    /* The model is fixed until content is unloaded. Switching here would
       invalidate the frontend's RAM pointer and could grow its state buffer.
       Reject before restoring any CPU, port, RAM or peripheral state. */
    if (current_machine_type != machine) return false;

    while (offset < length) {
        uint16_t id = rd16(source + offset);
        uint32_t block_length = rd32(source + offset + 2);
        const uint8_t *data;
        offset += 6;
        data = source + offset;
        offset += block_length;
        switch (id) {
        case ZSF_Z80_REGS:
            load_cpu_registers(data);
            break;
        case ZSF_Z80_HALT:
            z80_halt_signal.v = data[0];
            break;
        case ZSF_PCW_CONF:
            memcpy(pcw_bank_registers, data, 4);
            pcw_port_f4_value = data[4];
            pcw_port_f5_value = data[5];
            pcw_port_f6_value = data[6];
            pcw_port_f7_value = data[7];
            pcw_port_f8_value = data[8];
            pcw_set_memory_pages();
            break;
        case ZSF_PCW_RAM:
            if (data[0] & 1)
                (void)decode_page(data + 6, block_length - 6,
                                  pcw_ram_mem_table[data[5]]);
            else memcpy(pcw_ram_mem_table[data[5]], data + 6, PCW_PAGE_SIZE);
            break;
        case ZSF_PCW_RUNTIME:
            runtime = data;
            runtime_length = block_length;
            break;
        case ZSF_AY_CHIP:
            ay_chip_present.v = 1;
            ay_chip_selected = data[1];
            if (total_ay_chips <= data[0]) total_ay_chips = data[0] + 1;
            ay_3_8912_registro_sel[data[0]] = data[2];
            memcpy(ay_3_8912_registros[data[0]], data + 3, 16);
            break;
        default:
            break;
        }
    }
    ay_establece_frecuencias_todos_canales();
    /* Restore after the legacy CPU block so IFF2 and all three interrupt
       modes retain their exact values. The entire input was validated first. */
    struct pcw_state_io io = { runtime, NULL, runtime_length, 0, true, true };
    (void)pcw_fdc_state(&io);
    (void)pcw_machine_state(&io);
    (void)pcw_boot_state(&io);
    pcw_state_rle_reset_cache();
    return true;
}
