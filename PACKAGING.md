<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Binary packages and corresponding source

Build a package from a clean committed checkout with Python 3.10+, Git, make,
the target C compiler and strip:

```sh
python3 package.py --platform linux-x86_64
```

Every package includes the complete core sources, embedded guest disk, full
OpenPCW-OS/Microsoft MIT notices and an immutable reference to the published
[OpenPCW-OS project](https://github.com/retrodiv/OpenPCW-OS). Its exact revision
and source-manifest hash are recorded in [SOURCE.md](sources/openpcw-os/SOURCE.md),
`README.txt` and `RELEASE.json`. Normal core builds and packaging use the embedded
resources already included in this repository and require no download.

The standalone guest source archive is optional. Add
`--openpcw-source OPENPCW_SOURCE` to include it for offline guest development;
the matching local tree's manifest and every listed file must match the pinned
identity. OpenPCW-OS's MIT terms require preserving its notices, without requiring
source delivery. The emulator's complete GPL corresponding source, including
its build tools and editable GPL assets, remains included in every package.

Targets: `linux-x86_64`, `linux-aarch64`, `windows-x86_64`, `android-arm64`,
`macos-x86_64`, `macos-arm64`. The defaults use GCC, the corresponding GNU cross
compiler, or native Apple Clang. Build macOS packages on the matching architecture.
For Android, pass `--cc` and `--strip` pointing to the NDK's
`aarch64-linux-android21-clang` and `llvm-strip`. `--jobs`, `--workdir` and
`--output` select parallelism, temporary storage and output location.

The script verifies `SOURCE_MANIFEST.sha256`, compares its complete file set and
bytes to HEAD and checks the staged index. Untracked build outputs are not source
inputs. It freezes only the manifest inventory in a fresh temporary directory,
builds it, strips the library and runs `check.py` on that exact library before
creating the ZIP. It accepts no existing binary. Cross checks inspect metadata
and exports; execution on the target remains a separate validation requirement.
The source archive contains that same inventory, including build scripts,
tests, WAVs, PNGs, YAML and generators. Files have normalized timestamps and
permissions; their contents match the committed files exactly.

To package an uncommitted source snapshot, use `--snapshot`. The archive
name and record then contain the full manifest SHA-256 and `commit: null`.
It never labels modified source as a clean commit. The manifest must still be
current. After changing the sources, update their inventory with:

```sh
python3 - <<'PY'
import hashlib
import subprocess
from pathlib import Path
names = subprocess.check_output(['git', 'ls-files', '-z']).decode().split('\0')
Path('SOURCE_MANIFEST.sha256').write_text(''.join(
    f'{hashlib.sha256(Path(name).read_bytes()).hexdigest()}  {name}\n'
    for name in sorted(filter(None, names)) if name != 'SOURCE_MANIFEST.sha256'),
    encoding='utf-8', newline='\n')
PY
```

Stage intended new source files first; review the inventory and changes before
committing. Publication mode requires the resulting manifest to be committed
with its source files. Do not add build outputs or local data to that inventory.

## Release identity

The core API's `library_version` and the `.info` file's `display_version` both
use `13.0.1`. package.py checks that they agree and uses that value in ZIP names
and RELEASE.json. A release of this version should use tag `v13.0.1` on the exact
source commit; create the tag only when that revision is ready to publish.
A snapshot remains identified by its full manifest hash and `commit: null`.
The version of the embedded OpenPCW-OS project is recorded separately.

## Package contents and verification

Every platform ZIP contains:

- The checked, stripped `zesarpcw_libretro` library and its `.info` file.
- The GPL text, AUTHORS, component licence/provenance documents, full MIT notices
  for OpenPCW-OS and Microsoft, the original libretro API notice and the sound
  sample copyright/licence notice. The hardware-identification notice is `licenses/AMSTRAD.md`.
- A `*-source.tar.gz` containing the exact core sources, including the guest
  reconstruction tool and verification records. A separate
  `OpenPCW-OS-*-source.tar.gz` is included only with `--openpcw-source`.
- `RELEASE.json`: version, full commit or snapshot identity, binary/source hashes,
  OpenPCW-OS identity and compiler/build settings. No local source paths are stored.
- `SHA256SUMS` covering every package file except itself, and `README.txt`.

After extracting the ZIP, run `sha256sum -c SHA256SUMS` (or an equivalent SHA-256
tool). Extract the core source archive and run `sha256sum -c SOURCE_MANIFEST.sha256`
inside its top directory. Build with the platform and compiler named in
`RELEASE.json`, for example:

```sh
SOURCE_DATE_EPOCH=0 make platform=unix CC=gcc CFLAGS=-O2 CPPFLAGS= LDFLAGS= LDLIBS= -j4
strip --strip-unneeded zesarpcw_libretro.so
python3 check.py --core ./zesarpcw_libretro.so --platform unix
```

Use the target strip tool; macOS uses `strip -x`. Matching source does not promise
identical binary bytes across different compiler/linker/SDK versions. The build
record states the compiler version and target actually used. Run the editable
asset checks documented in [sources/README.md](sources/README.md). If the guest
archive is included, extract it and use the core's
`python3 sources/openpcw-os/rebuild.py --source OPENPCW_SOURCE --check`
with Python and Pasmo to rebuild the guest and compare its embedded resources.

## Redistribution and hosting

Distribute the complete platform ZIP. Preserve the notices and make the exact
source archives as readily available for as long as their binary is available.
Publish these packages as Release assets; GitHub's automatic source archive is
not a substitute for the packaged core sources and component notices. Keep an archival
copy of the complete distribution. Never replace an archive with sources from a
moving branch or a newer revision while retaining the old binary.

GitHub Actions uploads each whole ZIP as one artifact with the same 30-day
retention for its binary, notices and sources, and fails when no package exists.
Expired CI artifacts are not a durable source host for a binary mirrored elsewhere.
The general Actions version/runner review is separate from this package format.

For libretro integration, supply the actual core revision, platform ZIPs, `.info`,
`RELEASE.json`, GPL and component notices, the exact core source archive, and
the pinned OpenPCW-OS source reference (or its optional source archive).
Confirm how the buildbot/Core Updater makes these available alongside its rebuilt
library, including retention and mirrors. The current GitLab template build does
not establish that delivery; this must be agreed and tested with that distributor
before declaring the channel complete. Do not rely on licence strings surviving
the linker inside a bare `.so`, `.dll` or `.dylib`.
The [libretro distribution guide](docs/LIBRETRO.md) records the build interfaces,
artifact filenames and external registration steps.

Optional `--debug-output FILE` writes a local unstripped companion outside the
ZIP. If a distributor publishes it too, retain the same notices and corresponding
sources with it. There are no automatic publication or contacting steps here.
