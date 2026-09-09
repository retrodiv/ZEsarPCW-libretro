/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX)
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>

    Per-game gamepad -> PCW-key mapping: resolve the loaded disc to a binding
    from the embedded table (pcw_keyb2joypad_db.h, generated from the maintained
    mapping YAML files; see licenses/MAPPINGS.md) and expose the ZEsarUX key per
    RetroPad slot. Identification is local-only -- sha1 of the .dsk, then the
    software-list shortname derived from the filename -- never a network lookup.

    Distributed under the GNU General Public License v3 (same as ZEsarUX).
*/

#include <stdio.h>
#include "pcw_log.h"
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "pcw_keyb2joypad.h"
#include "pcw_keyb2joypad_db.h"   /* slot/token enums, resolver + the game table */

/* ------------------------------------------------------------------------- *
 * SHA-1 (RFC 3174) -- a self-contained implementation so the core can key the
 * mapping table on the .dsk hash without pulling in a crypto dependency. Used
 * only as a fast, stable local content fingerprint (no security role, no network).
 * ------------------------------------------------------------------------- */
struct sha1_ctx { uint32_t h[5]; uint64_t len; unsigned char buf[64]; size_t n; };

static uint32_t rol32(uint32_t v, int c) { return (v << c) | (v >> (32 - c)); }

static void sha1_block(struct sha1_ctx *c, const unsigned char *p)
{
    uint32_t w[80], a, b, d, e, f, k, t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 |
               (uint32_t)p[i*4+2] << 8 | (uint32_t)p[i*4+3];
    for (i = 16; i < 80; i++)
        w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    a = c->h[0]; b = c->h[1]; d = c->h[2]; e = c->h[3]; f = c->h[4];
    /* (variable names: a,b,c->d,d->e,e->f to avoid clashing with the ctx 'c'.) */
    {
        uint32_t va = a, vb = b, vc = d, vd = e, ve = f;
        for (i = 0; i < 80; i++) {
            if      (i < 20) { f = (vb & vc) | (~vb & vd);            k = 0x5A827999; }
            else if (i < 40) { f = vb ^ vc ^ vd;                     k = 0x6ED9EBA1; }
            else if (i < 60) { f = (vb & vc) | (vb & vd) | (vc & vd);k = 0x8F1BBCDC; }
            else             { f = vb ^ vc ^ vd;                     k = 0xCA62C1D6; }
            t = rol32(va, 5) + f + ve + k + w[i];
            ve = vd; vd = vc; vc = rol32(vb, 30); vb = va; va = t;
        }
        c->h[0] += va; c->h[1] += vb; c->h[2] += vc; c->h[3] += vd; c->h[4] += ve;
    }
}

static void sha1_init(struct sha1_ctx *c)
{
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0; c->len = 0; c->n = 0;
}

static void sha1_update(struct sha1_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = data;
    c->len += len;
    while (len) {
        size_t take = 64 - c->n; if (take > len) take = len;
        memcpy(c->buf + c->n, p, take);
        c->n += take; p += take; len -= take;
        if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; }
    }
}

static void sha1_final(struct sha1_ctx *c, unsigned char out[20])
{
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80;
    int i;
    sha1_update(c, &pad, 1);
    pad = 0;
    while (c->n != 56) sha1_update(c, &pad, 1);
    for (i = 7; i >= 0; i--) { unsigned char b = (bits >> (i*8)) & 0xff; sha1_update(c, &b, 1); }
    for (i = 0; i < 5; i++) {
        out[i*4]   = (c->h[i] >> 24) & 0xff; out[i*4+1] = (c->h[i] >> 16) & 0xff;
        out[i*4+2] = (c->h[i] >> 8) & 0xff;  out[i*4+3] = c->h[i] & 0xff;
    }
}

static int sha1_file(const char *path, unsigned char out[20])
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    struct sha1_ctx c; sha1_init(&c);
    unsigned char buf[1 << 16]; size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) sha1_update(&c, buf, r);
    int ok = !ferror(f);
    fclose(f);
    if (ok) sha1_final(&c, out);
    return ok;
}

/* ------------------------------------------------------------------------- *
 * active mapping
 * ------------------------------------------------------------------------- */

/* The generic navigation profile applied when a disc matches no table entry
   (should not happen for the 140 known titles, but keeps an unknown disc
   playable): d-pad -> cursors, B -> SPACE, A/Start -> RETURN, R3 -> EXIT. */
