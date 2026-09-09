<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Building and distributing the libretro core

ZEsarPCW builds a shared library implementing the libretro API. RetroArch loads
that library; no standalone emulator executable or installation script is needed.
The repository contains the C sources, embedded resources and build recipes.
`make` and `ndk-build` do not download source or need a Git checkout to build.

## Build interfaces and outputs

| Target | Build interface | Output consumed by the frontend or buildbot |
|---|---|---|
| Linux x86-64 | `make platform=unix` with native GCC | `zesarpcw_libretro.so` |
| Linux AArch64 | `make platform=linux-aarch64 CC=aarch64-linux-gnu-gcc` | `zesarpcw_libretro.so` |
| Windows x86-64 | `make platform=win64` with mingw-w64 or the buildbot's MXE compiler | `zesarpcw_libretro.dll` |
| macOS x86-64 | `make platform=osx-x86_64` with Apple Clang | `zesarpcw_libretro.dylib` |
| macOS ARM64 | `make platform=osx-arm64` with Apple Clang | `zesarpcw_libretro.dylib` |
| Android ARM64, API 21+ | `ndk-build -C jni APP_ABI=arm64-v8a` | `libs/arm64-v8a/libretro.so`; the buildbot renames it to `zesarpcw_libretro_android.so` |

The [README](../README.md#build-and-check) documents toolchain selection and
checks. [package.py](../PACKAGING.md) builds distribution ZIPs for these six
targets; its Android Make recipe produces `zesarpcw_libretro.so`. These are
different build interfaces for the same core and API. Android load segments
are aligned to 16 KiB as described in the
[Android NDK guidance](https://developer.android.com/guide/practices/page-sizes#compile).

A target recipe does not establish compatibility with every OS release or
frontend. Linux's minimum glibc version depends on the selected build toolchain
and sysroot; macOS's minimum version depends on its deployment target and SDK.
Cross-compilation and export checks do not exercise the core on the target OS.
There are no recipes here for 32-bit CPUs, static cores, iOS or consoles.

## The libretro buildbot

[.gitlab-ci.yml](../.gitlab-ci.yml) uses libretro's shared CI templates with
`CORENAME=zesarpcw`, `MAKEFILE_PATH=.` and `JNI_PATH=.`. The configured jobs are
Linux x86-64, Windows x86-64, both macOS architectures and Android ARM64.
Linux AArch64 has a public build/package recipe and a GitHub Actions cross-build;
it has no GitLab buildbot job configured here.

The template contracts were reviewed on 2026-09-07 at
[ci-templates revision b67581f](https://git.libretro.com/libretro-infrastructure/ci-templates/-/tree/b67581f7969f6d02e0594507ecdbd1c0b98d5468).
Linux and Windows use `make` in their supplied compiler images. Apple's jobs
provide the target triple and SDK to `make`. Android uses the
[NDK template](https://git.libretro.com/libretro-infrastructure/ci-templates/-/blob/b67581f7969f6d02e0594507ecdbd1c0b98d5468/android-jni.yml).
The configuration follows the shared templates, as existing cores such as
[Fuse](https://github.com/libretro/fuse-libretro/blob/master/.gitlab-ci.yml)
do; template changes still need review and an actual pipeline run.

These includes resolve on **git.libretro.com**. Merely publishing this repository
on GitHub does not register a project there or add it to RetroArch's downloads.
The independent [GitHub workflow](../.github/workflows/build.yml) builds and
checks platform ZIPs and also exercises the Android NDK recipe. It does not
publish a release or register a core with libretro.

## Registration and source delivery

The destination for a core is **Online Updater → Core Downloader**. Content
Downloader is for content. Following libretro's
[core integration guide](https://docs.libretro.com/development/cores/developing-cores/#add-your-core-to-libretro-infrastructure),
the remaining external steps are:

1. Publish the reviewed source revision and complete distribution material.
2. Submit [zesarpcw_libretro.info](../zesarpcw_libretro.info) to
   [libretro-super/dist/info](https://github.com/libretro/libretro-super/tree/master/dist/info).
   This file supplies the display name, supported extensions, firmware requirements
   and feature descriptions used by RetroArch. The library and info names must
   retain the `zesarpcw` identifier.
3. Arrange the source mirror/project and buildbot integration with libretro,
   run its actual jobs, and test installation and loading through Core Downloader.
4. Contribute the user documentation to
   [libretro/docs](https://github.com/libretro/docs).

Database and thumbnail contributions are separate from loading `.dsk`/`.m3u`
content and are not part of the supplied build recipes.

Use [PACKAGING.md](../PACKAGING.md) for the exact source archives, component
notices and release identity. A buildbot library is rebuilt by that distributor;
its source must match that build's revision and configuration. Retain immutable
source access and the notices for as long as the binary remains downloadable,
including mirrors. The templates' short-lived job artifacts are not a permanent
source archive. GPLv3 section 6(d) permits corresponding source on another server
with clear directions alongside the binary; it still requires equivalent access.
See the [licence](../LICENSE).

The [native C bootstrap](BOOTSTRAP.md) is covered by the core's GPLv3 licence;
no Amstrad firmware permission is needed for a bundled ROM because none is shipped.
The published OpenPCW-OS source location and immutable revision are recorded in
[SOURCE.md](../sources/openpcw-os/SOURCE.md); its complete MIT notices accompany
the binary, and its separate source archive is optional.
None of these external registration or hosting steps is
performed automatically by the files in this repository.
