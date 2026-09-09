/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Disk control implementation. The core keeps a small list of disc image paths;
    the frontend's disc menu drives eject / select / insert, and an insert mounts
    the chosen image into the PCW floppy controller WITHOUT resetting the CPU (a
    real "insert disc 2 and press a key" swap), by re-reading the .dsk into the FDC
    buffer in place.

    The list is seeded either from the single loaded .dsk, or -- for multi-disc
    titles -- by parsing an .m3u playlist here in the core (the frontend never
    expands .m3u itself; that is the core's job in every multi-disc libretro core).

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#include <stdio.h>
#include "pcw_log.h"
#include <string.h>
#include <inttypes.h>

#include "pcw_disk.h"
#include "pcw_state_io.h"
#include "cpu.h"      /* z80_bit */

/* ZEsarUX disc state: the mounted .dsk path, the "DSK emulation on" flag, and the
   routine that (re-)reads dskplusthree_file_name into the FDC buffer. Clearing the
   flag before calling enable() forces a fresh read -- the in-place swap. */
extern char    dskplusthree_file_name[];
extern z80_bit dskplusthree_emulation;
extern void    dskplusthree_enable(void);
extern void    dskplusthree_disable(void);

#define PCW_DISK_MAX      16
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define PCW_DISK_PATHLEN  PATH_MAX
#define PCW_DISK_LABELLEN 128

static char disk_paths[PCW_DISK_MAX][PCW_DISK_PATHLEN];
static char disk_labels[PCW_DISK_MAX][PCW_DISK_LABELLEN];  /* "" = use basename */
static int  disk_count   = 0;
static int  disk_index   = 0;
static bool disk_ejected = false;
/* A state can restore a modified disc buffer while the virtual tray is open.
   Preserve that exact buffer for the next insertion instead of re-reading the
   original host file and losing guest writes. */
static bool disk_restored_buffer = false;

/* set_initial_image hint: index + the path the frontend expects to find there. */
static unsigned disk_initial = 0;
static char disk_initial_hint[PCW_DISK_PATHLEN];

static bool copy_path(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || cap == 0 || !src || !src[0]) return false;
    n = strlen(src);
    if (n >= cap) {
        pcw_log(RETRO_LOG_ERROR, "[pcw-disk] path too long (%" PRIuMAX
                ", maximum %" PRIuMAX "): %s\n",
                (uintmax_t)n, (uintmax_t)(cap - 1), src);
        return false;
    }
    memcpy(dst, src, n + 1);
    return true;
}

static const char *basename_of(const char *p)
{
    const char *b = p;
    for (; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

/* Case-insensitive string equality (avoids strcasecmp/_stricmp platform split). */
static bool ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return false;
    }
    return *a == *b;
}

/* Mount the currently-selected image into the FDC buffer, no CPU reset. */
static bool mount_current(void)
{
    if (disk_index < 0 || disk_index >= disk_count) return false;
    if (disk_restored_buffer) {
        if (!copy_path(dskplusthree_file_name, PATH_MAX, disk_paths[disk_index]))
            return false;
        dskplusthree_emulation.v = 1;
        disk_restored_buffer = false;
        disk_ejected = false;
        return true;
    }
    if (!copy_path(dskplusthree_file_name, PATH_MAX, disk_paths[disk_index]))
        return false;
    dskplusthree_disable();           /* force enable() to re-read the image */
    dskplusthree_enable();
    if (!dskplusthree_emulation.v) {
        pcw_log(RETRO_LOG_ERROR, "[pcw-disk] failed to insert disc %d: %s\n",
                disk_index, disk_paths[disk_index]);
        return false;
    }
    disk_restored_buffer = false;
    disk_ejected = false;
    pcw_log(RETRO_LOG_INFO, "[pcw-disk] inserted disc %d: %s\n",
            disk_index, basename_of(disk_paths[disk_index]));
    return true;
}

/* ------------------------------------------------------------------------- *
 * libretro disk-control callbacks
 * ------------------------------------------------------------------------- */
