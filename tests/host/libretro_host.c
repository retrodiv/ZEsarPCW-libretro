/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
    Headless dlopen test host. Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3 (see LICENSE). */
/*
 * libretro_host.c -- a tiny headless libretro front end to smoke-test the
 * ZEsarUX Amstrad PCW core without RetroArch (avoids the frontend's video-driver
 * init on headless boxes).
 *
 * dlopen()s a core, wires the callbacks, loads a .dsk, runs N frames, and dumps
 * the first and last presented frames as PPM so the boot screen can be eyeballed.
 * Reports whether video/audio callbacks fired and the geometry.
 *
 *   cc libretro_host.c -ldl -o host
 *   ./host <core.so> [content.dsk] [system_dir] [frames]
 *
 * Test knobs (env vars):
 *   ZPCW_VMODE=N     serve core option zesarpcw_video_mode (0=2col 1=4col ...)
 *   ZPCW_MODEL=PCW8256|PCW8512
 *   ZPCW_MODE1=N     serve zesarpcw_mode1_palette
 *   ZPCW_TINT=green|white
 *   ZPCW_VARUPDATE=1 report a one-shot variable-update (re-reads options once)
 *   ZPCW_DUMP=path.ppm  dump the last frame here as well
 *   ZPCW_FRAMEHASHES=path  write one RGB framebuffer hash per presented frame
 *   ZPCW_FRAMEHASH_START=N / ZPCW_FRAMEHASH_INTERVAL=N  limit fingerprint work
 *   ZPCW_NO_FIXED_DUMPS=1  skip the conventional /tmp first/last PPM files
 *   ZPCW_LITLOG=1    log lit-pixel count per frame in the PCW screen rect
 *   ZPCW_KEY="code:dn:up[,...]"  tap keyboard keys via the core's keyboard cb
 *   ZPCW_CONTROLLER=3  select Custom Keyboard Bindings (1 = RetroPad, 0 = None)
 *   ZPCW_BENCH=1     measure unpaced core throughput with lightweight callbacks
 *   ZPCW_BENCH_REWIND=N  additionally serialize every N frames (1 = real rewind)
 *   ZPCW_BENCH_WARMUP=N  exclude the first N frames from the measurement (50)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <time.h>
#endif
#ifdef _WIN32
#include <windows.h>
#define DLOPEN(p)  ((void*)LoadLibraryA(p))
#define DLSYM(h,n) ((void*)GetProcAddress((HMODULE)(h), (n)))
#define DLCLOSE(h) (FreeLibrary((HMODULE)(h)) ? 0 : -1)
#define DLERR()    "LoadLibrary failed"
#else
#include <dlfcn.h>
#define DLOPEN(p)  dlopen((p), RTLD_NOW | RTLD_LOCAL)
#define DLSYM(h,n) dlsym((h), (n))
#define DLCLOSE(h) dlclose(h)
#define DLERR()    dlerror()
#endif

#define ENV_GET_SYSTEM_DIRECTORY 9
#define ENV_SET_PIXEL_FORMAT     10
#define ENV_SET_KEYBOARD_CALLBACK 12
#define ENV_SET_DISK_CONTROL_INTERFACE 13
#define ENV_SET_DISK_CONTROL_EXT_INTERFACE 58
#define ENV_SET_GEOMETRY         37
#define ENV_SET_SYSTEM_AV_INFO   32
#define ENV_GET_VARIABLE         15
#define ENV_SET_VARIABLES        16
#define ENV_GET_VARIABLE_UPDATE  17
#define ENV_SET_SUPPORT_NO_GAME  18
#define ENV_SET_CORE_OPTIONS_DISPLAY 55
#define ENV_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK 69
#define ENV_SET_VARIABLE         70

struct retro_variable { const char *key; const char *value; };
struct game_info { const char *path; const void *data; size_t size; const char *meta; };
struct sysinfo { const char *name,*ver,*ext; unsigned char need_fullpath, block_extract; };
struct geom { unsigned bw,bh,mw,mh; float ar; };
struct timing { double fps, sr; };
struct avinfo { struct geom geometry; struct timing timing; };

typedef char (*env_t)(unsigned, void*);
typedef void (*vrf_t)(const void*, unsigned, unsigned, size_t);
typedef size_t (*aud_t)(const int16_t*, size_t);
typedef void (*ip_t)(void);
typedef int16_t (*is_t)(unsigned,unsigned,unsigned,unsigned);
typedef void (*kbd_event_t)(int down, unsigned keycode, uint32_t ch, uint16_t mod);
struct kbd_cb { kbd_event_t callback; };
/* Frontend-side option-display refresh callback (RETRO_ENVIRONMENT_SET_CORE_
   OPTIONS_UPDATE_DISPLAY_CALLBACK). RetroArch invokes this whenever the user
   changes ANY option; we mimic that on a mid-run flip so the core's two-read
   path is exercised exactly as the real frontend drives it. */
typedef char (*upd_disp_t)(void);
struct upd_disp_cb { upd_disp_t callback; };

/* Captured disk-control callbacks (ext layout; v0 is the first 7). */
struct dc_cb {
    char (*set_eject)(char); char (*get_eject)(void);
    unsigned (*get_index)(void); char (*set_index)(unsigned);
    unsigned (*get_num)(void);
    char (*replace)(unsigned, const struct game_info*);
    char (*add)(void);
    char (*set_initial)(unsigned, const char*);
    char (*get_path)(unsigned, char*, size_t);
    char (*get_label)(unsigned, char*, size_t);
};
static struct dc_cb g_dc; static int g_have_dc = 0;

static const char *g_sysdir = ".";
static int g_video_frames = 0, g_audio_frames = 0;
static int g_benchmark_quiet = 0;
static int g_av_pushes = 0;   /* count of SET_SYSTEM_AV_INFO (must be 0 during load) */
static unsigned g_w=0, g_h=0;
static uint32_t *g_last = NULL;
struct frame_fingerprint {
    uint64_t hash, cell_hash;
    uint32_t number, lit, cells, width, height;
};
static struct frame_fingerprint *g_frame_fingerprints = NULL;
static size_t g_frame_fingerprint_capacity = 0, g_frame_fingerprint_count = 0;
static int g_frame_fingerprint_start = 1, g_frame_fingerprint_interval = 1;
static int g_requested_frames = 0;
static kbd_event_t g_kbd = NULL;
static int g_varupdate_pending = 0;
static upd_disp_t g_update_display = NULL;
/* Value the core last pushed for the colour-mode option via SET_VARIABLE
   (Task A two-way sync). Once set, it shadows ZPCW_CMODE just as the frontend's
   stored value would, so a later GET_VARIABLE returns what the core selected. */
static char g_cmode_pushed[160] = {0};
static char g_cga_pushed[160]   = {0};   /* same, for the CGA-palette option */
/* An in-session "user changed the menu" flip (ZPCW_FLIP_*). Stored in C globals
   rather than via setenv() so the flip works identically on Windows/wine, where
   setenv() does not reliably reach getenv() (the .dll runs under MSVCRT). These
   model the frontend's current menu value after the user edits it, so they take
   priority over both the saved env default and any value the core pushed. */
static char g_cmode_forced[160] = {0};
static char g_cga_forced[160]   = {0};
static char g_phos_forced[64]   = {0};
static char g_beep_forced[16]   = {0};
static char g_arate_forced[32]  = {0};   /* live audio-rate flip (ZPCW_ARATE2) */
static const char *g_model_forced = NULL; /* content-reload state-contract test */
/* Last visibility the core requested per option via SET_CORE_OPTIONS_DISPLAY.
   -1 = never set. Lets us check the menu would show the right row (phosphor for
   monochrome, CGA palette for 4-colour). */
static int g_vis_phosphor = -1;
static int g_vis_cga      = -1;
static int g_v2_registrations = 0;
static int g_legacy_registrations = 0;
static int g_keymap_toggle_v2 = 0;
static int g_keymap_rows_v2 = 0;
static int g_keymap_bad_values_v2 = 0;
static int g_keymap_rows_hidden = 0;
static int g_keymap_rows_shown = 0;

static uint32_t rd32le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32le(unsigned char *p, uint32_t v) {
    p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8);
    p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
