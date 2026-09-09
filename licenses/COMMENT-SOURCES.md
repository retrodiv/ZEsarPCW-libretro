<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Technical comments and research credits

Review date: 2026-09-07.

The ZEsarPCW port replaces extended manual excerpts, copied explanatory prose
and conversation fragments in seven retained ZEsarUX units with short notes
written for the implementation. This is a comment-only change; it does not
replace the emulation algorithms or alter their licensing. Each affected file
retains its original copyright/GPL notice and records the modification date.

## Upstream record and credits

The source is ZEsarUX tag `ZEsarUX-13.0`, from the archive identified in
[PROVENANCE.md](PROVENANCE.md). These published tag files were compared
byte-for-byte with the original ZEsarUX release:

| Upstream file | SHA-256 |
|---|---|
| [src/ACKNOWLEDGEMENTS](https://github.com/chernandezba/zesarux/blob/ZEsarUX-13.0/src/ACKNOWLEDGEMENTS) | `1bcac4e14f0b0a4206bcb8aede3fcd07e7ea6b1c2509317721b1617e2a866f39` |
| [src/soundchips/ay38912.c](https://github.com/chernandezba/zesarux/blob/ZEsarUX-13.0/src/soundchips/ay38912.c) | `2b288c1530f2cceba9309fdabfee20610737c6a8ab08f7eed5e15a3167d21717` |
| [src/cpus/z80_codpred.c](https://github.com/chernandezba/zesarux/blob/ZEsarUX-13.0/src/cpus/z80_codpred.c) | `ce64e47b0023ffdc3311b1c3ed45ddf63579bd70e5d583a9b694292ffeba9275` |

The AY file credits Miguel Angel Rodriguez Jodar for the serial/MIDI emulator
and introduces an explanation attributed to him. The acknowledgements also
recognize his hardware information. The credit is retained, while the extended
explanation is removed. The serial decoder itself is excluded from the PCW
build. The interrupt-mode note retains the upstream credits to Goran Devic,
Simon Owen, Gerton Lunter and Miguel Angel Rodriguez Jodar.

The reviewed files carry the ZEsarUX GPL notice, but the reviewed release does
not include a separate permission record for those explanatory passages.
Their removal does not assert that the contributions were unauthorized, and
the preserved credits are not presented as new permission grants. Existing
copyright and licence notices are unchanged. Cesar Hernandez Bano's account
of developing `pd765.c` remains as upstream project history.

## References for the replacement notes

The following are references to technical facts and research, not additional
licences for this repository. The manuals and articles are not bundled here.

| Retained unit | Replacement notes and reference |
|---|---|
| `src/soundchips/ay38912.c` | Register layout and local sample mixing. General Instrument, [AY-3-8910/8912 Programmable Sound Generator Data Manual](https://pub.intvprime.com/ba4ef/ie/Programming/AY-3-8910-8912-Programmable-Sound-Generator-Data-Manual.pdf), 1979. The Spectrum manual OCR, duplicated register prose and serial/MIDI discussion are removed. |
| `src/storage/pd765.c` | Short notes beside the implemented command/status paths replace repeated data-sheet descriptions. NEC Electronics, [uPD765A/uPD765B Single/Double Density Floppy-Disk Controller](https://hxc2001.com/download/datasheet/floppy/thirdparty/FDC/NEC/UPD765_Datasheet_OCRed.pdf). Existing upstream documentation links remain available in the source. |
| `src/cpu.c` | Interrupt latches: Zilog, [Z80 CPU User Manual](https://www.zilog.com/docs/z80/um0080.pdf), CPU Control. Interrupted block-instruction flags: David Banks (hoglet67), [Undocumented Flags](https://github.com/hoglet67/Z80Decoder/wiki/Undocumented-Flags). The copied prose/pseudocode is replaced; the implementation and research reference remain. |
| `src/cpus/z80_codpred.c` | Compact IM opcode mapping, retaining all four upstream research credits. Goran Devic's [Z80Explorer](https://github.com/gdevic/Z80Explorer) is a reference to his reverse-engineering work, not evidence of a separate licence for the removed explanation. |
| `src/cpus/z80_codsinpr.c` | Local SCF/CCF flag-history behaviour. Banks's [SCF/CCF notes](https://github.com/hoglet67/Z80Decoder/wiki/Undocumented-Flags#scfccf) credit Patrik Rak's discovery; the original forum reference is also retained in the source. |
| `src/machines/pcw.c` | Keyboard/status and port notes refer to Richard Fairhurst's [Amstrad PCW Hardware Reference](https://www.systemed.net/pcw/hardware.html), 1996–1997, and John Elliott's [PCW Hardware](https://www.seasip.info/Unix/Joyce/hardware.pdf). Colour-mode comments describe the existing implementation. |
| `src/operaciones.c` | The DKTronics joystick excerpt is replaced with a bit summary referencing Elliott's PCW Hardware, DKTronics interface. Other replacements describe the existing arithmetic or legacy matrix/memory notes without reproducing the former chapter/conversation prose. |

The replacement notes describe the current implementation and retain unresolved
upstream TODOs. They do not claim to validate all hardware behaviour or resolve
the separate provenance reviews for embedded data and mappings.
