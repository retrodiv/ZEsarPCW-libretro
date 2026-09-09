<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# OpenPCW-OS embedded-artifact provenance

OpenPCW-OS version `0.2.0` generates the embedded guest
artifacts from its MIT-licensed assembly source and deterministic Python
builder. Copyright (c) 2026 retrodiv <retrodiv@proton.me>. The published
repository, immutable revision and source-manifest hash are pinned in `SOURCE.md`.

The runtime display font is generated from Microsoft's MS-DOS CP437 8x8 source
at repository commit `2d04cacc5322951f187bb17e017c12920ac8ebe2`, path
`v4.0/src/DEV/DISPLAY/EGA/437-8X8.ASM`. Microsoft publishes that source under
the MIT licence preserved here as `LICENSE.microsoft-msdos`.

The generated `LICENSES.txt` retains both complete upstream MIT notices.
Keep the applicable notices with binaries that embed this disk.

`SHA256SUMS` is the upstream release inventory: it covers the EMS, disk, font,
integration JSON, notices and ZIP. This core embeds the disk and translates
the retained integration JSON to a C header; it also carries the notices. The separate
EMS, font and ZIP are upstream release artifacts, not additional runtime inputs
required by the core. Their checksums identify that release, not missing files
in this repository.

The original source inventory and release identity are retained in
`SOURCE_MANIFEST.sha256` and `SOURCE.json`. [SOURCE.md](SOURCE.md) describes
`rebuild.py`, which verifies a matching standalone source tree and reproduces
the release and these C headers without other development tools.

| Artifact | Bytes | SHA-256 | Integration role |
|---|---:|---|---|
| `src/openpcw_os.dsk` | 194816 | `7057b1f9679f4f6ea153ded0e5254e20b355b4f8c97e4fdf89c6f34f33831001` | bootable native guest disk embedded by the core |
| `src/libretro/openpcw_os_integration.h` | 505 | `450ec98e5eab48159148e03fe0cc4ae0177662f97c43635678095cbd57eba886` | shell-ready address generated from the standalone assembler symbol metadata |

ZEsarPCW uses the handoff point only to reinsert the requested content disk and
begin its existing input-injection sequence after the native shell is ready.