static uint32_t crc32_bytes(const unsigned char *p, size_t n) {
    uint32_t crc=0xffffffffu;
    for (size_t i=0;i<n;i++) {
        crc ^= p[i];
        for (unsigned b=0;b<8;b++)
            crc=(crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}

static uint64_t benchmark_now_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)((double)counter.QuadPart * 1000000000.0 /
                      (double)frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
#endif
}

/* Locate a byte in the mounted-disc payload using only the public serialized
   format. This keeps the host honest when the core hides all non-libretro
   symbols. Returns 1 and the complete payload end on success. */
static int state_disk_offset(unsigned char *blob, size_t size, unsigned disk_off,
                             size_t *byte_pos, size_t *payload_end) {
    enum { MAGIC=0x01504357u, HDR=12, EXTRA=12+16*4, DISKHDR=12 };
    if (!blob || size < HDR || rd32le(blob) != MAGIC) return 0;
    uint32_t zlen=rd32le(blob+4);
    if (zlen > size-HDR || EXTRA > size-HDR-zlen) return 0;
    size_t dk=HDR+(size_t)zlen+EXTRA;
    if (DISKHDR > size-dk) return 0;
    uint32_t dsize=rd32le(blob+dk+8);
    size_t data=dk+DISKHDR;
    if (dsize > size-data || disk_off >= dsize) return 0;
    *byte_pos=data+disk_off; *payload_end=data+dsize;
    return 1;
}

static char env_cb(unsigned cmd, void *data) {
    if (cmd == ENV_SET_PIXEL_FORMAT) return 1;
    if (cmd == ENV_SET_SUPPORT_NO_GAME) return 1;
    if (cmd == ENV_GET_SYSTEM_DIRECTORY) { *(const char**)data = g_sysdir; return 1; }
    if (cmd == ENV_SET_DISK_CONTROL_INTERFACE) return 1;
    if (cmd == ENV_SET_KEYBOARD_CALLBACK) { g_kbd = ((struct kbd_cb*)data)->callback; return 1; }
    if (cmd == 11 /* SET_INPUT_DESCRIPTORS */) {
        struct idesc { unsigned port, device, index, id; const char *desc; } *d = data;
        for (; d->desc; d++) fprintf(stderr, "[host] input desc: id=%u -> '%s'\n", d->id, d->desc);
        return 1;
    }
    if (cmd == ENV_SET_DISK_CONTROL_EXT_INTERFACE) {
        memcpy(&g_dc, data, sizeof g_dc); g_have_dc = 1;
        fprintf(stderr, "[host] disk control interface registered\n");
        return 1;
    }
    if (cmd == ENV_SET_GEOMETRY) {
        /* struct retro_game_geometry { unsigned bw,bh,mw,mh; float ar; } */
        struct geom *g = data;
        fprintf(stderr, "[host] SET_GEOMETRY %ux%u (max %ux%u ar=%.3f)\n",
                g->bw, g->bh, g->mw, g->mh, g->ar);
        return 1;
    }
    if (cmd == ENV_SET_SYSTEM_AV_INFO) {
        g_av_pushes++;
        fprintf(stderr, "[host] SET_SYSTEM_AV_INFO push #%d\n", g_av_pushes);
        return 1;
    }
    if (cmd == 52 /* GET_CORE_OPTIONS_VERSION */) {
        /* default: report v0 so the classic SET_VARIABLES path (and the smoke
           tests) run; ZPCW_OPTV2=1 reports v2 to exercise the categorised path. */
        if (!getenv("ZPCW_OPTV2")) return 0;
        *(unsigned*)data = 2; return 1;
    }
    if (cmd == 67 /* SET_CORE_OPTIONS_V2 */) {
        struct v2cat { const char *key,*desc,*info; };
        struct v2val { const char *value,*label; };
        struct v2def { const char *key,*desc,*dc,*info,*ic,*cat; struct v2val values[128]; const char *def; };
        struct { struct v2cat *cats; struct v2def *defs; } *o = data;
        for (struct v2cat *c=o->cats; c->key; c++)
            fprintf(stderr,"[host] v2 category: %s -- %s\n", c->key, c->desc);
        for (struct v2def *d=o->defs; d->key; d++) {
            fprintf(stderr,"[host] v2 opt [%s] %s = %s | info: %.40s\n", d->cat, d->key, d->def, d->info);
            if (!strcmp(d->key, "zesarpcw_keyboard_mapping"))
                g_keymap_toggle_v2++;
            if (!strncmp(d->key, "zesarpcw_key_", 13)) {
                int values = 0;
                while (values < 128 && d->values[values].value) values++;
                g_keymap_rows_v2++;
                if (values != 107 || strcmp(d->def, "Default") ||
                    strcmp(d->values[0].value, "Default") ||
                    strcmp(d->values[1].value, "Unmapped"))
                    g_keymap_bad_values_v2++;
            }
        }
        g_v2_registrations++;
        /* In Options v2, false means categories are not displayed; the option
           registration itself still succeeded and the core must not overwrite
           it with the legacy API. */
        return getenv("ZPCW_OPTV2_FALSE") ? 0 : 1;
    }
    if (cmd == ENV_SET_VARIABLES) { g_legacy_registrations++; return 1; }
    if (cmd == ENV_SET_CORE_OPTIONS_DISPLAY) {
        /* struct retro_core_option_display { const char *key; bool visible; } */
        struct coredisp { const char *key; unsigned char visible; } *d = data;
        if      (!strcmp(d->key, "zesarpcw_phosphor"))    g_vis_phosphor = d->visible ? 1 : 0;
        else if (!strcmp(d->key, "zesarpcw_cga_palette")) g_vis_cga      = d->visible ? 1 : 0;
        else if (!strncmp(d->key, "zesarpcw_key_", 13)) {
            if (d->visible) g_keymap_rows_shown++;
            else            g_keymap_rows_hidden++;
        }
        return 1;
    }
    if (cmd == ENV_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK) {
        g_update_display = ((struct upd_disp_cb*)data)->callback;
        return 1;
    }
    if (cmd == ENV_SET_VARIABLE) {
        /* The core pushes a new displayed value (Task A). Record it and let it
           shadow the env var, exactly as the frontend would store + redisplay it.
           RetroArch also refreshes option visibility after a SET_VARIABLE by
           invoking the update-display callback; ZPCW_SETVAR_REENTER reproduces
           that re-entrancy so we can test the ordering: 1 = callback AFTER the
           value is committed, 2 = callback BEFORE (some frontend paths do this,
           which makes the callback read the *old* value). */
        struct retro_variable *v = (struct retro_variable*)data;
        int reenter = getenv("ZPCW_SETVAR_REENTER") ? atoi(getenv("ZPCW_SETVAR_REENTER")) : 0;
        fprintf(stderr, "[host] SET_VARIABLE %s = '%s'\n", v->key, v->value ? v->value : "(null)");
        if (reenter == 2 && g_update_display) g_update_display();
        /* ZPCW_STICKY_ENV simulates a frontend that IGNORES the core's SET_VARIABLE
           push (GET_VARIABLE keeps returning the saved env value) + constantly
           re-offers the saved options -- the worst case for a stale saved value. */
        /* ZPCW_IGNORE_PUSH: the frontend records the saved option but does NOT let
           the core's SET_VARIABLE push change what GET_VARIABLE returns -- i.e. the
           push "does not stick", so a later GET_VARIABLE re-reads the stale saved
           value. Reproduces the real-RetroArch case behind the menu-shows-CGA bug. */
        if (!getenv("ZPCW_STICKY_ENV") && !getenv("ZPCW_IGNORE_PUSH")) {
            if (v->value && !strcmp(v->key, "zesarpcw_colour_mode"))
                snprintf(g_cmode_pushed, sizeof g_cmode_pushed, "%s", v->value);
            if (v->value && !strcmp(v->key, "zesarpcw_cga_palette"))
                snprintf(g_cga_pushed, sizeof g_cga_pushed, "%s", v->value);
        }
        if (reenter == 1 && g_update_display) g_update_display();
        return 1;
    }
    if (cmd == ENV_GET_VARIABLE_UPDATE) {
        *(char*)data = (g_varupdate_pending || getenv("ZPCW_STICKY_ENV")) ? 1 : 0;
        g_varupdate_pending = 0;
        return 1;
    }
    if (cmd == ENV_GET_VARIABLE) {
        struct retro_variable *v = (struct retro_variable*)data;
        int sticky = getenv("ZPCW_STICKY_ENV") != NULL;
        const char *e = NULL;
        if      (!strcmp(v->key, "zesarpcw_colour_mode"))    e = g_cmode_forced[0] ? g_cmode_forced : (!sticky && g_cmode_pushed[0]) ? g_cmode_pushed : getenv("ZPCW_CMODE");
        else if (!strcmp(v->key, "zesarpcw_model"))          e = g_model_forced ? g_model_forced : getenv("ZPCW_MODEL");
        else if (!strcmp(v->key, "zesarpcw_cpm_autolaunch")) e = getenv("ZPCW_CPMAUTO");
        else if (!strcmp(v->key, "zesarpcw_cga_palette"))    e = g_cga_forced[0] ? g_cga_forced : (!sticky && g_cga_pushed[0]) ? g_cga_pushed : getenv("ZPCW_CGA");
        else if (!strcmp(v->key, "zesarpcw_phosphor"))       e = g_phos_forced[0] ? g_phos_forced : getenv("ZPCW_PHOSPHOR");
        else if (!strcmp(v->key, "zesarpcw_allow_videomode_change")) e = getenv("ZPCW_ALLOW");
        else if (!strcmp(v->key, "zesarpcw_ay_chip"))        e = getenv("ZPCW_AY");
        else if (!strcmp(v->key, "zesarpcw_beeper"))         e = g_beep_forced[0] ? g_beep_forced : getenv("ZPCW_BEEPER");
        else if (!strcmp(v->key, "zesarpcw_fdc_sound"))      e = getenv("ZPCW_FDC");
        else if (!strcmp(v->key, "zesarpcw_crop"))           e = getenv("ZPCW_CROP");
        else if (!strcmp(v->key, "zesarpcw_audio_rate"))     e = g_arate_forced[0] ? g_arate_forced : getenv("ZPCW_ARATE");
        else if (!strcmp(v->key, "zesarpcw_osk_button"))     e = getenv("ZPCW_OSKBUTTON");
        else if (!strcmp(v->key, "zesarpcw_osk_transparent")) e = getenv("ZPCW_OSKTRANSP");
        else if (!strcmp(v->key, "zesarpcw_osk_layout"))     e = getenv("ZPCW_OSKLAYOUT");
        else if (!strcmp(v->key, "zesarpcw_osk_release"))    e = getenv("ZPCW_OSKREL");
        else if (!strcmp(v->key, "zesarpcw_keyboard_mapping")) e = getenv("ZPCW_KEYMAP");
        if (!e) return 0;
        /* The host CLI selects a model by machine name. Send its declared core
           option value, just as a frontend selecting that menu entry does. */
        if (!strcmp(v->key, "zesarpcw_model")) {
            if (!strcmp(e, "PCW8512")) e = "PCW8512 (512K)";
            else if (!strcmp(e, "PCW8256")) e = "PCW8256 (256K)";
        }
        v->value = e;
        return 1;
    }
    return 0;
}

static void dump_ppm(const char *fn) {
    if (!g_last || !g_w || !g_h) return;
    FILE *f = fopen(fn, "wb"); if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", g_w, g_h);
    for (unsigned i=0;i<g_w*g_h;i++){ uint32_t p=g_last[i]; unsigned char rgb[3]={(p>>16)&0xff,(p>>8)&0xff,p&0xff}; fwrite(rgb,1,3,f);}
    fclose(f);
}

/* Histogram of distinct RGB values across the frame -- a cheap way to tell a
   2-colour screen (2 entries) from a 4-colour one (more) without a display. */
static void color_histogram(void) {
    if (!g_last || !g_w || !g_h) return;
    uint32_t seen[64]; int counts[64]; int n=0;
    for (unsigned i=0;i<g_w*g_h && n<64;i++){
        uint32_t p=g_last[i]&0x00FFFFFFu; int j;
        for (j=0;j<n;j++) if (seen[j]==p) { counts[j]++; break; }
        if (j==n){ seen[n]=p; counts[n]=1; n++; }
    }
    fprintf(stderr,"[host] distinct colours in frame: %d%s\n", n, n>=64?"+":"");
    for (int j=0;j<n && j<8;j++) fprintf(stderr,"        #%06x : %d px\n", seen[j], counts[j]);
}

/* Track the most-lit frame over the whole run (robust content detector for
   slow-loading games: a fixed end-frame can catch a black moment). Dumped to
   ZPCW_DUMPMAX at exit. */
long g_max_lit = 0; uint32_t *g_maxframe = NULL; unsigned g_maxw=0, g_maxh=0;

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch) {
    g_video_frames++;
    if (!data) return; /* libretro frame duplication */
    g_w=w; g_h=h;
    if (g_benchmark_quiet) return;
    free(g_last); g_last=malloc((size_t)w*h*4);
    for (unsigned y=0;y<h;y++) memcpy(g_last+(size_t)y*w, (const char*)data+y*pitch, (size_t)w*4);
    if (g_frame_fingerprints &&
        g_video_frames >= g_frame_fingerprint_start &&
        (((g_video_frames - g_frame_fingerprint_start) %
           g_frame_fingerprint_interval) == 0 ||
         g_video_frames == g_requested_frames) &&
        g_frame_fingerprint_count < g_frame_fingerprint_capacity) {
        struct frame_fingerprint *fp =
            &g_frame_fingerprints[g_frame_fingerprint_count++];
        uint64_t hash = UINT64_C(1469598103934665603);
        uint64_t cell_hash = UINT64_C(1469598103934665603);
        uint32_t lit = 0;
        uint32_t cells = 0;
        for (size_t i = 0; i < (size_t)w * h; i++) {
            uint32_t rgb = g_last[i] & 0x00FFFFFFu;
            if (rgb) lit++;
            hash ^= rgb & 0xFFu; hash *= UINT64_C(1099511628211);
            hash ^= (rgb >> 8) & 0xFFu; hash *= UINT64_C(1099511628211);
            hash ^= (rgb >> 16) & 0xFFu; hash *= UINT64_C(1099511628211);
        }
        /* A second, deliberately font-independent fingerprint records which
           8x8 PCW text cells contain ink. A 64-pixel solid cell is the BIOS
           cursor blob and is omitted, so cursor visibility alone cannot turn
           the same textual screen into a different layout. This is weaker
           evidence than the RGB hash and is reported as similarity, never as
           pixel equivalence. */
        for (unsigned cy = 0; cy < h; cy += 8) {
            for (unsigned cx = 0; cx < w; cx += 8) {
                unsigned ink = 0;
                for (unsigned y = cy; y < h && y < cy + 8; y++)
                    for (unsigned x = cx; x < w && x < cx + 8; x++)
                        if (g_last[(size_t)y*w+x] & 0x00FFFFFFu) ink++;
                unsigned occupied = ink != 0 && ink != 64;
                cells += occupied;
                cell_hash ^= occupied;
                cell_hash *= UINT64_C(1099511628211);
            }
        }
        fp->hash = hash; fp->number = (uint32_t)g_video_frames;
        fp->cell_hash = cell_hash; fp->lit = lit; fp->cells = cells;
        fp->width = w; fp->height = h;
    }
    if (g_video_frames==1 && !getenv("ZPCW_NO_FIXED_DUMPS"))
        dump_ppm("/tmp/zpcw_frame_first.ppm");
    if (getenv("ZPCW_DUMPMAX")) {
        long lit=0;
        for (unsigned y=0;y<h;y++) for (unsigned x=0;x<w;x++)
            if (g_last[(size_t)y*w+x] & 0x00FFFFFFu) lit++;
        if (lit > g_max_lit) {
            g_max_lit=lit; g_maxw=w; g_maxh=h;
            free(g_maxframe); g_maxframe=malloc((size_t)w*h*4);
            memcpy(g_maxframe, g_last, (size_t)w*h*4);
        }
    }
    if (getenv("ZPCW_LITLOG")) {
        long lit=0;
        for (unsigned y=24;y<h-24;y++)
            for (unsigned x=32;x<w-32;x++)
                if (g_last[(size_t)y*w+x] & 0x00FFFFFFu) lit++;
        fprintf(stderr,"[lit] frame=%d lit=%ld\n", g_video_frames, lit);
    }
}

