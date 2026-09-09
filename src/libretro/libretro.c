/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    libretro entry points: drives the ZEsarUX Amstrad PCW emulation one video
    frame per retro_run(), bridges its scrlibretro / audiolibretro drivers to the
    frontend, maps RetroPad + keyboard to the PCW key matrix, and exposes the
    ZEsarUX "Settings -> Hardware" knobs as core options.

    The embedded PCW engine initializes the machine and renders one frame per
    call. This bridge owns its lifecycle, frontend callbacks and error handling.

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#include <string.h>
#include <inttypes.h>

#include "libretro.h"

/* ZEsarUX core headers (the Makefile adds -I. -Ivideo -Iaudio -Imachines ...). */
#include "pcw_engine.h"
#include "pcw_log.h"
#include "audio.h"          /* FRECUENCIA_SONIDO */
#include "scrlibretro.h"
#include "audiolibretro.h"
#include "pcw.h"            /* pcw_video_mode, pcw_total_ram, ... */
#include "core_pcw.h"       /* fixed PCW frame pump */
#include "dsk.h"            /* p3dsk_buffer_disco (mounted-disc block in states) */
#include "operaciones.h"    /* visualmem buffers owned by the embedded engine */
#include "pcw_keyb2joypad.h" /* per-game gamepad -> PCW-key mapping */
#include "pcw_keyboard.h"    /* canonical host-keyboard mapping */
#include "pcw_osk.h"         /* on-screen keyboard */
#include "pcw_state_rle.h"   /* rewind RAM encoder/cache lifecycle */
#include "pcw_zsf.h"
#include "pcw_disk.h"        /* disk control (multi-disc swap) */
#include "pcw_fdc_sound.h"   /* mechanical disc-drive sound */
#include "pcw_crop.h"        /* Smart-crop decision (pure, unit-tested) */

/* This recent environment command lets the core push a new *displayed*
   value for one of its own options back to the frontend, so the menu reflects a
   value the core (or the running program) chose -- used for the I/O-driven
   colour-mode two-way sync below. struct retro_variable {key,value}; value must
   be one of the strings declared for that key. Frontends that predate this
   command simply return false from environ_cb (the push is then a no-op). */
#ifndef RETRO_ENVIRONMENT_SET_VARIABLE
#define RETRO_ENVIRONMENT_SET_VARIABLE 70
#endif

/* ------------------------------------------------------------------------- *
 * libretro callbacks
 * ------------------------------------------------------------------------- */
static retro_environment_t   environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t    input_poll_cb;
static retro_input_state_t   input_state_cb;
static bool can_dupe;
static bool input_bitmasks;
static uint16_t pad_mask;
static const uint32_t *last_video_data;
static unsigned last_video_width, last_video_height;
static size_t last_video_pitch;

RETRO_API void retro_set_environment(retro_environment_t cb);
RETRO_API void retro_unload_game(void);
static void set_core_options(void);
static bool read_variables(void);
static bool apply_options(void);
static void apply_cheats(void);
static void set_input_descriptors(void);
static void reset_session_state(void);
static void release_all_game_keys(void);
static bool update_option_visibility(void);
RETRO_API void retro_cheat_reset(void);

/* Pushed by the libretro audio driver each time the emulator completes an audio
 * block (see audiolibretro.c). */
void libretro_audio_batch(const int16_t *data, unsigned frames)
{
    size_t done = 0;
    if (!audio_batch_cb) return;
    while (done < frames) {
        size_t n = audio_batch_cb(data + done * 2, frames - done);
        if (n == 0 || n > frames - done) break;
        done += n;
    }
}

/* ------------------------------------------------------------------------- *
 * core state
 * ------------------------------------------------------------------------- */
static bool core_running = false;
static bool engine_initialized = false;
static bool core_failed = false;
static unsigned frame_guard_failures = 0;
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
static char loaded_game_path[PATH_MAX];

static bool set_loaded_game_path(const char *path)
{
    size_t n;
    if (!path || !path[0]) return false;
    n = strlen(path);
    if (n >= sizeof loaded_game_path) {
        pcw_log(RETRO_LOG_ERROR, "[zesarux-pcw] content path is too long (%" PRIuMAX
                ", maximum %" PRIuMAX ")\n", (uintmax_t)n,
                (uintmax_t)(sizeof loaded_game_path - 1));
        return false;
    }
    memcpy(loaded_game_path, path, n + 1);
    return true;
}


/* Audio chip enables -- the exact globals ZEsarUX's "Audio" menu toggles.
   beeper_enabled also lives in audio.h (included); ay_chip_present is in
   soundchips/ay38912.h. Both are consulted live in the per-frame audio mix, so
   flipping them takes effect immediately. */
extern z80_bit ay_chip_present;
extern z80_bit beeper_enabled;

/* Set by the patched pcw.c once the program programs its own colour palette
   (beyond the 2 mono colours). When forcing a colour mode with Allow-I/O OFF, we
   load the mode's default palette only if the program did NOT set one -- so a
   monochrome game forced to colour (Livingstone) gets the default CGA, while a
   program that ships its own palette (Coliseum) keeps it. */
extern int pcw_libretro_program_set_palette;

/* Cached core-option values so we can detect changes and apply them live. */
static int  opt_video_mode   = 0;     /* 0 mono (default), 1 4col(CGA), 2 16col-packed, 3 16col-attr */
static int  opt_cga_palette  = 0;     /* 0..3 working-palette group (0 = the program's own / default) */
static int  opt_phosphor     = 0;     /* 0 green, 1 white, 2 amber (monochrome mode) */
static bool opt_allow_io     = true;  /* Allow videomode changes by I/O (game owns the mode) */
static bool opt_ay_chip      = true;  /* AY chip present (ay_chip_present); default on */
static bool opt_beeper       = true;  /* Beeper enabled (beeper_enabled); PCW default on */
static int  opt_audio_rate   = 15600; /* output sample rate: 15600 native (default) or 48000 */
static int  opt_crop         = 0;     /* crop: 0 off, 1 Smart (auto bbox), 2 Fixed 256x192 */

/* The output sample rate currently reported to the frontend. Non-static so the
   libretro audio driver (audiolibretro.c) can read it and resample the PCW's
   native 15600 Hz stream to match. */
int pcw_libretro_audio_rate  = 15600;
/* SET_SYSTEM_AV_INFO must NOT be pushed during retro_load_game (before the frontend
   has done its first get_system_av_info) -- doing so corrupts its A/V setup. Only
   allow the live re-publish once the initial load has completed. */
static bool av_rate_live_push = false;
static unsigned opt_osk_button = RETRO_DEVICE_ID_JOYPAD_SELECT;
static bool input_descriptors_dirty = true;
static unsigned controller_device = RETRO_DEVICE_JOYPAD;
static const struct retro_controller_description controller_types[] = {
    { "RetroPad", RETRO_DEVICE_JOYPAD },
    { "Custom Keyboard Bindings", RETRO_DEVICE_KEYBOARD },
};
static const struct retro_controller_info controller_ports[] = {
    { controller_types, sizeof controller_types / sizeof controller_types[0] },
    { NULL, 0 },
};
static int  osk_button_prev = 0;
static int  opt_osk_transp   = 0;     /* OSK overlay: 0 Solid-Full, 1 Transparent (keys only, blended), 2 Solid-Only keys */
static int  opt_osk_layout_us = 0;    /* OSK legends: 0 UK (default), 1 US */
static int  opt_osk_release_ms = 60;  /* on-screen keyboard: minimum tapped-key hold time (ms) */
static char opt_model[16]    = "PCW8512";

/* With "Allow videomode changes by I/O" = ON the running program owns
   pcw_video_mode; we remember the mode we last reflected into the option so we
   can tell a change the *program* made via I/O (push it to the menu) from one the
   *user* made in the menu (apply it). -1 = not yet established. */
static int  last_applied_mode = -1;

/* Frames after load (Allow-I/O ON) during which the core actively RE-SYNCS the
   frontend's saved colour-mode to the program's native one. A single SET_VARIABLE
   push at load is not enough: RetroArch applies the saved config slightly later and
   overwrites it, so the menu keeps showing the old saved value (e.g. "[1] 4-colour"
   on a reloaded monochrome game) even though the render is native. So we keep
   re-reading the frontend value and re-pushing native for a short window until it
   actually sticks (or the window ends). */
static int native_resync_frames = 0;

/* The persisted colour-mode the frontend offered at load that we must IGNORE (it
   differs from the program's native boot mode). With Allow-I/O ON the program owns
   the mode, so a saved colour-mode re-offered at load -- and again every time you
   merely OPEN Core Options (the core is PAUSED then, so this, not the live mode, is
   what the menu keys off) -- must not drive the mode: a reloaded monochrome game
   must keep showing the Phosphor row, not the saved 4-colour's CGA row. The moment
   you dial a DIFFERENT value it's a genuine edit, so the saved value stops being
   special for the rest of the session (set to -1). -1 = nothing to ignore. This is
   a STABLE flag (not "changed since last read"), so it survives the frontend
   reading the option twice per edit (update_display_cb + retro_run). */
static int ignore_saved_mode = -1;

/* The four colour-mode option values, indexed by internal pcw_video_mode. A
   single source of truth: set_core_options builds the option's value list from
   these, and the I/O two-way sync re-selects one via SET_VARIABLE -- the pushed
   string must be byte-identical to what was declared, so both paths use these.
   Ordered by internal mode number with Monochrome first; the "[WxH]" suffixes are
   ZEsarUX's native per-mode resolutions (pcw_video_mode_names). read_variables()
   accepts these exact declared values. */
static const char *const colour_mode_labels[4] = {
    "[0] Monochrome (native) [720x256]",
    "[1] 4-colour (CGA) [360x256]",
    "[2] 16-colour (packed) [180x256]",
    "[3] 16-colour (attribute) [360x256]",
};

/* CGA-palette option values, indexed by working-palette group (i.e. index ==
   pcw_mode1_palette). The 4-colour mode reads pcw_rgb_table_15_bits[i +
   pcw_mode1_palette*4]. A program that programs its own mode-1 colours writes them
   into group 0 via the I/O ports, so group 0 holds the program's own palette when
   it sets one (and the default Green/Red/Brown otherwise). With I/O ON the program
   owns the palette, so the core selects group 0 and reflects it in the menu (see
   retro_run); the other three presets are for when the mode is *forced* (I/O OFF).
   We label group 0 "Green/Red/Brown" (its default identity) rather than guessing
   "custom", since the core cannot reliably tell per-program whether the default
   palette was overwritten. */
static const char *const cga_palette_labels[4] = {
    "Green/Red/Brown",
    "Green/Red/Brown (bright)",
    "Cyan/Magenta/Grey",
    "Cyan/Magenta/White (bright)",
};

/* Read by the patched pcw_get_rgb_color_mode0(): selects the monochrome
   phosphor colour (0 green, 1 white, 2 amber). A real PCW monitor is a fixed
   1-bit on/off phosphor, so this overrides whatever palette the program last
   programmed -- which also makes "forced Monochrome" look right on games that
   natively run in a colour mode. ZEsarUX itself only offers green / white;
   amber is the libretro core's addition. */
int pcw_libretro_phosphor = 0;

/* ------------------------------------------------------------------------- *
 * boilerplate
 * ------------------------------------------------------------------------- */
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)      { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)        { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb)            { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb)          { input_state_cb = cb; }

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
    if (!info) return;
    memset(info, 0, sizeof(*info));
    info->library_name     = "ZEsarPCW";
    /* MAJOR.MINOR tracks the upstream ZEsarUX release this core derives from;
       the last digit is the port's own release on top of it. */
    info->library_version  = "13.0.1";
    info->valid_extensions = "dsk|m3u";   /* .m3u is parsed by the core (pcw_disk.c) */
    info->need_fullpath    = true;   /* we mount the .dsk by path */
    /* Let the frontend transparently handle archives (.zip): it opens the archive,
       finds the inner file matching a supported extension ("dsk"), extracts it to a
       temp path and hands us that path (need_fullpath). This is how disk/tape cores
       get .zip support for free; only romset cores like MAME set block_extract=true
       to receive the archive untouched. So a zipped .dsk just works -- no zip code
       here. */
    info->block_extract    = false;
}

/* ---- crop ("show only the game area") -------------------------------------
   With the crop option on, the core delivers only the bounding box of the game's
   non-paper content -- a smaller framebuffer at a smaller resolution -- instead
   of the full native raster (much of which is blank paper for games that do not
   fill the screen). The box is recomputed each frame but only committed once it
   has stayed stable for a few frames, so a settled screen gives a steady crop
   (no per-frame window resizing). */
static int crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;  /* committed crop; w==0 => not computed yet */
static uint32_t crop_last_paper = 1;
static int crop_last_W = -1, crop_last_H = -1;
static int crop_wf = 0, crop_wmnx = 0, crop_wmny = 0, crop_wmxx = 0, crop_wmxy = 0;
static pcw_crop_state crop_smart_state = { 0, 0, 0, 0, 0 };
static int geom_last_rw = 0, geom_last_rh = 0, geom_last_nw = 0, geom_last_nh = 0;

/* "Once full, stay full" latch (Smart crop). After the game has settled -- the
   first ~3 s from when it first shows the 256x192 view are ignored, since a game
   can flip resolution rapidly right after starting -- the first time it switches
   from 256x192 to the full raster, we latch to full and never crop back to 256x192.
   The 3 s clock starts at the first 256x192 view (not at reset), so it also works
   for titles that take a while to load (CP/M) -- the loader is full-raster text, so
   the clock does not start until the actual game screen appears. Reset per game. */
#define CROP_LATCH_GRACE 150            /* ~3 s at 50 fps */
static pcw_crop_latch crop_latch = { 0, 0, -1 };   /* "once full, stay full" (pcw_crop.c) */

static void crop_session_reset(void)    /* call on a new game / reset */
{
    crop_x = crop_y = crop_w = crop_h = 0;
    crop_last_paper = 1;
    crop_last_W = crop_last_H = -1;
    crop_wf = 0;
    crop_wmnx = crop_wmny = crop_wmxx = crop_wmxy = 0;
    memset(&crop_smart_state, 0, sizeof crop_smart_state);
    geom_last_rw = geom_last_rh = geom_last_nw = geom_last_nh = 0;
    pcw_crop_latch_reset(&crop_latch);
}

