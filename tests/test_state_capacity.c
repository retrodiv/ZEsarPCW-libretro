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

/* Exercise the actual wrapper and ZSF writer without an emulation session.
   Including the wrapper gives the fixture access to its private running flag;
   rename only its writer call so a probe can enforce the capacity contract and
   simulate a future larger payload. Unused frontend/engine sections are linked
   out, as in the audio unit test; no test hooks or exports enter the core. */
#define pcw_zsf_save capacity_probe_save
#include "libretro.c"
#undef pcw_zsf_save
#include "pcw_zsf.c"

#include <assert.h>

/* State consumed by the real serializers. Execution and device I/O are tested
   separately through libretro_host, using the full engine. */
z80_byte current_machine_type;
int pcw_total_ram, pcw_video_mode, pcw_mode1_palette, pcw_libretro_program_set_palette;
int pcw_rgb_table_15_bits[16];
z80_byte p3dsk_buffer_disco[DSK_MAX_BUFFER_DISCO];
int p3dsk_buffer_disco_size;
z80_registro registro_hl, registro_de, registro_bc;
z80_byte reg_a, reg_i, reg_r, reg_r_bit7, reg_a_shadow;
z80_byte reg_b_shadow, reg_c_shadow, reg_d_shadow, reg_e_shadow, reg_h_shadow, reg_l_shadow;
z80_byte Z80_FLAGS, Z80_FLAGS_SHADOW, im_mode;
z80_int reg_ix, reg_iy, reg_sp, reg_pc;
z80_bit iff1, z80_halt_signal, ay_chip_present;
z80_byte pcw_bank_registers[4], pcw_port_f4_value, pcw_port_f5_value;
z80_byte pcw_port_f6_value, pcw_port_f7_value, pcw_port_f8_value;
z80_byte *pcw_ram_mem_table[32];
int total_ay_chips, ay_chip_selected;
z80_byte ay_3_8912_registro_sel[MAX_AY_CHIPS], ay_3_8912_registros[MAX_AY_CHIPS][16];

int pcw_disk_get_index(void) { return 0; }
bool pcw_disk_get_ejected(void) { return false; }

/* Size-only fixture for the device block; real device state is exercised by
   the full-engine regression host. Keep enough bytes to test the new overhead. */
bool pcw_fdc_state(struct pcw_state_io *s)
{
    uint8_t buffer[9000] = {0};
    pcw_state_bytes(s, buffer, sizeof buffer);
    return s->ok;
}
bool pcw_machine_state(struct pcw_state_io *s)
{
    uint8_t buffer[512] = {0};
    pcw_state_bytes(s, buffer, sizeof buffer);
    return s->ok;
}

bool pcw_boot_state(struct pcw_state_io *s)
{
    uint8_t buffer[32] = {0};
    pcw_state_bytes(s, buffer, sizeof buffer);
    return s->ok;
}

static size_t expected_capacity;
static unsigned probe_mode, probe_calls;

bool capacity_probe_save(uint8_t *out, size_t capacity, size_t *length)
{
    probe_calls++;
    if (capacity != expected_capacity) {
        fprintf(stderr, "FAIL: writer received %zu bytes, expected %zu available bytes\n",
                capacity, expected_capacity);
        return false;
    }
    if (probe_mode == 1) {
        /* A future format uses every available byte; trailing data must fit. */
        memset(out, 0x69, capacity);
        *length = capacity;
        return true;
    }
    if (probe_mode == 2) return false; /* writer reports insufficient capacity */
    if (probe_mode == 3) {
        *length = capacity + 1; /* wrapper must also reject a broken writer */
        return true;
    }
    return pcw_zsf_save(out, capacity, length);
}

static uint8_t *guarded_buffer(size_t capacity)
{
    uint8_t *allocation = malloc(capacity + 32);
    assert(allocation);
    memset(allocation, 0xa5, capacity + 32);
    return allocation;
}

static void check_guards(const uint8_t *allocation, size_t capacity)
{
    for (size_t i = 0; i < 16; i++) {
        assert(allocation[i] == 0xa5);
        assert(allocation[16 + capacity + i] == 0xa5);
    }
}

