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

The template contracts were reviewed on 2026-09-09 at
[ci-templates revision a3694df](https://git.libretro.com/libretro-infrastructure/ci-templates/-/tree/a3694dfdf409a890050a443b4dea64a33df63422).
Linux and Windows use `make` in their supplied compiler images. The macOS Intel
job selects its deployment target; the ARM64 job provides the target triple
and SDK to `make`. Android uses the
[NDK template](https://git.libretro.com/libretro-infrastructure/ci-templates/-/blob/a3694dfdf409a890050a443b4dea64a33df63422/android-jni.yml).
The configuration follows the shared templates, as existing cores such as
[Fuse](https://github.com/libretro/fuse-libretro/blob/master/.gitlab-ci.yml)
do; template changes still need review and an actual pipeline run.

These includes resolve on **git.libretro.com**. Merely publishing this repository
on GitHub does not register a project there or add it to RetroArch's downloads.
The independent [GitHub workflow](../.github/workflows/build.yml) builds and
checks platform ZIPs and also exercises the Android NDK recipe. It does not
publish a release or register a core with libretro.

## Validate the revision to submit

The first public release is **13.0.1**, with tag **v13.0.1** on the reviewed
source commit. The API's `library_version`, the info file's `display_version`
and the package identity must agree. The save-state format has its own version;
see [BOOTSTRAP.md](BOOTSTRAP.md#compatibility-boundary).

Use a clean checkout with no sibling development repositories. On Linux x86-64:

```sh
make platform=unix check -j4
python3 -m unittest discover -s tests -p 'test_*.py'
retroarch --verbose -L ./zesarpcw_libretro.so ./src/openpcw_os.dsk
```

For Android, exercise the same JNI entry point as the buildbot, with an installed
NDK at `NDK_ROOT`:

```sh
"$NDK_ROOT/ndk-build" -C jni APP_ABI=arm64-v8a -j4
python3 check.py --core ./libs/arm64-v8a/libretro.so --platform android-arm64
```

Record the source commit, compiler, OS/architecture and RetroArch version for
each tested target. Distinguish a cross-build/export check from execution in
RetroArch on that target. Test content loading, keyboard/Game Focus, RetroPad,
the on-screen keyboard, options, save/load states, closing and reopening content,
and eject/select/insert with a multi-disc `.m3u`. Use content you can distribute
for a public reproducer; the bundled `src/openpcw_os.dsk` needs no external BIOS.

## Registration and source delivery

The destination for a core is **Online Updater → Core Downloader**. Content
Downloader is for content. Following libretro's
[core integration guide](https://docs.libretro.com/development/cores/developing-cores/#add-your-core-to-libretro-infrastructure),
the remaining external steps are:

1. Publish the reviewed source revision and complete distribution material.
   Identify the branch to mirror and the maintainer/issue tracker. The source
   repository must build directly with the interfaces above.
2. Submit [zesarpcw_libretro.info](../zesarpcw_libretro.info) to
   [libretro-super/dist/info](https://github.com/libretro/libretro-super/tree/master/dist/info).
   This file supplies the display name, supported extensions, firmware requirements
   and feature descriptions used by RetroArch. The library and info names must
   retain the `zesarpcw` identifier. Test the file in RetroArch's configured
   **Core Info** directory under **Information → Core Information**.
   [libretro-core-info](https://github.com/libretro/libretro-core-info) is a
   mirror; submit the change to `libretro-super`.
3. Arrange the source mirror/project and buildbot integration with libretro,
   run its actual jobs, and test installation and loading through Core Downloader.
   Request this explicitly in the info PR or the issue tracker indicated by
   the maintainers. An accepted info PR does not activate compilation.
4. Contribute the user documentation to
   [libretro/docs](https://github.com/libretro/docs). Follow its core template
   and new-core checklist: add `docs/library/zesarpcw.md`, navigation in
   `mkdocs.yml`, and entries in the core and license lists. Document no external
   BIOS requirement, `.dsk`/`.m3u` loading, controls, options, disc handling and
   the save-state/persistence limitations described in this repository.

The buildbot uses the root `.gitlab-ci.yml` and a source mirror arranged with
libretro. The old `libretro-super/recipes/` registration process was replaced
by this workflow; see the
[infrastructure announcement](https://www.libretro.com/index.php/our-new-libretro-infrastructure-is-now-going-live-plus-dosbox-pure-out-for-androidwindows/).

Supply these details with the mirror/buildbot request:

- Source URL, branch and exact reviewed commit.
- Info PR and documentation PR links when available.
- `CORENAME=zesarpcw`, `MAKEFILE_PATH=.` and `JNI_PATH=.`.
- Requested targets and build/runtime results for each one.
- No external BIOS; `.dsk` and `.m3u` content support.
- Maintainer contact, license/component notices and corresponding-source links.

Ask the maintainers to confirm synchronization, enabled jobs and how notices and
source references accompany their rebuilt libraries. Once enabled, verify the
mirrored commit and each job's artifact, then update **Core Info Files** and
install through **Core Downloader** in a RetroArch profile without a manually
installed copy. Check the loaded version and repeat the runtime checks above.
A successful build still needs artifact publication and indexing before users
can download it. Steam, app-store and other distribution packages have separate
integration processes.

Database and thumbnail contributions are separate from loading `.dsk`/`.m3u`
content and are not part of the supplied build recipes. Add an optional
`database` info field only after confirming the corresponding libretro database
name and supported content. The checker permits that standard field without
requiring a database for this core.

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