static void crop_update(int W, int H)
{
    if (opt_crop == 0) { crop_w = 0; return; }              /* off -> present full (see present_rect) */
    if (W <= 0 || H <= 0) { crop_w = 0; return; }

    if (opt_crop == 2) {
        /* Fixed crop = the 256x192 Spectrum game area these ports draw, centred in
           the raster. The WIDTH scales with the mode's horizontal resolution -- 256
           in the 360-wide colour modes (1 and 3), 512 in the 720-wide mono mode 0,
           128 in the 180-wide mode 2 -- so it is the same physical part of the screen
           in every mode. (The PCW raster is always 256 lines tall, so height is 192.) */
        const int REF_W = 360, BASE_W = 256, BASE_H = 192;
        int cw = (int)((long)W * BASE_W / REF_W);
        int ch = (H < BASE_H) ? H : BASE_H;
        if (cw > W) cw = W;
        if (cw < 1) cw = W;
        crop_x = (W - cw) / 2;
        crop_y = (H - ch) / 2;
        crop_w = cw; crop_h = ch;
        return;
    }

    /* opt_crop == 1 : Smart (auto-detect) -- crop to the game's drawn content. */
    const uint32_t *fb = scrlibretro_framebuffer;
    const int stride = scrlibretro_max_width;
    if (!fb) { crop_w = 0; return; }

    /* "paper" = the most common of the four corners (games fill the corners with
       their background colour). It also flags a screen change: when it (or the
       video mode) changes, we re-baseline the box to this frame's content. */
    uint32_t c[4] = { fb[0], fb[W-1], fb[(size_t)(H-1)*stride], fb[(size_t)(H-1)*stride + W-1] };
    uint32_t paper = c[0]; int best = 0, i, j, x, y;
    for (i = 0; i < 4; i++) { int n = 0; for (j = 0; j < 4; j++) if (c[j]==c[i]) n++; if (n>best){best=n;paper=c[i];} }

    int minx = W, miny = H, maxx = -1, maxy = -1;
    for (y = 0; y < H; y++) {
        const uint32_t *row = fb + (size_t)y * stride;
        for (x = 0; x < W; x++)
            if (row[x] != paper) {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
    }
    if (maxx < 0) return;   /* a blank frame -> keep the last crop */

    /* Smart picks between just TWO framings -- the central 256x192 game area or the
       full raster -- never adapting to the exact box. It crops to 256x192 when the
       drawn content fits inside it, and shows the full frame when it doesn't (a
       full-screen splash/cover/loader). The choice is made from the MAX content
       extent over a short window (~0.6s) so a moving sprite that briefly reaches the
       edge doesn't flip it every frame; a paper/mode change re-baselines at once. */
    enum { CROP_WINDOW = 30 };
    int screen_changed = (paper != crop_last_paper || W != crop_last_W || H != crop_last_H);
    crop_last_paper = paper; crop_last_W = W; crop_last_H = H;

    if (screen_changed) { crop_wf = 0; pcw_crop_state_reset(&crop_smart_state); }
    if (crop_wf == 0) { crop_wmnx = minx; crop_wmny = miny; crop_wmxx = maxx; crop_wmxy = maxy; }
    else {
        if (minx < crop_wmnx) crop_wmnx = minx;
        if (miny < crop_wmny) crop_wmny = miny;
        if (maxx > crop_wmxx) crop_wmxx = maxx;
        if (maxy > crop_wmxy) crop_wmxy = maxy;
    }
    crop_wf++;

    if (crop_wf >= CROP_WINDOW || screen_changed || crop_w == 0) {
        /* The sticky centred/slide/full decision lives in pcw_crop.c (unit-tested in
           tests/test_crop.c); we just feed it the windowed-max content bbox. */
        pcw_crop_smart_decide(W, H, crop_wmnx, crop_wmny, crop_wmxx, crop_wmxy,
                              &crop_smart_state);
        crop_x = crop_smart_state.x; crop_y = crop_smart_state.y;
        crop_w = crop_smart_state.w; crop_h = crop_smart_state.h;
        crop_wf = 0;
    }

    /* "Once full, stay full" latch (pcw_crop.c, unit-tested). Runs every frame;
       crop_x/y/w/h hold the current committed crop between window commits. */
    if (crop_w > 0) {
        int decided_full = (crop_w >= W && crop_h >= H);
        if (pcw_crop_latch_step(&crop_latch, decided_full, CROP_LATCH_GRACE)) {
            crop_x = 0; crop_y = 0; crop_w = W; crop_h = H;   /* force full raster */
        }
    }
}

/* The rectangle into the framebuffer to present this frame, plus the pixel-exact
   aspect ratio. Full native frame unless the crop is on, committed, fits the
   current mode, and the on-screen keyboard (which needs the whole frame) is up. */
static void present_rect(int W, int H, int *rx, int *ry, int *rw, int *rh, float *aspect)
{
    int cx = crop_x, cy = crop_y, cw = crop_w, ch = crop_h;
    /* Present the full frame when: crop off; not computed yet; the box is stale for
       the current mode (a just-happened mode switch); or the OSK (which needs the
       whole frame) is up. */
    if (opt_crop == 0 || cw <= 0 || cx + cw > W || cy + ch > H || pcw_osk_is_active()) {
        cx = cy = 0; cw = W; ch = H;
    }
    *rx = cx; *ry = cy; *rw = cw; *rh = ch;
    /* Keep every PCW pixel the same shape as in the un-cropped 4:3 frame: the full
       W x H shows at 4:3, so a cw x ch sub-rect shows at 4/3 * (cw*H)/(ch*W). */
    *aspect = (4.0f / 3.0f) * ((float)cw * (float)H) / ((float)ch * (float)W);
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
    if (!info) return;
    /* base_* is the area presented right now (the native mode, or the crop if on);
       max_* is the largest mode (the monochrome 720x256 grid) so the frontend
       reserves a big-enough texture. On a mode/crop change retro_run pushes the
       new geometry via SET_GEOMETRY. */
    int W = core_running ? scrlibretro_cur_width()  : scrlibretro_max_width;
    int H = core_running ? scrlibretro_cur_height() : scrlibretro_max_height;
    if (W <= 0) W = scrlibretro_max_width;
    if (H <= 0) H = scrlibretro_max_height;

    int rx, ry, rw, rh; float aspect;
    present_rect(W, H, &rx, &ry, &rw, &rh, &aspect);

    info->geometry.base_width   = rw;
    info->geometry.base_height  = rh;
    info->geometry.max_width    = scrlibretro_max_width;
    info->geometry.max_height   = scrlibretro_max_height;
    info->geometry.aspect_ratio = aspect;

    info->timing.fps         = 50.0;
    /* 48000 (default) or the PCW's native 15600 -- see the "Audio sample rate"
       core option. audiolibretro.c resamples the 15600 Hz stream to match. */
    info->timing.sample_rate = (double)pcw_libretro_audio_rate;
}

/* Push the current presented geometry to the frontend (called when the PCW video
   mode or the crop changes, so RetroArch re-derives the window/crop/aspect).
   Timing is unchanged, so SET_GEOMETRY (not the heavier SET_SYSTEM_AV_INFO). */
static void push_geometry(void)
{
    int W = scrlibretro_cur_width(), H = scrlibretro_cur_height();
    int rx, ry, rw, rh; float aspect;
    present_rect(W, H, &rx, &ry, &rw, &rh, &aspect);
    struct retro_game_geometry geom;
    geom.base_width   = rw;
    geom.base_height  = rh;
    geom.max_width    = scrlibretro_max_width;
    geom.max_height   = scrlibretro_max_height;
    geom.aspect_ratio = aspect;
    environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
}

/* ------------------------------------------------------------------------- *
 * core options ("Settings -> Hardware")
 *
 * Declared twice: as categorised options-v2 (with per-value info tooltips) when
 * the frontend supports it, and as the classic SET_VARIABLES list as a fallback.
 * Both expose the SAME keys + value strings, so the option-reading and two-way
 * colour-mode sync work identically whichever path the frontend takes.
 * ------------------------------------------------------------------------- */
static struct retro_core_option_v2_category v2_categories[] = {
    { "system", "System", "Which PCW model to emulate." },
    { "video",  "Video",  "Colour mode, palette and display look." },
    { "audio",  "Audio",  "The PCW's sound." },
    { "input",  "Input",  "Physical/virtual keyboard and gamepad mapping." },
    { NULL, NULL, NULL },
};

#define PCW_BASE_OPTION_COUNT       16
#define PCW_KEYBOARD_TARGETS_MAX    PCW_KEYBOARD_TARGET_COUNT
#define PCW_CORE_OPTION_CAPACITY    (PCW_BASE_OPTION_COUNT + PCW_KEYBOARD_TARGETS_MAX + 1)

/* Options-v2 structures contain 128 value slots each, so keeping both the
 * English and translated tables static used almost half a megabyte of BSS.
 * Libretro requires the frontend to copy the definitions during the
 * environment callback; build them only for that call and release them. */
static struct retro_core_option_v2_definition *v2_defs;
static char (*keyboard_option_desc)[64];
static char (*keyboard_default_label)[96];

struct osk_button_option {
    const char *value;
    unsigned id;
};

/* Every digital control exposed by the standard 16-button RetroPad. Select is
   first because it is the default and must remain the first v0 value too. */
static const struct osk_button_option osk_button_options[] = {
    { "Select",   RETRO_DEVICE_ID_JOYPAD_SELECT },
    { "Start",    RETRO_DEVICE_ID_JOYPAD_START },
    { "D-Pad Up", RETRO_DEVICE_ID_JOYPAD_UP },
    { "D-Pad Down", RETRO_DEVICE_ID_JOYPAD_DOWN },
    { "D-Pad Left", RETRO_DEVICE_ID_JOYPAD_LEFT },
    { "D-Pad Right", RETRO_DEVICE_ID_JOYPAD_RIGHT },
    { "B", RETRO_DEVICE_ID_JOYPAD_B },
    { "A", RETRO_DEVICE_ID_JOYPAD_A },
    { "Y", RETRO_DEVICE_ID_JOYPAD_Y },
    { "X", RETRO_DEVICE_ID_JOYPAD_X },
    { "L", RETRO_DEVICE_ID_JOYPAD_L },
    { "R", RETRO_DEVICE_ID_JOYPAD_R },
    { "L2", RETRO_DEVICE_ID_JOYPAD_L2 },
    { "R2", RETRO_DEVICE_ID_JOYPAD_R2 },
    { "L3", RETRO_DEVICE_ID_JOYPAD_L3 },
    { "R3", RETRO_DEVICE_ID_JOYPAD_R3 },
};
#define OSK_BUTTON_OPTION_COUNT ((int)(sizeof osk_button_options / sizeof osk_button_options[0]))

static unsigned osk_button_from_value(const char *value)
{
    int i;
    if (value)
        for (i = 0; i < OSK_BUTTON_OPTION_COUNT; i++)
            if (!strcmp(value, osk_button_options[i].value))
                return osk_button_options[i].id;
    return UINT32_MAX;
}

static void v2_def(int i, const char *key, const char *desc, const char *info,
                   const char *cat, const char *def)
{
    v2_defs[i].key = key; v2_defs[i].desc = desc; v2_defs[i].desc_categorized = NULL;
    v2_defs[i].info = info; v2_defs[i].info_categorized = NULL;
    v2_defs[i].category_key = cat; v2_defs[i].default_value = def;
}
static void v2_val(int i, int k, const char *value, const char *label)
{
    v2_defs[i].values[k].value = value; v2_defs[i].values[k].label = label;
}

/* Build the v2 definitions (run-time, so the colour-mode / CGA values can reuse
   the same label arrays the two-way sync pushes). Returns the option count. */
static void build_v2_defs(void)
{
    int i = 0, k;
    int target_count = pcw_keyboard_target_count();
    memset(v2_defs, 0, PCW_CORE_OPTION_CAPACITY * sizeof *v2_defs);
    if (target_count > PCW_KEYBOARD_TARGETS_MAX)
        target_count = PCW_KEYBOARD_TARGETS_MAX;
    v2_def(i, "zesarpcw_model", "Machine model",
           "PCW8256 has 256K of RAM, PCW8512 has 512K. Applies on restart.",
           "system", "PCW8512 (512K)");
    v2_val(i, 0, "PCW8512 (512K)", NULL); v2_val(i, 1, "PCW8256 (256K)", NULL); v2_val(i, 2, NULL, NULL); i++;

    v2_def(i, "zesarpcw_cpm_autolaunch", "CP/M auto-launch",
           "Some PCW titles are CP/M applications whose on-disc self-boot loader never "
           "completes under emulation (it stalls polling the floppy controller for a timing "
           "condition that is not reproduced). For a known set of these, boot CP/M and start "
           "the program automatically -- as you would from the CP/M prompt. Off: leave the disc "
           "to boot on its own (those titles will not load). Applies on disc load / restart.",
           "system", "enabled");
    v2_val(i, 0, "enabled", NULL); v2_val(i, 1, "disabled", NULL); v2_val(i, 2, NULL, NULL); i++;

    v2_def(i, "zesarpcw_allow_videomode_change", "Allow videomode changes by I/O",
           "On: the program owns the video mode. Off: your Colour mode is forced and persists.",
           "video", "enabled");
    v2_val(i, 0, "enabled", NULL); v2_val(i, 1, "disabled", NULL); v2_val(i, 2, NULL, NULL); i++;

    v2_def(i, "zesarpcw_colour_mode", "Colour mode",
           "The PCW video mode. The colour modes only look right under a program that drives them (or with Allow-I/O off).",
           "video", colour_mode_labels[0]);
    for (k = 0; k < 4; k++) v2_val(i, k, colour_mode_labels[k], NULL);
    v2_val(i, 4, NULL, NULL); i++;

    v2_def(i, "zesarpcw_phosphor", "Phosphor",
           "The monochrome display colour (mode 0).", "video", "Green");
    v2_val(i, 0, "Green", NULL); v2_val(i, 1, "White", NULL); v2_val(i, 2, "Amber", NULL); v2_val(i, 3, NULL, NULL); i++;

    v2_def(i, "zesarpcw_cga_palette", "CGA palette",
           "Which working-palette group the 4-colour mode shows.", "video", cga_palette_labels[0]);
    for (k = 0; k < 4; k++) v2_val(i, k, cga_palette_labels[k], NULL);
    v2_val(i, 4, NULL, NULL); i++;

    v2_def(i, "zesarpcw_crop", "Crop to game area",
           "Drop the blank paper border many games leave around the playfield; the "
           "internal resolution shrinks to the kept area (smaller, sharper, correct "
           "aspect). Fixed always keeps the central 256x192 game area (scaled per "
           "video mode). Smart keeps that 256x192 when the game's content fits inside "
           "it, but shows the full frame when it doesn't (a full-screen splash/cover). "
           "Off shows the full raster.",
           "video", "disabled");
    v2_val(i, 0, "disabled", NULL); v2_val(i, 1, "Smart (auto)", NULL);
    v2_val(i, 2, "Fixed 256x192", NULL); v2_val(i, 3, NULL, NULL); i++;

    v2_def(i, "zesarpcw_ay_chip", "AY sound chip (dk'tronics)",
           "The optional dk'tronics AY add-on (many PCW games use it for music).", "audio", "enabled");
    v2_val(i, 0, "enabled", NULL); v2_val(i, 1, "disabled", NULL); v2_val(i, 2, NULL, NULL); i++;

    v2_def(i, "zesarpcw_beeper", "Beeper",
           "The PCW's 1-bit internal speaker.", "audio", "enabled");
    v2_val(i, 0, "enabled", NULL); v2_val(i, 1, "disabled", NULL); v2_val(i, 2, NULL, NULL); i++;

    v2_def(i, "zesarpcw_fdc_sound", "Floppy drive sound",
           "Play the mechanical noise of the 3\" disc drive while loading -- the motor "
           "spin, the head read chatter and the head seek -- from real drive "
           "recordings. Purely cosmetic.",
           "audio", "disabled");
    v2_val(i, 0, "disabled", NULL); v2_val(i, 1, "enabled", NULL);
    v2_val(i, 2, NULL, NULL); i++;

    v2_def(i, "zesarpcw_audio_rate", "Audio sample rate",
           "Native outputs the PCW's raw 15600 Hz. 48000 Hz is the standard rate "
           "frontends expect; the core resamples to it so RetroArch does not have to "
           "rate-convert the odd 15600 Hz (the source of the slight speed wobble).",
           "audio", "15600 Hz (native)");
    v2_val(i, 0, "15600 Hz (native)", NULL); v2_val(i, 1, "48000 Hz", NULL); v2_val(i, 2, NULL, NULL); i++;

    v2_def(i, "zesarpcw_osk_button", "Virtual keyboard button",
           "Choose the RetroPad button that opens and closes the on-screen keyboard. "
           "That button is reserved for the keyboard and overrides any game action "
           "assigned to it. Select+Start additionally sends the active profile's R3 action.",
           "input", "Select");
    for (k = 0; k < OSK_BUTTON_OPTION_COUNT; k++)
        v2_val(i, k, osk_button_options[k].value, NULL);
    v2_val(i, OSK_BUTTON_OPTION_COUNT, NULL, NULL); i++;

    v2_def(i, "zesarpcw_osk_transparent", "Virtual keyboard overlay",
           "How the on-screen keyboard is drawn: Solid - Full (the whole panel); "
           "Solid - Only keys (just the keys, solid -- no background, AMSTRAD logo or "
           "divider, the game shows between them); or Transparent (the keys blended "
           "over the game).", "input", "Solid - Full");
    v2_val(i, 0, "Solid - Full", NULL); v2_val(i, 1, "Solid - Only keys", NULL);
    v2_val(i, 2, "Transparent", NULL); v2_val(i, 3, NULL, NULL); i++;

    v2_def(i, "zesarpcw_osk_layout", "Virtual keyboard layout",
           "Choose the physical key legends shown by the on-screen keyboard.",
           "input", "UK");
    v2_val(i, 0, "UK", NULL); v2_val(i, 1, "US", NULL);
    v2_val(i, 2, NULL, NULL); i++;

    v2_def(i, "zesarpcw_osk_release", "Virtual keyboard minimum key hold",
           "Minimum time used for a short tap. Holding the physical A, X or Y "
           "button keeps the emulated key down until that button is released.",
           "input", "60 ms");
    v2_val(i, 0, "60 ms", NULL); v2_val(i, 1, "100 ms", NULL);
    v2_val(i, 2, "300 ms", NULL); v2_val(i, 3, "500 ms", NULL);
    v2_val(i, 4, "1000 ms", NULL); v2_val(i, 5, NULL, NULL); i++;

    v2_def(i, "zesarpcw_keyboard_mapping", "Physical keyboard mapping",
           "Default preserves the core's original keyboard passthrough. Custom "
           "shows one row per physical PCW key and maps it to a UK ISO host key. "
           "The same host key may be assigned to more than one PCW key.",
           "input", "Default");
    v2_val(i, 0, "Default", NULL); v2_val(i, 1, "Custom", NULL);
    v2_val(i, 2, NULL, NULL); i++;

    for (int t = 0; t < target_count; t++, i++) {
        snprintf(keyboard_option_desc[t], sizeof keyboard_option_desc[t],
                 "PCW key: %s", pcw_keyboard_target_label(t));
        snprintf(keyboard_default_label[t], sizeof keyboard_default_label[t],
                 "Default — %s", pcw_keyboard_target_default_host_label(t));
        v2_def(i, pcw_keyboard_target_option_key(t), keyboard_option_desc[t],
               "Choose the UK ISO physical keyboard key that presses this PCW key. "
               "Default uses the core's suggested binding; Unmapped disables it. "
               "Duplicate host-key assignments are allowed.",
               "input", "Default");
        v2_val(i, 0, "Default", keyboard_default_label[t]);
        v2_val(i, 1, "Unmapped", NULL);
        for (k = 0; k < pcw_keyboard_host_key_count(); k++)
            v2_val(i, k + 2, pcw_keyboard_host_key_value(k),
                   pcw_keyboard_host_key_label(k));
        v2_val(i, pcw_keyboard_host_key_count() + 2, NULL, NULL);
    }

    v2_def(i, NULL, NULL, NULL, NULL, NULL);   /* terminator */
}

/* ---- Translated options (SET_CORE_OPTIONS_V2_INTL): es / fr / de ----------
   Same keys, values and defaults as the English table -- only desc / info /
   value labels differ. Each language is a text table + a value-label table,
   overlaid onto a copy of the English defs, so the tables can never drift
   structurally. */
struct v2_loc_text  { const char *key, *desc, *info; };
struct v2_loc_label { const char *value, *label; };

static const struct retro_core_option_v2_category v2_categories_es[] = {
    { "system", "Sistema", "Modelo de PCW a emular." },
    { "video",  "Vídeo",  "Modo de color, paleta y aspecto de la imagen." },
    { "audio",  "Audio",   "El sonido del PCW." },
    { "input",  "Entrada", "Mapeo de los teclados físico/virtual y del mando." },
    { NULL, NULL, NULL },
};

static const struct v2_loc_text v2_es_texts[] = {
    { "zesarpcw_model", "Modelo de máquina",
      "El PCW8256 tiene 256K de RAM; el PCW8512, 512K. Se aplica al reiniciar." },
    { "zesarpcw_cpm_autolaunch", "Autoarranque de CP/M",
      "Algunos títulos de PCW son aplicaciones CP/M cuyo cargador autoarrancable "
      "nunca termina bajo emulación (se queda esperando una condición de tiempos "
      "del controlador de disquete que no se reproduce). Para un conjunto conocido de "
      "estos títulos, arranca CP/M e inicia el programa automáticamente, como se "
      "haría desde el prompt de CP/M. Desactivado: el disco arranca por sí solo "
      "(esos títulos no cargarán). Se aplica al cargar el disco / reiniciar." },
    { "zesarpcw_allow_videomode_change", "Permitir cambios de modo de vídeo por E/S",
      "Activado: el programa controla el modo de vídeo. Desactivado: tu Modo de "
      "color se fuerza y persiste." },
    { "zesarpcw_colour_mode", "Modo de color",
      "El modo de vídeo del PCW. Los modos de color solo se ven bien con un "
      "programa que los use (o con E/S desactivada)." },
    { "zesarpcw_phosphor", "Fósforo",
      "El color de la pantalla monocroma (modo 0)." },
    { "zesarpcw_cga_palette", "Paleta CGA",
      "Qué grupo de paleta de trabajo muestra el modo de 4 colores." },
    { "zesarpcw_crop", "Recortar al área de juego",
      "Elimina el borde de papel en blanco que muchos juegos dejan alrededor del "
      "área de juego; la resolución interna se reduce al área conservada "
      "(más pequeña, más nítida, aspecto correcto). Fijo mantiene "
      "siempre el área central de juego de 256x192 (escalada según el modo "
      "de vídeo). Inteligente mantiene ese 256x192 cuando el contenido del juego "
      "cabe dentro, pero muestra el cuadro completo cuando no (una portada a pantalla "
      "completa). Desactivado muestra el raster completo." },
    { "zesarpcw_ay_chip", "Chip de sonido AY (dk'tronics)",
      "El accesorio AY opcional de dk'tronics (muchos juegos de PCW lo usan para la "
      "música)." },
    { "zesarpcw_beeper", "Altavoz interno (beeper)",
      "El altavoz interno de 1 bit del PCW." },
    { "zesarpcw_fdc_sound", "Sonido de la disquetera",
      "Reproduce el ruido mecánico de la unidad de 3\" durante la carga -- el "
      "giro del motor, el traqueteo de lectura y el desplazamiento del cabezal -- a "
      "partir de grabaciones de una unidad real. Puramente cosmético." },
    { "zesarpcw_audio_rate", "Frecuencia de muestreo de audio",
      "Nativo emite los 15600 Hz originales del PCW. 48000 Hz es la frecuencia "
      "estándar que esperan los frontends; el core remuestrea para que RetroArch "
      "no tenga que convertir los inusuales 15600 Hz (el origen del ligero temblor de "
      "velocidad)." },
    { "zesarpcw_osk_button", "Botón del teclado virtual",
      "Elige el botón del RetroPad que abre y cierra el teclado en pantalla. Ese "
      "botón queda reservado para el teclado e ignora cualquier acción del juego "
      "que tuviera asignada. Select+Start envía además la acción R3 del perfil." },
    { "zesarpcw_osk_transparent", "Panel del teclado virtual",
      "Cómo se dibuja el teclado en pantalla: Sólido - Completo (todo el "
      "panel); Sólido - Solo teclas (solo las teclas, sin fondo, logo AMSTRAD ni "
      "divisor: el juego se ve entre ellas); o Transparente (las teclas fundidas "
      "sobre el juego)." },
    { "zesarpcw_osk_layout", "Distribución del teclado virtual",
      "Elige las leyendas físicas UK o US mostradas en el teclado en pantalla." },
    { "zesarpcw_osk_release", "Retención mínima del teclado virtual",
      "Tiempo mínimo de una pulsación breve. Al mantener pulsado el botón físico "
      "A, X o Y, la tecla emulada permanece pulsada hasta soltar ese botón." },
    { "zesarpcw_keyboard_mapping", "Mapeo del teclado físico",
      "Predeterminado conserva el mapeo original del core. Personalizado muestra "
      "una opción por cada tecla física del PCW y permite asignarla a una tecla "
      "UK ISO. Se permiten asignaciones duplicadas." },
    { NULL, NULL, NULL },
};

static const struct v2_loc_label v2_es_value_labels[] = {
    { "Default", "Predeterminado" }, { "Custom", "Personalizado" },
    { "Unmapped", "Sin asignar" },
    { "enabled",  "Activado" },
    { "disabled", "Desactivado" },
    { "Green", "Verde" }, { "White", "Blanco" }, { "Amber", "Ámbar" },
    { "Smart (auto)", "Inteligente (auto)" },
    { "Fixed 256x192", "Fijo 256x192" },
    { "15600 Hz (native)", "15600 Hz (nativo)" },
    { "D-Pad Up", "Cruceta arriba" }, { "D-Pad Down", "Cruceta abajo" },
    { "D-Pad Left", "Cruceta izquierda" }, { "D-Pad Right", "Cruceta derecha" },
    { "Solid - Full", "Sólido - Completo" },
    { "Solid - Only keys", "Sólido - Solo teclas" },
    { "Transparent", "Transparente" },
    { "[0] Monochrome (native) [720x256]",   "[0] Monocromo (nativo) [720x256]" },
    { "[1] 4-colour (CGA) [360x256]",        "[1] 4 colores (CGA) [360x256]" },
    { "[2] 16-colour (packed) [180x256]",    "[2] 16 colores (empaquetado) [180x256]" },
    { "[3] 16-colour (attribute) [360x256]", "[3] 16 colores (atributos) [360x256]" },
    { "Green/Red/Brown",             "Verde/Rojo/Marrón" },
    { "Green/Red/Brown (bright)",    "Verde/Rojo/Marrón (brillante)" },
    { "Cyan/Magenta/Grey",           "Cian/Magenta/Gris" },
    { "Cyan/Magenta/White (bright)", "Cian/Magenta/Blanco (brillante)" },
    { NULL, NULL },
};

static const struct retro_core_option_v2_category v2_categories_fr[] = {
    { "system", "Système", "Modèle de PCW à émuler." },
    { "video",  "Vidéo",   "Mode couleur, palette et rendu de l'image." },
    { "audio",  "Audio",   "Le son du PCW." },
    { "input",  "Entrée",  "Mappage des claviers physique/virtuel et de la manette." },
    { NULL, NULL, NULL },
};

static const struct v2_loc_text v2_fr_texts[] = {
    { "zesarpcw_model", "Modèle de machine",
      "Le PCW8256 a 256 Ko de RAM ; le PCW8512, 512 Ko. Appliqué au redémarrage." },
    { "zesarpcw_cpm_autolaunch", "Lancement automatique de CP/M",
      "Certains titres PCW sont des applications CP/M dont le chargeur "
      "auto-amorçable ne se termine jamais sous émulation (il attend une condition "
      "de timing du contrôleur de disquette qui n'est pas reproduite). Pour un "
      "ensemble connu de ces titres, démarre CP/M et lance le programme "
      "automatiquement, comme depuis l'invite CP/M. Désactivé : le disque démarre "
      "seul (ces titres ne chargeront pas). Appliqué au chargement du disque / "
      "redémarrage." },
    { "zesarpcw_allow_videomode_change", "Autoriser les changements de mode vidéo par E/S",
      "Activé : le programme contrôle le mode vidéo. Désactivé : votre Mode "
      "couleur est forcé et persiste." },
    { "zesarpcw_colour_mode", "Mode couleur",
      "Le mode vidéo du PCW. Les modes couleur ne s'affichent correctement "
      "qu'avec un programme qui les utilise (ou avec E/S désactivé)." },
    { "zesarpcw_phosphor", "Phosphore",
      "La couleur de l'écran monochrome (mode 0)." },
    { "zesarpcw_cga_palette", "Palette CGA",
      "Le groupe de palette de travail affiché par le mode 4 couleurs." },
    { "zesarpcw_crop", "Recadrer sur la zone de jeu",
      "Supprime la bordure de papier vierge que beaucoup de jeux laissent autour "
      "de l'aire de jeu ; la résolution interne se réduit à la zone conservée "
      "(plus petite, plus nette, aspect correct). Fixe garde toujours la zone "
      "de jeu centrale de 256x192 (mise à l'échelle selon le mode vidéo). "
      "Intelligent garde ce 256x192 quand le contenu du jeu y tient, mais montre "
      "l'image entière sinon (une page de titre plein écran). Désactivé montre "
      "le raster complet." },
    { "zesarpcw_ay_chip", "Puce sonore AY (dk'tronics)",
      "L'extension AY optionnelle de dk'tronics (beaucoup de jeux PCW "
      "l'utilisent pour la musique)." },
    { "zesarpcw_beeper", "Haut-parleur interne (beeper)",
      "Le haut-parleur interne 1 bit du PCW." },
    { "zesarpcw_fdc_sound", "Son du lecteur de disquettes",
      "Joue le bruit mécanique du lecteur 3\" pendant le chargement -- rotation "
      "du moteur, cliquetis de lecture et déplacement de la tête -- à partir "
      "d'enregistrements d'un vrai lecteur. Purement cosmétique." },
    { "zesarpcw_audio_rate", "Fréquence d'échantillonnage audio",
      "Natif sort les 15600 Hz d'origine du PCW. 48000 Hz est la fréquence "
      "standard attendue par les frontends ; le core rééchantillonne pour que "
      "RetroArch n'ait pas à convertir les 15600 Hz inhabituels (la source du "
      "léger flottement de vitesse)." },
    { "zesarpcw_osk_button", "Bouton du clavier virtuel",
      "Choisit le bouton du RetroPad qui ouvre et ferme le clavier à l'écran. Ce "
      "bouton est réservé au clavier et remplace toute action du jeu qui lui est "
      "assignée. Select+Start envoie aussi l'action R3 du profil." },
    { "zesarpcw_osk_transparent", "Panneau du clavier virtuel",
      "Comment le clavier à l'écran est dessiné : Opaque - Complet (tout le "
      "panneau) ; Opaque - Touches seules (seulement les touches, sans fond, "
      "logo AMSTRAD ni séparateur : le jeu reste visible entre elles) ; ou "
      "Transparent (les touches fondues sur le jeu)." },
    { "zesarpcw_osk_layout", "Disposition du clavier virtuel",
      "Choisit les légendes physiques UK ou US affichées sur le clavier à l'écran." },
    { "zesarpcw_osk_release", "Maintien minimal du clavier virtuel",
      "Durée minimale d'un appui bref. Maintenir le bouton physique A, X ou Y "
      "garde la touche émulée enfoncée jusqu'au relâchement du bouton." },
    { "zesarpcw_keyboard_mapping", "Mappage du clavier physique",
      "Par défaut conserve le mappage original du cœur. Personnalisé affiche une "
      "option pour chaque touche physique du PCW et permet de l'assigner à une "
      "touche UK ISO. Les assignations en double sont autorisées." },
    { NULL, NULL, NULL },
};

static const struct v2_loc_label v2_fr_value_labels[] = {
    { "Default", "Par défaut" }, { "Custom", "Personnalisé" },
    { "Unmapped", "Non assignée" },
    { "enabled",  "Activé" },
    { "disabled", "Désactivé" },
    { "Green", "Vert" }, { "White", "Blanc" }, { "Amber", "Ambre" },
    { "Smart (auto)", "Intelligent (auto)" },
    { "Fixed 256x192", "Fixe 256x192" },
    { "15600 Hz (native)", "15600 Hz (natif)" },
    { "D-Pad Up", "Croix haut" }, { "D-Pad Down", "Croix bas" },
    { "D-Pad Left", "Croix gauche" }, { "D-Pad Right", "Croix droite" },
    { "Solid - Full", "Opaque - Complet" },
    { "Solid - Only keys", "Opaque - Touches seules" },
    { "Transparent", "Transparent" },
    { "[0] Monochrome (native) [720x256]",   "[0] Monochrome (natif) [720x256]" },
    { "[1] 4-colour (CGA) [360x256]",        "[1] 4 couleurs (CGA) [360x256]" },
    { "[2] 16-colour (packed) [180x256]",    "[2] 16 couleurs (compact) [180x256]" },
    { "[3] 16-colour (attribute) [360x256]", "[3] 16 couleurs (attributs) [360x256]" },
    { "Green/Red/Brown",             "Vert/Rouge/Marron" },
    { "Green/Red/Brown (bright)",    "Vert/Rouge/Marron (vif)" },
    { "Cyan/Magenta/Grey",           "Cyan/Magenta/Gris" },
    { "Cyan/Magenta/White (bright)", "Cyan/Magenta/Blanc (vif)" },
    { NULL, NULL },
};

static const struct retro_core_option_v2_category v2_categories_de[] = {
    { "system", "System",  "Welches PCW-Modell emuliert wird." },
    { "video",  "Video",   "Farbmodus, Palette und Bilddarstellung." },
    { "audio",  "Audio",   "Der Klang des PCW." },
    { "input",  "Eingabe", "Zuordnung der physischen/Bildschirmtastatur und des Gamepads." },
    { NULL, NULL, NULL },
};

static const struct v2_loc_text v2_de_texts[] = {
    { "zesarpcw_model", "Maschinenmodell",
      "Der PCW8256 hat 256K RAM, der PCW8512 512K. Wirkt nach Neustart." },
    { "zesarpcw_cpm_autolaunch", "CP/M-Autostart",
      "Einige PCW-Titel sind CP/M-Anwendungen, deren selbststartender "
      "Disketten-Lader unter Emulation nie fertig wird (er wartet auf eine "
      "Timing-Bedingung des Diskettencontrollers, die nicht reproduziert wird). "
      "Für eine bekannte Auswahl dieser Titel wird CP/M gestartet und das "
      "Programm automatisch aufgerufen -- wie von der CP/M-Eingabeaufforderung "
      "aus. Deaktiviert: die Diskette startet allein (diese Titel laden dann "
      "nicht). Wirkt beim Laden der Diskette / Neustart." },
    { "zesarpcw_allow_videomode_change", "Videomodus-Wechsel per E/A erlauben",
      "Aktiviert: das Programm bestimmt den Videomodus. Deaktiviert: dein "
      "Farbmodus wird erzwungen und bleibt bestehen." },
    { "zesarpcw_colour_mode", "Farbmodus",
      "Der Videomodus des PCW. Die Farbmodi sehen nur mit einem Programm "
      "richtig aus, das sie nutzt (oder mit deaktiviertem E/A)." },
    { "zesarpcw_phosphor", "Phosphor",
      "Die Farbe des Monochrom-Bildschirms (Modus 0)." },
    { "zesarpcw_cga_palette", "CGA-Palette",
      "Welche Arbeitspaletten-Gruppe der 4-Farben-Modus zeigt." },
    { "zesarpcw_crop", "Auf Spielbereich zuschneiden",
      "Entfernt den leeren Papierrand, den viele Spiele um das Spielfeld "
      "lassen; die interne Auflösung schrumpft auf den behaltenen Bereich "
      "(kleiner, schärfer, korrektes Seitenverhältnis). Fest behält immer den "
      "zentralen 256x192-Spielbereich (je Videomodus skaliert). Intelligent "
      "behält diese 256x192, wenn der Spielinhalt hineinpasst, zeigt sonst das "
      "ganze Bild (ein Vollbild-Titelbild). Deaktiviert zeigt das volle Raster." },
    { "zesarpcw_ay_chip", "AY-Soundchip (dk'tronics)",
      "Das optionale AY-Zusatzmodul von dk'tronics (viele PCW-Spiele nutzen es "
      "für Musik)." },
    { "zesarpcw_beeper", "Interner Lautsprecher (Beeper)",
      "Der interne 1-Bit-Lautsprecher des PCW." },
    { "zesarpcw_fdc_sound", "Laufwerksgeräusch",
      "Spielt das mechanische Geräusch des 3\"-Laufwerks beim Laden -- "
      "Motordrehung, Leseknattern und Kopfbewegung -- aus Aufnahmen eines "
      "echten Laufwerks. Rein kosmetisch." },
    { "zesarpcw_audio_rate", "Audio-Abtastrate",
      "Nativ gibt die originalen 15600 Hz des PCW aus. 48000 Hz ist die "
      "Standardrate der Frontends; der Core rechnet um, damit RetroArch die "
      "ungewöhnlichen 15600 Hz nicht wandeln muss (die Ursache des leichten "
      "Tempo-Schwankens)." },
    { "zesarpcw_osk_button", "Taste für die Bildschirmtastatur",
      "Wählt die RetroPad-Taste, die die Bildschirmtastatur öffnet und schließt. "
      "Diese Taste ist dafür reserviert und überschreibt jede Spielaktion. "
      "Select+Start sendet zusätzlich die R3-Aktion des Profils." },
    { "zesarpcw_osk_transparent", "Darstellung der Bildschirmtastatur",
      "Wie die Bildschirmtastatur gezeichnet wird: Deckend - Komplett (das "
      "ganze Panel); Deckend - Nur Tasten (nur die Tasten, ohne Hintergrund, "
      "AMSTRAD-Logo und Trennlinie: das Spiel bleibt dazwischen sichtbar); oder "
      "Transparent (die Tasten über das Spiel geblendet)." },
    { "zesarpcw_osk_layout", "Layout der Bildschirmtastatur",
      "Wählt die UK- oder US-Tastenbeschriftung der Bildschirmtastatur." },
    { "zesarpcw_osk_release", "Minimale Tastenhaltezeit",
      "Mindestdauer eines kurzen Tastendrucks. Wird die physische A-, X- oder "
      "Y-Taste gehalten, bleibt die emulierte Taste bis zum Loslassen gedrückt." },
    { "zesarpcw_keyboard_mapping", "Zuordnung der physischen Tastatur",
      "Standard behält die ursprüngliche Tastaturbelegung des Cores bei. "
      "Benutzerdefiniert zeigt eine Option für jede physische PCW-Taste und "
      "ordnet sie einer UK-ISO-Taste zu. Doppelte Zuordnungen sind erlaubt." },
    { NULL, NULL, NULL },
};

static const struct v2_loc_label v2_de_value_labels[] = {
    { "Default", "Standard" }, { "Custom", "Benutzerdefiniert" },
    { "Unmapped", "Nicht zugeordnet" },
    { "enabled",  "Aktiviert" },
    { "disabled", "Deaktiviert" },
    { "Green", "Grün" }, { "White", "Weiß" }, { "Amber", "Bernstein" },
    { "Smart (auto)", "Intelligent (auto)" },
    { "Fixed 256x192", "Fest 256x192" },
    { "15600 Hz (native)", "15600 Hz (nativ)" },
    { "D-Pad Up", "Steuerkreuz oben" }, { "D-Pad Down", "Steuerkreuz unten" },
    { "D-Pad Left", "Steuerkreuz links" }, { "D-Pad Right", "Steuerkreuz rechts" },
    { "Solid - Full", "Deckend - Komplett" },
    { "Solid - Only keys", "Deckend - Nur Tasten" },
    { "Transparent", "Transparent" },
    { "[0] Monochrome (native) [720x256]",   "[0] Monochrom (nativ) [720x256]" },
    { "[1] 4-colour (CGA) [360x256]",        "[1] 4 Farben (CGA) [360x256]" },
    { "[2] 16-colour (packed) [180x256]",    "[2] 16 Farben (gepackt) [180x256]" },
    { "[3] 16-colour (attribute) [360x256]", "[3] 16 Farben (Attribute) [360x256]" },
    { "Green/Red/Brown",             "Grün/Rot/Braun" },
    { "Green/Red/Brown (bright)",    "Grün/Rot/Braun (hell)" },
    { "Cyan/Magenta/Grey",           "Cyan/Magenta/Grau" },
    { "Cyan/Magenta/White (bright)", "Cyan/Magenta/Weiß (hell)" },
    { NULL, NULL },
};

/* Build whichever translation is active over a copy of the English defs. */
static void build_v2_defs_local(struct retro_core_option_v2_definition *defs,
                                const struct v2_loc_text *texts,
                                const struct v2_loc_label *labels)
{
    memcpy(defs, v2_defs, PCW_CORE_OPTION_CAPACITY * sizeof *defs);
    for (int i = 0; defs[i].key; i++) {
        for (int t = 0; texts[t].key; t++)
            if (!strcmp(defs[i].key, texts[t].key)) {
                defs[i].desc = texts[t].desc;
                defs[i].info = texts[t].info;
                break;
            }
        for (int k = 0; defs[i].values[k].value; k++)
            for (int t = 0; labels[t].value; t++)
                if (!strcmp(defs[i].values[k].value, labels[t].value)) {
                    /* Per-key Default values already carry the useful
                       "Default — <suggested host key>" label. Keep that detail;
                       the plain mapping-mode Default has no label and is localised. */
                    if (strcmp(labels[t].value, "Default") ||
                        !defs[i].values[k].label)
                        defs[i].values[k].label = labels[t].label;
                    break;
                }
    }
}

/* Try the categorised options-v2 API; returns true once v2 was negotiated.
   SET_CORE_OPTIONS_V2[_INTL] returning false means categories are unsupported,
   not that registration failed: the options are still registered. */
static bool set_core_options_v2(void)
{
    unsigned ver = 0;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &ver) || ver < 2)
        return false;
    v2_defs = calloc(PCW_CORE_OPTION_CAPACITY, sizeof *v2_defs);
    keyboard_option_desc = calloc(PCW_KEYBOARD_TARGETS_MAX,
                                  sizeof *keyboard_option_desc);
    keyboard_default_label = calloc(PCW_KEYBOARD_TARGETS_MAX,
                                    sizeof *keyboard_default_label);
    if (!v2_defs || !keyboard_option_desc || !keyboard_default_label) {
        free(v2_defs); free(keyboard_option_desc); free(keyboard_default_label);
        v2_defs = NULL; keyboard_option_desc = NULL; keyboard_default_label = NULL;
        return false;
    }
    build_v2_defs();
    struct retro_core_options_v2 v2 = { v2_categories, v2_defs };

    /* Frontend in a language we ship a translation for? Offer it (falls through
       to the plain English v2 call if the frontend lacks the _INTL environment). */
    unsigned lang = RETRO_LANGUAGE_ENGLISH;
    const struct retro_core_option_v2_category *loc_cats = NULL;
    const struct v2_loc_text  *loc_texts  = NULL;
    const struct v2_loc_label *loc_labels = NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &lang)) {
        switch (lang) {
        case RETRO_LANGUAGE_SPANISH:
            loc_cats = v2_categories_es; loc_texts = v2_es_texts;
            loc_labels = v2_es_value_labels; break;
        case RETRO_LANGUAGE_FRENCH:
            loc_cats = v2_categories_fr; loc_texts = v2_fr_texts;
            loc_labels = v2_fr_value_labels; break;
        case RETRO_LANGUAGE_GERMAN:
            loc_cats = v2_categories_de; loc_texts = v2_de_texts;
            loc_labels = v2_de_value_labels; break;
        }
    }
    if (loc_texts) {
        struct retro_core_option_v2_definition *defs_local =
            malloc(PCW_CORE_OPTION_CAPACITY * sizeof *defs_local);
        if (defs_local) {
            build_v2_defs_local(defs_local, loc_texts, loc_labels);
            struct retro_core_options_v2 v2_loc = {
                (struct retro_core_option_v2_category *)loc_cats, defs_local };
            struct retro_core_options_v2_intl intl = { &v2, &v2_loc };
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL, &intl);
            free(defs_local);
        } else {
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &v2);
        }
    } else {
        environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &v2);
    }
    free(v2_defs); free(keyboard_option_desc); free(keyboard_default_label);
    v2_defs = NULL; keyboard_option_desc = NULL; keyboard_default_label = NULL;
    return true;
}