static void writer_boundary(const uint8_t *reference, size_t used, size_t capacity)
{
    uint8_t *allocation = guarded_buffer(capacity);
    size_t length = 0;
    bool saved = pcw_zsf_save(allocation + 16, capacity, &length);
    assert(saved == (capacity >= used));
    check_guards(allocation, capacity);
    if (saved) {
        assert(length == used);
        assert(!memcmp(reference, allocation + 16, used));
    }
    free(allocation);
}

static void check_writer(void)
{
    size_t bound = (size_t)pcw_total_ram + 16384, used = 0;
    uint8_t *reference = malloc(bound);
    assert(reference && pcw_zsf_save(reference, bound, &used));
    assert(used <= bound);
    writer_boundary(reference, used, 0);
    writer_boundary(reference, used, 1);
    size_t offset = sizeof("ZSF ZEsarUX Snapshot File.") - 1;
    writer_boundary(reference, used, offset - 1);
    writer_boundary(reference, used, offset);
    /* Every actual block boundary, including all RAM banks and AY chips. */
    while (offset < used) {
        size_t end = offset + 6 + rd32(reference + offset + 2);
        writer_boundary(reference, used, offset + 5);
        writer_boundary(reference, used, offset + 6);
        writer_boundary(reference, used, end - 1);
        writer_boundary(reference, used, end);
        offset = end;
    }
    writer_boundary(reference, used, used + 1);
    free(reference);
}

static void check_wrapper(void)
{
    const int disks[] = { 0, 1, DSK_MAX_BUFFER_DISCO };
    for (unsigned disk = 0; disk < sizeof disks / sizeof disks[0]; disk++) {
        p3dsk_buffer_disco_size = disks[disk];
        for (unsigned extra = 0; extra < 2; extra++) {
            size_t capacity = retro_serialize_size() + (extra ? 37 : 0);
            expected_capacity = capacity - 12 - 76 - 12 - (size_t)disks[disk];
            for (probe_mode = 0; probe_mode < 4; probe_mode++) {
                uint8_t *allocation = guarded_buffer(capacity), *state = allocation + 16;
                unsigned old_calls = probe_calls;
                bool saved = retro_serialize(state, capacity);
                assert(probe_calls == old_calls + 1);
                assert(saved == (probe_mode < 2));
                check_guards(allocation, capacity);
                if (saved) {
                    size_t zlen = zpcw_rd32(state + 4);
                    size_t disk_at = 12 + zlen + 76;
                    size_t used = disk_at + 12 + (size_t)disks[disk];
                    assert(zlen <= expected_capacity && used <= capacity);
                    if (probe_mode == 1) assert(used == capacity);
                    assert(zpcw_rd32(state + disk_at + 8) == (uint32_t)disks[disk]);
                    assert(!memcmp(state + disk_at + 12, p3dsk_buffer_disco, (size_t)disks[disk]));
                    for (size_t i = used; i < capacity; i++) assert(state[i] == 0);
                }
                free(allocation);
            }
        }
    }
}

int main(void)
{
    static uint8_t ram[512 * 1024];
    core_running = 1;
    ay_chip_present.v = 1;
    total_ay_chips = MAX_AY_CHIPS;
    for (unsigned page = 0; page < 32; page++) pcw_ram_mem_table[page] = ram + page * 16384;
    for (size_t i = 0; i < sizeof p3dsk_buffer_disco; i++) p3dsk_buffer_disco[i] = (uint8_t)i;
    for (unsigned model = 0; model < 2; model++) {
        current_machine_type = model ? MACHINE_ID_PCW_8512 : MACHINE_ID_PCW_8256;
        pcw_total_ram = model ? 512 * 1024 : 256 * 1024;
        for (unsigned randomize = 0; randomize < 2; randomize++) {
            uint32_t random = 0x9e3779b9u;
            for (size_t i = 0; i < sizeof ram; i++) {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                ram[i] = randomize ? (uint8_t)random : 0;
            }
            pcw_state_rle_reset_cache();
            check_writer();
            check_wrapper();
        }
    }
    pcw_state_rle_free_cache();
    puts("PASS: ZSF boundaries and real wrapper capacity for both models, compressed/raw RAM and maximum disc");
    return 0;
}
