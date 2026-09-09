# SPDX-License-Identifier: GPL-3.0-only
# ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GPLv3 (see LICENSE; same licence as ZEsarUX).
# libretro_sources.mk -- generated from the upstream ZEsarUX Makefile source list,
# with the host video/audio drivers (SDL/X11/curses/fbdev/pulse/alsa/dsp/stdout/
# simpletext), the Linux real-joystick backend and main_unix.c removed. The PCW
# libretro driver + entry point are added in Makefile.libretro.
#
# Non-PCW machines and inaccessible standalone UI/debug/network subsystems are
# omitted.  The remaining list is the complete PCW engine used by this core.

UPSTREAM_SRC := \
  libretro/pcw_only_impl.c \
  cores/core_pcw.c \
  cpu.c \
  cpus/z80_codprddfd.c \
  cpus/z80_codpred.c \
  cpus/z80_codsinpr.c \
  dsk.c \
  machines/pcw.c \
  operaciones.c \
  soundchips/ay38912.c \
  storage/pd765.c