static bool append_core_option_value(char *dst, size_t size, const char *value)
{
    size_t used = strlen(dst);
    size_t length = strlen(value);
    if (used >= size || size - used < 2 || length > size - used - 2)
        return false;
    dst[used] = '|';
    memcpy(dst + used + 1, value, length + 1);
    return true;
}

static void set_core_options(void)
{
    int i = 0;
    int target_count;
    if (set_core_options_v2()) return;   /* categorised + tooltips on modern frontends */

    /* Mirrors ZEsarUX's PCW "Display / Video" settings, wired to its variables:
       Colour mode -> pcw_video_mode, Allow videomode changes by I/O -> (re-assert
       toggle), CGA palette -> pcw_mode1_palette, Phosphor -> the monochrome colour.
       Everything except the machine model applies live (no restart) -- see retro_run
       / apply_options. */
    /* Colour-mode values are built from colour_mode_labels so the menu strings and
       the SET_VARIABLE two-way sync can never drift. Monochrome (mode 0) is first,
       so it is the default. */
    static char colour_mode_opt[256];
    snprintf(colour_mode_opt, sizeof colour_mode_opt, "Colour mode; %s|%s|%s|%s",
             colour_mode_labels[0], colour_mode_labels[1],
             colour_mode_labels[2], colour_mode_labels[3]);

    /* CGA palette likewise built from cga_palette_labels (Custom first = default)
       so the menu strings and the SET_VARIABLE push can never drift. */
    static char cga_palette_opt[256];
    snprintf(cga_palette_opt, sizeof cga_palette_opt, "CGA palette; %s|%s|%s|%s",
             cga_palette_labels[0], cga_palette_labels[1],
             cga_palette_labels[2], cga_palette_labels[3]);

    /* v0 requires the strings to remain alive after SET_VARIABLES returns, so
       both the descriptors and their dynamically assembled value lists are
       static (matching the v2 definitions above). */
    static struct retro_variable vars[PCW_CORE_OPTION_CAPACITY];
    /* The 105 current host-key values occupy 727 bytes including separators;
       leave ample growth room without reserving 2 KiB for every PCW key. */
    static char keyboard_mapping_options[PCW_KEYBOARD_TARGETS_MAX][1024];
    memset(vars, 0, sizeof vars);

    vars[i++] = (struct retro_variable){ "zesarpcw_model",
        "Machine model (needs restart); PCW8512 (512K)|PCW8256 (256K)" };
    vars[i++] = (struct retro_variable){ "zesarpcw_cpm_autolaunch",
        "CP/M auto-launch; enabled|disabled" };
    vars[i++] = (struct retro_variable){ "zesarpcw_allow_videomode_change",
        "Allow videomode changes by I/O; enabled|disabled" };
    vars[i++] = (struct retro_variable){ "zesarpcw_colour_mode", colour_mode_opt };
    vars[i++] = (struct retro_variable){ "zesarpcw_phosphor",
        "Phosphor; Green|White|Amber" };
    vars[i++] = (struct retro_variable){ "zesarpcw_cga_palette", cga_palette_opt };
    vars[i++] = (struct retro_variable){ "zesarpcw_crop",
        "Crop to game area; disabled|Smart (auto)|Fixed 256x192" };
    vars[i++] = (struct retro_variable){ "zesarpcw_ay_chip",
        "AY sound chip (dk'tronics); enabled|disabled" };
    vars[i++] = (struct retro_variable){ "zesarpcw_beeper",
        "Beeper; enabled|disabled" };
    vars[i++] = (struct retro_variable){ "zesarpcw_fdc_sound",
        "Floppy drive sound; disabled|enabled" };
    vars[i++] = (struct retro_variable){ "zesarpcw_audio_rate",
        "Audio sample rate; 15600 Hz (native)|48000 Hz" };
    vars[i++] = (struct retro_variable){ "zesarpcw_osk_button",
        "Virtual keyboard button; Select|Start|D-Pad Up|D-Pad Down|D-Pad Left|D-Pad Right|B|A|Y|X|L|R|L2|R2|L3|R3" };
    vars[i++] = (struct retro_variable){ "zesarpcw_osk_transparent",
        "Virtual keyboard overlay; Solid - Full|Solid - Only keys|Transparent" };
    vars[i++] = (struct retro_variable){ "zesarpcw_osk_layout",
        "Virtual keyboard layout; UK|US" };
    vars[i++] = (struct retro_variable){ "zesarpcw_osk_release",
        "Virtual keyboard minimum key hold; 60 ms|100 ms|300 ms|500 ms|1000 ms" };
    vars[i++] = (struct retro_variable){ "zesarpcw_keyboard_mapping",
        "Physical keyboard mapping; Default|Custom" };

    target_count = pcw_keyboard_target_count();
    if (target_count > PCW_KEYBOARD_TARGETS_MAX)
        target_count = PCW_KEYBOARD_TARGETS_MAX;
    for (int t = 0; t < target_count; t++) {
        int length = snprintf(keyboard_mapping_options[t], sizeof keyboard_mapping_options[t],
                              "PCW key: %s; Default|Unmapped", pcw_keyboard_target_label(t));
        bool fits = length >= 0 && (size_t)length < sizeof keyboard_mapping_options[t];
        for (int h = 0; fits && h < pcw_keyboard_host_key_count(); h++)
            fits = append_core_option_value(keyboard_mapping_options[t],
                                            sizeof keyboard_mapping_options[t],
                                            pcw_keyboard_host_key_value(h));
        if (!fits) {
            pcw_log(RETRO_LOG_ERROR, "[zesarux-pcw] core option value list exceeds its buffer: %s",
                    pcw_keyboard_target_option_key(t));
            return; /* Never register a partial menu or a truncated value. */
        }
        vars[i++] = (struct retro_variable){ pcw_keyboard_target_option_key(t),
                                             keyboard_mapping_options[t] };
    }
    vars[i] = (struct retro_variable){ NULL, NULL };
    environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)vars);
}

