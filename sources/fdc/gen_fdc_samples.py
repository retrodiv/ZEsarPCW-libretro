#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3 (see ../../LICENSE).
"""Generate src/libretro/pcw_fdc_samples.h from the original PituKa WAVs."""

from __future__ import annotations

import argparse
import struct
import sys
import wave
from pathlib import Path


HERE = Path(__file__).resolve().parent
SAMPLE_RATE = 44100
SAMPLES = (
    ("motor", "motor.wav"),
    ("read", "read_drive.wav"),
    ("seek", "seek_drive.wav"),
)

HEADER = """/* SPDX-License-Identifier: GPL-3.0-only
 * C array template: Copyright (c) 2026 retrodiv <retrodiv@proton.me>; see LICENSE.
 */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX) -- floppy-drive sound samples.

    PCM recordings of a real 3" disc drive (motor spin loop, head read chatter,
    head seek), used by pcw_fdc_sound.c to voice the drive while a disc loads.
    44100 Hz, 16-bit signed, mono.

    These samples originate from the PituKa / wiituka Amstrad CPC emulator:

        PituKa - Amstrad CPC Emulator
        (c) Copyright 2004-2005 David Colmenero - D_Skywalk
        GNU General Public License v2 or (at your option) any later version.

    They are redistributed here under the GPL (v3, as the rest of this core),
    preserving the original author's copyright as the licence requires.

    Generated from the upstream WAV assets; do not edit by hand.
*/

#ifndef PCW_FDC_SAMPLES_H
#define PCW_FDC_SAMPLES_H

#include <stdint.h>

#define PCW_FDC_SAMPLE_RATE 44100
"""


def read_pcm(path: Path) -> tuple[int, ...]:
    try:
        with wave.open(str(path), "rb") as wav:
            details = (
                wav.getnchannels(),
                wav.getsampwidth(),
                wav.getframerate(),
                wav.getcomptype(),
            )
            expected = (1, 2, SAMPLE_RATE, "NONE")
            if details != expected:
                raise ValueError(
                    f"{path}: expected mono 16-bit PCM at {SAMPLE_RATE} Hz, got {details}"
                )
            frames = wav.readframes(wav.getnframes())
    except (EOFError, wave.Error) as exc:
        raise ValueError(f"{path}: invalid WAV: {exc}") from exc
    if len(frames) % 2:
        raise ValueError(f"{path}: odd-length 16-bit PCM payload")
    return struct.unpack(f"<{len(frames) // 2}h", frames)


def wrapped_values(values: tuple[int, ...]) -> list[str]:
    """Match the historical header's deterministic 92-column payload wrapping."""
    lines: list[str] = []
    current = ""
    for value in values:
        token = f"{value},"
        if current and len(current) + len(token) > 92:
            lines.append("    " + current)
            current = ""
        current += token
    if current:
        lines.append("    " + current)
    return lines


def render(input_dir: Path) -> bytes:
    lines = [HEADER.rstrip("\n")]
    for symbol, filename in SAMPLES:
        values = read_pcm(input_dir / filename)
        lines.extend(("", f"static const int16_t pcw_fdc_{symbol}_pcm[] = {{"))
        lines.extend(wrapped_values(values))
        lines.extend((
            "};",
            f"static const int pcw_fdc_{symbol}_len = "
            f"(int)(sizeof(pcw_fdc_{symbol}_pcm)/sizeof(int16_t));",
        ))
    lines.extend(("", "", "#endif /* PCW_FDC_SAMPLES_H */"))
    return ("\n".join(lines) + "\n").encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, default=HERE)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--out", type=Path, help="write the generated C header")
    mode.add_argument("--check", type=Path, help="verify an existing C header")
    args = parser.parse_args()
    try:
        generated = render(args.input_dir)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))

    if args.check is not None:
        try:
            current = args.check.read_bytes()
        except OSError as exc:
            parser.error(str(exc))
        if current != generated:
            print(f"out of date: {args.check}", file=sys.stderr)
            return 1
        print(f"up to date: {args.check}")
        return 0

    assert args.out is not None
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(generated)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
