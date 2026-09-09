#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-3.0-only
# ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3 (see LICENSE; same licence as ZEsarUX).
"""Generate a PCW on-screen-keyboard skin from its editable PNG.

Run from the repository root after editing Keyboard_US.png or Keyboard_UK.png:

    python3 sources/mappings/gen_osk_skin.py
    python3 sources/mappings/gen_osk_skin.py \
        --png sources/mappings/Keyboard_UK.png --symbol pcw_osk_skin_uk
    make -j4
    make check

The default input is Keyboard_US.png beside this script. In sources/mappings,
the default output is src/libretro/<symbol>.h, resolved from the script rather
than the working directory. Elsewhere, --out is required. Explicit --png and
--out paths are used as supplied. Install the pinned Pillow version from
sources/mappings/requirements-generators.txt before regenerating a skin.

Optional --preview FILE writes the composed skin and a nearest-neighbour 3x
preview into an existing directory. The previews show the same composition as
src/libretro/pcw_osk.c. A normal core build uses the checked-in headers directly.

Artwork and decomposition
-------------------------
The artwork is an opaque 360x133 RGBA image: logo and divider, five key rows,
word-processor keypad and cursor keys. Regular keys occupy 16-pixel tiles at an
18-pixel pitch; the first key is at x=10, with row tops at y=36,54,72,90,108.
The logo is retained as artwork in the header sprite.

Uniform key bevels use OUTER (167,173,163), INNER (198,209,199), FACE
(190,201,191) and BG (238,241,236). The border is one pixel on all sides; the
inner highlight covers top, left and right. The renderer reconstructs these
bevels at each key's width. Black legends become separate 1bpp sprites so the
face can be highlighted independently. Every labelled key must have artwork;
the spacebar is intentionally unlabelled. A key that cannot be reconstructed
exactly, including the stepped RETURN shape, is stored as an RGBA sprite.
Its transparent mask excludes background and pixels owned by neighbouring
rectangular keys, even when they fall inside the sprite's bounding box.

SHIFT-LOCK's 2x2 indicator is dynamic: (87,0,13) when off, red when active.
It is painted from the current lock state. The selected key's face is amber,
with the legend drawn on top. The whole panel is anchored to the bottom of the
256-line screen. For 720-pixel monochrome modes, the renderer doubles width
only (scale_x=2, scale_y=1), preserving the proportions of the 360-pixel artwork.

Keyboard and renderer contract
------------------------------
M maps physical keys to the PCW keyboard matrix: pressed keys clear a bit in
pcw_keyboard_table. F1/F3/F5/F7 use Shift with F2/F4/F6/F8. The cursor cluster
uses the core's UTIL_KEY_* codes. The word-processor keys occupy keypad cells:
EXCH/FIND=k7, DOC/PAGE=k8, UNIT/PARA=k9, LINE/EOL=k4, UP=k5, WORD/CHAR=k6,
LEFT=k1, centre-grid=k2, RIGHT=k3, REL=k0, DOWN=k.; the keys flanking SPACE are
keypad [+] and [-], and ENT is keypad Enter.

Output contains dimensions, bevel colours, header/legend/irregular-key sprites
and per-key geometry, matrix cell or utility key, kind, flags and navigation
row. The types are declared in src/libretro/pcw_osk_skin_types.h. Keep
src/libretro/pcw_osk.c's template_pixel rule and layout interpretation in sync
with this generator. Header dependencies trigger an incremental rebuild.

Distributed under the GNU General Public License v3 (same as ZEsarUX).
"""
import argparse, os, re, sys
from pathlib import Path