/* Phosphor is meaningful when mode 0 can render (Auto, where most PCW software
   is monochrome, or forced Monochrome); the CGA palette when mode 1 can render
   (Auto or forced 4-colour). Hide each otherwise, in the spirit of JOYCE. */
static int visibility_phosphor = -1;
static int visibility_cga = -1;
static int visibility_keyboard_mapping[PCW_KEYBOARD_TARGETS_MAX];

static bool set_option_visible(const char *key, bool visible, int *last)
{
    struct retro_core_option_display d;
    int value = visible ? 1 : 0;
    if (*last == value) return false;
    *last = value;
    d.key = key; d.visible = visible;
    environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &d);
    return true;
}

static bool update_option_visibility(void)
{
    bool changed = false;
    changed |= set_option_visible("zesarpcw_phosphor", opt_video_mode == 0,
                                  &visibility_phosphor);
    changed |= set_option_visible("zesarpcw_cga_palette", opt_video_mode == 1,
                                  &visibility_cga);
    for (int i = 0; i < pcw_keyboard_target_count() &&
                        i < PCW_KEYBOARD_TARGETS_MAX; i++)
        changed |= set_option_visible(pcw_keyboard_target_option_key(i),
                                      pcw_keyboard_custom_enabled(),
                                      &visibility_keyboard_mapping[i]);
    return changed;
}

