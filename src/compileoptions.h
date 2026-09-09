/* SPDX-License-Identifier: GPL-3.0-only */
/*
    ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX).
    Copyright (c) 2026 retrodiv <retrodiv@proton.me>. Distributed under the GNU General Public License v3
    (the same licence as ZEsarUX, Copyright (C) Cesar Hernandez Bano).
*/

#ifndef OPTIONS_H
#define OPTIONS_H

/* ------------------------------------------------------------------------- *
 * compileoptions.h -- libretro Amstrad PCW build.
 *
 * This replaces the upstream configure-generated compileoptions.h. The PCW
 * libretro core has NO host video/audio layer of its own: SDL, X11, curses,
 * fbdev, AA/caca, PulseAudio, ALSA, OSS/DSP, the one-bit speaker and the
 * stdout/simpletext text drivers are all compiled OUT. Video, audio and input
 * are bridged to the frontend by the native libretro driver
 * (libretro/scrlibretro.c, libretro/audiolibretro.c) selected under COMPILE_LIBRETRO.
 *
 * Kept: only the emulated Z80 MEMPTR register. Libretro supplies frame pacing;
 * the standalone timer thread, visual-memory instrumentation, CPU statistics,
 * generic contention tables and putpixel cache are inaccessible in this core.
 * ------------------------------------------------------------------------- */

/* The libretro bridge. Gates the scrlibretro / audiolibretro driver selection
 * and the "return instead of run the blocking main loop" path in zesarux_main.
 * Also passed on the command line (-DCOMPILE_LIBRETRO) so it is defined before any
 * header; guard against the resulting redefinition. */

/* ZEsarUX's own networking (ZENG netplay + the ZRCP remote-command/debug protocol) is
 * useless in a libretro core -- RetroArch has its own netplay and there is no remote
 * console -- so compile it out with ZEsarUX's supported build switch. This removes the
 * whole network/ZRCP subsystem from the binary and from the source.
 * Verified to build on Linux + Windows and boot. */
#define NETWORKING_DISABLED

/* Emulation-accuracy switches used throughout the shared core. */
#define EMULATE_MEMPTR

#define COMPILATION_DATE "libretro build"
#if defined(MINGW) || defined(_WIN32)
#define COMPILATION_SYSTEM "Windows (MinGW)"
#elif defined(__ANDROID__)
#define COMPILATION_SYSTEM "Android"
#elif defined(__APPLE__)
#define COMPILATION_SYSTEM "macOS"
#elif defined(__linux__)
#define COMPILATION_SYSTEM "GNU/Linux"
#else
#define COMPILATION_SYSTEM "libretro target"
#endif
#define COMPILATION_SYSTEM_RELEASE ""
#if defined(__aarch64__)
#define COMPILATION_MACHINE_HARDWARE_NAME "aarch64"
#elif defined(__x86_64__) || defined(_M_X64)
#define COMPILATION_MACHINE_HARDWARE_NAME "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
#define COMPILATION_MACHINE_HARDWARE_NAME "x86"
#elif defined(__arm__) || defined(_M_ARM)
#define COMPILATION_MACHINE_HARDWARE_NAME "arm"
#else
#define COMPILATION_MACHINE_HARDWARE_NAME "unknown"
#endif
#define CONFIGURE_OPTIONS "libretro (Amstrad PCW only)"
#define COMPILE_VARIABLES " COMPILE_LIBRETRO EMULATE_MEMPTR"
#define COMPILE_INITIALCFLAGS ""
#define COMPILE_INITIALLDFLAGS ""
#define COMPILE_INITIALLIBS ""
#define COMPILE_FINALCFLAGS ""
#define COMPILE_FINALLDFLAGS ""
#define COMPILE_FINALLIBS ""
#define INSTALL_PREFIX "/usr/local"

#define LINES_SOURCE 0
#define TOTAL_LINES 0
#define TOTAL_COMMENTS 0
#define TOTAL_TODO_ITEMS 0
#define TOTAL_C_FILES 0
#define TOTAL_H_FILES 0

#define BUILDNUMBER "libretro"


#endif
