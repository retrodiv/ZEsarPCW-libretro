/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GNU GPL v3; see LICENSE. */
/* Real controller transfers through the core's public state API. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"
#include "dsk.h"
#include "pd765.h"
#include "pcw.h"
#include "pcw_keyboard.h"
#include "pcw_boot.h"
#include "core_pcw.h"

extern int p3dsk_buffer_disco_size;
extern void pcw_cpm_begin_autorun_keys(const char *command);

static const char *model = "PCW8512 (512K)";
static bool environment(unsigned command, void *data)
{
    if (command == RETRO_ENVIRONMENT_GET_VARIABLE) {
        struct retro_variable *v = data;
        if (!strcmp(v->key, "zesarpcw_model")) { v->value = model; return true; }
    }
    return command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT;
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{ (void)data; (void)w; (void)h; (void)pitch; }
static size_t audio(const int16_t *data, size_t frames)
{ (void)data; return frames; }
static void poll(void) { }
static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id)
{ (void)port; (void)device; (void)index; (void)id; return 0; }

static const unsigned char read_command[] = { 0x46, 0, 0, 0, 1, 2, 1, 0x2a, 0xff };
static const unsigned char write_command[] = { 0x45, 0, 0, 0, 1, 2, 1, 0x2a, 0xff };

static void send(const unsigned char *data, unsigned count)
{ for (unsigned i = 0; i < count; i++) pd765_out_port_data_register(data[i]); }
static void receive(unsigned char *data, unsigned count)
{ for (unsigned i = 0; i < count; i++) data[i] = pd765_read(); }
static unsigned char pattern(unsigned i) { return (unsigned char)(i ^ 0x5a); }

static void boot_record(unsigned char record[32])
{
    struct pcw_state_io io = { NULL, record, 32, 0, false, true };
    assert(pcw_boot_state(&io) && io.offset == 32);
}

static void reach_boot_point(unsigned phase, unsigned cursor, unsigned received)
{
    for (unsigned i = 0; i < 2000000; ++i) {
        unsigned char record[32];
        boot_record(record);
        if (record[4] == phase && record[12] == cursor &&
            (record[16] | (unsigned)record[17] << 8) == received) return;
        cpu_core_loop_pcw();
    }
    unsigned char record[32];
    boot_record(record);
    fprintf(stderr, "boot wanted %u/%u/%u, got %u/%u/%u PC=%04x\n",
            phase, cursor, received, record[4], record[12],
            record[16] | (unsigned)record[17] << 8, reg_pc);
    assert(!"native bootstrap did not reach the expected controller phase");
}

static void check_boot_states(void *state, size_t size)
{
    /* Real uPD765, actual RAM banking and the complete public state API.
     * Pause before a command, inside its parameters, partway through sector
     * data, and while collecting the result. Every continuation must agree. */
    static const unsigned points[][3] = {
        { 2, 0, 0 }, { 3, 1, 0 }, { 9, 0, 73 }, { 10, 3, 512 }
    };
    void *expected = malloc(size), *actual = malloc(size);
    assert(expected && actual);
    for (unsigned point = 0; point < sizeof points / sizeof points[0]; ++point) {
        retro_reset();
        reach_boot_point(points[point][0], points[point][1], points[point][2]);
        assert(retro_serialize(state, size));
        reach_boot_point(0, 0, 512);
        assert(reg_pc == 0xf010 && reg_sp == 0xfff0 && !iff1.v && !iff2.v);
        for (unsigned bank = 0; bank < 4; ++bank)
            assert(pcw_bank_registers[bank] == 0x80 + bank);
        assert(!memcmp(memoria_spectrum + 0xf000,
                       p3dsk_buffer_disco + 0x200, 512));
        for (unsigned byte = 0; byte < 0x200; ++byte)
            assert(memoria_spectrum[byte] == 0);
        assert(retro_serialize(expected, size));
        retro_reset();
        assert(retro_unserialize(state, size));
        reach_boot_point(0, 0, 512);
        assert(retro_serialize(actual, size));
        assert(!memcmp(actual, expected, size));
    }
    /* A checksum error activates the bundled helper exactly once. */
    p3dsk_buffer_disco[0x200] ^= 1;
    retro_reset();
    reach_boot_point(0, 0, 512);
    assert(reg_pc == 0xf010);
    assert(!memcmp(memoria_spectrum + 0xf000,
                   p3dsk_buffer_disco + 0x200, 512));
    /* Soft boot also works after a program changes the memory map. */
    pcw_out_port_bank(0xf0, 0x84);
    pcw_out_port_f8(1);
    reach_boot_point(0, 0, 512);
    assert(reg_pc == 0xf010 && pcw_bank_registers[0] == 0x80);

    /* Failure of the helper itself must wait, without recursive substitution.
     * Restoring valid media and pressing Space starts a fresh attempt. */
    p3dsk_buffer_disco[0x200] ^= 1;
    pcw_out_port_f8(1);
    reach_boot_point(11, 0, 512);
    assert(reg_pc == 0);
    for (unsigned i = 0; i < 1000; ++i) cpu_core_loop_pcw();
    reach_boot_point(11, 0, 512);
    p3dsk_buffer_disco[0x200] ^= 1;
    pcw_keyboard_ascii_event(' ', true);
    cpu_core_loop_pcw();
    pcw_keyboard_ascii_event(' ', false);
    reach_boot_point(0, 0, 512);

    unsigned char before[32], malformed[32], after[32];
    boot_record(before);
    /* Reject a command cursor outside its packet and impossible byte counts
     * before/during transfer: resuming those could write past the boot sector.
     * Even an applying decode must leave the running bootstrap unchanged. */
    static const unsigned invalid[][3] = {{3, 9, 0}, {8, 0, 512}, {9, 0, 512}};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        memcpy(malformed, before, sizeof malformed);
        malformed[4] = invalid[i][0];
        malformed[12] = invalid[i][1];
        malformed[16] = invalid[i][2] & 255;
        malformed[17] = invalid[i][2] >> 8;
        struct pcw_state_io io = { malformed, NULL, 32, 0, true, true };
        assert(!pcw_boot_state(&io));
        boot_record(after);
        assert(!memcmp(before, after, 32));
    }
    free(actual); free(expected);
    puts("PASS: native boot, banks, checksum fallback, soft boot and in-flight states");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    struct retro_game_info game = { argv[1], NULL, 0, NULL };
    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample_batch(audio);
    retro_set_input_poll(poll);
    retro_set_input_state(input);
    retro_init();
    assert(retro_load_game(&game));
    size_t size = retro_serialize_size();
    void *state = malloc(size);
    assert(state);
    check_boot_states(state, size);
    free(state);
    retro_unload_game();
    model = "PCW8256 (256K)";
    assert(retro_load_game(&game));
    assert(pcw_total_ram == 256 * 1024);
    size = retro_serialize_size();
    state = malloc(size);
    assert(state);
    check_boot_states(state, size);
    pd765_reset();
    pd765_motor_on();

    /* Save between command parameters, then between bytes of its data phase. */
    send(read_command, 4);
    assert(retro_serialize(state, size));
    pd765_reset();
    assert(retro_unserialize(state, size));
    send(read_command + 4, sizeof read_command - 4);
    unsigned char prefix[73], expected[512-73+7], actual[sizeof expected];
    receive(prefix, sizeof prefix);
    assert(!memcmp(prefix, p3dsk_buffer_disco + 0x200, sizeof prefix));
    assert(retro_serialize(state, size));
    receive(expected, sizeof expected);
    pd765_reset();
    assert(retro_unserialize(state, size));
    receive(actual, sizeof actual);
    assert(!memcmp(actual, expected, sizeof actual));

    /* A restored partially written sector must reproduce all data and status,
       including the prefix that has not yet reached the image buffer. */
    pd765_reset(); pd765_motor_on();
    send(write_command, sizeof write_command);
    for (unsigned i = 0; i < 101; i++) pd765_out_port_data_register(pattern(i));
    assert(retro_serialize(state, size));
    for (unsigned i = 101; i < 512; i++) pd765_out_port_data_register(pattern(i));
    unsigned char write_status[7], restored_status[7];
    receive(write_status, sizeof write_status);
    for (unsigned i = 0; i < 512; i++)
        assert(p3dsk_buffer_disco[0x200+i] == pattern(i));
    size_t disk_size = (size_t)p3dsk_buffer_disco_size;
    void *disk = malloc(disk_size);
    assert(disk);
    memcpy(disk, p3dsk_buffer_disco, disk_size);
    pd765_reset();
    assert(retro_unserialize(state, size));
    for (unsigned i = 101; i < 512; i++) pd765_out_port_data_register(pattern(i));
    receive(restored_status, sizeof restored_status);
    assert(!memcmp(disk, p3dsk_buffer_disco, disk_size));
    assert(!memcmp(write_status, restored_status, sizeof write_status));

    /* A pending SEEK keeps its timer and trusted callback after restoration. */
    pd765_reset(); pd765_motor_on();
    pd765_next_event_from_core();
    const unsigned char seek[] = { 0x0f, 0, 4 };
    send(seek, sizeof seek);
    assert(signal_se.running && pd765_pcn == 0);
    assert(retro_serialize(state, size));
    t_estados += 160;
    pd765_next_event_from_core();
    assert(pd765_pcn == 4);
    pd765_reset();
    assert(retro_unserialize(state, size));
    assert(signal_se.running && pd765_pcn == 0);
    t_estados += 160;
    pd765_next_event_from_core();
    assert(pd765_pcn == 4);

    /* Rewinding an auto-typed key restores its release, without rewinding
       physical keys the user is currently holding. */
    unsigned row_a = 0, mask_a = 0, row_b = 0, mask_b = 0;
    for (int i = 0; i < pcw_keyboard_target_count(); i++) {
        if (!strcmp(pcw_keyboard_target_label(i), "A"))
            assert(pcw_keyboard_target_matrix(i, &row_a, &mask_a));
        if (!strcmp(pcw_keyboard_target_label(i), "B"))
            assert(pcw_keyboard_target_matrix(i, &row_b, &mask_b));
    }
    assert(mask_a && mask_b);
    pcw_keyboard_release_all();
    pcw_cpm_begin_autorun_keys("a");
    for (int i = 0; i < 3; i++) pcw_keyboard_ticker_update();
    assert(!(pcw_keyboard_table[row_a] & mask_a));
    assert(retro_serialize(state, size));
    for (int i = 0; i < 3; i++) pcw_keyboard_ticker_update();
    assert(pcw_keyboard_table[row_a] & mask_a);
    assert(pcw_keyboard_default_event(true, RETROK_b));
    assert(retro_unserialize(state, size));
    assert(!(pcw_keyboard_table[row_a] & mask_a));
    assert(!(pcw_keyboard_table[row_b] & mask_b));
    for (int i = 0; i < 3; i++) pcw_keyboard_ticker_update();
    assert(pcw_keyboard_table[row_a] & mask_a);
    assert(!(pcw_keyboard_table[row_b] & mask_b));
    assert(pcw_keyboard_default_event(false, RETROK_b));
    assert(pcw_keyboard_table[row_b] & mask_b);

    free(disk); free(state);
    retro_unload_game(); retro_deinit();
    puts("PASS: states during command parameters, sector read/write timed SEEK and autorun keys");
    return 0;
}