/* Set while we are pushing one of our own option values to the frontend. Some
   frontends synchronously re-enter update_display_cb from inside SET_VARIABLE --
   sometimes BEFORE they commit the new value -- so a re-read there would see the
   stale (old) option value and clobber the state we are in the middle of
   reflecting (and, via apply_options, even fight the running program). While this
   is set, update_display_cb refreshes only the row visibility, from the
   already-correct cache, and does not re-read/apply. */
static bool pushing_variable = false;

/* Push a new *displayed* value for one of our options to the frontend (so the
   menu reflects something the running program chose). No-op on frontends that
   predate SET_VARIABLE. */
static void push_variable(const char *key, const char *value)
{
    struct retro_variable var;
    var.key = key; var.value = value;
    pushing_variable = true;
    environ_cb(RETRO_ENVIRONMENT_SET_VARIABLE, &var);
    pushing_variable = false;
}

static void push_colour_mode_to_frontend(int mode)
{
    if (mode < 0 || mode > 3) return;
    push_variable("zesarpcw_colour_mode", colour_mode_labels[mode]);
}

static void push_cga_palette_to_frontend(int idx)
{
    if (idx < 0 || idx > 3) return;
    push_variable("zesarpcw_cga_palette", cga_palette_labels[idx]);
}

/* The frontend calls this whenever an option changes (even while the options menu
   is open), so the Phosphor / CGA-palette rows appear/disappear as soon as the
   user changes Colour mode.

   It MUST apply, not just read: read_variables() consumes the change (it updates
   the option cache and reports it once). If this callback only read, the later
   GET_VARIABLE_UPDATE handler in retro_run would re-read and see no change, so
   apply_options() would never run -- which is exactly why Phosphor (and the CGA
   palette) used to need a restart to take effect, while Colour mode appeared to
   work only because retro_run re-asserts it for the "I/O = OFF" lock. Applying
   here makes every live option take effect immediately. */
static bool update_display_cb(void)
{
    bool visibility_changed = false;
    if (!pushing_variable && read_variables() && core_running)
        visibility_changed = apply_options();
    if (core_running && input_descriptors_dirty) set_input_descriptors();
    visibility_changed |= update_option_visibility();
    return visibility_changed;
}

/* Apply the current option cache to the live emulator. Returns true when applying
 * the options changed the visibility of any dependent Core Options row. */
static bool apply_options(void)
{
    /* Audio chips -- the ZEsarUX "AY Chip" / "Beeper" toggles. Live globals; the
       per-frame re-assert in retro_run keeps them set across the PCW boot's CPU
       reset (which clears ay_chip_present). */
    ay_chip_present.v = opt_ay_chip ? 1 : 0;
    beeper_enabled.v  = opt_beeper  ? 1 : 0;

    /* Output sample rate (48000 standard vs the native 15600). When it changes
       live, re-publish the av_info so the frontend re-opens audio at the new rate
       (audiolibretro.c resamples to match). At load the frontend reads av_info
       fresh, so only push the heavier SET_SYSTEM_AV_INFO while already running. */
    if (opt_audio_rate != pcw_libretro_audio_rate) {
        pcw_libretro_audio_rate = opt_audio_rate;
        /* Only re-publish once running for real -- never during the initial load
           (the frontend reads the fresh rate from its own get_system_av_info right
           after load; an early push here breaks audio AND video at boot). */
        if (core_running && av_rate_live_push) {
            struct retro_system_av_info av;
            retro_get_system_av_info(&av);
            environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
        }
    }

    pcw_mode1_palette     = opt_cga_palette;
    pcw_libretro_phosphor = opt_phosphor;   /* mode-0 colour (phosphor patch) */

    /* On-screen keyboard appearance / locale / minimum hold time. */
    pcw_osk_set_options(opt_osk_release_ms, opt_osk_transp, opt_osk_layout_us);

    /* Keep the I/O ports internally open so the game can program its mode-1
       palette during boot -- actually clearing pcw_allow_videomode_change makes
       the colour modes render black. "Allow videomode changes by I/O = OFF" is
       instead enforced by re-asserting the forced mode every frame in retro_run,
       which leaves the game's palette intact (matching how upstream looks when
       you lock the mode AFTER the game has set its palette). */
    pcw_allow_videomode_change.v = 1;

    /* Force the mode only when Allow-I/O is OFF (locked). With Allow ON the program
       owns it: retro_run mirrors the program's mode and applies your in-session
       changes (bidirectional), and retro_load_game starts at the program's native
       mode -- so a stale saved colour-mode never forces the mode at load. The
       default palette for a forced/seen colour mode is handled per-frame in
       retro_run (so it applies whether the mode is forced by Allow-OFF or chosen by
       you with Allow-ON). */
    if (!opt_allow_io)
        pcw_video_mode = opt_video_mode;

    /* Baseline the I/O sync measures program changes against. */
    last_applied_mode = pcw_video_mode;

    /* Keep the Phosphor / CGA-palette row visibility in step with the mode we just
       committed. apply_options is the single choke point every mode change goes
       through (initial load, user menu change, Allow-OFF force), so refreshing
       here guarantees the right row shows even when the frontend does not re-call
       update_display_cb -- e.g. a game booting straight into 4-colour would
       otherwise keep the start-up Monochrome default's Phosphor row. */
    bool visibility_changed = update_option_visibility();

    return visibility_changed;
}

/* Read the frontend's variables into the option cache. Returns true if any
 * value that can be applied live actually changed. */
/* Option values are protocol identifiers, even when labels are translated.
   Unknown values leave the current setting alone. */
static int choice_index(const char *value, const char *const *values, unsigned count)
{
    if (value)
        for (unsigned i = 0; i < count; i++)
            if (!strcmp(value, values[i])) return (int)i;
    return -1;
}

static int read_choice(const char *key, const char *const *values, unsigned count)
{
    struct retro_variable var = { key, NULL };
    if (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var)) return -1;
    return choice_index(var.value, values, count);
}

static int read_toggle(const char *key)
{
    static const char *const values[] = { "disabled", "enabled" };
    return read_choice(key, values, 2);
}