static const int generic_util[PCW_PAD_NSLOTS] = {
    [PCW_PAD_UP]    = UTIL_KEY_UP,   [PCW_PAD_DOWN]  = UTIL_KEY_DOWN,
    [PCW_PAD_LEFT]  = UTIL_KEY_LEFT, [PCW_PAD_RIGHT] = UTIL_KEY_RIGHT,
    [PCW_PAD_B]     = UTIL_KEY_SPACE, [PCW_PAD_A]    = UTIL_KEY_ENTER,
    [PCW_PAD_START] = UTIL_KEY_ENTER, [PCW_PAD_R3] = UTIL_KEY_CONTROL_R,
};
static const char *const generic_label[PCW_PAD_NSLOTS] = {
    [PCW_PAD_UP]="UP", [PCW_PAD_DOWN]="DOWN", [PCW_PAD_LEFT]="LEFT",
    [PCW_PAD_RIGHT]="RIGHT", [PCW_PAD_B]="SPACE", [PCW_PAD_A]="RETURN",
    [PCW_PAD_START]="RETURN", [PCW_PAD_R3]="Quit to Title (EXIT)",
};

static const struct pcw_pad_entry *g_active = NULL;   /* NULL -> generic profile */

static const struct pcw_pad_entry *lookup_sha1(const unsigned char sha1[20])
{
    int i;
    for (i = 0; i < PCW_PAD_DB_COUNT; i++)
        if (!memcmp(pcw_pad_db[i].sha1, sha1, 20)) return &pcw_pad_db[i];
    return NULL;
}

/* Derive a candidate software-list shortname from a content path: basename,
   drop the extension, lowercase, strip a trailing "_pcw" and any non-alnum
   tail (" - side a", " (uk)" leftovers). Best-effort fallback when the exact
   .dsk sha1 is not in the table (e.g. a renamed or re-imaged disc). */
static const struct pcw_pad_entry *lookup_shortname(const char *path)
{
    if (!path) return NULL;
    const char *base = path;
    const char *p;
    for (p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;

    char name[64]; size_t n = 0;
    for (p = base; *p && *p != '.' && n < sizeof(name) - 1; p++)
        name[n++] = (char)tolower((unsigned char)*p);
    name[n] = 0;
    /* strip trailing "_pcw" */
    if (n >= 4 && !strcmp(name + n - 4, "_pcw")) { n -= 4; name[n] = 0; }
    /* strip any trailing run of non [a-z0-9] (spaces, dashes, "(uk)" remnants) */
    while (n && !isalnum((unsigned char)name[n - 1])) name[--n] = 0;

    if (!n) return NULL;
    int i;
    for (i = 0; i < PCW_PAD_DB_COUNT; i++)
        if (!strcmp(pcw_pad_db[i].id, name)) return &pcw_pad_db[i];
    return NULL;
}

void pcw_keyb2joypad_select(const char *content_path)
{
    g_active = NULL;
    unsigned char sha[20];
    const char *how = "generic";

    if (content_path && sha1_file(content_path, sha)) {
        const struct pcw_pad_entry *e = lookup_sha1(sha);
        if (e) { g_active = e; how = "sha1"; }
    }
    if (!g_active) {
        const struct pcw_pad_entry *e = lookup_shortname(content_path);
        if (e) { g_active = e; how = "shortname"; }
    }

    if (g_active)
        pcw_log(RETRO_LOG_INFO, "[pcw-pad] mapping '%s' (%s) matched by %s\n",
                g_active->id, g_active->name, how);
    else
        pcw_log(RETRO_LOG_INFO, "[pcw-pad] no table entry for '%s' -> generic profile\n",
                content_path ? content_path : "(none)");
}

int pcw_keyb2joypad_util_for_slot(int slot)
{
    if (slot < 0 || slot >= PCW_PAD_NSLOTS) return 0;
    if (g_active) return pcw_tk_to_util[g_active->slot[slot]];
    return generic_util[slot];
}

const char *pcw_keyb2joypad_label_for_slot(int slot)
{
    if (slot < 0 || slot >= PCW_PAD_NSLOTS) return "";
    if (g_active) {
        const char *label = g_active->label[slot];
        return (label && label[0]) ? label : pcw_tk_name[g_active->slot[slot]];
    }
    return generic_label[slot] ? generic_label[slot] : "";
}
