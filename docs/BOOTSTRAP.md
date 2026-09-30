<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Native PCW bootstrap

`src/libretro/pcw_boot.c` is a project-authored C implementation of the PCW
8256/8512 disk bootstrap interface, distributed under GPL-3.0-only. It replaces
the firmware image previously inherited from ZEsarUX. The core does not ship
`pcw_boot.rom`, embed its bytes in a header, or require a user-supplied BIOS.

## Functional basis and design

Jacob Nevins' [PCW boot-sequence investigation](https://www.chiark.greenend.org.uk/~jacobn/cpm/pcwboot.html)
documents observations of a PCW8512 at power-on. The interface used here is:

- Map RAM banks 0–3 at ports F0–F3 using values 80h–83h.
- Read the 512-byte sector C=0, H=0, R=1, N=2 from drive A.
- Accept an eight-bit sector sum of FFh.
- Place the sector at F000h and start guest execution at F010h, with interrupts
  disabled and a stack below the memory-mapped keyboard.

The implementation uses a native C state machine which sends commands through
the emulator's existing uPD765. It advances emulated time while the normal
scanline, audio and controller events continue. It loads no bootstrap
instructions into RAM and executes no replacement Z80 firmware. The sector
contents, command results and checksum determine success. A failed boot uses
the existing OpenPCW-OS helper-disk service; failure of that helper waits for
Space or a reset instead of repeatedly substituting disks.

This is a new implementation of that functional interface, not a C array or a
line-by-line translation of Amstrad's program. The earlier provenance review
consulted the published disassembly to identify the old image, so no
clean-room development claim is made. The reference documents the interface;
it is not used as a licence for reproducing firmware code.

## Compatibility boundary

The core emulates the sector-loading contract, rather than the original
instruction timing, power-on delays, error beeps or firmware scratch contents.
Low RAM is cleared during native bootstrap. Software which inspects or calls
the original bootstrap bytes at 0002h–0101h cannot rely on them being present.
Ordinary guest instructions execute normally after the handoff; the native
bootstrap is rearmed only by a reset or the PCW soft-boot command.

Bootstrap progress is a required, validated part of the runtime state block.
The save-state format starts at v1 for the first published core. Its explicit
`ZPCW_STATE_VERSION` in `src/libretro/libretro.c` is independent of the core's
core release number and must increase when the saved layout or semantics
become incompatible. The first four bytes are `57 43 50 01` (WCP followed by a
numeric version byte); development formats used an ASCII digit instead.
Earlier development states, unknown format versions and states missing the
bootstrap data are rejected before changing the running session. There are
no legacy readers or migrations.

`tests/test_disk_state.c` exercises the real controller and public state API
during command, data and result phases, checks the sector and RAM map, and
tests checksum fallback and soft boot. The normal `check.py` suite also checks
continued frames after loading a state on both supported machine models.

OpenPCW-OS is a separate MIT-licensed project. The core integrates version
0.2.0, whose protected interrupt stack preserves application buffers while
keeping the established PCW loader allocation boundary.
That guest correction also applies outside this emulator; its MIT notices
remain unchanged. The bootstrap firmware's former permission
record is no longer a distribution dependency. The separate hardware names
and keyboard depiction are described in [AMSTRAD.md](../licenses/AMSTRAD.md).