static bool read_variables(void)
{
    struct retro_variable var;
    bool changed = false;
    int v;
    v = read_choice("zesarpcw_keyboard_mapping", (const char *const[]){ "Default", "Custom" }, 2);
    if (v >= 0 && pcw_keyboard_set_custom_enabled(v != 0)) changed = true;

    for (int i = 0; i < pcw_keyboard_target_count(); i++) {
        var.key = pcw_keyboard_target_option_key(i); var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
            if (pcw_keyboard_set_binding_value(i, var.value)) changed = true;
    }

    v = read_choice("zesarpcw_colour_mode", colour_mode_labels, 4);
    if (v >= 0) {
        /* Allow-I/O ON: the program owns the mode. While the frontend keeps offering
           the saved colour-mode we flagged stale at load, do NOT let it drive the
           mode -- mirror the live mode so a reloaded mono game keeps showing
           Phosphor, not the stale 4-colour's CGA row. Dialling a DIFFERENT value is
           a genuine edit: honour it and retire the stale flag for this session.
           With Allow-I/O OFF the option is authoritative, so always take it. */
        if (opt_allow_io && core_running && ignore_saved_mode >= 0) {
            int native = (last_applied_mode >= 0) ? last_applied_mode : pcw_video_mode;
            if (v == ignore_saved_mode || v == native)
                v = native;               /* the stale saved value re-offered, OR our own
                                             native re-sync push reflected back -> mirror
                                             native and keep ignoring the saved value */
            else
                ignore_saved_mode = -1;   /* a genuine pick of a DIFFERENT mode -> honour it
                                             (the saved value stops being special) */
        }
        if (v != opt_video_mode) { opt_video_mode = v; changed = true; }
    }

    v = read_toggle("zesarpcw_allow_videomode_change");
    if (v >= 0 && (bool)v != opt_allow_io) { opt_allow_io = v != 0; changed = true; }

    v = read_choice("zesarpcw_phosphor", (const char *const[]){ "Green", "White", "Amber" }, 3);
    if (v >= 0 && v != opt_phosphor) { opt_phosphor = v; changed = true; }

    v = read_choice("zesarpcw_cga_palette", cga_palette_labels, 4);
    if (v >= 0 && v != opt_cga_palette) { opt_cga_palette = v; changed = true; }

    v = read_toggle("zesarpcw_ay_chip");
    if (v >= 0 && (bool)v != opt_ay_chip) { opt_ay_chip = v != 0; changed = true; }
    v = read_toggle("zesarpcw_beeper");
    if (v >= 0 && (bool)v != opt_beeper) { opt_beeper = v != 0; changed = true; }

    /* These toggles apply directly, without a video repaint. */
    v = read_toggle("zesarpcw_cpm_autolaunch");
    if (v >= 0) {
        extern int pcw_cpm_autolaunch_enabled;
        pcw_cpm_autolaunch_enabled = v;
    }
    v = read_toggle("zesarpcw_fdc_sound");
    if (v >= 0) pcw_fdc_sound_set_level(v ? 3 : 0);

    v = read_choice("zesarpcw_audio_rate", (const char *const[]){ "15600 Hz (native)", "48000 Hz" }, 2);
    if (v >= 0) {
        int rate = v ? 48000 : FRECUENCIA_SONIDO;
        if (rate != opt_audio_rate) { opt_audio_rate = rate; changed = true; }
    }
    v = read_choice("zesarpcw_crop", (const char *const[]){ "disabled", "Smart (auto)", "Fixed 256x192" }, 3);
    if (v >= 0) opt_crop = v;

    var.key = "zesarpcw_osk_button"; var.value = NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        unsigned button = osk_button_from_value(var.value);
        if (button != UINT32_MAX && button != opt_osk_button) {
            opt_osk_button = button;
            osk_button_prev = 0;
            input_descriptors_dirty = true;
            changed = true;
        }
    }
    v = read_choice("zesarpcw_osk_transparent",
                    (const char *const[]){ "Solid - Full", "Transparent", "Solid - Only keys" }, 3);
    if (v >= 0 && v != opt_osk_transp) { opt_osk_transp = v; changed = true; }
    v = read_choice("zesarpcw_osk_layout", (const char *const[]){ "UK", "US" }, 2);
    if (v >= 0 && v != opt_osk_layout_us) { opt_osk_layout_us = v; changed = true; }
    v = read_choice("zesarpcw_osk_release",
                    (const char *const[]){ "60 ms", "100 ms", "300 ms", "500 ms", "1000 ms" }, 5);
    if (v >= 0) {
        static const int milliseconds[] = { 60, 100, 300, 500, 1000 };
        if (milliseconds[v] != opt_osk_release_ms) {
            opt_osk_release_ms = milliseconds[v]; changed = true;
        }
    }
    v = read_choice("zesarpcw_model", (const char *const[]){ "PCW8512 (512K)", "PCW8256 (256K)" }, 2);
    if (v >= 0) strcpy(opt_model, v ? "PCW8256" : "PCW8512");
    return changed;
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;
    pcw_log_set_environment(cb);
    if (!cb) return;
    bool no_game = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)controller_ports);
    set_core_options();

    struct retro_core_options_update_display_callback ucb = { update_display_cb };
    cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK, &ucb);
    visibility_phosphor = visibility_cga = -1;
    for (int i = 0; i < PCW_KEYBOARD_TARGETS_MAX; i++)
        visibility_keyboard_mapping[i] = -1;
    update_option_visibility();
}

/* ------------------------------------------------------------------------- *
 * input: RetroPad + keyboard -> ZEsarUX universal key matrix
 *
 * Each RetroPad slot injects the PCW key bound to it by the active per-game
 * mapping (pcw_keyb2joypad.*), looked up from the disc fingerprint at load.
 * One configurable RetroPad button is reserved for toggling the OSK. R3 is a
 * normal per-game slot, and Select+Start aliases that same R3 action without
 * assuming what the emulated key means for a particular game.
 * ------------------------------------------------------------------------- */
static const unsigned slot_retro_id[PCW_PAD_NSLOTS] = {
    [PCW_PAD_UP]     = RETRO_DEVICE_ID_JOYPAD_UP,
    [PCW_PAD_DOWN]   = RETRO_DEVICE_ID_JOYPAD_DOWN,
    [PCW_PAD_LEFT]   = RETRO_DEVICE_ID_JOYPAD_LEFT,
    [PCW_PAD_RIGHT]  = RETRO_DEVICE_ID_JOYPAD_RIGHT,
    [PCW_PAD_B]      = RETRO_DEVICE_ID_JOYPAD_B,
    [PCW_PAD_A]      = RETRO_DEVICE_ID_JOYPAD_A,
    [PCW_PAD_Y]      = RETRO_DEVICE_ID_JOYPAD_Y,
    [PCW_PAD_X]      = RETRO_DEVICE_ID_JOYPAD_X,
    [PCW_PAD_L]      = RETRO_DEVICE_ID_JOYPAD_L,
    [PCW_PAD_R]      = RETRO_DEVICE_ID_JOYPAD_R,
    [PCW_PAD_L2]     = RETRO_DEVICE_ID_JOYPAD_L2,
    [PCW_PAD_R2]     = RETRO_DEVICE_ID_JOYPAD_R2,
    [PCW_PAD_START]  = RETRO_DEVICE_ID_JOYPAD_START,
    [PCW_PAD_SELECT] = RETRO_DEVICE_ID_JOYPAD_SELECT,
    [PCW_PAD_R3]     = RETRO_DEVICE_ID_JOYPAD_R3,
};
static int pad_prev[PCW_PAD_NSLOTS];

/* RetroPad button state, for the OSK's poll callback. */
static int pad_poll(int id)
{
    if (input_bitmasks)
        return id >= 0 && id < 16 && (pad_mask & (1u << id)) ? 1 : 0;
    return input_state_cb && input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, (unsigned)id) ? 1 : 0;
}

/* The configured toggle is reserved even while the OSK is open, so choosing A,
   B or a direction cannot type/navigate and close the OSK at the same time. */
static int osk_pad_poll(int id)
{
    return (unsigned)id == opt_osk_button ? 0 : pad_poll(id);
}

/* The left analog stick mirrors the d-pad: report a digital direction when the
   stick is pushed past half-travel, OR'd with the d-pad button. */
static int analog_dir(int slot)
{
    int ax, ay;
    if (slot != PCW_PAD_UP && slot != PCW_PAD_DOWN &&
        slot != PCW_PAD_LEFT && slot != PCW_PAD_RIGHT) return 0;
    ax = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
                        RETRO_DEVICE_ID_ANALOG_X);
    ay = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
                        RETRO_DEVICE_ID_ANALOG_Y);
    switch (slot) {
        case PCW_PAD_LEFT:  return ax < -0x4000;
        case PCW_PAD_RIGHT: return ax >  0x4000;
        case PCW_PAD_UP:    return ay < -0x4000;
        case PCW_PAD_DOWN:  return ay >  0x4000;
    }
    return 0;
}

/* Release any game keys still held (e.g. when the OSK takes over the pad). */
static void set_pad_slot_state(int slot, int down)
{
    if (down != pad_prev[slot]) {
        int uk = pcw_keyb2joypad_util_for_slot(slot);
        if (uk) pcw_keyboard_set_util_key(uk, down != 0);
        if (getenv("ZPCW_INPUTLOG"))
            pcw_log(RETRO_LOG_DEBUG, "[pcw-pad] slot %d -> '%s' (util %d) down=%d\n",
                    slot, pcw_keyb2joypad_label_for_slot(slot), uk, down);
        pad_prev[slot] = down;
    }
}

static void release_all_game_keys(void)
{
    int slot;
    for (slot = 0; slot < PCW_PAD_NSLOTS; slot++) {
        if (pad_prev[slot]) set_pad_slot_state(slot, 0);
    }
}

/* The OSK normally owns the whole pad. The explicit Select+Start alias is the
   sole exception: it must keep the profile's R3 action held even if Select has
   already opened the keyboard. */
static void release_game_keys_for_osk(int keep_r3)
{
    int slot;
    for (slot = 0; slot < PCW_PAD_NSLOTS; slot++) {
        if (slot == PCW_PAD_R3 && keep_r3) continue;
        if (pad_prev[slot]) set_pad_slot_state(slot, 0);
    }
}

/* Tell the frontend what each RetroPad button does in the active mapping, so its
   Controls / input-remap menu shows the bound PCW key instead of generic names.
   The label strings are static (token-name table / generic profile), so the
   descriptor array can hold them directly. */
static struct retro_input_descriptor input_desc[PCW_PAD_NSLOTS + 2];

static void set_input_descriptors(void)
{
    int n = 0, slot, osk_described = 0;
    if (controller_device == RETRO_DEVICE_NONE ||
        controller_device == RETRO_DEVICE_KEYBOARD) goto publish;
    for (slot = 0; slot < PCW_PAD_NSLOTS; slot++) {
        const char *label;
        if (slot_retro_id[slot] == opt_osk_button) {
            label = "On-screen keyboard";
            osk_described = 1;
        } else {
            label = pcw_keyb2joypad_label_for_slot(slot);
            if (!label || !label[0]) continue;       /* unbound -> omit */
        }
        input_desc[n].port = 0;
        input_desc[n].device = RETRO_DEVICE_JOYPAD;
        input_desc[n].index = 0;
        input_desc[n].id = slot_retro_id[slot];
        input_desc[n].description = label;
        n++;
    }
    /* L3 has no per-game slot yet, but it is still a valid OSK choice. */
    if (!osk_described) {
        input_desc[n].port = 0;
        input_desc[n].device = RETRO_DEVICE_JOYPAD;
        input_desc[n].index = 0;
        input_desc[n].id = opt_osk_button;
        input_desc[n].description = "On-screen keyboard";
        n++;
    }
publish:
    input_desc[n].port = input_desc[n].device = input_desc[n].index = 0;
    input_desc[n].id = 0; input_desc[n].description = NULL;   /* terminator */
    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, input_desc);
    input_descriptors_dirty = false;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
    if (port != 0 || (device != RETRO_DEVICE_NONE && device != RETRO_DEVICE_JOYPAD &&
                      device != RETRO_DEVICE_KEYBOARD)) return;
    if (device == controller_device) return;
    release_all_game_keys();
    pcw_osk_reset();
    /* Native remaps arrive as keyboard events, indistinguishable from physical
       keys. Clear their holds when changing that route so a later key-up cannot
       leave a key from the previous route pressed. */
    if (controller_device == RETRO_DEVICE_KEYBOARD || device == RETRO_DEVICE_KEYBOARD)
        pcw_keyboard_release_all();
    osk_button_prev = 0;
    controller_device = device;
    input_descriptors_dirty = true;
    if (environ_cb) {
        set_input_descriptors();
        update_option_visibility();
    }
}

static void update_input(void)
{
    int slot, active_before, active_now, just_opened;
    if (!input_poll_cb || !input_state_cb) return;
    input_poll_cb();
    if (input_bitmasks)
        pad_mask = (uint16_t)input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
                                          RETRO_DEVICE_ID_JOYPAD_MASK);
    /* With the keyboard device, RetroArch sends its remaps through the keyboard
       callback, exactly as for DOSBox's Custom Keyboard Bindings. Do not also
       apply the automatic pad profile to the same physical buttons. */
    if (controller_device == RETRO_DEVICE_NONE ||
        controller_device == RETRO_DEVICE_KEYBOARD) return;

    int sel   = pad_poll(RETRO_DEVICE_ID_JOYPAD_SELECT);
    int start = pad_poll(RETRO_DEVICE_ID_JOYPAD_START);
    int combo = sel && start;
    int osk_down = pad_poll((int)opt_osk_button);

    active_before = pcw_osk_is_active();
    if (osk_down && !osk_button_prev) {
        pcw_osk_toggle();
        if (getenv("ZPCW_INPUTLOG"))
            pcw_log(RETRO_LOG_DEBUG, "[pcw-pad] OSK toggle button id=%u active=%d\n",
                    opt_osk_button, pcw_osk_is_active());
    }
    osk_button_prev = osk_down;
    active_now = pcw_osk_is_active();
    just_opened = !active_before && active_now;

    /* Once open, the OSK owns ordinary pad input. On its opening frame we still
       allow already-pressed individual actions to last for one emulated frame;
       this preserves the requested "actions first, then chord" behaviour. */
    if (active_now && !just_opened) {
        release_game_keys_for_osk(combo);
        set_pad_slot_state(PCW_PAD_R3, combo);
        pcw_osk_handle_input(osk_pad_poll);
        return;
    }

    for (slot = 0; slot < PCW_PAD_NSLOTS; slot++) {
        int down = 0;
        if (slot_retro_id[slot] != opt_osk_button) {
            down = pad_poll((int)slot_retro_id[slot]);
            down |= analog_dir(slot);
        }
        if (slot == PCW_PAD_R3) down |= combo;
        set_pad_slot_state(slot, down);
    }

    if (active_now) pcw_osk_handle_input(osk_pad_poll);
}

/* Keyboard passthrough: canonical libretro keycodes -> ZEsarUX universal keys.
 * Event-based (the matrix persists until the matching key-up). */
static void keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t mods)
{
    (void)character; (void)mods;
    if (pcw_keyboard_custom_event(down, keycode)) return;
    if (controller_device == RETRO_DEVICE_KEYBOARD)
        pcw_keyboard_native_event(down, keycode);
    else
        pcw_keyboard_default_event(down, keycode);
}

static void reset_session_state(void)
{
    release_all_game_keys();
    pcw_osk_reset();
    pcw_keyboard_release_custom_keys();
    pcw_keyboard_release_all();
    pcw_keyboard_reset_custom_state();
    memset(pad_prev, 0, sizeof pad_prev);
    osk_button_prev = 0;
    input_descriptors_dirty = true;
    last_applied_mode = -1;
    native_resync_frames = 0;
    ignore_saved_mode = -1;
    crop_session_reset();
    pcw_fdc_sound_reset();
    retro_cheat_reset();
    frame_guard_failures = 0;
    pad_mask = 0;
    last_video_data = NULL;
    last_video_width = last_video_height = 0;
    last_video_pitch = 0;
    loaded_game_path[0] = 0;
}

/* Idempotent teardown also owns partially initialized machine resources. */
static void shutdown_embedded_engine(void)
{

    if (!engine_initialized) return;
    scrlibretro_end();

    free(memoria_spectrum); memoria_spectrum = NULL;
    audio_buffer = NULL;
    audio_buffer_indice = 0;
    audiolibretro_discard_frame();
    engine_initialized = false;
}