static bool cb_set_eject_state(bool ejected)
{
    if (ejected == disk_ejected) return true;
    if (ejected) {
        dskplusthree_disable();
        disk_ejected = true;
        return true;
    }
    return mount_current();           /* insert -> load the selected disc */
}
static bool cb_get_eject_state(void)        { return disk_ejected; }
static unsigned cb_get_image_index(void)
{
    return (disk_index >= 0 && disk_index < disk_count)
         ? (unsigned)disk_index : (unsigned)disk_count;
}
static unsigned cb_get_num_images(void)     { return (unsigned)disk_count; }

static bool cb_set_image_index(unsigned index)
{
    if (!disk_ejected) return false;
    if (index >= (unsigned)disk_count) {
        disk_index = -1;              /* documented "no disc selected" sentinel */
        disk_restored_buffer = false;
        return true;
    }
    disk_index = (int)index;          /* the insert (eject->false) does the mount */
    disk_restored_buffer = false;
    return true;
}

static bool cb_replace_image_index(unsigned index, const struct retro_game_info *info)
{
    if (!disk_ejected || index >= (unsigned)disk_count) return false;
    if (!info || !info->path) {        /* remove the entry */
        if (index + 1 < (unsigned)disk_count) {
            memmove(disk_paths[index], disk_paths[index + 1],
                    ((size_t)disk_count - index - 1) * sizeof disk_paths[0]);
            memmove(disk_labels[index], disk_labels[index + 1],
                    ((size_t)disk_count - index - 1) * sizeof disk_labels[0]);
        }
        disk_count--;
        memset(disk_paths[disk_count], 0, sizeof disk_paths[0]);
        memset(disk_labels[disk_count], 0, sizeof disk_labels[0]);
        if (disk_index == (int)index) {
            disk_index = -1;
            disk_restored_buffer = false;
        }
        else if (disk_index > (int)index) disk_index--;
        return true;
    }
    if (!copy_path(disk_paths[index], sizeof disk_paths[index], info->path))
        return false;
    disk_labels[index][0] = 0;
    if (disk_index == (int)index) disk_restored_buffer = false;
    return true;
}

static bool cb_add_image_index(void)
{
    if (disk_count >= PCW_DISK_MAX) return false;
    disk_paths[disk_count][0]  = 0;
    disk_labels[disk_count][0] = 0;
    disk_count++;
    return true;
}

static bool cb_set_initial_image(unsigned index, const char *path)
{
    disk_initial = index;
    if (path && path[0] && !copy_path(disk_initial_hint, sizeof disk_initial_hint, path))
        return false;
    if (!path || !path[0]) disk_initial_hint[0] = 0;
    return true;
}

static bool cb_get_image_path(unsigned index, char *path, size_t len)
{
    size_t n;
    if (index >= (unsigned)disk_count || !path || len == 0 ||
        !disk_paths[index][0]) return false;
    n = strlen(disk_paths[index]);
    if (n >= len) { path[0] = 0; return false; }
    memcpy(path, disk_paths[index], n + 1);
    return true;
}

static bool cb_get_image_label(unsigned index, char *label, size_t len)
{
    const char *src;
    size_t n;
    if (index >= (unsigned)disk_count || !label || len == 0) return false;
    src = disk_labels[index][0] ? disk_labels[index] : basename_of(disk_paths[index]);
    n = strlen(src);
    if (!n || n >= len) { label[0] = 0; return false; }
    memcpy(label, src, n + 1);
    return true;
}

/* ------------------------------------------------------------------------- *
 * registration
 * ------------------------------------------------------------------------- */
