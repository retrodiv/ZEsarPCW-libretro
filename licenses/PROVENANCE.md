<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Embedded-data provenance and redistribution review

This document is a technical provenance record, not legal advice.

The review of imported technical comments and the preserved research credits
is recorded in [COMMENT-SOURCES.md](COMMENT-SOURCES.md).

Gamepad catalogue metadata and maintained controls are described separately in
[MAPPINGS.md](MAPPINGS.md), including the verified MAME reference and CC0 notice.

## Native bootstrap and OpenPCW-OS

The PCW 8256/8512 bootstrap in `src/libretro/pcw_boot.c` is original port work,
Copyright (c) 2026 retrodiv <retrodiv@proton.me>, under GPL-3.0-only. Its
[functional basis, implementation and compatibility boundary](../docs/BOOTSTRAP.md)
are documented. The previously supplied Amstrad firmware image and generated
byte-array header have been removed. They are excluded from the exported core,
source archives and binary packages.

The fallback operating environment is the project-authored
[**OpenPCW-OS**](https://github.com/retrodiv/OpenPCW-OS), copyright (c) 2026
retrodiv <retrodiv@proton.me>, licensed under the MIT License preserved at
`sources/openpcw-os/LICENSE`. Its canonical standalone project assembles
the boot chain and kernels, derives the runtime font from the licensed source
described below, constructs the `OPCWEMS1` container and 180 KiB DSK, and
publishes deterministic SHA-256 records. Compatibility derives from published
PCW hardware, XBIOS, and CP/M application interfaces plus executable guest and
guest-program validation. `sources/openpcw-os/SOURCE.md` records the version and
source-manifest pin; `SOURCE_MANIFEST.sha256` records every file in this
generated core.

The display source comes byte-for-byte from Microsoft's archived
[MS-DOS repository](https://github.com/microsoft/MS-DOS) at commit
`2d04cacc5322951f187bb17e017c12920ac8ebe2`, path
`v4.0/src/DEV/DISPLAY/EGA/437-8X8.ASM`. Microsoft publishes the repository under
the MIT licence preserved as `LICENSE.microsoft-msdos`. The standalone builder
selects the low 128 glyphs, then replaces PCW cells 09h/0Bh/7Ch with Microsoft
glyph 1Ah, 0Ah with 19h, and 0Ch/0Dh with 1Bh. Every runtime glyph is selected
unchanged from the licensed input. A lossless
64-byte row dictionary plus 6-bit indices stores the resulting 1,024-byte font
inside the guest disk.

The source font's SHA-256 is
`9ba690ac66ea37c6afb257a7416fef29a5cf8184936150526de0ad081739f5d7`;
the resulting runtime font is
`f2327dfe38fcf76829086457298e6f8fc7bf569ec80db87412ed77c6526b272b`.
The complete upstream project and Microsoft distribution notices are preserved
in [`sources/openpcw-os/LICENSES.txt`](../sources/openpcw-os/LICENSES.txt).

| Shipped source | Bytes | SHA-256 | Transformation |
|---|---:|---|---|
| `sources/openpcw-os/LICENSE.microsoft-msdos` | 1074 | `b5179f780ec212a434efcc989a2295a140deb0bdb17182d2bf9c5f6f1f1a01c4` | Verbatim MIT notice from the root of the same Microsoft repository. |
| `src/libretro/openpcw_os_integration.h` | 505 | `450ec98e5eab48159148e03fe0cc4ae0177662f97c43635678095cbd57eba886` | Shell-ready address generated from the standalone assembler symbol metadata. |
| `src/openpcw_os.dsk` | 194816 | `7057b1f9679f4f6ea153ded0e5254e20b355b4f8c97e4fdf89c6f34f33831001` | New 40-track, single-sided DSK generated from the OpenPCW-OS bootstrap, kernels, filesystem/interrupt helpers, losslessly packed MIT font and status text. |

The public interface basis is John Elliott's
[XBIOS documentation](https://www.seasip.info/Cpm/xbios.html), Jacob Nevins'
[PCW port notes](https://www.chiark.greenend.org.uk/~jacobn/cpm/pcwports.html),
and Digital Research's published
[*CP/M Plus Programmer's Guide*](https://www.cpm.z80.de/manuals/cpm3-pgr.pdf).
OpenPCW-OS maps those contracts to project-authored terminal, Page Zero,
SCB, XBIOS, filesystem, and disk-controller services. Its volatile `M:` work
drive uses a project-designed 128-slot directory, sparse record map, and 2 KiB
allocation map sized from the installed 256 or 512 KiB.

## Floppy WAV source

PituKa stores the three complete WAV files as byte arrays in libretro-cap32. The
shipped WAVs were extracted byte-for-byte from commit
`d4e9c83aa44cd4c1142d4a58e1b1961ad19998b1` (the last commit touching all three
headers):

- <https://github.com/libretro/libretro-cap32/blob/d4e9c83aa44cd4c1142d4a58e1b1961ad19998b1/libretro/snd/motor.h>
- <https://github.com/libretro/libretro-cap32/blob/d4e9c83aa44cd4c1142d4a58e1b1961ad19998b1/libretro/snd/read_drive.h>
- <https://github.com/libretro/libretro-cap32/blob/d4e9c83aa44cd4c1142d4a58e1b1961ad19998b1/libretro/snd/seek_drive.h>

Each upstream header preserves David Colmenero's copyright and GPLv2-or-later
notice. Decoding the WAV data produces 7,785, 10,704 and 16,152 signed 16-bit
samples respectively; all three streams compare exactly, sample-for-sample, with
the generated C arrays.

| Shipped source | Bytes | SHA-256 | Transformation |
|---|---:|---|---|
| `sources/fdc/motor.wav` | 15614 | `78966b8cdde68425f85a5eb4d6a4634c989f9f447307676249bd6e8cb82102f7` | Complete RIFF stream extracted from upstream `motor.h`. |
| `sources/fdc/read_drive.wav` | 21452 | `fb61640e63d040aedf98902372e54df51533c0fcdd24bed1e8bef872a1252fb4` | Complete RIFF stream extracted from upstream `read_drive.h`. |
| `sources/fdc/seek_drive.wav` | 32348 | `10dcedf7cd996587ec2e676cffb3359ccd6cbdade646003a42706895418e0a66` | Complete RIFF stream extracted from upstream `seek_drive.h`. |
| `src/libretro/pcw_fdc_samples.h` | 177148 | `040295faf1172e6ad8d910e99ee5327330c586e2187a75325baca4e4aba6df77` | Deterministically generated from the three WAV files by `sources/fdc/gen_fdc_samples.py`; header carries PituKa/wiituka attribution and GPLv2-or-later notice. |

## libretro API header

`src/libretro/libretro.h` is the unmodified canonical header from RetroArch commit
`9a58f2631f51760f112481bf6b83050c43652a1a`, SHA-256
`a11f7887be2af72bdf7f97dbd975f24df66922aaa6d5e6a7100f0bc1595fc1ae`:
<https://raw.githubusercontent.com/libretro/RetroArch/9a58f2631f51760f112481bf6b83050c43652a1a/libretro-common/include/libretro.h>.
Its own permissive licence notice is preserved at the top of the file.

## Executable-data licence summary

The native C bootstrap is distributed under the core's GPLv3 licence.
The OpenPCW-OS disk and shell-ready metadata are covered by their preserved
MIT licence and canonical preferred source. The reconstruction tool and C
header templates are port work under GPLv3; that does not replace the data
licences. ZEsarUX-derived emulator code and port-authored code follow the GPL
notices shipped with this repository. See [MODIFICATIONS](MODIFICATIONS.md) for
the retained upstream units and the scope of their port changes.

## README screenshot

`docs/openpcw-osk.png` is a lossless capture of the core's native 720×256
framebuffer after booting the bundled OpenPCW-OS disk and opening the UK OSK.
It shows only the project-authored shell and keyboard artwork already described
in these notices. The font retains the OpenPCW-OS/Microsoft MIT attribution;
the Amstrad mark retains the hardware-identification notice in
[AMSTRAD.md](AMSTRAD.md). No commercial application, private path or frontend
chrome is included. The README displays the native pixels at the core's 4:3
aspect ratio. Capture by retrodiv's project, 2026; distributed with the core
under GPLv3, subject to the existing component notices.