# ---------------------------------------------------------------------------
# PCW keyboard matrix (machines/pcw.c:967).  (row, mask) per physical key.
#   b7=0x80 b6=0x40 b5=0x20 b4=0x10 b3=0x08 b2=0x04 b1=0x02 b0=0x01
# pressed = clear bit, released = set bit (table inits to 0xFF).
# ---------------------------------------------------------------------------
M = {
 'k2':(0,0x80),'k3':(0,0x40),'k6':(0,0x20),'k9':(0,0x10),'paste':(0,0x08),'f2':(0,0x04),'k0':(0,0x02),'f4':(0,0x01),
 'k1':(1,0x80),'k5':(1,0x40),'k4':(1,0x20),'k8':(1,0x10),'copy':(1,0x08),'cut':(1,0x04),'ptr':(1,0x02),'exit':(1,0x01),
 'kpplus':(2,0x80),'half':(2,0x40),'shift':(2,0x20),'k7':(2,0x10),'hash':(2,0x08),'return':(2,0x04),'rbracket':(2,0x02),'delr':(2,0x01),
 'period':(3,0x80),'slash':(3,0x40),'semicolon':(3,0x20),'pound':(3,0x10),'P':(3,0x08),'lbracket':(3,0x04),'minus':(3,0x02),'equal':(3,0x01),
 'comma':(4,0x80),'M':(4,0x40),'K':(4,0x20),'L':(4,0x10),'I':(4,0x08),'O':(4,0x04),'9':(4,0x02),'0':(4,0x01),
 'space':(5,0x80),'N':(5,0x40),'J':(5,0x20),'H':(5,0x10),'Y':(5,0x08),'U':(5,0x04),'7':(5,0x02),'8':(5,0x01),
 'V':(6,0x80),'B':(6,0x40),'F':(6,0x20),'G':(6,0x10),'T':(6,0x08),'R':(6,0x04),'5':(6,0x02),'6':(6,0x01),
 'X':(7,0x80),'C':(7,0x40),'D':(7,0x20),'S':(7,0x10),'W':(7,0x08),'E':(7,0x04),'3':(7,0x02),'4':(7,0x01),
 'Z':(8,0x80),'lock':(8,0x40),'A':(8,0x20),'tab':(8,0x10),'Q':(8,0x08),'stop':(8,0x04),'2':(8,0x02),'1':(8,0x01),
 'dell':(9,0x80),
 'alt':(0xA,0x80),'kpdot':(0xA,0x40),'enter':(0xA,0x20),'f8':(0xA,0x10),'kpminus':(0xA,0x08),'can':(0xA,0x04),'extra':(0xA,0x02),'f6':(0xA,0x01),
}

# util_teclas codes the core also understands (utils.h) -- used for the cursor
# cluster, which is proven to drive PCW games through util_set_reset_key.
UTIL = {'UP':148,'DOWN':147,'LEFT':145,'RIGHT':146}  # UTIL_KEY_UP/DOWN/LEFT/RIGHT (informative; real values resolved in C)

# kinds / flags mirrored in pcw_osk_skin_types.h
K_NORMAL, K_SHIFT, K_LOCK, K_ALT, K_EXTRA = 0, 1, 2, 3, 4
F_NEEDS_SHIFT, F_BAKED, F_LOCKDOT = 1, 2, 4

# A logical key: (label, matrix-name|None, kind, needs_shift, util-name|None, note)
def key(label, m=None, kind=K_NORMAL, shift=False, util=None, note=""):
    return dict(label=label, m=m, kind=kind, shift=shift, util=util, note=note)

