<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Gamepad mapping metadata

Reviewed on 2026-09-07.

The maintained YAML files combine catalogue metadata with the port's gamepad
bindings. Identifiers, titles and disc fingerprints initially came from MAME's
PCW software list; controls, action labels, confidence levels, additional titles
and alternate-image fingerprints are maintained by ZEsarPCW.

## Verified catalogue reference

| Field | Reference |
|---|---|
| Project | MAME, PCW software list |
| Release used for this verification | MAME 0.285 (`mame0285`) |
| Commit | `f379cd060ac3b2bd567ebbd0957aed059d6895ff` |
| File | [`hash/pcw.xml`](https://github.com/mamedev/mame/blob/f379cd060ac3b2bd567ebbd0957aed059d6895ff/hash/pcw.xml) |
| SHA-256 | `8c2e3871e37edb7d9a14c524ddb4273eb5caba9b7af207ada9c673221c148010` |
| File's own licence notice | `CC0-1.0` — [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/legalcode.en) |

The original import did not record its exact MAME revision. The reference above
is a verified source for the retained catalogue data, not an inferred date or
revision of that earlier import. All 140 catalogue identifiers and their SHA-1
associations occur in the maintained database. This count includes `sirlance`,
whose entry is commented out in the XML but was included by the initial importer.
Displayed titles may have been edited since that import.

The catalogue metadata follows its own CC0 notice. The generator, key bindings
and port-authored annotations follow the project's GPLv3 notices. The catalogue
notice concerns metadata; it grants no rights to the programs identified by the
fingerprints. The XML and the identified game images are not bundled here.

## Maintained additions

The database currently contains 169 profiles and 314 SHA-1 table rows. Additional
fingerprints recognize alternate images and colour editions without depending
on their filenames or storage locations. The catalogue reference above does not
cover every maintained addition.

`sources/mappings/pcw_keyb2joypad.yml` supplies existing profiles and controls.
`pcw_keyb2joypad_extra.yml` adds unique fingerprints to an existing identifier,
or a full profile for a new identifier. Existing profiles keep the controls from
the main file. See [the editing instructions](../sources/mappings/README.md).

Both files are maintained directly. The generator reads these YAML files and
writes the C table; it does not fetch or import an external catalogue. Normal
compilation consumes the generated table already present in the repository.