void pcw_disk_register(retro_environment_t environ_cb)
{
    static struct retro_disk_control_ext_callback ext = {
        cb_set_eject_state, cb_get_eject_state, cb_get_image_index, cb_set_image_index,
        cb_get_num_images, cb_replace_image_index, cb_add_image_index,
        cb_set_initial_image, cb_get_image_path, cb_get_image_label
    };
    if (environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &ext)) return;

    /* Older frontend: the v0 interface (no per-image label / initial image). */
    static struct retro_disk_control_callback v0 = {
        cb_set_eject_state, cb_get_eject_state, cb_get_image_index, cb_set_image_index,
        cb_get_num_images, cb_replace_image_index, cb_add_image_index
    };
    environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &v0);
}

bool pcw_disk_seed_first(const char *path)
{
    pcw_disk_reset();
    if (!copy_path(disk_paths[0], sizeof disk_paths[0], path)) return false;
    disk_count   = 1;
    disk_index   = 0;
    disk_ejected = false;
    disk_restored_buffer = false;
    disk_labels[0][0] = 0;
    return true;
}

/* ------------------------------------------------------------------------- *
 * .m3u playlists
 * ------------------------------------------------------------------------- */
int pcw_disk_load_m3u(const char *m3u_path)
{
    FILE *f = fopen(m3u_path, "r");
    if (!f) {
        pcw_log(RETRO_LOG_ERROR, "[pcw-disk] cannot open m3u: %s\n", m3u_path);
        return 0;
    }

    /* Directory of the .m3u (trailing separator kept), for relative entries. */
    char dir[PCW_DISK_PATHLEN];
    snprintf(dir, sizeof dir, "%s", m3u_path);
    int dlen = 0;
    for (int i = 0; dir[i]; i++) if (dir[i] == '/' || dir[i] == '\\') dlen = i + 1;
    dir[dlen] = 0;

    dskplusthree_disable();
    memset(disk_paths, 0, sizeof disk_paths);
    memset(disk_labels, 0, sizeof disk_labels);
    disk_count = 0; disk_index = 0; disk_ejected = false;
    disk_restored_buffer = false;
    int listed = 0;
    bool first_line = true;
    char line[PCW_DISK_PATHLEN];
    while (fgets(line, sizeof line, f)) {
        bool complete = strchr(line, '\n') != NULL || feof(f);
        char *s = line;
        if (first_line && !strncmp(s, "\xEF\xBB\xBF", 3)) s += 3;   /* UTF-8 BOM */
        first_line = false;
        while (*s == ' ' || *s == '\t') s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1]=='\n' || e[-1]=='\r' || e[-1]==' ' || e[-1]=='\t')) *--e = 0;
        if (!complete) {
            int ch;
            while ((ch = fgetc(f)) != '\n' && ch != EOF) { }
            pcw_log(RETRO_LOG_WARN, "[pcw-disk] m3u line exceeds %d bytes; skipped\n",
                    PCW_DISK_PATHLEN - 1);
            continue;
        }
        if (!*s || *s == '#') continue;          /* blank / #EXTM3U / comment */
        listed++;

        char *label = strchr(s, '|');            /* "disc.dsk|Side B" convention */
        if (label) {
            *label++ = 0;
            char *e2 = s + strlen(s);
            while (e2 > s && (e2[-1]==' ' || e2[-1]=='\t')) *--e2 = 0;
            while (*label == ' ' || *label == '\t') label++;
        }
        if (disk_count >= PCW_DISK_MAX) continue;   /* keep reading, for the log */

#ifndef _WIN32
        /* A Windows-authored playlist may use '\' separators; normalise them so
           the entry also resolves on this host. */
        for (char *q = s; *q; q++) if (*q == '\\') *q = '/';