static void fail_running_session(void)
{
    core_failed = true;
    audiolibretro_discard_frame();
    pcw_log(RETRO_LOG_ERROR, "[zesarux-pcw] fatal emulation error: %s",
            pcw_engine_error());
    /* Keep exposed RAM alive until unload. If shutdown is declined, further
       run/reset/state calls stay inert; a new content load starts a new session. */
    if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

/* ------------------------------------------------------------------------- *
 * lifecycle
 * ------------------------------------------------------------------------- */
RETRO_API void retro_init(void)
{
    pcw_log_set_environment(environ_cb);
    can_dupe = false;
    input_bitmasks = false;
    if (environ_cb) {
        bool supported = false;
        can_dupe = environ_cb(RETRO_ENVIRONMENT_GET_CAN_DUPE, &supported) && supported;
        input_bitmasks = environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);
    }
    core_running = false;
    core_failed = false;
    reset_session_state();
    pcw_disk_reset();
    if (environ_cb) pcw_disk_register(environ_cb);
}

RETRO_API void retro_deinit(void)
{
    if (core_running) retro_unload_game();
    else {
        reset_session_state();
        pcw_disk_reset();
    }
    shutdown_embedded_engine();
    pcw_state_rle_free_cache();
    pcw_log_set_environment(NULL);
    controller_device = RETRO_DEVICE_JOYPAD;
}

RETRO_API bool retro_load_game(const struct retro_game_info *info)
{
    unsigned fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    bool content_is_m3u;
    const char *boot_path;

    if (!environ_cb || !info || !info->path || !info->path[0]) {
        pcw_log(RETRO_LOG_ERROR, "[zesarux-pcw] no content path given\n");
        return false;
    }
    if (core_running) retro_unload_game();

    reset_session_state();
    pcw_disk_begin_load();
    if (!set_loaded_game_path(info->path)) return false;

    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        pcw_log(RETRO_LOG_ERROR, "[zesarux-pcw] XRGB8888 not supported by frontend\n");
        loaded_game_path[0] = 0;
        return false;
    }

    /* Register the keyboard callback (host keyboard -> PCW keys). */
    struct retro_keyboard_callback kbcb = { keyboard_event };
    environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kbcb);

    /* Suppress the live av-info push for the duration of this load (re-enabled at
       the end) so a reload, like a first load, never pushes SET_SYSTEM_AV_INFO
       before the frontend reads av_info itself. */
    av_rate_live_push = false;

    /* Read machine model + initial options before bringing the machine up. */
    read_variables();

    /* .m3u playlist: the core parses it (the frontend never expands .m3u itself),
       seeds the full disc list, and boots from the initial disc -- the one the
       frontend asked for via set_initial_image ("continue where I left off"),
       else the first entry. */
    {
        size_t n = strlen(loaded_game_path);
        content_is_m3u = n > 4 &&
            (loaded_game_path[n-4]=='.') &&
            (loaded_game_path[n-3]=='m' || loaded_game_path[n-3]=='M') &&
            (loaded_game_path[n-2]=='3') &&
            (loaded_game_path[n-1]=='u' || loaded_game_path[n-1]=='U');
    }
    if (content_is_m3u) {
        if (!pcw_disk_load_m3u(loaded_game_path)) {
            pcw_log(RETRO_LOG_ERROR, "[zesarux-pcw] empty or unreadable .m3u: %s\n",
                    loaded_game_path);
            return false;
        }
        int first = pcw_disk_initial_index();
        pcw_disk_set_index_quiet(first);
        if (!set_loaded_game_path(pcw_disk_path(first))) {
            pcw_disk_reset();
            return false;
        }
        pcw_log(RETRO_LOG_INFO, "[zesarux-pcw] m3u: booting disc %d: %s\n",
                first, loaded_game_path);
    }
    else if (!pcw_disk_seed_first(loaded_game_path)) {
        loaded_game_path[0] = 0;
        return false;
    }
    boot_path = loaded_game_path;

    engine_initialized = true; /* teardown owns any partial allocations */
    current_machine_type = !strncmp(opt_model, "PCW8512", 7)
        ? MACHINE_ID_PCW_8512 : MACHINE_ID_PCW_8256;
    if (zesarpcw_libretro_init(current_machine_type) != 0) {
        pcw_log(RETRO_LOG_ERROR, "[zesarux-pcw] PCW engine init failed: %s",
                pcw_engine_error());
        shutdown_embedded_engine();
        pcw_disk_reset();
        reset_session_state();
        return false;
    }

    if (!pcw_disk_insert_current()) {
        pcw_log(RETRO_LOG_ERROR, "[zesarux-pcw] cannot mount content: %s\n", boot_path);
        shutdown_embedded_engine();
        pcw_disk_reset();
        reset_session_state();
        return false;
    }

    core_running = true;

    /* Choose the per-game gamepad -> PCW-key mapping for this disc (by .dsk sha1,
       then software-list shortname, else a generic profile). */
    pcw_keyb2joypad_select(loaded_game_path);

    /* Label each RetroPad button with the PCW key it sends, for the remap menu. */
    set_input_descriptors();

    /* Apply the initial hardware options on top of the freshly booted machine. */
    apply_options();

    /* With Allow-I/O ON, start at the program's NATIVE video mode, ignoring any
       saved colour-mode: entering a monochrome game after a colour game must show
       mono, not inherit the saved "16-colour" and look broken. The program then
       drives the mode (a colour game reflects its own; you can still override
       in-session). With Allow-I/O OFF the saved colour-mode is kept and forced
       (your override persists). */
    if (opt_allow_io) {
        /* opt_video_mode currently holds the SAVED colour-mode (from the load read).
           If it differs from the native boot mode, flag it as the stale value to
           ignore for this session -- so re-offering it (startup, or every menu open
           while the core is paused) never snaps the program's mode to it -- and open
           a window in retro_run to push the menu back to native until it sticks. */
        ignore_saved_mode = (opt_video_mode != pcw_video_mode) ? opt_video_mode : -1;
        native_resync_frames = (ignore_saved_mode >= 0) ? 120 : 0;
        opt_video_mode    = pcw_video_mode;   /* = the booted (native) mode */
        last_applied_mode = pcw_video_mode;
        update_option_visibility();
    }

    /* Load done: the frontend will now read av_info itself. From here on, a live
       audio-rate change may re-publish it via SET_SYSTEM_AV_INFO. */
    av_rate_live_push = true;
    crop_session_reset();   /* new game: restart the Smart-crop "once full, stay full" latch */
    return true;
}

RETRO_API bool retro_load_game_special(unsigned t, const struct retro_game_info *info, size_t n)
{
    (void)t; (void)info; (void)n;
    return false;
}

RETRO_API void retro_unload_game(void)
{
    core_running = false;
    core_failed = false;
    reset_session_state();
    pcw_disk_reset();
    av_rate_live_push = false;
    shutdown_embedded_engine();
}

RETRO_API void retro_reset(void)
{
    if (!core_running || core_failed) return;
    release_all_game_keys();
    pcw_osk_reset();
    pcw_keyboard_release_custom_keys();
    pcw_keyboard_release_all();
    pcw_keyboard_reset_custom_state();
    memset(pad_prev, 0, sizeof pad_prev);
    osk_button_prev = 0;
    if (pcw_engine_reset()) { fail_running_session(); return; }
    audiolibretro_discard_frame();
    pcw_fdc_sound_reset();   /* re-baseline head position; silence any clatter */
    crop_session_reset();    /* restart the Smart-crop "once full, stay full" latch */
}

RETRO_API void retro_run(void)
{
    long guard = 0;

    if (!core_running || core_failed) return;

    /* Live core-option changes (the frontend sets this flag when the user edits
     * an option, even mid-frame). */
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
        if (read_variables()) apply_options();
        update_option_visibility();
    }
    if (input_descriptors_dirty) set_input_descriptors();

    /* Re-sync the frontend's stale saved colour-mode to the program's native one
       after a reload (Allow-I/O ON). read_variables already mirrors the live mode so
       the render + row visibility are right; this also fixes the displayed VALUE by
       re-pushing native until the frontend reflects it (a single push at load gets
       overwritten when RetroArch applies the saved config a few frames later). */
    if (opt_allow_io && ignore_saved_mode >= 0 && native_resync_frames > 0) {
        struct retro_variable cm = { "zesarpcw_colour_mode", NULL };
        native_resync_frames--;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &cm) && cm.value) {
            int fv = choice_index(cm.value, colour_mode_labels, 4);
            /* Keep re-pushing native for the WHOLE window (the config re-apply that
               clobbers our push can land at any frame within it -- not just frame 1).
               Only once the window ends AND native has actually stuck do we trust the
               frontend again, so you can then re-select even the saved mode by hand. */
            if (fv >= 0 && fv != pcw_video_mode)
                push_colour_mode_to_frontend(pcw_video_mode);
            else if (fv >= 0 && native_resync_frames == 0)
                ignore_saved_mode = -1;
        }
    }

    /* Re-assert the audio-chip enables every frame: the PCW boot's CPU reset (and
       retro_reset) clear ay_chip_present, which would otherwise drop the AY toggle.
       Cheap, and keeps the AY Chip / Beeper toggles authoritative. */
    ay_chip_present.v = opt_ay_chip ? 1 : 0;
    beeper_enabled.v  = opt_beeper  ? 1 : 0;

    update_input();
    pcw_osk_tick();   /* time out auto-release of tapped OSK keys */
    apply_cheats();   /* re-write any enabled POKE cheats before the frame */

    /* Video mode ownership:
         OFF -> you force the mode: held every frame, persists across reloads.
         ON  -> the program owns the mode. We sync BOTH ways via last_applied_mode:
                if the PROGRAM changed the mode (Phantis1's menu->start, etc.) we
                mirror it into the menu; if YOU changed the Colour-mode option we
                apply it (override). Last writer wins. retro_load_game starts this at
                the program's NATIVE mode (ignoring a saved colour-mode), so entering
                a mono game after a colour game never inherits a broken mode. */
    if (!opt_allow_io) {
        if (pcw_video_mode != opt_video_mode) {
            pcw_video_mode = opt_video_mode;
        }
    }
    else if (pcw_video_mode != last_applied_mode) {
        /* program changed the mode -> reflect into the menu */
        opt_video_mode    = pcw_video_mode;
        last_applied_mode = pcw_video_mode;
        push_colour_mode_to_frontend(pcw_video_mode);
        /* program (re-)entering 4-colour just (re)programmed its colours into group 0
           -> select it; only on this event, so a manual CGA change of yours sticks. */
        if (pcw_video_mode == 1 && opt_cga_palette != 0) {
            opt_cga_palette   = 0;
            pcw_mode1_palette = 0;
            push_cga_palette_to_frontend(0);
        }
        update_option_visibility();
    }
    else if (opt_video_mode != last_applied_mode) {
        /* you changed the Colour-mode option by hand -> apply it (override). The
           program may re-impose its own mode on its next mode write (last writer). */
        pcw_video_mode    = opt_video_mode;
        last_applied_mode = opt_video_mode;
        update_option_visibility();
    }

    /* Default palette for a colour mode the program never programmed one for -- a
       mono game forced/seen in colour, or a colour game that uses the defaults.
       Held each frame (a boot reset would otherwise lose it); a program that ships
       its own palette (pcw_libretro_program_set_palette) keeps it. */
    if (pcw_video_mode >= 1 && !pcw_libretro_program_set_palette)
        pcw_init_colour_palette_mode(pcw_video_mode);

    /* Run exactly one PCW video frame: pump opcodes until the libretro video
     * driver signals a completed frame (it sets scrlibretro_frame_ready inside
     * scr_refresca_pantalla, called once per frame at the frame boundary). */
    int frame_result = pcw_engine_run_frame(&guard);
    if (frame_result < 0) { fail_running_session(); return; }

    if (!scrlibretro_frame_ready) {
        frame_guard_failures++;
        if (frame_guard_failures == 1 || (frame_guard_failures % 50u) == 0)
            pcw_log(RETRO_LOG_WARN, "[zesarux-pcw] frame guard reached after %ld opcodes "
                            "(%u occurrence%s); duplicating the previous frame\n",
                    guard, frame_guard_failures,
                    frame_guard_failures == 1 ? "" : "s");
        audiolibretro_silence_frame();
        if (video_cb) {
            bool previous_frame = last_video_data != NULL;
            if (!previous_frame) {
                /* No frame can be duplicated before the first presentation.
                   The newly allocated framebuffer is black until composed. */
                struct retro_system_av_info av;
                retro_get_system_av_info(&av);
                last_video_data = scrlibretro_framebuffer;
                last_video_width = av.geometry.base_width;
                last_video_height = av.geometry.base_height;
                last_video_pitch = (size_t)scrlibretro_max_width * sizeof(uint32_t);
            }
            video_cb(can_dupe && previous_frame ? NULL : last_video_data,
                     last_video_width, last_video_height, last_video_pitch);
        }
        return;
    }
    frame_guard_failures = 0;
    audiolibretro_flush_frame();

    if (video_cb) {
        int w = scrlibretro_cur_width();
        int h = scrlibretro_cur_height();
        if (w <= 0 || w > scrlibretro_max_width)  w = scrlibretro_max_width;
        if (h <= 0 || h > scrlibretro_max_height) h = scrlibretro_max_height;

        /* The on-screen keyboard draws over the freshly-composed native frame
           (full coords) -- do it before cropping so present_rect can show the
           whole frame while the keyboard is up. */
        int osk_up = pcw_osk_is_active();
        if (osk_up)
            pcw_osk_render(scrlibretro_framebuffer, w, h, scrlibretro_max_width);

        /* Decide the crop from this frame's content -- but NOT while the OSK is up:
           the keyboard fills the frame, which would read as full-raster content and
           wrongly trip the "once full, stay full" latch (and contaminate the crop
           window). present_rect forces the full frame for the OSK regardless; the
           frozen crop resumes the instant the keyboard closes, so a game that was at
           256x192 goes full for the OSK and returns to 256x192 when it is hidden. */
        if (!osk_up)
            crop_update(w, h);
        int rx, ry, rw, rh; float aspect; (void)aspect;
        present_rect(w, h, &rx, &ry, &rw, &rh, &aspect);

        /* If the presented size changed (a video-mode switch, or the crop box
           moved/toggled), hand the frontend the new geometry before presenting so
           it re-derives the window/crop/aspect. Not on the first frame --
           retro_get_system_av_info already reported that one. */
        if (rw != geom_last_rw || rh != geom_last_rh ||
            w != geom_last_nw || h != geom_last_nh) {
            if (geom_last_rw != 0) push_geometry();
            geom_last_rw = rw; geom_last_rh = rh;
            geom_last_nw = w;  geom_last_nh = h;
        }

        /* The driver composes only at the completed-frame boundary, so this
           persistent surface remains intact if a later frame hits its guard. */
        last_video_data = scrlibretro_framebuffer + (size_t)ry * scrlibretro_max_width + rx;
        last_video_width = (unsigned)rw;
        last_video_height = (unsigned)rh;
        last_video_pitch = (size_t)scrlibretro_max_width * sizeof(uint32_t);
        video_cb(last_video_data, last_video_width, last_video_height, last_video_pitch);
    }
}

