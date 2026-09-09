/* SPDX-License-Identifier: GPL-3.0-only */
/* ZEsarPCW -- Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GNU GPL v3; see LICENSE. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"
#include "pcw_debug.h"
#include "pcw_log.h"
#include "cpu.h"
#include "scrlibretro.h"

enum fault { NONE, RAM, VIDEO, RESET, FRAME, FRAME_END };
static enum fault injected;
static unsigned shutdowns, videos, audios, instructions, errors, warnings, infos;
static bool logging, in_cpu, resumed_after_panic;
static const struct retro_disk_control_ext_callback *disk_control;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void __real_reset_cpu(void);
void __real_cpu_core_loop_pcw(void);

void *__wrap_malloc(size_t size)
{
    if (injected == RAM && size == 2u*1024u*1024u) {
        injected = NONE;
        return NULL;
    }
    return __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    if (injected == VIDEO && count == 720u*256u && size == sizeof(uint32_t)) {
        injected = NONE;
        return NULL;
    }
    return __real_calloc(count, size);
}

void __wrap_reset_cpu(void)
{
    if (injected == RESET) {
        injected = NONE;
        cpu_panic("injected reset failure");
    }
    __real_reset_cpu();
}

void __wrap_cpu_core_loop_pcw(void)
{
    instructions++;
    in_cpu = true;
    if (injected == FRAME && instructions == 5) {
        injected = NONE;
        (void)devuelve_reg_offset(8); /* the real decoder panic, before NULL use */
        resumed_after_panic = true;
    }
    __real_cpu_core_loop_pcw();
    if (injected == FRAME_END && scrlibretro_frame_ready) {
        injected = NONE;
        cpu_panic("injected failure after frame audio was prepared");
    }
    in_cpu = false;
}

static void RETRO_CALLCONV frontend_log(enum retro_log_level level, const char *format, ...)
{
    char message[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    size_t n = strlen(message);
    assert(n && message[n-1] == '\n');
    if (level == RETRO_LOG_ERROR) errors++;
    if (level == RETRO_LOG_WARN) warnings++;
    if (level == RETRO_LOG_INFO) infos++;
    fprintf(stderr, "[frontend-log:%d] %s", level, message);
}

static bool environment(unsigned cmd, void *data)
{
    if (cmd == RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE) {
        disk_control = data;
        return true;
    }
    if (cmd == RETRO_ENVIRONMENT_GET_LOG_INTERFACE && logging) {
        ((struct retro_log_callback *)data)->log = frontend_log;
        return true;
    }
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) return true;
    if (cmd == RETRO_ENVIRONMENT_SHUTDOWN) {
        shutdowns++;
        return false; /* even a frontend declining shutdown must remain safe */
    }
    return false;
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{ (void)data; (void)w; (void)h; (void)pitch; assert(!in_cpu); videos++; }
static size_t audio(const int16_t *data, size_t frames)
{ (void)data; assert(!in_cpu); audios++; return frames; }
static void poll_input(void) { }
static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id)
{ (void)port; (void)device; (void)index; (void)id; return 0; }

static void run_frame(void) { retro_run(); in_cpu = false; }

static void retry(const struct retro_game_info *game)
{
    assert(retro_load_game(game));
    unsigned v = videos, a = audios;
    run_frame();
    assert(videos == v+1 && audios == a+1);
    retro_unload_game();
    assert(!retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
    assert(!memoria_spectrum && !scrlibretro_framebuffer);
}

static void reject_short_disks(const struct retro_game_info *game, const char *path)
{
    unsigned char header[256];
    const size_t sizes[] = { 0, 1, 7, 8, 0x33, sizeof header - 1 };
    FILE *file = fopen(game->path, "rb");
    assert(file && fread(header, 1, sizeof header, file) == sizeof header);
    assert(fclose(file) == 0);
    struct retro_game_info bad = { path, NULL, 0, NULL };
    assert(disk_control);

    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        file = fopen(path, "wb");
        assert(file && fwrite(header, 1, sizes[i], file) == sizes[i]);
        assert(fclose(file) == 0);

        retry(game); /* leave a valid disk's header in the reusable buffer */
        unsigned previous_errors = errors;
        assert(!retro_load_game(&bad));
        assert(!memoria_spectrum && !scrlibretro_framebuffer);
        assert(retro_serialize_size() == 0);
        if (logging) assert(errors > previous_errors);

        assert(retro_load_game(game));
        assert(disk_control->set_eject_state(true));
        assert(disk_control->replace_image_index(0, &bad));
        assert(!disk_control->set_eject_state(false));
        assert(disk_control->get_eject_state());
        assert(disk_control->replace_image_index(0, game));
        assert(disk_control->set_eject_state(false));
        run_frame();
        retro_unload_game();
    }
    retry(game); /* invalid content must not prevent the next valid load */
}