#endif

        bool absolute = (s[0] == '/' || s[0] == '\\' || (s[0] && s[1] == ':'));
        int written = snprintf(disk_paths[disk_count], PCW_DISK_PATHLEN,
                               "%s%s", absolute ? "" : dir, s);
        if (written < 0 || written >= PCW_DISK_PATHLEN) {
            pcw_log(RETRO_LOG_WARN, "[pcw-disk] m3u path too long; skipped: %s\n", s);
            continue;
        }
        snprintf(disk_labels[disk_count], PCW_DISK_LABELLEN, "%s", label ? label : "");

        FILE *probe = fopen(disk_paths[disk_count], "rb");
        if (probe) fclose(probe);
        else {
            pcw_log(RETRO_LOG_WARN, "[pcw-disk] m3u entry not found; skipped: %s\n",
                    disk_paths[disk_count]);
            disk_paths[disk_count][0] = disk_labels[disk_count][0] = 0;
            continue;
        }
        disk_count++;
    }
    fclose(f);

    if (listed > PCW_DISK_MAX)
        pcw_log(RETRO_LOG_WARN, "[pcw-disk] m3u lists %d entries; at most %d are supported\n",
                listed, PCW_DISK_MAX);
    if (listed != disk_count)
        pcw_log(RETRO_LOG_WARN, "[pcw-disk] accepted %d of %d non-comment entries\n",
                disk_count, listed);
    pcw_log(RETRO_LOG_INFO, "[pcw-disk] m3u: %d disc(s) from %s\n",
            disk_count, basename_of(m3u_path));
    return disk_count;
}

int pcw_disk_initial_index(void)
{
    int idx;
    if (disk_initial >= (unsigned)disk_count) return 0;
    idx = (int)disk_initial;
    if (!disk_initial_hint[0]) return idx;
    if (!strcmp(disk_paths[idx], disk_initial_hint)) return idx;

    /* Paths can differ across sessions (drive letters, separators); accept the
       hint if the image NAME at that index matches, else look it up by name. */
    const char *want = basename_of(disk_initial_hint);
    if (ci_eq(basename_of(disk_paths[idx]), want)) return idx;
    for (int i = 0; i < disk_count; i++)
        if (ci_eq(basename_of(disk_paths[i]), want)) return i;
    return 0;   /* the playlist changed since the hint was recorded: ignore it */
}

const char *pcw_disk_path(int index)
{
    if (index < 0 || index >= disk_count) return "";
    return disk_paths[index];
}

int  pcw_disk_get_index(void)   { return disk_index; }
bool pcw_disk_get_ejected(void) { return disk_ejected; }

void pcw_disk_set_index_quiet(int index)
{
    if (index >= 0 && index < disk_count) disk_index = index;
}

void pcw_disk_reset(void)
{
    dskplusthree_disable();
    memset(disk_paths, 0, sizeof disk_paths);
    memset(disk_labels, 0, sizeof disk_labels);
    disk_count = 0;
    disk_index = -1;
    disk_ejected = true;
    disk_restored_buffer = false;
    disk_initial = 0;
    disk_initial_hint[0] = 0;
}

void pcw_disk_begin_load(void)
{
    /* The frontend may call set_initial_image after retro_init and immediately
       before retro_load_game. Clear the previous carousel without discarding
       that one-shot load hint. */
    dskplusthree_disable();
    memset(disk_paths, 0, sizeof disk_paths);
    memset(disk_labels, 0, sizeof disk_labels);
    disk_count = 0;
    disk_index = -1;
    disk_ejected = true;
    disk_restored_buffer = false;
}

bool pcw_disk_insert_current(void)
{
    return mount_current();
}

void pcw_disk_restore_state(int index, bool ejected, bool buffer_present)
{
    disk_index = (index >= 0 && index < disk_count) ? index : -1;
    pcw_restore_boot_path(pcw_disk_path(disk_index));
    if (ejected || disk_index < 0 || !buffer_present) {
        dskplusthree_disable();
        disk_ejected = true;
        disk_restored_buffer = buffer_present && disk_index >= 0;
        return;
    }

    /* The state loader has already restored the complete FDC buffer. Do not
       re-read the host file; just make the selected path and drive state agree. */
    if (!copy_path(dskplusthree_file_name, PATH_MAX, disk_paths[disk_index])) {
        dskplusthree_disable();
        disk_ejected = true;
        return;
    }
    dskplusthree_emulation.v = 1;
    disk_ejected = false;
    disk_restored_buffer = false;
}
