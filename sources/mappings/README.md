<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Editable input and on-screen-keyboard sources

This directory contains the preferred editable forms for the generated input
headers shipped by the core:

- `pcw_keyb2joypad.yml` and `pcw_keyb2joypad_extra.yml` generate the per-title
  gamepad database;
- `Keyboard_UK.png` and `Keyboard_US.png` generate the two OSK skins;
- `pcw_keyb2joypad_gen.py` and `gen_osk_skin.py` perform those conversions.

The YAML generator uses only Python's standard library. The PNG generator pins
its Pillow version in `requirements-generators.txt`. Both require Python 3.10+.
Install Pillow in a virtual environment; the ordinary core build does not need
it. From the repository root, create and activate the environment on Linux/macOS:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r sources/mappings/requirements-generators.txt
```

On Windows PowerShell, activate it with `.venv\Scripts\Activate.ps1` instead.
The `.venv/` directory is ignored by Git.

Edit controls for an existing title in `pcw_keyb2joypad.yml`. To recognize another
image of that title, add its identifier and `sha1_alt` to
`pcw_keyb2joypad_extra.yml`; a new title needs its own name, hashes, profile and
`input_pad_*` bindings. Extra hashes inherit the main profile's controls. Repeated
hashes for the same identifier are ignored when merging the extra file.

Both generators resolve their default inputs beside the script and write their
headers under this repository's `src/libretro/`, independently of the working
directory. From the repository root:

```sh
python3 sources/mappings/pcw_keyb2joypad_gen.py c
python3 sources/mappings/gen_osk_skin.py
python3 sources/mappings/gen_osk_skin.py \
    --png sources/mappings/Keyboard_UK.png --symbol pcw_osk_skin_uk
make -j4
make check
```

The OSK defaults to `Keyboard_US.png`, symbol `pcw_osk_skin_us` and output
`src/libretro/pcw_osk_skin_us.h`. The UK command selects its own symbol and
output header. The complete artwork, including its logo, is preserved.
The generator's module documentation describes the bevels, separate legends,
PCW matrix, scaling and renderer contract. Optional `--preview FILE` also writes
a composed PNG and a nearest-neighbour 3x preview in an existing directory.

Explicit paths take precedence and are interpreted from the working directory:

```sh
python3 sources/mappings/pcw_keyb2joypad_gen.py c \
    --yaml sources/mappings/pcw_keyb2joypad.yml \
    --extra sources/mappings/pcw_keyb2joypad_extra.yml \
    --cheader src/libretro/pcw_keyb2joypad_db.h
python3 sources/mappings/gen_osk_skin.py \
    --png sources/mappings/Keyboard_US.png \
    --symbol pcw_osk_skin_us \
    --out src/libretro/pcw_osk_skin_us.h
```

If a script is moved out of `sources/mappings/`, or `src/libretro/` is absent,
specify `--cheader` or `--out`. The script reports an error instead of guessing
a destination. The YAML-to-C generator uses the two maintained YAML files;
regeneration needs no external catalogue.

The pixel art, key bindings and port-authored annotations follow the port's
GPLv3 notices. Initial catalogue identifiers, titles and disc hashes derive
from MAME's CC0-1.0 PCW software list; alternate images and additional profiles
are maintained by the port. The exact reference and scope are recorded in
[licenses/MAPPINGS.md](../../licenses/MAPPINGS.md).

Copyright and licence notices for the UK/US PNG artwork are preserved in the
adjacent `.png.license` files without changing the image bytes.