int main(int argc, char **argv)
{
    assert(argc == 6);
    struct retro_game_info game = { argv[1], NULL, 0, NULL };
    pcw_log(RETRO_LOG_WARN, "[test] logging before environment is available");
    for (unsigned mode = 0; mode < 2; mode++) {
        logging = mode != 0;
        errors = warnings = infos = 0;
        retro_set_environment(environment);
        retro_set_video_refresh(video);
        retro_set_audio_sample_batch(audio);
        retro_set_input_poll(poll_input);
        retro_set_input_state(input);
        retro_init();

        reject_short_disks(&game, argv[5]);

        enum fault load_faults[] = { RAM, VIDEO, RESET };
        for (unsigned i = 0; i < sizeof load_faults / sizeof load_faults[0]; i++) {
            unsigned previous_shutdowns = shutdowns;
            injected = load_faults[i];
            assert(!retro_load_game(&game) && injected == NONE);
            assert(!memoria_spectrum && !scrlibretro_framebuffer);
            assert(!retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
            assert(retro_serialize_size() == 0 && shutdowns == previous_shutdowns);
            retry(&game);
        }
        for (unsigned i = 2; i < 4; i++) {
            struct retro_game_info bad = { argv[i], NULL, 0, NULL };
            unsigned previous_errors = errors;
            assert(!retro_load_game(&bad));
            assert(!memoria_spectrum && !scrlibretro_framebuffer);
            if (logging) assert(errors > previous_errors);
            retry(&game);
        }
        struct retro_game_info playlist = { argv[4], NULL, 0, NULL };
        retry(&playlist); /* unreadable entry warning, followed by a valid image */

        enum fault runtime_faults[] = { FRAME, FRAME_END, RESET };
        for (unsigned i = 0; i < sizeof runtime_faults / sizeof runtime_faults[0]; i++) {
            assert(retro_load_game(&game));
            void *ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
            size_t size = retro_serialize_size();
            void *state = malloc(size);
            assert(state && retro_serialize(state, size));
            unsigned v = videos, a = audios, previous_shutdowns = shutdowns;
            instructions = 0;
            injected = runtime_faults[i];
            if (injected == RESET) retro_reset(); else run_frame();
            assert(injected == NONE && !resumed_after_panic);
            assert(shutdowns == previous_shutdowns+1 && videos == v && audios == a);
            unsigned stopped_at = instructions;
            run_frame(); retro_reset(); run_frame();
            assert(instructions == stopped_at && shutdowns == previous_shutdowns+1);
            assert(retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM) == ram);
            assert(retro_serialize_size() == size);
            assert(!retro_serialize(state, size) && !retro_unserialize(state, size));
            free(state);
            retro_unload_game();
            assert(!memoria_spectrum && !scrlibretro_framebuffer);
            retry(&game);
        }
        if (logging) assert(errors && warnings && infos);
        else assert(!errors && !warnings && !infos);
        retro_deinit();
        unsigned old_errors = errors;
        pcw_log(RETRO_LOG_ERROR, "[test] logging after deinit uses stderr");
        assert(errors == old_errors);
        /* Init again without set_environment must reacquire the logger. */
        retro_init();
        retry(&game);
        retro_deinit();
        printf("PASS: failures, retry and logging (%s)\n", logging ? "frontend" : "stderr");
    }
    return 0;
}
