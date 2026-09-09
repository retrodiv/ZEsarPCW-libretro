#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>
# Distributed under the GNU General Public License, version 3, WITHOUT ANY
# WARRANTY. See the LICENSE file at the repository root for the full terms.
"""Opt-in, repeatable throughput benchmark for libretro rewind.

This is deliberately not part of ``make check``: absolute timing depends on the
host. It drives the permanent ZPCW_BENCH hooks in libretro_host.c, reports medians,
and can compare a candidate core against an older reference build.
"""
from __future__ import annotations

import argparse
import os
import re
import statistics
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
BENCH_RE = re.compile(
    r"^\[bench\].*state=(\d+).*run_ms=([0-9.]+) "
    r"serialize_ms=([0-9.]+).*fps=([0-9.]+)$", re.MULTILINE
)


def build_host(destination: Path) -> None:
    subprocess.run([
        "cc", "-O2", "-Wall", "-Wextra", "-Werror",
        str(HERE / "host" / "libretro_host.c"), "-ldl", "-o", str(destination),
    ], check=True)


def run_once(host: Path, core: str, disk: str, model: str,
             system_dir: str, frames: int, warmup: int,
             rewind: bool) -> tuple[int, float, float, float]:
    env = dict(os.environ)
    env.update({"ZPCW_MODEL": model, "ZPCW_BENCH_WARMUP": str(warmup)})
    env["ZPCW_BENCH_REWIND" if rewind else "ZPCW_BENCH"] = "1"
    result = subprocess.run(
        [str(host), core, disk, system_dir, str(frames)],
        env=env, text=True, capture_output=True, check=True,
    )
    match = BENCH_RE.search(result.stdout + result.stderr)
    if not match:
        raise RuntimeError("benchmark result was not emitted by the host")
    return (int(match.group(1)), float(match.group(2)),
            float(match.group(3)), float(match.group(4)))


def measure(host: Path, core: str, disk: str, model: str,
            system_dir: str, frames: int, warmup: int,
            runs: int) -> dict[str, float]:
    results = {"baseline": [], "rewind": []}
    states = []
    serial = []
    for _ in range(runs):
        base = run_once(host, core, disk, model, system_dir, frames, warmup, False)
        rew = run_once(host, core, disk, model, system_dir, frames, warmup, True)
        results["baseline"].append(base[3])
        results["rewind"].append(rew[3])
        states.append(rew[0])
        serial.append(rew[2] / (frames - warmup))
    return {
        "baseline": statistics.median(results["baseline"]),
        "rewind": statistics.median(results["rewind"]),
        "serialize_ms": statistics.median(serial),
        "state": statistics.median(states),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", default=os.environ.get("ZPCW_SO", "./zesarpcw_libretro.so"),
                        help="candidate .so (default: $ZPCW_SO or ./zesarpcw_libretro.so)")
    parser.add_argument("--reference", help="optional older .so for an A/B comparison")
    parser.add_argument("--disk", default=os.environ.get("ZPCW_DSK"),
                        help="test .dsk (or $ZPCW_DSK)")
    parser.add_argument("--sysdir", default=os.environ.get("ZPCW_SYSDIR", "."))
    parser.add_argument("--model", default="PCW8512", choices=("PCW8256", "PCW8512"))
    parser.add_argument("--frames", type=int, default=1000)
    parser.add_argument("--warmup", type=int, default=200)
    parser.add_argument("--runs", type=int, default=5)
    args = parser.parse_args()

    core = args.core
    disk = args.disk
    if not disk:
        parser.error("set --disk or $ZPCW_DSK")
    if args.frames <= args.warmup or args.runs < 1:
        parser.error("frames must exceed warmup and runs must be positive")

    with tempfile.TemporaryDirectory(prefix="zpcw_bench_") as temp:
        host = Path(temp) / "libretro_host"
        build_host(host)
        measurements = []
        if args.reference:
            measurements.append(("reference", measure(
                host, args.reference, disk, args.model, args.sysdir,
                args.frames, args.warmup, args.runs)))
        measurements.append(("candidate", measure(
            host, core, disk, args.model, args.sysdir,
            args.frames, args.warmup, args.runs)))

    print(f"model={args.model} frames={args.frames} warmup={args.warmup} runs={args.runs}")
    for label, result in measurements:
        print(f"{label:9s} baseline={result['baseline']:.1f} fps  "
              f"rewind={result['rewind']:.1f} fps  "
              f"serialize={result['serialize_ms']:.3f} ms/frame  "
              f"state={int(result['state'])} bytes")
    if len(measurements) == 2:
        old = measurements[0][1]
        new = measurements[1][1]
        print(f"rewind uplift: {(new['rewind'] / old['rewind'] - 1) * 100:+.1f}%  "
              f"serializer reduction: {(1 - new['serialize_ms'] / old['serialize_ms']) * 100:.1f}%")


if __name__ == "__main__":
    main()
