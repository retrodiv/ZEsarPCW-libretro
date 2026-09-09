<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Contributing to ZEsarPCW

Use this repository's Issues tab for bug reports, questions, feature proposals
and attribution or licensing concerns. Submit code and documentation changes
as pull requests against this repository.

## Report a problem

Describe what you expected, what happened and the shortest steps that reproduce
it. Include:

- The release version and, for a source build, its commit or snapshot identity.
- Operating system, CPU architecture and RetroArch version.
- PCW model, relevant core options and input device or keyboard mapping.
- Disc title, image size and SHA-256; for a playlist, the relevant entries and
  the sequence of eject/select/insert operations.
- A relevant log excerpt and whether the same issue occurs after loading the
  content afresh, without a save state or enabled cheats.

Use `sha256sum "game.dsk"` on Linux, `shasum -a 256 "game.dsk"` on macOS, or
`Get-FileHash "game.dsk" -Algorithm SHA256` in PowerShell. Review logs and playlist
paths before attaching them; replace personal paths with placeholders and omit
credentials or other private information. Share a small reproducer you are
entitled to distribute when possible, rather than uploading commercial discs.

For a concern about a distributed file, identify the file and release, explain
the concern and provide any public reference that helps establish its source
or attribution. Issues are public; do not post private correspondence there.

## Make and test a change

Work directly in this checkout. The build, existing tests, embedded resources
and editable assets are included here. On Linux, from the repository root:

```sh
make platform=unix -j4
make platform=unix check -j4
```

`make check` works after source edits. It builds the core and runs the included
unit tests and libretro runtime host with the bundled OpenPCW-OS disk.
Python 3.10+ and a native C compiler are needed for these checks. See
[README.md](README.md#build-and-check) for other targets, cross-check limits
and compiler selection. Run `make clean` when changing the compiler or flags
for the same platform.

Explain the bug or intended behaviour in your pull request, describe the change
and report the checks you ran. Keep changes focused. Preserve component
copyright and licence notices, and credit new sources or assets. For generated
assets, edit their [preferred source files](sources/README.md), run the included
generator and submit the resulting header together with those source changes.

The existing Python build and packaging tests can also be run on an unmodified
release checkout:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
```

Packaging tests validate the recorded source manifest. After editing sources,
use the manifest-update instructions in [PACKAGING.md](PACKAGING.md) if you
need to exercise packaging; ordinary build/runtime checks do not need this.
Keep binaries, build directories and local logs out of pull requests.

## Collect diagnostics

On Linux or macOS, launch RetroArch from a terminal. With a locally built Linux
core and your content, for example:

```sh
ZPCW_INPUTLOG=1 ZPCW_DISCID=1 retroarch --verbose \
    -L ./zesarpcw_libretro.so game.dsk \
    > /tmp/zesarpcw-diagnostic.log 2>&1
```

Replace `game.dsk` with your content path; use the matching `.dylib` on macOS. Set **Settings → Logging → Core Logging Level** to
**0 (Debug)** to receive the input trace through RetroArch's logger. These variables
are optional and affect diagnostics only:

| Variable | Output |
|---|---|
| `ZPCW_INPUTLOG` | RetroPad key transitions and OSK toggles, tagged `[pcw-pad]`, at libretro debug level; stderr fallback if the frontend supplies no logger. |
| `ZPCW_DISCID` | Disc-buffer FNV-1a fingerprint, bytes read, autorun decision and command, tagged `[discid]`, directly on stderr when an external disc file is read. |

A variable's presence enables its trace, even if its value is `0`. Leave it
unset to disable it. The disc fingerprint is for autorun diagnostics; use a
full-file SHA-256 when identifying an image in a report. Neither variable
creates a log file itself; the terminal command redirects the output. The
embedded `openpcw_os.dsk` shortcut and buffers restored from a save state do not
read an external disc file, so they do not emit the fingerprint trace.

## Rewind benchmark

The included benchmark is optional and is separate from `make check` because
timings depend on the host. After building a Linux core, run:

```sh
python3 tests/benchmark_rewind.py --core ./zesarpcw_libretro.so \
    --disk src/openpcw_os.dsk --sysdir src --model PCW8512 \
    --frames 1000 --warmup 200 --runs 5
```

It reports median baseline and rewind throughput, serialization time per frame
and state size. Use `--model PCW8256` for that model, `--disk` for a selected
image and `--reference PATH` for an optional comparison with another build.
Keep the machine model, image, options, compiler and host conditions comparable.
Include those details when reporting numbers; throughput does not establish
game compatibility or a real-hardware result.
