#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3 (see LICENSE; same licence as ZEsarUX).
"""Generate the embedded PCW gamepad table from the maintained YAML files.

Edit pcw_keyb2joypad.yml for existing controls and
pcw_keyb2joypad_extra.yml for additional disc hashes or new titles, then run:

    python3 sources/mappings/pcw_keyb2joypad_gen.py c

The generator uses only Python's standard library. Input and output defaults
are resolved relative to this script, independently of the working directory.
The compiled table identifies discs locally by SHA-1 or title identifier.
Metadata provenance is recorded in licenses/MAPPINGS.md.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEF_YAML = HERE / "pcw_keyb2joypad.yml"
DEF_EXTRA = HERE / "pcw_keyb2joypad_extra.yml"
# Defaults are confined to the public sources/mappings layout.
DEF_CHEADER = None
if (HERE.name == "mappings" and HERE.parent.name == "sources"
        and (HERE.parents[1] / "src" / "libretro").is_dir()):
    DEF_CHEADER = HERE.parents[1] / "src" / "libretro" / "pcw_keyb2joypad_db.h"

# --------------------------------------------------------------------------- #
# The pad-input slots (one per RetroPad control we bind). Order is the C slot
# enum order (PCW_PAD_*). The left analog stick mirrors the d-pad at runtime, so
# it needs no separate slots here.
# --------------------------------------------------------------------------- #
SLOTS = ["up", "down", "left", "right",
         "b", "a", "y", "x", "l", "r", "l2", "r2", "start", "select", "r3"]

# Token -> ZEsarUX util_teclas symbol (or, for letters/digits, a literal ASCII
# char). The core resolver emits exactly these; verified against the PCW key
# matrix dispatch in upstream utils.c (util_set_reset_key):
#   RETURN -> ENTER    -> PCW &3FF2.b2 (ret)
#   ENTER  -> KP_ENTER -> PCW &3FFA.b5 (ent)
#   STOP   -> ESC      -> PCW &3FF8.b2 (stop)
#   EXIT   -> CONTROL_R -> PCW &3FF1.b0 (exit)
#   CTRL  -> CONTROL_L -> PCW &3FFA.b1 (extra)   [host Ctrl is PCW 'extra']
#   DEL   -> DEL       -> PCW &3FF2.b0 (del>)
#   cursors/space/return/f1-f8 -> the obvious PCW keys
TOKEN_UTIL = {
    "UP": "UTIL_KEY_UP", "DOWN": "UTIL_KEY_DOWN",
    "LEFT": "UTIL_KEY_LEFT", "RIGHT": "UTIL_KEY_RIGHT",
    "SPACE": "UTIL_KEY_SPACE", "RETURN": "UTIL_KEY_ENTER",
    "ENTER": "UTIL_KEY_KP_ENTER", "STOP": "UTIL_KEY_ESC",
    "EXIT": "UTIL_KEY_CONTROL_R", "CTRL": "UTIL_KEY_CONTROL_L",
    "EXTRA": "UTIL_KEY_CONTROL_L", "SHIFT": "UTIL_KEY_SHIFT_L",
    "ALT": "UTIL_KEY_ALT_L", "DEL": "UTIL_KEY_DEL", "TAB": "UTIL_KEY_TAB",
    "f1": "UTIL_KEY_F1", "f2": "UTIL_KEY_F2", "f3": "UTIL_KEY_F3",
    "f4": "UTIL_KEY_F4", "f5": "UTIL_KEY_F5", "f6": "UTIL_KEY_F6",
    "f7": "UTIL_KEY_F7", "f8": "UTIL_KEY_F8",
}

# English labels are maintained; older translated values remain readable.
CONF_VAL  = {
    "low": 0, "medium": 1, "high": 2,
    "baja": 0, "media": 1, "alta": 2,
}


def tk_id(token: str) -> str:
    """The C TK_* identifier for a token ('q' -> TK_Q, 'SPACE' -> TK_SPACE)."""
    if token == "+":
        return "TK_PLUS"
    if len(token) == 1 and token.isalnum():
        return "TK_" + token.upper()
    return "TK_" + token.upper()


def norm_token(raw: str) -> str | None:
    """Normalise a YAML key token to the supported vocabulary.

    Letters come lowercase, the named keys uppercase, f-keys as 'fN'. Returns
    None for anything outside the vocabulary (combos etc. -> first part)."""
    t = raw.strip()
    if not t:
        return None
    if t == "+":
        return "+"
    if "+" in t:                       # a combo: take the leading key (note kept)
        t = t.split("+", 1)[0].strip()
    if re.fullmatch(r"f[1-8]", t):
        return t
    up = t.upper()
    if up in ("UP", "DOWN", "LEFT", "RIGHT", "SPACE", "RETURN", "ENTER",
              "STOP", "EXIT", "CTRL", "EXTRA", "SHIFT", "ALT", "DEL", "TAB"):
        return up
    if len(t) == 1 and t.isalpha():
        return t.lower()
    if len(t) == 1 and t.isdigit():
        return t
    return None


def apply_extra(entries: list[dict], path: Path) -> list[dict]:
    """Add unique alias hashes and new titles from the extra YAML.

    Existing titles keep the controls in the main YAML. Alias rows only add
    fingerprints; a new title supplies its own name, profile and controls.
    """
    extra = read_yaml(path)
    by_id = {e["id"]: e for e in entries}
    aliased = added = 0
    for ex in extra:
        tgt = by_id.get(ex["id"])
        if tgt is not None:
            for s in ex.get("sha1s", []):
                if s and s not in tgt["sha1s"]:
                    tgt["sha1s"].append(s)
                    aliased += 1
        else:
            ex.setdefault("binds", {})
            ex.setdefault("notes", {})
            ex.setdefault("conf", 0)
            ex.setdefault("profile", "game")
            ex.setdefault("name", ex["id"])
            entries.append(ex)
            by_id[ex["id"]] = ex
            added += 1
    print(f"[gen] extras: +{aliased} alias sha1s, +{added} new titles "
          f"(from {path.name})", file=sys.stderr)
    return entries


def read_yaml(path: Path) -> list[dict]:
    """Read the maintained flat list of YAML mapping records."""
    entries = []
    cur = None
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        if raw.startswith("- id:"):
            if cur:
                entries.append(cur)
            cur = {"binds": {}, "notes": {}, "sha1s": []}
            cur["id"] = raw.split(":", 1)[1].strip()
            continue
        if cur is None:
            continue
        key, _, val = raw.strip().partition(":")
        val = val.strip()
        if key == "sha1":
            if val:
                cur["sha1s"].insert(0, val)
        elif key == "sha1_alt":
            cur["sha1s"].extend(v for v in val.split(",") if v)
        elif key == "name":
            cur["name"] = val[1:-1] if val.startswith('"') else val
        elif key == "conf":
            cur["conf"] = CONF_VAL.get(val, 1)
        elif key == "profile":
            cur["profile"] = val
        elif key.startswith("input_pad_"):
            slot = key[len("input_pad_"):]
            # Maintained schema: Action text (TOKEN), or a bare TOKEN when the
            # action is unknown. Accept the old TOKEN Action form for compatibility
            # with older maintained records.
            bare = norm_token(val)
            if bare:
                cur["binds"][slot] = bare
                continue
            action_key = re.match(r"^(.*?)\s+\(([^()]*)\)\s*$", val)
            if action_key:
                action = action_key.group(1).strip()
                token = norm_token(action_key.group(2))
                if token:
                    cur["binds"][slot] = token
                    if action:
                        cur["notes"][slot] = action
                    continue
            parts = val.split(None, 1)
            token = norm_token(parts[0]) if parts else None
            if token:
                cur["binds"][slot] = token
                if len(parts) > 1:
                    cur["notes"][slot] = parts[1]
    if cur:
        entries.append(cur)
    return entries


# --------------------------------------------------------------------------- #
# C header emit
# --------------------------------------------------------------------------- #
def all_tokens(entries: list[dict]) -> list[str]:
    seen = []
    # Stable, readable order: named keys, f-keys, letters, digits.
    order = (["UP", "DOWN", "LEFT", "RIGHT", "SPACE", "RETURN", "ENTER", "STOP",
              "EXIT", "CTRL", "EXTRA", "SHIFT", "ALT", "DEL", "TAB"]
             + [f"f{i}" for i in range(1, 9)]
             + [chr(c) for c in range(ord("a"), ord("z") + 1)]
             + [str(d) for d in range(10)]
             + ["+"])
    used = set()
    for e in entries:
        for slot in SLOTS:
            t = e["binds"].get(slot)
            if t:
                used.add(t)
    for t in order:
        if t in used:
            seen.append(t)
    # Any unexpected token still gets an id so the table never dangles.
    for t in sorted(used):
        if t not in seen:
            seen.append(t)
    return seen


def emit_c(entries: list[dict], path: Path) -> None:
    tokens = all_tokens(entries)
    profile_val = {"game": 0, "text": 1, "program": 2, "mouse": 3}

    L = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/*",
        "    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).",
        "    Copyright (c) 2026 retrodiv <retrodiv@proton.me>. Distributed under the GNU General Public License v3",
        "    (the same licence as ZEsarUX, Copyright (C) Cesar Hernandez Bano).",
        "    GENERATED FILE -- do not edit by hand; re-run its generator (see below).",
        "*/",
    ]
    L.append("/* pcw_keyb2joypad_db.h -- GENERATED by sources/mappings/pcw_keyb2joypad_gen.py.")
    L.append("   Do not edit by hand; edit sources/mappings/pcw_keyb2joypad.yml and re-run")
    L.append("   `python3 sources/mappings/pcw_keyb2joypad_gen.py c`. Per-game gamepad -> PCW-key table embedded")
    L.append("   in the zesarpcw_libretro core, keyed by the .dsk sha1 / software-list")
    L.append("   shortname of the loaded disc. */")
    L.append("#ifndef PCW_KEYB2JOYPAD_DB_H")
    L.append("#define PCW_KEYB2JOYPAD_DB_H")
    L.append("")
    L.append('#include "pcw_keyb2joypad.h"   /* enum pcw_pad_slot (PCW_PAD_*) */')
    L.append('#include "pcw_keyboard.h"      /* PCW input key codes (UTIL_KEY_*) */')
    L.append("")
    L.append("/* Token ids stored in the table (0 = unbound). */")
    L.append("enum pcw_token {")
    L.append("    TK_NONE = 0,")
    for t in tokens:
        L.append(f"    {tk_id(t)},")
    L.append("    TK__COUNT")
    L.append("};")
    L.append("")
    L.append("/* Token -> ZEsarUX key fed to util_set_reset_key(): a util_teclas value,")
    L.append("   or a raw ASCII code for letters/digits (util handles <128 as ASCII). */")
    L.append("static const short pcw_tk_to_util[TK__COUNT] = {")
    L.append("    [TK_NONE] = 0,")
    for t in tokens:
        if t in TOKEN_UTIL:
            val = TOKEN_UTIL[t]
        elif len(t) == 1 and t.isalpha():
            val = f"'{t.lower()}'"
        elif len(t) == 1 and (t.isdigit() or t == "+"):
            val = f"'{t}'"
        else:
            val = "0"
        L.append(f"    [{tk_id(t)}] = {val},")
    L.append("};")
    L.append("")
    L.append("/* Token -> printable label for the binding (debug / introspection). */")
    L.append("static const char *const pcw_tk_name[TK__COUNT] = {")
    L.append('    [TK_NONE] = "",')
    for t in tokens:
        L.append(f'    [{tk_id(t)}] = "{t}",')
    L.append("};")
    L.append("")
    L.append("struct pcw_pad_entry {")
    L.append("    unsigned char  sha1[20];                 /* boot .dsk sha1 (canonical key) */")
    L.append("    const char    *id;                       /* MAME software-list shortname  */")
    L.append("    const char    *name;                     /* human title                   */")
    L.append("    unsigned char  conf;                     /* 0 low / 1 medium / 2 high     */")
    L.append("    unsigned char  profile;                  /* 0 game /1 text /2 program /3 mouse */")
    L.append("    unsigned char  slot[PCW_PAD_NSLOTS];     /* per-slot token id             */")
    L.append("    const char    *label[PCW_PAD_NSLOTS];    /* Action (TOKEN), or TOKEN      */")
    L.append("};")
    L.append("")

    # The table: one row per entry, primary sha1. Multi-disk alt sha1s are added
    # as extra rows pointing at the same binding so any disk of a set resolves.
    rows = []
    for e in entries:
        binds = e["binds"]
        slot_inits = []
        for s in SLOTS:
            t = binds.get(s)
            slot_inits.append(tk_id(t) if t else "TK_NONE")
        prof = profile_val.get(e.get("profile", "game"), 0)
        name_c = e.get("name", e["id"]).replace("\\", "\\\\").replace('"', '\\"')
        for sha in (e["sha1s"] or [""]):
            sha_bytes = bytes.fromhex(sha) if sha else b"\0" * 20
            sb = ",".join(str(b) for b in sha_bytes.ljust(20, b"\0")[:20])
            slot_labels = []
            for s in SLOTS:
                t = binds.get(s)
                note = e.get("notes", {}).get(s, "")
                label = f"{note} ({t})" if note and t else (t or "")
                label = label.replace("\\", "\\\\").replace('"', '\\"')
                slot_labels.append(f'"{label}"' if label else "NULL")
            rows.append((e["id"], sha, sb, name_c, e.get("conf", 1), prof,
                         slot_inits, slot_labels))

    L.append(f"#define PCW_PAD_DB_COUNT {len(rows)}")
    L.append("static const struct pcw_pad_entry pcw_pad_db[PCW_PAD_DB_COUNT] = {")
    for (sid, sha, sb, name_c, conf, prof, slot_inits, slot_labels) in rows:
        L.append(f'    {{ {{{sb}}}, "{sid}", "{name_c}", {conf}, {prof},')
        L.append(f"      {{ {', '.join(slot_inits)} }},")
        L.append(f"      {{ {', '.join(slot_labels)} }} }},")
    L.append("};")
    L.append("")
    L.append("#endif /* PCW_KEYB2JOYPAD_DB_H */")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(("\n".join(L) + "\n").encode("utf-8"))
    print(f"[gen] wrote {path} ({len(rows)} rows, {len(tokens)} tokens)")


# --------------------------------------------------------------------------- #
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stage", choices=["c"], nargs="?", default="c",
                    help="generate the C table (default)")
    ap.add_argument("--yaml", type=Path, default=DEF_YAML,
                    help="main mapping YAML (default: beside this script)")
    ap.add_argument("--extra", type=Path, default=DEF_EXTRA,
                    help="additional hashes/titles YAML (default: beside this script)")
    ap.add_argument("--cheader", type=Path, default=DEF_CHEADER,
                    help="output C header (default: this repository's generated table)")
    args = ap.parse_args(argv)
    if args.cheader is None:
        ap.error("cannot locate src/libretro from this script; pass --cheader explicitly")

    try:
        entries = apply_extra(read_yaml(args.yaml), args.extra)
        emit_c(entries, args.cheader)
    except (OSError, ValueError) as error:
        ap.error(str(error))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