static long g_audio_nonzero = 0;
static size_t aud_cb(const int16_t *d, size_t n){
    g_audio_frames++;
    if (g_benchmark_quiet) return n;
    for (size_t i=0;i<n*2;i++) if (d[i]) g_audio_nonzero++;
    /* ZPCW_RAW=path: append the LEFT channel as raw int16 LE -- one stream over the
       whole run so the waveform timing can be overlaid against the original. */
    const char *rw = getenv("ZPCW_RAW");
    if (rw) { static FILE *f=NULL; if(!f) f=fopen(rw,"wb");
              if(f){ for(size_t i=0;i<n;i++) fwrite(&d[2*i],2,1,f); fflush(f);} }
    return n;
}
static void ip_cb(void){}
/* RetroPad button state injected by ZPCW_PAD (see main): g_pad[id] = pressed. */
#define RETRO_DEVICE_JOYPAD 1
static int g_pad[24] = {0};
static int16_t is_cb(unsigned port,unsigned dev,unsigned idx,unsigned id){
    (void)port;(void)idx;
    if (dev==RETRO_DEVICE_JOYPAD && id<24) return g_pad[id];
    return 0;
}

static int run_disk_contract(void *h) {
    char path[32] = "sentinel", label[32] = "sentinel";
    struct game_info empty; memset(&empty,0,sizeof empty);
    if (!g_have_dc || !g_dc.get_num || !g_dc.get_index || !g_dc.set_index ||
        !g_dc.set_eject || !g_dc.get_eject || !g_dc.replace) return 0;
    unsigned n=g_dc.get_num();
    int ok=n>0;
    ok &= !g_dc.set_index(0);                       /* tray is inserted */
    ok &= !g_dc.replace(0,&empty);
    if (g_dc.get_path)  ok &= !g_dc.get_path(UINT32_MAX,path,sizeof path);
    if (g_dc.get_label) ok &= !g_dc.get_label(UINT32_MAX,label,sizeof label);
    unsigned selected=g_dc.get_index();
    ok &= g_dc.set_eject(1) && g_dc.get_eject();
    ok &= g_dc.get_index() == selected;             /* tray and selection are independent */
    /* Two images are enough to exercise a frontend's next/previous shortcuts,
       which read the current selection while the tray is open. */
    char first_path[4096];
    if (n == 1 && g_dc.get_path && g_dc.add && g_dc.get_path(0,first_path,sizeof first_path)) {
        struct game_info second = {first_path,NULL,0,NULL};
        ok &= g_dc.add() && g_dc.replace(1,&second);
        n=g_dc.get_num();
    }
    ok &= n >= 2 && g_dc.set_index(0);
    selected=g_dc.get_index(); if (selected < n-1) selected++;
    ok &= g_dc.set_index(selected) && g_dc.get_index()==1;
    selected=g_dc.get_index(); if (selected > 0) selected--;
    ok &= g_dc.set_index(selected) && g_dc.get_index()==0;
    ok &= g_dc.set_index(1) && g_dc.set_eject(0) && g_dc.get_index()==1;
    ok &= g_dc.set_eject(1) && g_dc.get_index()==1;
    size_t (*state_size)(void)=(size_t (*)(void))DLSYM(h,"retro_serialize_size");
    char (*save)(void *,size_t)=(char (*)(void *,size_t))DLSYM(h,"retro_serialize");
    char (*load)(const void *,size_t)=(char (*)(const void *,size_t))DLSYM(h,"retro_unserialize");
    if (state_size && save && load) {
        size_t size=state_size(), byte_pos, end;
        unsigned char *state=malloc(size), *probe=malloc(size);
        if (!state || !probe) { free(state); free(probe); return 0; }
        ok &= save(state,size);
        if (state_disk_offset(state,size,0,&byte_pos,&end)) {
            state[byte_pos]^=0xa5;
            wr32le(state+8,crc32_bytes(state+12,end-12));
            ok &= g_dc.set_index(0) && g_dc.set_eject(0);
            ok &= load(state,size) && g_dc.get_eject() && g_dc.get_index()==1;
            ok &= g_dc.set_eject(0) && g_dc.get_index()==1 && save(probe,size);
            ok &= probe[byte_pos]==state[byte_pos]; /* insertion preserves restored guest writes */
        } else ok=0;
        free(state); free(probe);
    } else ok=0;
    ok &= g_dc.set_eject(1);
    ok &= g_dc.set_index(n) && g_dc.get_index() >= n;
    ok &= !g_dc.set_eject(0) && g_dc.get_eject();   /* no selection */
    ok &= !g_dc.replace(UINT32_MAX,&empty);
    ok &= g_dc.set_index(0) && g_dc.set_eject(0);
    if (g_dc.get_path) {
        path[0]='x';
        ok &= !g_dc.get_path(0,path,1) && path[0]==0;
    }
    ok &= g_dc.set_eject(1) && g_dc.replace(0,&empty);
    ok &= g_dc.get_num()==n-1;
    ok &= g_dc.set_index(UINT32_MAX);
    fprintf(stderr,"[host] disk-control contract: %s\n",ok?"PASS":"FAIL");
    return ok;
}

