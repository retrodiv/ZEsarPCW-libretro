/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Disk control: lets the frontend list, eject and swap the discs of a multi-disc
    title (side A / side B, multi-load games, .m3u playlists) without resetting the
    machine -- so a game that asks for "disc 2" can be fed it from the disc menu.

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#ifndef PCW_DISK_H
#define PCW_DISK_H

#include <stdbool.h>
#include "libretro.h"

/* Register the disk control interface with the frontend from retro_init, before
   content is loaded, so an .m3u initial-image hint can arrive before load. */
void pcw_disk_register(retro_environment_t environ_cb);

/* Seed the image list with the first (already-loaded) disc. Call from
   retro_load_game once the disc is mounted, for plain .dsk content (an .m3u
   seeds the whole list via pcw_disk_load_m3u instead). The frontend can still
   grow the list at runtime via add_image_index / replace_image_index. */
bool pcw_disk_seed_first(const char *path);

/* Parse an .m3u playlist into the image list. Entries are one path per line,
   relative to the .m3u's directory (absolute paths work too), with the usual
   libretro conveniences: '#' comment lines, an optional UTF-8 BOM, and an
   optional "path|Display label" suffix per entry. Returns the number of discs
   listed (0 = unreadable or empty). Call from retro_load_game BEFORE the
   machine boots, then boot from pcw_disk_path(pcw_disk_initial_index()). */
int pcw_disk_load_m3u(const char *m3u_path);

/* The disc index to start on: the frontend's set_initial_image hint ("continue
   where I left off"), validated against the current list -- falls back to 0 if
   the hint is absent, out of range, or names a different image (per the
   libretro spec, a stale hint must be ignored). */
int pcw_disk_initial_index(void);

/* Path of image <index> ("" if out of range). */
const char *pcw_disk_path(int index);

/* Current carousel state for the savestate wrapper. */
int  pcw_disk_get_index(void);
bool pcw_disk_get_ejected(void);
void pcw_disk_set_index_quiet(int index);

/* Session/lifecycle helpers. reset() clears the carousel and physically ejects
   the FDC. insert_current() mounts the internally selected image. restore_state()
   reconciles carousel/eject state after the caller has restored the FDC buffer. */
void pcw_disk_reset(void);
void pcw_disk_begin_load(void);
bool pcw_disk_insert_current(void);
void pcw_disk_restore_state(int index, bool ejected, bool buffer_present);

#endif /* PCW_DISK_H */
