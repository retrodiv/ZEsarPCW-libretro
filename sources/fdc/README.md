<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Floppy-drive sound source

The three WAV files are the preferred editable form of the mechanical-drive
samples embedded in `../../src/libretro/pcw_fdc_samples.h`. They are mono,
16-bit PCM at 44.1 kHz and total only 69,414 bytes, so no lossy or container
conversion is needed.

They originate in PituKa by David Colmenero (D_Skywalk), GPLv2-or-later. The
historical upstream stored each complete RIFF/WAV byte stream in a C array. These
files were extracted byte-for-byte from `motor.h`, `read_drive.h` and
`seek_drive.h` at libretro-cap32 commit
`d4e9c83aa44cd4c1142d4a58e1b1961ad19998b1`. Exact URLs and hashes are recorded
in `../../licenses/PROVENANCE.md`.

Regenerate or verify the embedded header with only Python's standard library:

```sh
python3 sources/fdc/gen_fdc_samples.py \
    --out src/libretro/pcw_fdc_samples.h
python3 sources/fdc/gen_fdc_samples.py \
    --check src/libretro/pcw_fdc_samples.h
```