typedef void (*v_env_t)(env_t); typedef void (*v_vrf_t)(vrf_t); typedef void (*v_aud_t)(aud_t);
typedef void (*v_ip_t)(ip_t);  typedef void (*v_is_t)(is_t);  typedef void (*v_void_t)(void);
typedef void (*v_si_t)(struct sysinfo*); typedef void (*v_av_t)(struct avinfo*);
typedef char (*v_load_t)(const struct game_info*);

struct state_api {
    size_t (*size)(void);
    char (*save)(void *, size_t);
    char (*load)(const void *, size_t);
    void *(*ram)(unsigned);
    size_t (*ram_size)(unsigned);
};

static int rejected_state_unchanged(const struct state_api *api,
        const void *bad, size_t bad_size, const unsigned char *before,
        unsigned char *after, size_t size) {
    void *ram = api->ram(2);
    size_t ram_size = api->ram_size(2);
    unsigned index = g_dc.get_index(), count = g_dc.get_num();
    char ejected = g_dc.get_eject();
    int rejected = !api->load(bad, bad_size);
    int stable = ram == api->ram(2) && ram_size == api->ram_size(2) &&
                 size == api->size();
    int unchanged = stable && api->save(after, size) && !memcmp(before, after, size) &&
        index == g_dc.get_index() && count == g_dc.get_num() && ejected == g_dc.get_eject();
    fprintf(stderr, "[host] state rejection: rejected=%d memory_stable=%d unchanged=%d\n",
            rejected, stable, unchanged);
    return rejected && stable && unchanged;
}

/* Exercise real states from both models through the exported API only. A model
   option is changed by unloading/reloading content, never by modifying a state
   ID (which would only test a malformed page inventory). No game corpus needed. */
static int run_state_contract(void *h, const struct game_info *game) {
    const char *models[] = { "PCW8256", "PCW8512" };
    struct state_api api = {
        (size_t (*)(void))DLSYM(h, "retro_serialize_size"),
        (char (*)(void *, size_t))DLSYM(h, "retro_serialize"),
        (char (*)(const void *, size_t))DLSYM(h, "retro_unserialize"),
        (void *(*)(unsigned))DLSYM(h, "retro_get_memory_data"),
        (size_t (*)(unsigned))DLSYM(h, "retro_get_memory_size")
    };
    v_load_t load = (v_load_t)DLSYM(h, "retro_load_game");
    v_void_t unload = (v_void_t)DLSYM(h, "retro_unload_game");
    unsigned char *states[2] = { NULL, NULL }, *before = NULL, *after = NULL, *bad = NULL;
    size_t sizes[2] = { 0, 0 };
    int ok = 1;
    if (!api.size || !api.save || !api.load || !api.ram || !api.ram_size ||
        !load || !unload || !g_have_dc) return 0;

    for (unsigned model = 0; model < 2; model++) {
        unload(); g_model_forced = models[model];
        if (!load(game)) { ok = 0; goto done; }
        size_t ram_size = api.ram_size(2);
        unsigned char *ram = api.ram(2);
        sizes[model] = api.size();
        states[model] = malloc(sizes[model]);
        if (!ram || ram_size != (model ? 512u : 256u) * 1024u || !states[model]) {
            ok = 0; goto done;
        }
        /* Poorly compressible RAM exercises every bank and the upper bound. */
        uint32_t random = 0x7148a329u + model;
        for (size_t i = 0; i < ram_size; i++) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            ram[i] = (unsigned char)random;
        }
        if (!api.save(states[model], sizes[model]) ||
            rd32le(states[model]) != 0x01504357u) { /* WCP + numeric format v1 */
            ok = 0; goto done;
        }
    }
    before = malloc(sizes[1]); after = malloc(sizes[1]); bad = malloc(sizes[1]);
    if (!before || !after || !bad) { ok = 0; goto done; }

    for (unsigned model = 0; model < 2; model++) {
        unload(); g_model_forced = models[model];
        if (!load(game) || !api.save(before, sizes[model])) { ok = 0; goto done; }
        fprintf(stderr, "[host] cross-model %s -> %s\n", models[1-model], models[model]);
        int rejected = rejected_state_unchanged(&api, states[1-model], sizes[1-model],
                                                before, after, sizes[model]);
        ok &= rejected;
        if (!rejected) continue; /* still exercise the opposite model direction */

        size_t size = sizes[model];
        /* Current states load with or without the serializer's zero padding. */
        size_t zlen = rd32le(states[model] + 4);
        size_t disk = 12 + zlen + 76;
        size_t used = disk + 12 + rd32le(states[model] + disk + 8);
        if (used > size) { ok = 0; goto done; }
        void *ram = api.ram(2);
        size_t ram_size = api.ram_size(2);
        size_t input_sizes[] = { size, used };
        for (unsigned i = 0; i < sizeof input_sizes / sizeof input_sizes[0]; i++) {
            int restored = api.load(states[model], input_sizes[i]) && api.size() == size &&
                api.ram(2) == ram && api.ram_size(2) == ram_size &&
                api.save(after, size) && !memcmp(after, states[model], size);
            fprintf(stderr, "[host] state round-trip %s %s: %s\n", models[model],
                    i ? "compact" : "padded", restored ? "PASS" : "FAIL");
            ok &= restored;
        }
        if (!api.save(before, size)) { ok = 0; goto done; }
        /* Repair CRCs to reach the ZSF validator, including a late invalid block
           after registers and RAM. Rejection must not partially restore them. */
        for (unsigned mutation = 0; mutation < 8; mutation++) {
            memcpy(bad, before, size);
            size_t live_zlen = rd32le(bad + 4);
            size_t end = 12 + live_zlen + 76 + 12;
            end += rd32le(bad + end - 4);
            size_t first_block = 12 + sizeof("ZSF ZEsarUX Snapshot File.") - 1;
            if (mutation == 0) wr32le(bad + first_block + 2, UINT32_MAX);
            if (mutation == 1) {
                size_t cursor = first_block, last = cursor;
                while (cursor < 12 + live_zlen) {
                    last = cursor; cursor += 6 + rd32le(bad + cursor + 2);
                }
                bad[last] = 0xff; bad[last + 1] = 0x7f; /* unknown final block */
            }
            if (mutation == 2) wr32le(bad + 12 + live_zlen, 4); /* invalid colour mode */
            if (mutation == 3) bad[32] ^= 0x5a; /* bad CRC */
            if (mutation == 4) {
                /* A well-framed block outside the writer's vocabulary is invalid,
                   even when it has no payload and all required blocks are present. */
                size_t insert = 12 + live_zlen;
                if (end > size - 6) { ok = 0; goto done; }
                memmove(bad + insert + 6, bad + insert, end - insert);
                memset(bad + insert, 0, 6);
                wr32le(bad + 4, (uint32_t)live_zlen + 6);
                end += 6;
            }
            if (mutation == 5) wr32le(bad, UINT32_MAX); /* unsupported wrapper */
            if (mutation == 6 || mutation == 7) {
                size_t cursor = first_block;
                while (cursor < 12 + live_zlen &&
                       !(bad[cursor] == 0x43 && bad[cursor+1] == 0x50))
                    cursor += 6 + rd32le(bad + cursor + 2);
                if (cursor >= 12 + live_zlen) { ok = 0; goto done; }
                if (mutation == 6) {
                    wr32le(bad + cursor + 6, 2); /* invalid controller enable latch */
                } else {
                    /* A correctly framed runtime block still needs its complete
                       bootstrap record. Repair sizes and CRC to test the reader. */
                    size_t length = rd32le(bad + cursor + 2);
                    if (length < 32) { ok = 0; goto done; }
                    size_t tail = cursor + 6 + length;
                    memmove(bad + tail - 32, bad + tail, end - tail);
                    wr32le(bad + cursor + 2, (uint32_t)length - 32);
                    wr32le(bad + 4, (uint32_t)live_zlen - 32);
                    end -= 32;
                }
            }
            if (mutation != 3) wr32le(bad + 8, crc32_bytes(bad + 12, end - 12));
            ok &= rejected_state_unchanged(&api, bad, size, before, after, size);
        }
        /* Neither the discarded ASCII development versions nor a future
           numeric format may be loaded or partially applied. */
        const uint32_t incompatible[] = {
            0x31504357u, 0x32504357u, 0x33504357u, 0x34504357u, 0x02504357u
        };
        for (unsigned i = 0; i < sizeof incompatible / sizeof incompatible[0]; ++i) {
            memcpy(bad, before, size);
            wr32le(bad, incompatible[i]);
            ok &= rejected_state_unchanged(&api, bad, size, before, after, size);
        }
        ok &= rejected_state_unchanged(&api, before, used - 1, before, after, size);
        size_t short_sizes[] = { 0, 1, 11, size - 1 };
        for (unsigned i = 0; i < sizeof short_sizes / sizeof short_sizes[0]; i++) {
            size_t n = short_sizes[i];
            unsigned char *guarded = malloc(n + 32);
            if (!guarded) { ok = 0; goto done; }
            memset(guarded, 0xa5, n + 32);
            ok &= !api.save(guarded + 16, n);
            for (size_t j = 0; j < n + 32; j++) ok &= guarded[j] == 0xa5;
            free(guarded);
        }
    }
