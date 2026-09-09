<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# OpenPCW-OS source identity and reconstruction

This core embeds the boot disk produced by the standalone OpenPCW-OS
project and a generated constant identifying its shell-ready handoff point.

| Property | Pinned value |
|---|---|
| Project | [OpenPCW-OS](https://github.com/retrodiv/OpenPCW-OS) |
| Source revision | [`49bbbd078db93224736b60ab832b4cd8ab623da8`](https://github.com/retrodiv/OpenPCW-OS/tree/49bbbd078db93224736b60ab832b4cd8ab623da8) |
| Version | `0.2.0` |
| Source manifest SHA-256 | `65ff812a96527a9b5b7121039f27beeb2e6f043c070af24e51cb5e36cd15a663` |
| Machine-readable identity | [SOURCE.json](SOURCE.json) |
| Original source inventory | [SOURCE_MANIFEST.sha256](SOURCE_MANIFEST.sha256) |
| Original handoff metadata | [OpenPCW-OS-integration.json](OpenPCW-OS-integration.json) |
| Distribution notices | [LICENSES.txt](LICENSES.txt) |

The standalone project is the canonical location for guest assembly sources,
build tooling, tests, documentation, public ABI records, and release history.
This generated core carries that boot disk and the records needed for an
offline core build. OpenPCW-OS itself executes natively on the emulated
Z80; its canonical implementation remains entirely in the standalone project.

The source reference above identifies the standalone project and the exact
source revision used for this integration. Use a checkout or extracted source archive of that revision
with the commands below. The complete MIT notices travel with this core and its
binary packages; including another copy of the standalone sources is optional.

## Rebuild from a matching source tree

Requirements: Python 3.10 or newer and `pasmo` on PATH. The source tree must
contain its recorded `SOURCE_MANIFEST.sha256` and all listed files, including
the versioned release artifacts. The script checks their bytes, copies only
that verified inventory to a temporary directory, and rebuilds all seven
release files there. It leaves the supplied source tree untouched.

From the core repository root, replace `OPENPCW_SOURCE` with that directory:

```sh
python3 sources/openpcw-os/rebuild.py --source OPENPCW_SOURCE --check
```

This compares the rebuilt release against its source and release manifests,
including the separate EMS, font, ZIP and complete notices. It then compares
the disk, original integration JSON, notices and both C headers with this core.
Use the same command without `--check` to restore those resources from the
verified source. `--workdir DIRECTORY` selects an existing directory for the
temporary rebuild; the default uses the system temporary directory.

## Regenerate only the C representation

Python alone can regenerate the headers from the disk and JSON already here:

```sh
python3 sources/openpcw-os/rebuild.py --check
python3 sources/openpcw-os/rebuild.py
```

The first command checks all bundled inputs and generated headers without
writing; the second rewrites the generated resources. Paths within the core
are resolved relative to the script, independently of the working directory.
These commands perform no download. Normal `make` consumes the checked-in
headers and needs neither this reconstruction step nor Pasmo.

The manifest in this directory describes the upstream source tree; it is not
a list of files to place in this directory. Likewise, `SHA256SUMS` describes
the full upstream release, while the core uses the disk and integration JSON.
The separate EMS, font binary and ZIP remain upstream release artifacts.

The canonical source's `docs/DESIGN_PROVENANCE.md`, `docs/THIRD_PARTY.md` and
`docs/BUILD_AND_RELEASE.md` record implementation provenance, font selection
and release construction. This bundle retains the complete applicable notices
and [the integration provenance](PROVENANCE.md).

To adopt a different OpenPCW-OS revision, update the source identity, manifests
and retained notices together and rebuild/test the integration. A hash
mismatch is an error; this tool does not silently select another revision.