/* ------------------------------------------------------------------------- *
 * save state / rewind
 *
 * A state contains the PCW ZSF blocks (RAM banks, F0-F8 ports, Z80 registers and
 * AY registers), preceded by a length/CRC header and followed by the colour-board
 * state ZSF leaves out (pcw_video_mode + the mode-1 palette), then the mounted
 * disc: carousel index, eject flag and the FULL .dsk buffer. Carrying the disc
 * buffer makes a save state self-contained -- anything the program wrote to the
 * floppy (a LocoScript document, a saved game) is restored with the state, which
 * is this core's persistence model (the .dsk file on disk is never written).
 * RetroArch's rewind is just serialize/unserialize every frame, so wiring these
 * enables both save states and rewind.
 * ------------------------------------------------------------------------- */
extern int  pcw_rgb_table_15_bits[];        /* the program-written colour palette (16 entries) */
extern int  p3dsk_buffer_disco_size;           /* dsk.c: bytes of the mounted .dsk */

/* Save-state format version, independent of the core's release version.
 * Increment on incompatible layout or semantics changes. The header starts
 * with 'W', 'C', 'P', then a numeric version byte (not an ASCII digit), so v1
 * cannot collide with the discarded development formats. No legacy readers. */
#define ZPCW_STATE_VERSION  1u
#define ZPCW_STATE_MAGIC    (0x00504357u | (ZPCW_STATE_VERSION << 24))
#define ZPCW_STATE_MAX_ZSF  (16*1024*1024)  /* format limit, not output capacity */
#define ZPCW_STATE_HDR      12              /* magic + zsf length + CRC-32 */
#define ZPCW_STATE_PALN     16              /* PCW_TOTAL_PALETTE_COLOURS */
/* the colour-board state ZSF omits: video_mode + mode1_palette +
   program_set_palette + the 16-entry programmed palette */
#define ZPCW_STATE_EXTRA    (12 + ZPCW_STATE_PALN * 4)
/* the mounted-disc block: carousel index + eject flag + dsk size (0 = none) */
#define ZPCW_STATE_DISKHDR  12
#define ZPCW_STATE_HEADROOM (64*1024) /* ZSF block overhead beyond raw PCW RAM */

static void zpcw_wr32(unsigned char *p, uint32_t v)
{ p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24); }
static uint32_t zpcw_rd32(const unsigned char *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static uint32_t zpcw_crc32(const unsigned char *data, size_t len)
{
    static uint32_t table[8][256];
    static int table_ready;
    uint32_t crc = 0xffffffffu;
    size_t i;
    if (!table_ready) {
        for (i = 0; i < 256; i++) {
            uint32_t v = (uint32_t)i;
            unsigned bit;
            for (bit = 0; bit < 8; bit++)
                v = (v >> 1) ^ (0xedb88320u & (0u - (v & 1u)));
            table[0][i] = v;
        }
        for (i = 0; i < 256; i++) {
            uint32_t v = table[0][i];
            unsigned bit;
            for (bit = 1; bit < 8; bit++) {
                v = table[0][v & 0xffu] ^ (v >> 8);
                table[bit][i] = v;
            }
        }
        table_ready = 1;
    }
    while (len >= 8) {
        uint32_t first = crc ^ (uint32_t)data[0]
                             ^ ((uint32_t)data[1] << 8)
                             ^ ((uint32_t)data[2] << 16)
                             ^ ((uint32_t)data[3] << 24);
        uint32_t second = (uint32_t)data[4]
                        ^ ((uint32_t)data[5] << 8)
                        ^ ((uint32_t)data[6] << 16)
                        ^ ((uint32_t)data[7] << 24);
        crc = table[7][ first        & 0xffu]
            ^ table[6][(first >>  8) & 0xffu]
            ^ table[5][(first >> 16) & 0xffu]
            ^ table[4][ first >> 24]
            ^ table[3][ second        & 0xffu]
            ^ table[2][(second >>  8) & 0xffu]
            ^ table[1][(second >> 16) & 0xffu]
            ^ table[0][ second >> 24];
        data += 8;
        len -= 8;
    }
    while (len--) {
        crc = table[0][(crc ^ *data) & 0xffu] ^ (crc >> 8);
        data++;
    }
    return ~crc;
}

RETRO_API size_t retro_serialize_size(void)
{
    if (!core_running) return 0;
    /* A stable, session-constant upper bound: the PCW RAM, the mounted-disc
       buffer, plus generous room for the ZSF block headers, the Z80 registers
       and our wrapper. serialize writes the actual (usually smaller, compressed)
       blob and zero-pads the rest; unserialize trusts the embedded lengths, so a
       varying blob in a fixed buffer is exactly what rewind needs. */
    return (size_t)pcw_total_ram + DSK_MAX_BUFFER_DISCO +
           ZPCW_STATE_HEADROOM;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
    if (!core_running || core_failed || !data) return false;
    if (size < retro_serialize_size()) return false;
    unsigned char *out = data;
    /* Mounted-disc block: the .dsk buffer travels in the state (writes included). */
    uint32_t dsk_size = 0;
    int disk_index = pcw_disk_get_index();
    if (disk_index >= 0 && p3dsk_buffer_disco_size > 0 &&
        p3dsk_buffer_disco_size <= DSK_MAX_BUFFER_DISCO)
        dsk_size = (uint32_t)p3dsk_buffer_disco_size;

    /* Reserve the wrapper and complete trailing blocks before the first write.
       Keep the direct-to-frontend path for rewind, but give the ZSF writer only
       the capacity actually available to it, independently of its format limit. */
    const size_t fixed = ZPCW_STATE_HDR + ZPCW_STATE_EXTRA + ZPCW_STATE_DISKHDR;
    if (size < fixed || dsk_size > size - fixed) return false;
    size_t zsf_capacity = size - fixed - dsk_size;
    size_t zsf_len = 0;
    if (!pcw_zsf_save(out + ZPCW_STATE_HDR, zsf_capacity, &zsf_len) ||
        zsf_len == 0 || zsf_len > zsf_capacity || zsf_len > ZPCW_STATE_MAX_ZSF)
        return false;

    /* These sums are bounded by size: zsf_len fits the reserved capacity. */
    size_t base = ZPCW_STATE_HDR + zsf_len + ZPCW_STATE_EXTRA;
    size_t need = base + ZPCW_STATE_DISKHDR + dsk_size;
    zpcw_wr32(out,     ZPCW_STATE_MAGIC);
    zpcw_wr32(out + 4, (uint32_t)zsf_len);
    zpcw_wr32(out + 8, 0);             /* filled after the payload is complete */
    unsigned char *ex = out + ZPCW_STATE_HDR + zsf_len;
    zpcw_wr32(ex,     (uint32_t)pcw_video_mode);
    zpcw_wr32(ex + 4, (uint32_t)pcw_mode1_palette);
    zpcw_wr32(ex + 8, (uint32_t)pcw_libretro_program_set_palette);
    for (int i = 0; i < ZPCW_STATE_PALN; i++)
        zpcw_wr32(ex + 12 + i*4, (uint32_t)pcw_rgb_table_15_bits[i]);

    unsigned char *dk = ex + ZPCW_STATE_EXTRA;
    zpcw_wr32(dk,     (uint32_t)disk_index);
    zpcw_wr32(dk + 4, pcw_disk_get_ejected() ? 1u : 0u);
    zpcw_wr32(dk + 8, dsk_size);
    if (dsk_size) memcpy(dk + ZPCW_STATE_DISKHDR, p3dsk_buffer_disco, dsk_size);

    zpcw_wr32(out + 8, zpcw_crc32(out + ZPCW_STATE_HDR,
                                  need - ZPCW_STATE_HDR));

    if (need < size) memset(out + need, 0, size - need);   /* deterministic tail */
    return true;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
    size_t ex_off, dk_off, payload_end;
    uint32_t mode, palette, program_palette;
    uint32_t idx, ejected, dsk_size;

    if (!core_running || core_failed || !data) return false;
    const unsigned char *in = data;
    if (size < ZPCW_STATE_HDR + ZPCW_STATE_EXTRA + ZPCW_STATE_DISKHDR) return false;
    if (zpcw_rd32(in) != ZPCW_STATE_MAGIC) return false;
    uint32_t zsf_len = zpcw_rd32(in + 4);
    if (zsf_len == 0 || zsf_len > ZPCW_STATE_MAX_ZSF || zsf_len > INT_MAX)
        return false;
    if ((size_t)zsf_len > size - ZPCW_STATE_HDR) return false;
    ex_off = ZPCW_STATE_HDR + (size_t)zsf_len;
    if (ZPCW_STATE_EXTRA > size - ex_off) return false;

    /* Validate every wrapper field and length before mutating emulator state. */
    const unsigned char *ex = in + ex_off;
    mode            = zpcw_rd32(ex);
    palette         = zpcw_rd32(ex + 4);
    program_palette = zpcw_rd32(ex + 8);
    if (mode > 3 || palette > 3 || program_palette > 1) return false;

    dk_off = ex_off + ZPCW_STATE_EXTRA;
    if (ZPCW_STATE_DISKHDR > size - dk_off) return false;
    const unsigned char *dk = in + dk_off;
    idx      = zpcw_rd32(dk);
    ejected  = zpcw_rd32(dk + 4);
    dsk_size = zpcw_rd32(dk + 8);
    if ((idx != UINT32_MAX && idx >= 16u) || ejected > 1u ||
        dsk_size > DSK_MAX_BUFFER_DISCO || dsk_size > INT_MAX)
        return false;
    payload_end = dk_off + ZPCW_STATE_DISKHDR;
    if ((size_t)dsk_size > size - payload_end) return false;
    payload_end += (size_t)dsk_size;
    int restored_index = idx == UINT32_MAX ? -1 : (int)idx;

    uint32_t expected = zpcw_rd32(in + 8);
    if (expected != zpcw_crc32(in + ZPCW_STATE_HDR, payload_end - ZPCW_STATE_HDR))
        return false;

    /* Restore the ZSF (RAM/ports/Z80/AY), then the colour-board state. */
    if (!pcw_zsf_load(in + ZPCW_STATE_HDR, zsf_len)) return false;

    pcw_video_mode                   = (int)mode;
    pcw_mode1_palette                = (int)palette;
    pcw_libretro_program_set_palette = (int)program_palette;
    for (int i = 0; i < ZPCW_STATE_PALN; i++)
        pcw_rgb_table_15_bits[i] = (int)zpcw_rd32(ex + 12 + i*4);

    /* Mounted-disc block: put the disc -- writes included -- back in
       the drive. The buffer swap needs no remount: the FDC reads from it live. */
    if (dsk_size) {
        memcpy(p3dsk_buffer_disco, dk + ZPCW_STATE_DISKHDR, dsk_size);
        p3dsk_buffer_disco_size = (int)dsk_size;
    }
    else {
        p3dsk_buffer_disco_size = 0;
        p3dsk_buffer_disco[0] = 0;
    }
    pcw_disk_restore_state(restored_index, ejected != 0, dsk_size != 0);

    /* Re-baseline the two-way mode sync and force a full repaint of the restored
       frame (the colour mode / palette may have jumped). */
    last_applied_mode   = pcw_video_mode;
    opt_video_mode     = pcw_video_mode;
    pcw_osk_reset();
    crop_session_reset();
    pcw_fdc_sound_reset();
    return true;
}

/* The whole PCW RAM is one linear block: pcw_ram_mem_table[] just slices
   memoria_spectrum into 16K pages (machines/pcw.c, pcw_init_memory_tables), so
   the frontend gets it directly for RAM-watch / cheat search / achievements.
   Note this is PHYSICAL RAM in bank order, not the Z80's current 64K window. */
RETRO_API void *retro_get_memory_data(unsigned id)
{
    if (id == RETRO_MEMORY_SYSTEM_RAM && core_running) return memoria_spectrum;
    return NULL;
}
RETRO_API size_t retro_get_memory_size(unsigned id)
{
    if (id == RETRO_MEMORY_SYSTEM_RAM && core_running) return (size_t)pcw_total_ram;
    return 0;
}

/* ------------------------------------------------------------------------- *
 * cheats -- POKEs held every frame (the classic "infinite lives" style).
 *
 * A cheat code is a list of "address value" pairs, decimal or 0x.. / $.. hex
 * (e.g. "35899 0" or "POKE 35899,0" or "0x8C3B 0x00 0x8C3C 0x00"); any non-number
 * text between them is ignored, so POKE-database lines paste straight in. Each
 * enabled cheat re-writes its bytes before every emulated frame, via the active
 * machine's Z80 memory writer.
 * ------------------------------------------------------------------------- */
#define PCW_MAX_CHEATS 64
#define PCW_MAX_POKES  32
struct pcw_cheat {
    int enabled, count;
    unsigned short addr[PCW_MAX_POKES];
    unsigned char  val[PCW_MAX_POKES];
};
static struct pcw_cheat cheats[PCW_MAX_CHEATS];

RETRO_API void retro_cheat_reset(void) { memset(cheats, 0, sizeof cheats); }

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    if (index >= PCW_MAX_CHEATS) return;
    struct pcw_cheat *c = &cheats[index];
    c->enabled = enabled ? 1 : 0;
    c->count = 0;

    /* Scan the code into a flat list of numbers, then pair them (addr, value). */
    long nums[PCW_MAX_POKES * 2]; int n = 0;
    const char *p = code;
    while (p && *p && n < (int)(sizeof nums / sizeof nums[0])) {
        char *end = NULL; long v;
        if (*p == '$') { v = strtol(p + 1, &end, 16); if (end > p + 1) { nums[n++] = v; p = end; continue; } p++; }
        else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { v = strtol(p, &end, 16); if (end > p) { nums[n++] = v; p = end; continue; } p += 2; }
        else if (*p >= '0' && *p <= '9') { v = strtol(p, &end, 10); nums[n++] = v; p = end; continue; }
        else p++;
    }
    int i;
    for (i = 0; i + 1 < n && c->count < PCW_MAX_POKES; i += 2) {
        c->addr[c->count] = (unsigned short)(nums[i] & 0xFFFF);
        c->val[c->count]  = (unsigned char)(nums[i + 1] & 0xFF);
        c->count++;
    }
}

static void apply_cheats(void)
{
    int i, j;
    for (i = 0; i < PCW_MAX_CHEATS; i++)
        if (cheats[i].enabled)
            for (j = 0; j < cheats[i].count; j++)
                poke_byte_no_time((z80_int)cheats[i].addr[j], (z80_byte)cheats[i].val[j]);
}

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_PAL; }