done:
    free(states[0]); free(states[1]); free(before); free(after); free(bad);
    g_model_forced = NULL;
    /* Restore a freshly loaded normal session for the remaining lifecycle test. */
    unload();
    if (!load(game)) ok = 0;
    fprintf(stderr, "[host] save-state contract: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

#define LOAD(var, type, name) type var = (type)DLSYM(h, name); \
    if (!var) { fprintf(stderr, "missing %s\n", name); return 2; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s core.so [content] [sysdir] [frames]\n", argv[0]); return 1; }
    const char *content = argc>2 && argv[2][0] ? argv[2] : NULL;
    if (argc>3) g_sysdir = argv[3];
    int frames = argc>4 ? atoi(argv[4]) : 200;
    g_requested_frames = frames;
    g_benchmark_quiet = getenv("ZPCW_BENCH") || getenv("ZPCW_BENCH_REWIND");
    if (getenv("ZPCW_FRAMEHASHES") && frames > 0) {
        if (getenv("ZPCW_FRAMEHASH_START"))
            g_frame_fingerprint_start = atoi(getenv("ZPCW_FRAMEHASH_START"));
        if (getenv("ZPCW_FRAMEHASH_INTERVAL"))
            g_frame_fingerprint_interval = atoi(getenv("ZPCW_FRAMEHASH_INTERVAL"));
        if (g_frame_fingerprint_start < 1) g_frame_fingerprint_start = 1;
        if (g_frame_fingerprint_interval < 1) g_frame_fingerprint_interval = 1;
        g_frame_fingerprint_capacity = (size_t)frames;
        g_frame_fingerprints = calloc(g_frame_fingerprint_capacity,
                                      sizeof(*g_frame_fingerprints));
    }

    void *h = DLOPEN(argv[1]);
    if (!h) { fprintf(stderr, "dlopen: %s\n", DLERR()); return 1; }

    LOAD(set_env, v_env_t, "retro_set_environment");        set_env(env_cb);
    LOAD(set_vrf, v_vrf_t, "retro_set_video_refresh");      set_vrf(video_cb);
    LOAD(set_aud, v_aud_t, "retro_set_audio_sample_batch"); set_aud(aud_cb);
    LOAD(set_ip,  v_ip_t,  "retro_set_input_poll");         set_ip(ip_cb);
    LOAD(set_is,  v_is_t,  "retro_set_input_state");        set_is(is_cb);
    LOAD(r_init,  v_void_t,"retro_init");                   r_init();
    if (getenv("ZPCW_CONTROLLER")) {
        typedef void (*controller_t)(unsigned, unsigned);
        LOAD(set_controller, controller_t, "retro_set_controller_port_device");
        set_controller(0, (unsigned)strtoul(getenv("ZPCW_CONTROLLER"), NULL, 0));
    }
    LOAD(r_si,    v_si_t,  "retro_get_system_info");
    struct sysinfo si; memset(&si,0,sizeof si); r_si(&si);
    printf("[host] core: %s %s  ext=%s fullpath=%d\n", si.name, si.ver, si.ext, si.need_fullpath);

    LOAD(r_load,  v_load_t,"retro_load_game");
    LOAD(r_unload, v_void_t, "retro_unload_game");
    struct game_info gi; memset(&gi,0,sizeof gi); gi.path = content;

    /* ZPCW_DISK_INITIAL="idx:path" -- the frontend's "continue where I left off"
       hint (set_initial_image), sent BEFORE retro_load_game exactly as RetroArch
       does. With .m3u content the core must boot that disc of the playlist. */
    const char *di = getenv("ZPCW_DISK_INITIAL");
    if (di && g_have_dc && g_dc.set_initial) {
        char b[2048]; snprintf(b, sizeof b, "%s", di);
        char *colon = strchr(b, ':');
        const char *dpath = colon ? colon + 1 : "";
        if (colon) *colon = 0;
        g_dc.set_initial((unsigned)atoi(b), dpath);
        fprintf(stderr, "[host] set_initial_image(%s, '%s')\n", b, dpath);
    }

    printf("[host] retro_load_game(%s)...\n", content?content:"<no content>");
    char ok = r_load(content?&gi:NULL);
    printf("[host] retro_load_game -> %d\n", ok);
    if (!ok) { printf("[host] load failed; stopping\n"); return 3; }

    LOAD(r_av,    v_av_t,  "retro_get_system_av_info");
    struct avinfo av; memset(&av,0,sizeof av); r_av(&av);
    printf("[host] av: %ux%u  fps=%.1f sr=%.0f\n", av.geometry.bw, av.geometry.bh, av.timing.fps, av.timing.sr);
    fprintf(stderr, "[host] av_info pushes DURING load = %d (must be 0)\n", g_av_pushes);
    fprintf(stderr, "[host] option registration: v2=%d legacy=%d\n",
            g_v2_registrations, g_legacy_registrations);
    fprintf(stderr, "[host] physical keyboard options: toggle=%d rows=%d bad=%d hidden=%d shown=%d\n",
            g_keymap_toggle_v2, g_keymap_rows_v2, g_keymap_bad_values_v2,
            g_keymap_rows_hidden, g_keymap_rows_shown);

    /* ZPCW_MEMPROBE=1: report the SYSTEM_RAM memory map (RAM-watch / cheats /
       achievements) the core exposes once content is loaded. */
    if (getenv("ZPCW_MEMPROBE")) {
        typedef void  *(*gmd_t)(unsigned);
        typedef size_t (*gms_t)(unsigned);
        gmd_t gmd = (gmd_t)DLSYM(h, "retro_get_memory_data");
        gms_t gms = (gms_t)DLSYM(h, "retro_get_memory_size");
        if (gmd && gms)
            fprintf(stderr, "[host] memory map: SYSTEM_RAM data=%p size=%" PRIuMAX "\n",
                    gmd(2), (uintmax_t)gms(2));
    }

    /* Post-load disc-carousel dump (drives the .m3u assertions in the tests). */
    if (g_have_dc) {
        unsigned nd = g_dc.get_num();
        fprintf(stderr, "[host] disks: num=%u index=%u ejected=%d\n",
                nd, g_dc.get_index(), (int)g_dc.get_eject());
        for (unsigned d = 0; d < nd; d++) {
            char lp[1024] = "", ll[160] = "";
            if (g_dc.get_path)  g_dc.get_path(d, lp, sizeof lp);
            if (g_dc.get_label) g_dc.get_label(d, ll, sizeof ll);
            fprintf(stderr, "[host] disk %u: label='%s' path=%s\n", d, ll, lp);
        }
    }

    const char *kp = getenv("ZPCW_KEY");
    int nk=0, kc[64], kd[64], ku[64];
    if (kp) { char b[1024]; snprintf(b,sizeof b,"%s",kp);
        for (char *t=strtok(b,","); t && nk<64; t=strtok(NULL,","))
            if (sscanf(t,"%d:%d:%d",&kc[nk],&kd[nk],&ku[nk])==3) nk++; }

    /* ZPCW_PAD="id:dn:up[,...]" -- hold RetroPad button <id> down at frame <dn>,
       release at frame <up>. Lets the per-game gamepad map be exercised headless. */
    const char *pp = getenv("ZPCW_PAD");
    int np=0, pid[16], pdn[16], pup[16];
    if (pp) { char b[256]; snprintf(b,sizeof b,"%s",pp);
        for (char *t=strtok(b,","); t && np<16; t=strtok(NULL,","))
            if (sscanf(t,"%d:%d:%d",&pid[np],&pdn[np],&pup[np])==3) np++; }

    /* If asked, flip an option mid-run to prove a live (hot) change. We mimic the
       exact RetroArch sequence: store the new value, invoke the update-display
       callback (which the core uses to refresh option visibility), then raise the
       variable-update flag. This reproduces hot-apply bugs that only surface when
       both the display callback and GET_VARIABLE_UPDATE read the options. */
    const char *fc = getenv("ZPCW_FLIP_CMODE_AT");      int flip_cmode_at = fc ? atoi(fc) : -1;
    const char *fp = getenv("ZPCW_FLIP_PHOSPHOR_AT");   int flip_phos_at  = fp ? atoi(fp) : -1;
    const char *fg = getenv("ZPCW_FLIP_CGA_AT");        int flip_cga_at   = fg ? atoi(fg) : -1;
    const char *fb = getenv("ZPCW_FLIP_BEEPER_AT");     int flip_beep_at  = fb ? atoi(fb) : -1;
    const char *fa = getenv("ZPCW_FLIP_ARATE_AT");      int flip_arate_at = fa ? atoi(fa) : -1;
    /* ZPCW_OPEN_MENU_AT=N: at frame N, invoke the update-display callback WITHOUT
       any option edit -- models the user opening Core Options, which makes
       RetroArch ask the core to refresh option visibility (and, in the core, may
       re-read the saved option values). The lever for the menu-shows-CGA bug. */
    const char *om = getenv("ZPCW_OPEN_MENU_AT");       int open_menu_at  = om ? atoi(om) : -1;
    /* ZPCW_CYCLE_AT=N + ZPCW_CYCLE_SEQ="v1|v2|..." : at frame N, model the user
       OPENING Core Options and turning the Colour-mode dial through each value --
       all while the core is PAUSED (no retro_run between steps, exactly as
       RetroArch does). Each step sets the option value + invokes the update-display
       callback and prints the resulting row visibility, so we can prove visibility
       updates live during cycling (the regression that pcw_video_mode-based
       visibility caused, since pcw_video_mode is frozen while paused). */
    const char *cy = getenv("ZPCW_CYCLE_AT");           int cycle_at      = cy ? atoi(cy) : -1;
    /* ZPCW_CONFIG_OVERWRITE_AT=N: at frame N, discard any value the core pushed --
       models RetroArch applying the SAVED config a few frames into the run and
       clobbering the core's startup "native" push (so GET_VARIABLE goes back to the
       saved value). The core's re-sync loop must recover the menu to native. */
    const char *co = getenv("ZPCW_CONFIG_OVERWRITE_AT"); int cfg_over_at  = co ? atoi(co) : -1;

    /* Save-state / rewind test: serialize at frame ZPCW_STATE_SAVE, unserialize
       at ZPCW_STATE_LOAD. Lets us prove a restored state reproduces the same
       future (the heart of both save states and rewind). */
    typedef size_t (*ssz_t)(void);
    typedef char (*ser_t)(void*, size_t);
    typedef char (*unser_t)(const void*, size_t);
    ssz_t   r_ssz   = (ssz_t)DLSYM(h, "retro_serialize_size");
    ser_t   r_ser   = (ser_t)DLSYM(h, "retro_serialize");
    unser_t r_unser = (unser_t)DLSYM(h, "retro_unserialize");
    int state_save_at = getenv("ZPCW_STATE_SAVE") ? atoi(getenv("ZPCW_STATE_SAVE")) : -1;
    int state_load_at = getenv("ZPCW_STATE_LOAD") ? atoi(getenv("ZPCW_STATE_LOAD")) : -1;
    void *state_blob = NULL; size_t state_sz = 0;

    /* Permanent, opt-in rewind benchmark. The regular diagnostic host copies each
       video frame and scans every audio sample; benchmark mode keeps the callbacks
       valid but lightweight so it measures the core and its state serializer.
       Rewind cadence 1 matches a frontend capturing one state after every frame. */
    int bench_rewind_every = getenv("ZPCW_BENCH_REWIND")
                           ? atoi(getenv("ZPCW_BENCH_REWIND")) : 0;
    int bench_warmup = getenv("ZPCW_BENCH_WARMUP")
                     ? atoi(getenv("ZPCW_BENCH_WARMUP")) : 50;
    if (bench_rewind_every < 0) bench_rewind_every = 0;
    if (bench_warmup < 0) bench_warmup = 0;
    if (bench_warmup >= frames) bench_warmup = frames > 1 ? frames / 2 : 0;
    void *bench_state_blob = NULL;
    size_t bench_state_sz = 0;
    uint64_t bench_run_ns = 0, bench_serialize_ns = 0;
    unsigned bench_serialize_calls = 0, bench_serialize_failures = 0;
    if (g_benchmark_quiet && bench_rewind_every > 0) {
        if (!r_ssz || !r_ser) {
            fprintf(stderr, "[bench] serialize API unavailable\n");
            return 8;
        }
        bench_state_sz = r_ssz();
        bench_state_blob = malloc(bench_state_sz);
        if (!bench_state_blob) {
            fprintf(stderr, "[bench] cannot allocate state buffer (%" PRIuMAX " bytes)\n",
                    (uintmax_t)bench_state_sz);
            return 8;
        }
    }

    /* ZPCW_DISK_POKE=offset: mutate one byte inside the state's mounted-disc
       payload, repair its v3 checksum, load it, then re-serialize through the
       public API and verify the mounted buffer contains that exact byte. */
    int disk_poke_off = getenv("ZPCW_DISK_POKE") ? atoi(getenv("ZPCW_DISK_POKE")) : -1;
    unsigned char disk_poke_expected = 0;
    int disk_poke_ready = 0;

    /* Disk-control test: at frame 1 add a 2nd disc (ZPCW_DISK_ADD=path); at
       ZPCW_DISK_SWAP eject+select+insert it; at ZPCW_RESET_AT reboot (so the new
       disc boots, proving the swap reached the FDC). */
    const char *disk_add = getenv("ZPCW_DISK_ADD");
    int disk_swap_at = getenv("ZPCW_DISK_SWAP") ? atoi(getenv("ZPCW_DISK_SWAP")) : -1;
    /* ZPCW_DISK_EJECT=frame: eject EARLIER than the swap so the running program can
       observe the drive-empty state (2-disc loaders poll for eject, then insert). */
    int disk_eject_at = getenv("ZPCW_DISK_EJECT") ? atoi(getenv("ZPCW_DISK_EJECT")) : -1;
    int reset_at = getenv("ZPCW_RESET_AT") ? atoi(getenv("ZPCW_RESET_AT")) : -1;
    v_void_t r_reset = (v_void_t)DLSYM(h, "retro_reset");
    typedef void (*cheat_t)(unsigned, char, const char*);
    cheat_t r_cheat = (cheat_t)DLSYM(h, "retro_cheat_set");
    const char *cheat_code = getenv("ZPCW_CHEAT");

    LOAD(r_run,   v_void_t,"retro_run");
    for (int i=0;i<frames;i++) {
        if (i==1 && disk_add && g_have_dc) {
            struct game_info gi2; memset(&gi2,0,sizeof gi2); gi2.path = disk_add;
            unsigned original = g_dc.get_index();
            int added = g_dc.set_eject(1) && g_dc.add() &&
                        g_dc.replace(g_dc.get_num()-1, &gi2) &&
                        g_dc.set_index(original) && g_dc.set_eject(0);
            fprintf(stderr,"[host] add disc %s at %u: %s\n", added?"PASS":"FAIL",
                    g_dc.get_num()-1, disk_add);
        }
        if (i==disk_eject_at && g_have_dc) {
            g_dc.set_eject(1);
            fprintf(stderr,"[host] ejected disc @%d\n", i);
        }
        if (i==disk_swap_at && g_have_dc) {
            g_dc.set_eject(1); g_dc.set_index(g_dc.get_num()-1); g_dc.set_eject(0);
            fprintf(stderr,"[host] swapped to disc %u\n", g_dc.get_index());
        }
        if (i==reset_at && r_reset) { r_reset(); fprintf(stderr,"[host] retro_reset @%d\n", i); }
        if (i==1 && cheat_code && r_cheat) { r_cheat(0, 1, cheat_code); fprintf(stderr,"[host] cheat set: %s\n", cheat_code); }
        if (i==state_save_at && r_ssz && r_ser) {
            state_sz = r_ssz();
            state_blob = malloc(state_sz);
            char ok2 = r_ser(state_blob, state_sz);
            fprintf(stderr,"[host] serialize @%d size=%" PRIuMAX " ok=%d\n",
                    i, (uintmax_t)state_sz, ok2);
            if (ok2 && disk_poke_off >= 0) {
                size_t byte_pos, payload_end;
                unsigned char *b = state_blob;
                if (state_disk_offset(b, state_sz, (unsigned)disk_poke_off,
                                      &byte_pos, &payload_end)) {
                    unsigned char orig = b[byte_pos];
                    b[byte_pos] ^= 0xA5;
                    disk_poke_expected = b[byte_pos];
                    wr32le(b+8, crc32_bytes(b+12, payload_end-12));
                    disk_poke_ready = 1;
                fprintf(stderr,"[host] disk poke @%d: %02x -> %02x\n",
                            disk_poke_off, orig, disk_poke_expected);
                }
            }
        }
        if (i==state_load_at && r_unser) {
            /* ZPCW_STATE_IN=path: unserialize a blob from a file (lets a test load a
               state captured by an earlier run) rather than the in-run saved blob. */
            const char *si = getenv("ZPCW_STATE_IN");
            if (si) {
                FILE *f = fopen(si, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
                    void *b = malloc((size_t)sz);
                    if (fread(b, 1, (size_t)sz, f) == (size_t)sz) {
                        char ok2 = r_unser(b, (size_t)sz);
                        fprintf(stderr,"[host] unserialize(file %s) @%d size=%ld ok=%d\n", si, i, sz, ok2);
                    }
                    free(b); fclose(f);
                }
            } else if (state_blob) {
                char ok2 = r_unser(state_blob, state_sz);
                fprintf(stderr,"[host] unserialize @%d ok=%d\n", i, ok2);
            }
            if (disk_poke_ready && r_ser) {
                unsigned char *probe = malloc(state_sz);
                unsigned char actual = 0; int restored = 0;
                if (probe && r_ser(probe, state_sz)) {
                    size_t byte_pos, payload_end;
                    if (state_disk_offset(probe, state_sz, (unsigned)disk_poke_off,
                                          &byte_pos, &payload_end)) {
                        actual = probe[byte_pos];
                        restored = actual == disk_poke_expected;
                    }
                }
                fprintf(stderr,"[host] disk byte @%d after load: %02x restored=%d\n",
                        disk_poke_off, actual, restored);
                free(probe);
            }
        }
        /* ZPCW_INITIAL_UPDATE models RetroArch firing GET_VARIABLE_UPDATE once at
           startup (the core re-reads the saved options on the first frame). */
        if (i==1 && getenv("ZPCW_INITIAL_UPDATE")) g_varupdate_pending = 1;
        if (i==cfg_over_at) {
            g_cmode_pushed[0] = 0; g_cga_pushed[0] = 0; g_varupdate_pending = 1;
            fprintf(stderr,"[host] CONFIG-OVERWRITE at frame %d (push discarded, saved value back)\n", i);
        }
        if (i==open_menu_at && g_update_display) {
            g_update_display();
            fprintf(stderr,"[host] MENU-OPEN at frame %d: phosphor_visible=%d cga_visible=%d\n",
                    i, g_vis_phosphor, g_vis_cga);
        }
        if (i==cycle_at && g_update_display && getenv("ZPCW_CYCLE_SEQ")) {
            char seq[256]; snprintf(seq,sizeof seq,"%s",getenv("ZPCW_CYCLE_SEQ"));
            for (char *t=strtok(seq,"|"); t; t=strtok(NULL,"|")) {
                snprintf(g_cmode_forced,sizeof g_cmode_forced,"%s",t); g_cmode_pushed[0]=0;
                /* The core is PAUSED: only this callback runs. RetroArch rebuilds
                   the open options list only when it returns true, so checking the
                   visibility flags alone would miss a real no-refresh regression. */
                int refreshed = g_update_display() ? 1 : 0;
                fprintf(stderr,"[host] CYCLE '%s' -> phosphor_visible=%d cga_visible=%d refreshed=%d\n",
                        t, g_vis_phosphor, g_vis_cga, refreshed);
            }
        }
        if (g_kbd) for (int k=0;k<nk;k++){ if(i==kd[k]) g_kbd(1,(unsigned)kc[k],(uint32_t)kc[k],0); if(i==ku[k]) g_kbd(0,(unsigned)kc[k],0,0); }
        for (int k=0;k<np;k++){ if(i==pdn[k] && pid[k]<24) g_pad[pid[k]]=1; if(i==pup[k] && pid[k]<24) g_pad[pid[k]]=0; }
        /* In-session "user changed the menu" flips. Written to C globals (not the
           environment) so they behave identically on Linux and Windows/wine. */
        if (i==flip_cmode_at && getenv("ZPCW_CMODE2")) {
            snprintf(g_cmode_forced, sizeof g_cmode_forced, "%s", getenv("ZPCW_CMODE2")); g_cmode_pushed[0]=0;
            if (g_update_display) g_update_display();
            g_varupdate_pending=1;
            fprintf(stderr,"[host] flipped colour mode -> '%s' at frame %d\n", g_cmode_forced, i);
        }
        if (i==flip_phos_at && getenv("ZPCW_PHOSPHOR2")) {
            snprintf(g_phos_forced, sizeof g_phos_forced, "%s", getenv("ZPCW_PHOSPHOR2"));
            if (g_update_display) g_update_display();
            g_varupdate_pending=1;
            fprintf(stderr,"[host] flipped phosphor -> '%s' at frame %d\n", g_phos_forced, i);
        }
        if (i==flip_cga_at && getenv("ZPCW_CGA2")) {
            snprintf(g_cga_forced, sizeof g_cga_forced, "%s", getenv("ZPCW_CGA2")); g_cga_pushed[0]=0;
            if (g_update_display) g_update_display();
            g_varupdate_pending=1;
            fprintf(stderr,"[host] flipped CGA -> '%s' at frame %d\n", g_cga_forced, i);
        }
        if (i==flip_beep_at && getenv("ZPCW_BEEPER2")) {
            snprintf(g_beep_forced, sizeof g_beep_forced, "%s", getenv("ZPCW_BEEPER2"));
            if (g_update_display) g_update_display();
            g_varupdate_pending=1;
            g_audio_nonzero = 0;   /* reset so the post-flip count is isolated */
            fprintf(stderr,"[host] flipped beeper -> '%s' at frame %d (audio counter reset)\n", g_beep_forced, i);
        }
        if (i==flip_arate_at && getenv("ZPCW_ARATE2")) {
            snprintf(g_arate_forced, sizeof g_arate_forced, "%s", getenv("ZPCW_ARATE2"));
            g_varupdate_pending = 1;
            g_av_pushes = 0;   /* reset: count only the pushes the live change triggers */
            fprintf(stderr,"[host] flipped audio rate -> '%s' at frame %d (av-push counter reset)\n", g_arate_forced, i);
        }
        uint64_t bench_t0 = 0;
        if (g_benchmark_quiet && i >= bench_warmup) bench_t0 = benchmark_now_ns();
        r_run();
        if (g_benchmark_quiet && i >= bench_warmup)
            bench_run_ns += benchmark_now_ns() - bench_t0;

        if (g_benchmark_quiet && bench_rewind_every > 0 &&
            ((i + 1) % bench_rewind_every) == 0) {
            if (i >= bench_warmup) bench_t0 = benchmark_now_ns();
            char bench_ok = r_ser(bench_state_blob, bench_state_sz);
            if (i >= bench_warmup) {
                bench_serialize_ns += benchmark_now_ns() - bench_t0;
                bench_serialize_calls++;
                if (!bench_ok) bench_serialize_failures++;
            }
        }
#ifndef _WIN32
        /* ZPCW_PACE=us: sleep between frames to emulate a frontend pacing to 50 fps
           (20000 us). Lets us check audio/video as the real frontend would drive them. */
        if (getenv("ZPCW_PACE")) { struct timespec ts={0, atoi(getenv("ZPCW_PACE"))*1000}; nanosleep(&ts,NULL); }
#endif
    }

    if (g_benchmark_quiet) {
        unsigned measured = (unsigned)(frames - bench_warmup);
        uint64_t total_ns = bench_run_ns + bench_serialize_ns;
        double run_ms = (double)bench_run_ns / 1000000.0;
        double serialize_ms = (double)bench_serialize_ns / 1000000.0;
        double fps = total_ns ? (double)measured * 1000000000.0 / (double)total_ns : 0.0;
        fprintf(stderr,
                "[bench] mode=%s every=%d warmup=%d frames=%u state=%" PRIuMAX
                " run_ms=%.3f serialize_ms=%.3f serialize_calls=%u failures=%u fps=%.3f\n",
                bench_rewind_every > 0 ? "rewind" : "baseline",
                bench_rewind_every, bench_warmup, measured,
                (uintmax_t)bench_state_sz, run_ms, serialize_ms,
                bench_serialize_calls, bench_serialize_failures, fps);
        if (bench_serialize_failures) return 8;
    }

    if (!getenv("ZPCW_NO_FIXED_DUMPS"))
        dump_ppm("/tmp/zpcw_frame_last.ppm");
    if (getenv("ZPCW_DUMP")) dump_ppm(getenv("ZPCW_DUMP"));
    {
        const char *fh = getenv("ZPCW_FRAMEHASHES");
        if (fh && g_frame_fingerprints) {
            FILE *f = fopen(fh, "w");
            size_t count = g_frame_fingerprint_count;
            if (f) {
                for (size_t i = 0; i < count; i++) {
                    const struct frame_fingerprint *fp = &g_frame_fingerprints[i];
                    fprintf(f, "%" PRIuMAX " %016" PRIx64 " %u %u %u %016"
                               PRIx64 " %u\n",
                            (uintmax_t)fp->number, fp->hash, fp->lit,
                            fp->width, fp->height, fp->cell_hash, fp->cells);
                }
                fclose(f);
            }
            fprintf(stderr, "[host] FRAMEHASHES %s count=%" PRIuMAX " ok=%d\n",
                    fh, (uintmax_t)count, f != NULL);
        }
    }
    if (getenv("ZPCW_DUMPMAX") && g_maxframe) {
        FILE *f = fopen(getenv("ZPCW_DUMPMAX"), "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", g_maxw, g_maxh);
            for (size_t i=0;i<(size_t)g_maxw*g_maxh;i++){ uint32_t p=g_maxframe[i];
                unsigned char rgb[3]={(p>>16)&0xff,(p>>8)&0xff,p&0xff}; fwrite(rgb,1,3,f);}
            fclose(f);
        }
    }
    /* ZPCW_RAMDUMP=path: diagnostic snapshot of the complete libretro
       SYSTEM_RAM region.  Unlike a save state this has the same raw layout
       across two builds, which makes differential loader investigations
       possible without depending on either core's private state format. */
    {
        const char *rd = getenv("ZPCW_RAMDUMP");
        if (rd) {
            typedef void  *(*gmd_t)(unsigned);
            typedef size_t (*gms_t)(unsigned);
            gmd_t gmd = (gmd_t)DLSYM(h, "retro_get_memory_data");
            gms_t gms = (gms_t)DLSYM(h, "retro_get_memory_size");
            void *data = gmd ? gmd(2) : NULL;
            size_t size = gms ? gms(2) : 0;
            FILE *f = data && size ? fopen(rd, "wb") : NULL;
            if (f) { fwrite(data, 1, size, f); fclose(f); }
            fprintf(stderr, "[host] RAMDUMP %s size=%" PRIuMAX " ok=%d\n",
                    rd, (uintmax_t)size, f != NULL);
        }
    }
    color_histogram();
    /* ZPCW_STATE_OUT=path: serialize the CURRENT state at the end of the run and
       write the raw blob, so a test can compare a plain run's state against a
       save/unserialize round-trip's state (determinism + round-trip correctness). */
    {
        const char *so = getenv("ZPCW_STATE_OUT");
        if (so && r_ssz && r_ser) {
            size_t sz = r_ssz(); void *b = malloc(sz);
            char ok2 = r_ser(b, sz);
            FILE *f = fopen(so, "wb"); if (f) { fwrite(b, 1, sz, f); fclose(f); }
            fprintf(stderr, "[host] STATE_OUT %s size=%" PRIuMAX " ok=%d\n",
                    so, (uintmax_t)sz, ok2);
            free(b);
        }
    }
    printf("[host] ran %d frames: video_cb=%d audio_cb=%d last=%ux%u\n",
           frames, g_video_frames, g_audio_frames, g_w, g_h);
    fprintf(stderr,"[host] audio_nonzero=%ld\n", g_audio_nonzero);
    fprintf(stderr,"[host] av_info pushes at end = %d\n", g_av_pushes);
    /* The option-menu consistency check: which row would show under "Colour mode".
       phosphor must be visible only for monochrome, CGA only for 4-colour. */
    {
        const char *cm = g_cmode_forced[0] ? g_cmode_forced
                       : g_cmode_pushed[0] ? g_cmode_pushed
                       : (getenv("ZPCW_CMODE") ? getenv("ZPCW_CMODE") : "(default Monochrome)");
        fprintf(stderr, "[host] FINAL colour_mode='%s'  phosphor_visible=%d  cga_visible=%d\n",
                cm, g_vis_phosphor, g_vis_cga);
    }

    if (getenv("ZPCW_STATE_CORRUPT") && r_ssz && r_ser && r_unser) {
        size_t sz=r_ssz(); unsigned char *before=malloc(sz), *bad=malloc(sz), *after=malloc(sz);
        int rejected=0, unchanged=0;
        if (before && bad && after && r_ser(before,sz)) {
            memcpy(bad,before,sz);
            if (sz>32) bad[32]^=0x5a;               /* invalidate wrapper CRC */
            rejected=!r_unser(bad,sz);
            unchanged=r_ser(after,sz) && !memcmp(before,after,sz);
        }
        fprintf(stderr,"[host] corrupt-state rejection: rejected=%d unchanged=%d\n",
                rejected,unchanged);
        free(before); free(bad); free(after);
        if (!rejected || !unchanged) return 6;
    }

    if (getenv("ZPCW_DISK_CONTRACT") && !run_disk_contract(h)) return 7;

    if (getenv("ZPCW_STATE_CONTRACT") && !run_state_contract(h, content ? &gi : NULL)) return 9;

    /* Optional lifecycle stress: reuse the initialized engine repeatedly, then
       exercise deinit + actual library unload. ZPCW_RELOAD_CONTENT alternates a
       second image when supplied. */
    {
        int reloads = getenv("ZPCW_RELOADS") ? atoi(getenv("ZPCW_RELOADS")) : 0;
        const char *alt = getenv("ZPCW_RELOAD_CONTENT");
        for (int n=0; n<reloads; n++) {
            r_unload();
            gi.path = (alt && (n&1)) ? alt : content;
            if (!r_load(&gi)) {
                fprintf(stderr,"[host] reload %d failed: %s\n", n+1, gi.path);
                return 4;
            }
            r_run(); r_run();
        }
        if (reloads) fprintf(stderr,"[host] lifecycle reloads passed: %d\n", reloads);
    }
    r_unload();
    LOAD(r_deinit, v_void_t, "retro_deinit");      r_deinit();
    free(bench_state_blob); free(state_blob); free(g_last); free(g_maxframe);
    free(g_frame_fingerprints);
#ifndef _WIN32
    if (getenv("ZPCW_LSAN_BEFORE_DLCLOSE")) {
        /* LSan is optional; do not require its symbol when linking on macOS. */
        void *process = DLOPEN(NULL);
        int (*leak_check)(void) = process ? (int (*)(void))DLSYM(
            process, "__lsan_do_recoverable_leak_check") : NULL;
        if (leak_check) {
            int leaks = leak_check();
            fprintf(stderr, "[host] pre-dlclose LSan result: %d\n", leaks);
        }
        if (process) DLCLOSE(process);
    }
    if (getenv("ZPCW_DUMP_MAPS")) {
        FILE *maps = fopen("/proc/self/maps", "r");
        char line[1024];
        while (maps && fgets(line, sizeof line, maps))
            if (strstr(line, "zesarpcw_libretro")) fputs(line, stderr);
        if (maps) fclose(maps);
    }
#endif
    if (DLCLOSE(h) != 0) { fprintf(stderr,"[host] library unload failed\n"); return 5; }
    printf("[host] clean shutdown + library unload\n");
    return 0;
}
