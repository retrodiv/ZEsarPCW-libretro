/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GNU GPL v3; see LICENSE. */
#ifndef PCW_STATE_IO_H
#define PCW_STATE_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Fixed-width little-endian device state, with a read-only validation pass
   before any device is restored. No pointers, padding or host paths on disk. */
struct pcw_state_io {
    const uint8_t *input;
    uint8_t *output;
    size_t size, offset;
    bool apply, ok;
};

static inline int32_t pcw_state_value(struct pcw_state_io *s, int32_t value,
                                       int32_t minimum, int32_t maximum)
{
    uint32_t bits;
    if (!s->ok || s->size - s->offset < 4) { s->ok = false; return value; }
    if (s->input) {
        const uint8_t *p = s->input + s->offset;
        bits = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
               (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        value = bits <= INT32_MAX ? (int32_t)bits :
                -1 - (int32_t)(UINT32_MAX - bits);
    }
    if (value < minimum || value > maximum) { s->ok = false; return value; }
    if (s->output) {
        uint8_t *p = s->output + s->offset;
        bits = (uint32_t)value;
        p[0] = (uint8_t)bits; p[1] = (uint8_t)(bits >> 8);
        p[2] = (uint8_t)(bits >> 16); p[3] = (uint8_t)(bits >> 24);
    }
    s->offset += 4;
    return value;
}

#define PCW_STATE_FIELD(s, field, minimum, maximum) do { \
    int32_t pcw_state_v = pcw_state_value(s, (int32_t)(field), minimum, maximum); \
    if ((s)->apply && (s)->ok) (field) = pcw_state_v; \
} while (0)

static inline void pcw_state_bytes(struct pcw_state_io *s, void *data, size_t n)
{
    if (!s->ok || n > s->size - s->offset) { s->ok = false; return; }
    if (s->output) memcpy(s->output + s->offset, data, n);
    if (s->input && s->apply) memcpy(data, s->input + s->offset, n);
    s->offset += n;
}

bool pcw_fdc_state(struct pcw_state_io *s);
bool pcw_machine_state(struct pcw_state_io *s);
void pcw_restore_boot_path(const char *path);

#endif