# ---------------------------------------------------------------------------
# Logical layout, left-to-right per navigation row, matching the locale PNGs.
# Geometry (x/w) comes from auto-detecting the PNG; this gives MEANING + order.
# RET is inserted into the caps row at its detected x (it is tall + baked).
# ---------------------------------------------------------------------------
ROW_NUM = [
    key("STOP",'stop'), key("1",'1'), key("2",'2'), key("3",'3'), key("4",'4'),
    key("5",'5'), key("6",'6'), key("7",'7'), key("8",'8'), key("9",'9'), key("0",'0'),
    key("-",'minus'), key("=",'equal'),
    key("\x10DEL",'delr', note="del-right"), key("\x11DEL",'dell', note="del-left"),
    key("CAN",'can'), key("CUT",'cut'), key("COPY",'copy'), key("PASTE",'paste'),
]
ROW_TAB = [
    key("TAB",'tab'),
    key("Q",'Q'), key("W",'W'), key("E",'E'), key("R",'R'), key("T",'T'), key("Y",'Y'),
    key("U",'U'), key("I",'I'), key("O",'O'), key("P",'P'), key("[",'lbracket'), key("]",'rbracket'),
    key("f8/f7",'f8'),
    key("EXCH/FIND",'k7'), key("DOC/PAGE",'k8'), key("UNIT/PARA",'k9'),
]
ROW_CAPS = [
    key("LOCK",'lock',kind=K_LOCK),
    key("A",'A'), key("S",'S'), key("D",'D'), key("F",'F'), key("G",'G'), key("H",'H'),
    key("J",'J'), key("K",'K'), key("L",'L'),
    key(";",'semicolon'), key(":",'semicolon',shift=True), key("\xa4",'pound', note="pound/currency"),
    # RET inserted here by detection (tall, baked)
    key("f6/f5",'f6'),
    key("LINE/EOL",'k4'), key("\x1e",None,util='UP', note="cursor up / k5"),
    key("WORD/CHAR",'k6'),
]
ROW_SHIFT = [
    key("SHIFT",'shift',kind=K_SHIFT),
    key("Z",'Z'), key("X",'X'), key("C",'C'), key("V",'V'), key("B",'B'), key("N",'N'), key("M",'M'),
    key(",",'comma'), key(".",'period'), key("/",'slash'), key("1/2 @",'half'),
    key("SHIFT",'shift',kind=K_SHIFT),
    key("f4/f3",'f4'),
    key("\x11",None,util='LEFT', note="cursor left / k1"), key("\xfe",'k2', note="centre grid key"),
    key("\x10",None,util='RIGHT', note="cursor right / k3"),
]
ROW_SPACE = [
    key("ALT",'alt',kind=K_ALT), key("EXTRA",'extra',kind=K_EXTRA),
    key("\xfb",'kpplus', note="keypad +"), key("",'space', note="spacebar: intentionally blank"), key("\xfc",'kpminus', note="keypad -"),
    key("PTR",'ptr'), key("EXIT",'exit'),
    key("f2/f1",'f2'),
    key("REL",'k0'), key("\x1f",None,util='DOWN', note="cursor down / k."),
    key("ENT",'enter'),
]
NAVROWS = [ROW_NUM, ROW_TAB, ROW_CAPS, ROW_SHIFT, ROW_SPACE]

# RET key (tall, stepped). Geometry refined from the PNG below.
RET = key("RET",'return', note="tall, baked")

# Row tile-tops + the y to probe for key extents (face mid).
ROW_TILE_TOP = [36, 54, 72, 90, 108]
TILE_H = 16
PROBE_DY = 8   # mid of the face band

# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    here = Path(__file__).resolve().parent
    ap.add_argument("--png", type=Path, default=here / "Keyboard_US.png",
                    help="input PNG (default: Keyboard_US.png beside this script)")
    ap.add_argument("--out", type=Path,
                    help="output header (default: src/libretro/<symbol>.h in this repository)")
    ap.add_argument("--symbol", default="pcw_osk_skin_us",
                    help="C descriptor/array namespace (for example pcw_osk_skin_uk)")
    ap.add_argument("--preview", default="")
    a = ap.parse_args()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", a.symbol):
        sys.exit("--symbol must be a C identifier")
    if a.out is None:
        if (here.name != "mappings" or here.parent.name != "sources"
                or not (here.parents[1] / "src" / "libretro").is_dir()):
            ap.error("cannot locate src/libretro from this script; pass --out explicitly")
        a.out = here.parents[1] / "src" / "libretro" / (a.symbol + ".h")
    symbol = a.symbol
    macro = symbol.upper()
    ident = lambda suffix: "%s_%s" % (symbol, suffix)
    define = lambda suffix: "%s_%s" % (macro, suffix)

    import PIL
    from PIL import Image
    if PIL.__version__ != "12.1.1":
        sys.exit("Pillow 12.1.1 required; install requirements-generators.txt beside this script")
    im = Image.open(a.png).convert("RGBA")
    W, H = im.size
    px = im.load()
    def rgb(x, y): return px[x, y][:3]
    def opaque(x, y): return px[x, y][3] == 255

    # palette: pick the canonical bevel colours by frequency
    from collections import Counter
    cnt = Counter(rgb(x, y) for y in range(H) for x in range(W) if opaque(x, y))
    BG    = (238, 241, 236)
    FACE  = (190, 201, 191)
    INNER = (198, 209, 199)
    OUTER = (167, 173, 163)
    BLACK = (0, 0, 0)
    GRANATE = None
    for c, _ in cnt.items():
        if c[0] > 40 and c[1] < 60 and c[2] < 60:
            GRANATE = c; break
    GRANATE = GRANATE or (87, 0, 13)
    # A *pure-black* legend pixel. Some legends (RET, the right SHIFT) are drawn by
    # the artist with a two-tone font -- black outline + GREY FILL. We must NOT
    # flatten the grey to black (that merges the letters into a blob); such keys
    # therefore fail the template match and are baked verbatim, grey and all, so
    # they render exactly like the art. Only solid-black single-tone legends are
    # extracted as 1bpp sprites. (BLACK is defined with the palette above.)

    HEADER_H = ROW_TILE_TOP[0]            # everything above the first key row = baked header
    LOCKON = (255, 0, 0)

    # ---- detect key x-extents per row (runs of non-BG at the probe line) ----
    def detect_runs(y):
        cols = [x for x in range(W) if opaque(x, y) and rgb(x, y) != BG]
        runs = []
        if cols:
            s = p = cols[0]
            for x in cols[1:]:
                if x == p + 1: p = x
                else: runs.append((s, p)); s = p = x
            runs.append((s, p))
        return runs

    # RET bounding box: scan its column region, take the full vertical extent.
    ret_top = ROW_TILE_TOP[1]; ret_bot = ROW_TILE_TOP[2] + TILE_H  # 54..88
    ret_x0, ret_x1 = 254, 277
    RET_RECT = (ret_x0, ret_top, ret_x1 - ret_x0 + 1, ret_bot - ret_top)

    keys = []           # final key dicts with geometry
    warnings = []

    for ri, (logical, top) in enumerate(zip(NAVROWS, ROW_TILE_TOP)):
        runs = detect_runs(top + PROBE_DY)
        # drop the RET top-overlap run on the TAB row (it belongs to caps/RET)
        if ri == 1:
            runs = [r for r in runs if not (r[0] >= ret_x0 - 2 and r[1] <= ret_x1 + 2)]
        # build the ordered list, inserting RET into the caps row at its x
        seq = list(logical)
        if ri == 2:
            # find insert position: after the last key whose x < RET
            pass
        if len(runs) != len(seq) + (1 if ri == 2 else 0):
            warnings.append("row %d: %d runs vs %d logical keys" %
                            (ri, len(runs), len(seq) + (1 if ri == 2 else 0)))
        # zip runs to keys; if caps row, the run at RET_x is RET
        li = 0
        for (rx0, rx1) in runs:
            w = rx1 - rx0 + 1
            if ri == 2 and rx0 >= ret_x0 - 2 and rx1 <= ret_x1 + 2:
                kd = dict(RET); kd.update(navrow=ri, x=ret_x0, y=ret_top,
                                          w=RET_RECT[2], h=RET_RECT[3], baked=True)
                keys.append(kd)
                continue
            kd = dict(seq[li]) if li < len(seq) else key("")
            li += 1
            kd.update(navrow=ri, x=rx0, y=top, w=w, h=TILE_H, baked=False)
            keys.append(kd)

    if warnings:
        sys.stderr.write("[gen_osk_skin] layout notes:\n  " + "\n  ".join(warnings) + "\n")

    # ---- reconstruct chrome from the template; bake any key that doesn't match -
    def template_pixel(lx, ly, w, h):
        """colour of a standard key's local pixel (lx,ly) in a w x h tile."""
        if ly == 0 or ly == h - 1:          return OUTER     # top / bottom outer
        if lx == 0 or lx == w - 1:           return OUTER     # left / right outer
        if ly == 1 or lx == 1 or lx == w - 2: return INNER    # inner L/T/R
        return FACE

    canvas = [[BG for _ in range(W)] for _ in range(H)]  # full reconstructed panel (for preview + verify)
    # header: copied verbatim from the PNG
    for y in range(HEADER_H):
        for x in range(W):
            canvas[y][x] = rgb(x, y) if opaque(x, y) else BG

    label_blobs = []     # list of (w,h,bytes) 1bpp; key.label -> index
    baked_blobs = []     # list of (w,h,[rgba]) ; key.baked_idx -> index

    def add_label(pts, w, h):
        if not pts or w <= 0 or h <= 0:
            return -1
        stride = (w + 7) // 8
        data = bytearray(stride * h)
        for (x, y) in pts:
            if 0 <= x < w and 0 <= y < h:
                data[y * stride + (x >> 3)] |= (0x80 >> (x & 7))
        label_blobs.append((w, h, bytes(data)))
        return len(label_blobs) - 1

    # Preserve layout ownership before the chrome fallback can mark regular
    # keys as baked too. RET's bounding box includes part of its left neighbour.
    rectangular_keys = [kd for kd in keys if not kd.get('baked')]

    def add_baked(kd):
        x0, y0, w, h = kd['x'], kd['y'], kd['w'], kd['h']
        neighbours = [other for other in rectangular_keys if other is not kd]
        pixels = []
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                c = rgb(x, y)
                owned_by_neighbour = any(
                    other['x'] <= x < other['x'] + other['w'] and
                    other['y'] <= y < other['y'] + other['h']
                    for other in neighbours
                )
                if not opaque(x, y) or c == BG or owned_by_neighbour:
                    pixels.append(None)            # transparent (bg shows through)
                else:
                    pixels.append(c)               # verbatim -- keeps two-tone legends
        baked_blobs.append((w, h, pixels))
        return len(baked_blobs) - 1

    for kd in keys:
        x, y, w, h = kd['x'], kd['y'], kd['w'], kd['h']
        # --- legend: prefer the artist's black pixels inside the key face ---
        face_pts = []
        gx0 = gy0 = 1 << 30; gx1 = gy1 = -1
        for yy in range(y, min(y + h, H)):
            for xx in range(x, min(x + w, W)):
                if opaque(xx, yy) and rgb(xx, yy) == BLACK:
                    face_pts.append((xx, yy))
                    gx0 = min(gx0, xx); gy0 = min(gy0, yy)
                    gx1 = max(gx1, xx); gy1 = max(gy1, yy)
        if face_pts:
            lw, lh = gx1 - gx0 + 1, gy1 - gy0 + 1
            pts = {(xx - gx0, yy - gy0) for (xx, yy) in face_pts}
            kd['label_idx'] = add_label(pts, lw, lh)
            kd['lw'], kd['lh'] = lw, lh
            kd['lx'], kd['ly'] = gx0 - x, gy0 - y     # keep the artist's exact position
        elif kd['label']:
            raise SystemExit(
                "[gen_osk_skin] labelled key %r at (%d,%d) has no black artwork; "
                "complete locale Keyboard_XX.png instead of synthesising a legend" %
                (kd['label'], x, y)
            )
        else:
            kd['label_idx'] = -1; kd['lw'] = kd['lh'] = 0; kd['lx'] = kd['ly'] = 0

        # --- chrome: reconstruct; bake if the PNG disagrees ---
        if kd.get('baked'):
            kd['baked_idx'] = add_baked(kd)
        else:
            mismatch = False
            for ly in range(h):
                for lx in range(w):
                    xx, yy = x + lx, y + ly
                    if xx >= W or yy >= H: continue
                    if not opaque(xx, yy):
                        continue
                    c = rgb(xx, yy)
                    if c == BLACK or c == GRANATE:   # legend / lock dot, drawn separately
                        continue
                    if c != template_pixel(lx, ly, w, h):
                        mismatch = True; break
                if mismatch: break
            if mismatch:
                kd['baked'] = True
                kd['baked_idx'] = add_baked(kd)
            else:
                kd['baked_idx'] = -1

        # a baked sprite already contains its legend -- don't double-draw it
        if kd.get('baked'):
            kd['label_idx'] = -1

        # --- lock dot: 2x2 granate at the key's top-left inner corner ---
        if kd['kind'] == K_LOCK:
            dot = None
            for yy in range(y, min(y + h, H)):
                for xx in range(x, min(x + w, W)):
                    if opaque(xx, yy) and rgb(xx, yy) == GRANATE:
                        dot = (xx, yy) if dot is None else (min(dot[0], xx), min(dot[1], yy))
            if dot is None:
                dot = (x + 1, y + 1)
            kd['dot'] = dot
        else:
            kd['dot'] = None

    # ---- paint the reconstructed panel into `canvas` (verify + preview) ----
    def put(x, y, c):
        if 0 <= x < W and 0 <= y < H and c is not None:
            canvas[y][x] = c
    for kd in keys:
        x, y, w, h = kd['x'], kd['y'], kd['w'], kd['h']
        if kd.get('baked'):
            bw, bh, pix = baked_blobs[kd['baked_idx']]
            for ly in range(bh):
                for lx in range(bw):
                    put(x + lx, y + ly, pix[ly * bw + lx])
        else:
            for ly in range(h):
                for lx in range(w):
                    put(x + lx, y + ly, template_pixel(lx, ly, w, h))
        if kd['label_idx'] >= 0:
            lw, lh, data = label_blobs[kd['label_idx']]
            stride = (lw + 7) // 8
            ox = x + kd['lx']
            oy = y + kd['ly']
            for ly in range(lh):
                for lx in range(lw):
                    if data[ly * stride + (lx >> 3)] & (0x80 >> (lx & 7)):
                        put(ox + lx, oy + ly, BLACK)
        if kd['dot'] is not None:
            dx, dy = kd['dot']
            for yy in range(dy, dy + 2):
                for xx in range(dx, dx + 2):
                    put(xx, yy, GRANATE)

    # ---- emit the namespaced locale skin header ----
    def col(c): return "0xFF%02X%02X%02X" % (c[0], c[1], c[2])
    out = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/*",
        "    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).",
        "    Copyright (c) 2026 retrodiv <retrodiv@proton.me>. Distributed under the GNU General Public License v3",
        "    (the same licence as ZEsarUX, Copyright (C) Cesar Hernandez Bano).",
        "    GENERATED FILE -- do not edit by hand; re-run its generator (see below).",
        "*/",
    ]
    o = out.append
    o("/*")
    o(" * %s -- an on-screen-keyboard locale skin for the ZEsarPCW core." % os.path.basename(a.out))
    o(" *")
    o(" * GENERATED by sources/mappings/gen_osk_skin.py from %s -- DO NOT EDIT BY HAND;" % os.path.basename(a.png))
    o(" * re-run the generator (read its header comment for the full design + the PNG")
    o(" * format). Consumed by src/libretro/pcw_osk.c, which renders these tables.")
    o(" *")
    o(" * The keyboard is the real PCW8256, decomposed so the empty-button styling is")
    o(" * separate from the legends:")
    o(" *   *_W/H, *_HEADER_H         panel size; rows 0..HEADER_H are the AMSTRAD")
    o(" *                             logo/divider (a baked sprite), the rest is keys.")
    o(" *   *_C_*                     bevel palette: a key is OUTER border on 4 sides,")
    o(" *                             INNER border on left/top/right, flat FACE -- so a")
    o(" *                             key of any width is reconstructed pixel-perfect by")
    o(" *                             rule (see template_pixel in pcw_osk.c; keep the two")
    o(" *                             in sync). LOCKOFF/LOCKON = SHIFT-LOCK dot colours.")
    o(" *   *_header[]                baked AMSTRAD header (RGBA, W x HEADER_H).")
    o(" *   *_labels[]/_bits[]        1bpp legend sprite per key (black text), copied")
    o(" *                             directly from the PNG; no font is synthesised.")
    o(" *   *_bakeds[]/_pix[]         RGBA sprites for irregular keys the bevel rule can't")
    o(" *                             draw (the tall stepped RETURN). 0 = transparent.")
    o(" *   *_keys[]                  one entry per key: x,y,w,h; PCW key-matrix cell")
    o(" *                             (prow,pmask, active-low) OR a util_teclas code;")
    o(" *                             kind (normal/shift/lock/alt/extra); flags")
    o(" *                             (NEEDS_SHIFT/BAKED/LOCKDOT); label/baked indices;")
    o(" *                             navrow (d-pad grouping); lock-dot xy; lx,ly =")
    o(" *                             the legend's exact offset in the key (kept from the")
    o(" *                             PNG, NOT re-centred, so legends match the art 1:1).")
    o(" *")
    o(" * The word-processor cluster follows its PCW numeric-keypad matrix positions;")
    o(" * see the generator header for the exact k0..k9 mapping.")
    o(" */")
    guard = "%s_INCLUDED" % macro
    o("#ifndef %s" % guard)
    o("#define %s" % guard)
    o('#include "pcw_osk_skin_types.h"')
    o("")
    o("#define %s %d" % (define("W"), W))
    o("#define %s %d" % (define("H"), H))
    o("#define %s %d" % (define("HEADER_H"), HEADER_H))
    o("")
    o("#define %s %s" % (define("C_BG"), col(BG)))
    o("#define %s %s" % (define("C_OUTER"), col(OUTER)))
    o("#define %s %s" % (define("C_INNER"), col(INNER)))
    o("#define %s %s" % (define("C_FACE"), col(FACE)))
    o("#define %s %s" % (define("C_TEXT"), col(BLACK)))
    o("#define %s %s" % (define("C_LOCKOFF"), col(GRANATE)))
    o("#define %s %s" % (define("C_LOCKON"), col(LOCKON)))
    o("")
    # header sprite (RGBA, opaque)
    o("static const uint32_t %s[%s*%s] = {" %
      (ident("header"), define("W"), define("HEADER_H")))
    vals = []
    for y in range(HEADER_H):
        for x in range(W):
            vals.append(col(canvas[y][x]))
    for i in range(0, len(vals), 12):
        o("  " + ",".join(vals[i:i+12]) + ",")
    o("};")
    o("")
    # label bitmaps
    o("static const uint8_t %s[] = {" % ident("label_bits"))
    label_off = []
    blob = bytearray()
    for (w, h, data) in label_blobs:
        label_off.append(len(blob)); blob += data
    line = []
    for i, b in enumerate(blob):
        line.append("0x%02X" % b)
        if len(line) == 16:
            o("  " + ",".join(line) + ","); line = []
    if line: o("  " + ",".join(line) + ",")
    if not blob: o("  0")
    o("};")
    o("static const pcw_osk_skin_label_t %s[] = {" % ident("labels"))
    for i, (w, h, data) in enumerate(label_blobs):
        o("  { %d, %d, %d }," % (w, h, label_off[i]))
    if not label_blobs: o("  { 0, 0, 0 }")
    o("};")
    o("")
    # baked sprites (RGBA, 0 = transparent)
    o("static const uint32_t %s[] = {" % ident("baked_pix"))
    baked_off = []
    bvals = []
    for (w, h, pix) in baked_blobs:
        baked_off.append(len(bvals))
        for c in pix:
            bvals.append("0" if c is None else col(c))
    for i in range(0, len(bvals), 12):
        o("  " + ",".join(bvals[i:i+12]) + ",")
    if not bvals: o("  0")
    o("};")
    o("static const pcw_osk_skin_baked_t %s[] = {" % ident("bakeds"))
    for i, (w, h, pix) in enumerate(baked_blobs):
        o("  { %d, %d, %d }," % (w, h, baked_off[i]))
    if not baked_blobs: o("  { 0, 0, 0 }")
    o("};")
    o("")
    # keys
    o("static const pcw_osk_skin_key_t %s[] = {" % ident("keys"))
    UTILC = {'UP':'UTIL_KEY_UP','DOWN':'UTIL_KEY_DOWN','LEFT':'UTIL_KEY_LEFT','RIGHT':'UTIL_KEY_RIGHT'}
    for kd in keys:
        prow = pmask = 0
        if kd['m'] is not None:
            prow, pmask = M[kd['m']]
        util = UTILC.get(kd['util'], '0') if kd['util'] else '0'
        flags = 0
        if kd['shift']: flags |= F_NEEDS_SHIFT
        if kd.get('baked'): flags |= F_BAKED
        if kd['kind'] == K_LOCK: flags |= F_LOCKDOT
        dotx = kd['dot'][0] if kd['dot'] else -1
        doty = kd['dot'][1] if kd['dot'] else -1
        note = (" /* %s */" % kd['note']) if kd['note'] else ""
        o("  { %d,%d,%d,%d, %d,0x%02X, %s, %d,%d, %d,%d, %d, %d,%d, %d,%d },%s" % (
            kd['x'], kd['y'], kd['w'], kd['h'], prow, pmask, util,
            kd['kind'], flags, kd['label_idx'], kd['baked_idx'], kd['navrow'],
            dotx, doty, kd['lx'], kd['ly'], note))
    o("};")
    o("#define %s ((int)(sizeof %s / sizeof %s[0]))" %
      (define("NKEYS"), ident("keys"), ident("keys")))
    o("")
    o("static const pcw_osk_skin_t %s = {" % symbol)
    o("  %s, %s, %s," % (define("W"), define("H"), define("HEADER_H")))
    o("  %s, %s, %s, %s, %s, %s, %s," %
      tuple(define(x) for x in ("C_BG", "C_OUTER", "C_INNER", "C_FACE", "C_TEXT", "C_LOCKOFF", "C_LOCKON")))
    o("  %s, %s, %s," % (ident("header"), ident("label_bits"), ident("labels")))
    o("  %s, %s, %s, %s" %
      (ident("baked_pix"), ident("bakeds"), ident("keys"), define("NKEYS")))
    o("};")
    o("")
    o("#endif /* %s */" % guard)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    a.out.write_bytes(("\n".join(out) + "\n").encode("utf-8"))
    sys.stderr.write("[gen_osk_skin] wrote %s : %d keys, %d labels, %d baked\n" %
                     (a.out, len(keys), len(label_blobs), len(baked_blobs)))

    # ---- preview PNG (what the core will render at 1x) ----
    if a.preview:
        prev = Image.new("RGB", (W, H))
        pp = prev.load()
        for y in range(H):
            for x in range(W):
                pp[x, y] = canvas[y][x]
        prev.save(a.preview)
        prev.resize((W*3, H*3), Image.NEAREST).save(a.preview.replace(".png", "_3x.png"))
        sys.stderr.write("[gen_osk_skin] preview -> %s\n" % a.preview)

if __name__ == "__main__":
    main()
