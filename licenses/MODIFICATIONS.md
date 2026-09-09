<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# ZEsarPCW port modification record

Record updated: **2026-09-09**. Port maintainer: **retrodiv** (<retrodiv@proton.me>).

This is a modified PCW/libretro edition of ZEsarUX 13.0 by Cesar Hernandez Bano
and contributors. It is distributed under GNU GPL version 3; the original
upstream copyright, GPL version 3-or-later and warranty notices are retained.
See [LICENSE](../LICENSE), [AUTHORS](../AUTHORS) and the
[component licence summary](../README.md#licence-and-credits).

The 2026-09-07 review recorded the existing port and added its
modification notices. That is not an invented implementation date for the older
changes: their individual dates were not recorded in these files. The concise
source-comment revisions already dated 2026-09-07 retain their separate notices.
Future source changes should update the relevant file notice and this record.

2026-09-08: the DSK loader now rejects incomplete headers and failed/short reads
before inspecting the reused disk buffer. Failed loads clear its active length;
successful loads use the new length for protection detection. The public checks
cover empty/truncated images after unloading content and during tray insertion,
including recovery with a valid image.

2026-09-08: port-authored copyright notices now identify retrodiv
(<retrodiv@proton.me>). SPDX identifiers retain the existing component terms;
third-party notices and data licences are preserved.

## Changes from ZEsarUX 13.0

This catalogue describes the changes present in **ZEsarPCW 13.0.1** against
the final ZEsarUX 13.0 release. It distinguishes corrections to inherited PCW
behaviour, fixes in the libretro integration, and deliberate compatibility or
presentation choices. A feature added for this port is not, by itself, evidence
of a defect in the standalone emulator. Superseded implementation changes are
identified separately below.

The entries refer to the current source files and function names. The file
inventory and attribution record follow the catalogue.

### Inherited PCW corrections

#### Interrupts, display status and keypad

| Issue | Previous behaviour and correction | Current implementation |
|---|---|---|
| Interrupt accepted after interrupts were disabled | A pending maskable interrupt could still be accepted after an NMI or a guest `DI` cleared `IFF1` in the same scheduler turn. Acceptance now rechecks `IFF1`, preserves the pending PCW interrupt for a later `EI`, and retains the delay immediately after `EI`. | [`core_pcw.c`](../src/cores/core_pcw.c), `core_pcw_handle_interrupts` |
| Missing 256-line display-status bit | The PCW status byte omitted bit 4 for the normal 256-line display. Software using that bit to select its vertical scroll position could draw too high, including Hundra, Coliseum and Phantis. The status now includes `0x10`. | [`pcw.c`](../src/machines/pcw.c), `pcw_get_port_f8_value` |
| Keypad 4 / LINE-EOL press | The inherited keypad-4 press used OR instead of clearing the active-low PCW matrix bit, so the key was not asserted correctly and other bits in that row could be released. The PCW mapping now presses and releases row 1, mask `0x20`, through the common matrix helper. | [`pcw_keyboard.c`](../src/libretro/pcw_keyboard.c), `targets`, `matrix_key` |
| Missing PCW keypad-plus mapping | The inherited keypad-plus path operated Spectrum keys without asserting the PCW key. The port maps it to PCW row 2, mask `0x80`, with both press and release handling. | [`pcw_keyboard.c`](../src/libretro/pcw_keyboard.c), `targets`, `matrix_key` |

#### Floppy-controller commands, results and timing

These changes are implemented in [`src/storage/pd765.c`](../src/storage/pd765.c).
The command names below identify controller operations, not host file deletion.

| Issue | Previous behaviour and correction | Functions |
|---|---|---|
| Invalid commands generated interrupts | An invalid command, including `SENSE INTERRUPT STATUS` with no event pending, generated another interrupt. A driver draining pending events could loop indefinitely. The controller now returns `ST0=0x80` in the result phase without requesting a new interrupt. | `pd765_handle_command_invalid`, `pd765_read_result_command_invalid` |
| `SENSE DRIVE STATUS` generated an interrupt | Reading drive status raised an interrupt unnecessarily. The command now returns `ST3` directly without creating a pending event. | `pd765_handle_command_sense_drive_status` |
| Completed-transfer interrupts remained pending | Consuming a command's result did not clear its completion interrupt, allowing a later status query to observe a stale event. Reading results now clears that interrupt for `READ ID`, `FORMAT TRACK`, reads and writes. | `pd765_read_result_command_read_id`, `pd765_read_result_command_format_track`, `pd765_read_result_command_read_data`, `pd765_read_result_command_write_data` |
| Missing motor/READY-change events | Motor transitions with a disk inserted did not report a READY change, leaving some PCW BIOS disk routines waiting. Transitions now queue an event, and `SENSE INTERRUPT STATUS` reports `ST0=0xC0`; a pending seek completion is reported first. Drive spin-up remains instantaneous in this model. | `pd765_motor_on`, `pd765_motor_off`, `pd765_read_result_command_sense_interrupt_status` |
| Nonexistent drives reported ready | `ST3` reported readiness even for units other than the emulated drive A. Nonzero unit selections now return not-ready, allowing software to distinguish an absent second drive. | `pd765_get_st3` |
| Missing next sector failed immediately | When a multi-sector read advanced to an absent sector ID, the controller returned No Data immediately. A loader's Terminal Count could then arrive after an error that should still have been a search. Chained reads now wait 20 emulated frames, representing two rotations, with `RQM` low. Expiry reports No Data; Terminal Count cancels the pending search before that error. | `pd765_handle_command_read_data`, `pd765_next_event_from_core`, `pd765_read_data_sector_not_found_resolve`, `pd765_set_terminal_count_signal` |
| Terminal Count covered only ordinary reads | The read termination path handled `READ DATA` but omitted the other commands using the same transfer engine. It now also handles `READ DELETED DATA` and `READ TRACK`. | `pd765_set_terminal_count_signal` |
| A zero-distance seek reset rotational position | Seeking to the current cylinder reset the remembered physical sector, causing subsequent `READ ID` operations to restart their sequence. That position now survives a zero-distance seek; a cylinder change or recalibration resets it. | `pd765_signal_se_function_triggered` |
| Failed writes lost their error result | A missing or unwritable sector entered the result phase with a zero transfer length. The parameter reader then mistook that zero for a completed write and overwrote the error packet. Completion now requires the controller to remain in the write-data phase. | `pd765_read_parameters_write_data` |
| Write completion changed the read state | Both ordinary write completion and write Terminal Count assigned the read-state variable. They now advance the write-state variable to its ending state. | `pd765_read_parameters_write_data`, `pd765_set_terminal_count_signal` |
| `WRITE DELETED DATA` was unimplemented | The command now uses the write transfer/result path, including Terminal Count. Completed writes set the sector's deleted-data mark for this command and clear it for ordinary `WRITE DATA`; the DSK metadata is updated with the data. | `pd765_write_handle_phase_command`, `pd765_handle_command_start_write_data`, `pd765_handle_command_write_data_put_sector_data_from_bus`, `pd765_read_handle_phase_result`, `pd765_set_terminal_count_signal` |
| Writes inherited a stale skip flag | Starting a write could retain `SK` from a previous read. Both write commands now clear it when accepting the command, so an unrelated read cannot affect the next write's sector selection. | `pd765_write_handle_phase_command` |

#### Disk-image loading

These changes are implemented in [`src/dsk.c`](../src/dsk.c).

| Issue | Previous behaviour and correction | Functions |
|---|---|---|
| Standard DSK track offsets used a redundant sector-size field | Calculating each track stride from its local size byte mislocated later tracks in valid images whose local field disagreed with the global fixed track size. Standard DSK traversal now uses the size at header offsets `0x32`/`0x33`. The former calculation remains a fallback when that size is missing or smaller than a track header. | `dsk_basic_get_start_track` |
| Empty or short reads reused the previous disk buffer | A failed or incomplete read could leave old bytes available for signature and protection checks. Loading now clears the active length, requires a complete 256-byte disk header and a successful read matching the reported file size, and sets the new length before protection detection. Invalid loads are rejected and a later valid load can succeed. | `dsk_enable_image` |

### Libretro adaptations and fixes in the port

These entries concern embedding the emulator in a frontend or correcting
ZEsarPCW's own glue. They are not claims that the corresponding standalone
ZEsarUX user interface was broken.

| Area | Problem addressed and current behaviour | Current implementation |
|---|---|---|
| Frame pacing | Keeping the standalone wall-clock throttle alongside RetroArch's pacing slowed execution. Each `retro_run()` now advances one emulated frame without a second host-time wait. Rendering every completed frame also avoids the standalone frame-skip path suppressing the signal that ends a libretro frame. | [`core_pcw.c`](../src/cores/core_pcw.c), [`pcw_engine.c`](../src/libretro/pcw_engine.c), [`pcw_screen.c`](../src/libretro/pcw_screen.c) |
| Audio pacing and delivery | Timer-driven output and five-frame batches drifted against frontend-paced video. Audio is prepared once per emulated frame and delivered after the engine returns. Partial frontend acceptance is handled, and failed frames discard their pending audio. | [`pcw_audio.c`](../src/libretro/pcw_audio.c), [`audiolibretro.c`](../src/libretro/audiolibretro.c), [`libretro.c`](../src/libretro/libretro.c) |
| Keyboard timing | Wall-clock keyboard updates left auto-typed keys held for too many emulated frames during fast-forward or headless execution. Matrix housekeeping, keyboard heartbeat and autorun key pacing now advance with emulated frames. | [`core_pcw.c`](../src/cores/core_pcw.c), `core_pcw_final_frame`; [`pcw.c`](../src/machines/pcw.c), `pcw_keyboard_ticker_update` |
| PCM signedness | Plain `char` made negative PCM and AY samples depend on compiler defaults. The sample path now uses `int8_t`, including mixing and driver declarations. The remaining inherited engine still explicitly requires signed plain `char`. | [`pcw_audio.c`](../src/libretro/pcw_audio.c), [`audiolibretro.c`](../src/libretro/audiolibretro.c), [`ay38912.c`](../src/soundchips/ay38912.c), [`core_pcw.c`](../src/cores/core_pcw.c) |
| Initialization, failure and unload | Process-style startup, fatal exits and timer ownership were unsuitable for a shared library. The compact engine initializes and releases each content session, uses no standalone timer thread, and contains fatal engine errors at synchronous call boundaries. A load error rejects content; a runtime error stops the session and requests frontend shutdown. | [`pcw_engine.c`](../src/libretro/pcw_engine.c), [`libretro.c`](../src/libretro/libretro.c) |
| Incomplete-frame fallback | Reaching the instruction limit must still return a valid frontend frame. The core repeats the last complete image, using a null image only when the frontend supports duplication, and delivers one frame of silence instead of incomplete audio. | [`pcw_engine.c`](../src/libretro/pcw_engine.c), [`libretro.c`](../src/libretro/libretro.c) |
| Disk selection while ejected | Reporting no image whenever the tray was open broke Next/Previous Disc navigation. The selected index now survives ejection and save/load; reinsertion of a restored disk preserves its saved contents. | [`pcw_disk.c`](../src/libretro/pcw_disk.c), [`libretro.c`](../src/libretro/libretro.c) |
| Helper disk confused with external paths | A substring match for `openpcw_os.dsk` could substitute the embedded helper for a user's file or directory of that name. An explicit internal call now selects the helper; external paths always read their named files. | [`dsk.c`](../src/dsk.c), `dskplusthree_enable`, `dskplusthree_enable_openpcw`; [`pcw.c`](../src/machines/pcw.c), `pcw_boot_cpm` |
| Save-state model and memory stability | Loading a state from the other PCW model could change the session's RAM allocation and advertised state size. Such states are now rejected before mutation. To change models, select the model and reload content. | [`pcw_zsf.c`](../src/libretro/pcw_zsf.c), `pcw_zsf_load` |
| Save-state bounds and validation | The serializer previously passed a format limit as if it were the frontend buffer's writable capacity. It now reserves the wrapper and disk space and passes the actual remaining capacity. Loading validates CRC, lengths, block vocabulary and field values before applying state, preventing partial restoration on malformed input. | [`libretro.c`](../src/libretro/libretro.c), [`pcw_zsf.c`](../src/libretro/pcw_zsf.c) |
| Save-state completeness | CPU/RAM snapshots alone omitted in-flight FDC commands, pending bytes, clocks, interrupts, colour state and helper handoff. States now include those values, the mounted disk including guest writes, and native-bootstrap progress. Format v1 has an explicit version independent of core 13.0.1 and rejects development formats. | [Disk-state corrections](#disk-state-corrections-2026-09-08), [Native bootstrap](#native-bootstrap-2026-09-09) |
| Keyboard ownership during save/load | Restoring the raw keyboard matrix could disagree with keys currently held by the frontend. Live user keys remain live; only saved autorun key presses and keyboard heartbeat are restored. Shared matrix-key ownership also prevents one input source from releasing another's held key. | [`pcw.c`](../src/machines/pcw.c), `pcw_machine_state`; [`pcw_keyboard.c`](../src/libretro/pcw_keyboard.c), `matrix_key` |
| Rewind compression cost | Repeated snapshots allocated page scratch memory and recompressed unchanged RAM. The PCW state writer reuses scratch storage and caches unchanged pages while preserving the RAM RLE encoding. This is a performance adaptation. | [`pcw_zsf.c`](../src/libretro/pcw_zsf.c), [`pcw_state_rle.c`](../src/libretro/pcw_state_rle.c) |
| Option parsing and menu construction | Substring matching could accept undeclared values, and bounded legacy menu strings could truncate descriptions or choices. Values now match declared identifiers exactly; unknown values preserve the current setting, and incomplete menus are not submitted. | [`libretro.c`](../src/libretro/libretro.c), [`pcw_keyboard.c`](../src/libretro/pcw_keyboard.c) |
| On-screen RETURN focus | RET's sprite included two columns of the adjacent key's border, so selecting RET also tinted that border. The skin generator now excludes pixels owned by neighbouring rectangular keys while preserving RET's upper-left extension. Both UK and US layouts are checked at both rendering widths and in all three transparency modes. | [`gen_osk_skin.py`](../sources/mappings/gen_osk_skin.py), [`pcw_osk.c`](../src/libretro/pcw_osk.c), [`test_osk.c`](../tests/test_osk.c) |
| Windows paths | ANSI file APIs misread UTF-8 content and playlist paths. The Windows adapters now use UTF-16 file-open and size-query APIs and reject invalid UTF-8. | [`win_compat.h`](../src/libretro/win_compat.h), [`win_posix_compat.c`](../src/libretro/win_posix_compat.c) |
| Diagnostics | Core load and engine errors now use the frontend logger, falling back to `stderr` when unavailable. Invalid or partially usable playlists produce appropriate errors or warnings, and logging remains safe across initialization and unload. | [`pcw_log.c`](../src/libretro/pcw_log.c), [`pcw_disk.c`](../src/libretro/pcw_disk.c), [`pcw_engine.c`](../src/libretro/pcw_engine.c) |

Build and distribution corrections are also port work:

- **Compiler and SDK selection:** Make respects caller-selected `CC`, including
  MinGW/MXE, and forwards Apple architecture, target and SDK settings to both
  compilation and linking. Native test compilation uses `HOST_CC`, independently
  of a cross compiler. Windows and Android path-limit definitions are supplied
  where the retained source needs them.
- **Switching build targets:** objects and linked libraries are kept per
  platform, and Make restores the selected library to the public filename.
  Switching back to a previously built target no longer leaves another
  architecture's library in that location.
- **Exports and Android loading:** the platform export policies restrict the
  library to the libretro API. Android ARM64 uses 16 KiB ELF load-segment
  alignment, through both Make and the JNI build interface.
- **Standalone sources and packages:** the source tree includes the build
  inputs, editable assets and generators needed for its documented workflows.
  Packages bind the checked binary to exact matching core sources and component
  notices. See [PACKAGING](../PACKAGING.md) and the
  [libretro build interfaces](../docs/LIBRETRO.md).

### Compatibility workarounds and presentation choices

| Change | Scope and current behaviour | Current implementation |
|---|---|---|
| Loader timing-check workaround | A specific self-modifying loader sequence could loop or overwrite loaded data under deterministic controller timing. When the instruction bytes match, the core skips its call at `D465h` by continuing at `D468h`. This is a targeted guest-execution workaround, not a general correction of drive timing or proof of cycle accuracy. | [`core_pcw.c`](../src/cores/core_pcw.c), `cpu_core_loop_pcw` |
| CP/M application autorun | Selected whole-disk fingerprints identify application disks that stall in their self-boot loader or need a command at the shell. The helper boots OpenPCW-OS, restores the content disk at the native shell handoff and types the recorded command. This is title-specific integration, not a rewrite of the application's disk file. | [`dsk.c`](../src/dsk.c), `dsk_enable_image`; [`pcw.c`](../src/machines/pcw.c), `pcw_handle_end_boot_disk`, `pcw_boot_check_dsk_not_bootable` |
| Native bootstrap and retry | A GPLv3 C state machine replaces the inherited firmware image. It reads and checks the boot sector through the existing controller, with explicit failure/retry handling. A failed helper waits for Space or reset; a new content session clears unfinished helper substitution. | [Native bootstrap](#native-bootstrap-2026-09-09), [design and compatibility boundary](../docs/BOOTSTRAP.md) |
| Native video geometry | The core renders 720/360/180/360 by 256 pixels for modes 0/1/2/3 and lets the frontend apply the monitor aspect ratio. Standalone border offsets and vertical pixel doubling are removed; crop operates on the native frame. | [`pcw.c`](../src/machines/pcw.c), [`scrlibretro.c`](../src/libretro/scrlibretro.c), [`pcw_crop.c`](../src/libretro/pcw_crop.c) |
| Monochrome phosphor and forced colour palettes | Forced monochrome uses a fixed green, white or added amber phosphor, preventing a guest's colour palette from leaking into it. Forced colour modes supply a default palette when the guest has not programmed one, while retaining a guest-programmed palette. Mode and palette synchronization also follows state restoration. | [`pcw.c`](../src/machines/pcw.c), `pcw_get_rgb_color_mode0`, `pcw_change_palette_colour_indexfinal`; [`libretro.c`](../src/libretro/libretro.c) |
| Frontend controls and feedback | Physical keyboard mappings, configurable RetroPad bindings, an on-screen keyboard, crop options and sampled floppy sounds are additions for libretro users. They do not replace the guest CPU or disk-controller algorithms. | [Input and core options](../README.md#input-and-core-options), [`src/libretro/`](../src/libretro/) |

OpenPCW-OS is a separate project. Its 0.2.0 interrupt-stack correction protects
application buffers and preserves the established loader allocation boundary;
that is a guest-OS fix, not a ZEsarUX emulation fix. Its exact source identity
and notices are recorded in [SOURCE](../sources/openpcw-os/SOURCE.md).

### Superseded changes and source reduction

Earlier integration work also corrected code that the final PCW runtime no
longer includes:

- The standalone timer was made joinable, its event flag atomic, and its stop
  and registry initialization repeatable to permit library unload and reload.
  The current engine eliminates that timer thread entirely and advances the
  relevant work through emulated frames.
- Shared application code received `time_t` conversions for `localtime`,
  wide integer constants for shifts beyond 31 bits, numeric WinMM callback
  arguments, missing no-MIDI return values, an Android miniz platform branch,
  and corrected const/function parameter types. Those application, menu,
  MIDI, archive and utility implementations are absent from the final source.
  Their historical build fixes are not active emulator features.
- Embedded Amstrad ROM loading and its firmware-specific failure detection
  were replaced by the native bootstrap. The port no longer distributes that
  image or its generated byte array.
- Other machine targets, standalone menus and host drivers, networking and
  diagnostic experiments were removed from the shipped runtime. This includes
  experimental protection-sector aliases, synthetic data and timing overrides;
  the explicit loader workaround documented above remains.

The Z80 decoder changes select the retained PCW branches and remove unrelated
machine extensions. This record does not claim a new general Z80 instruction
implementation or list that source reduction as an instruction-accuracy fix.

### Verification in this repository

[`check.py`](../check.py) exercises the libretro contracts and the public C
tests. In particular, [`test_disk_state.c`](../tests/test_disk_state.c) checks
real bootstrap/controller continuations and disk writes,
[`test_state_capacity.c`](../tests/test_state_capacity.c) checks serializer
boundaries, and the [host harness](../tests/host/libretro_host.c) checks state
rejection, disk ownership and lifecycle. Keyboard, audio, crop and OSK have
their corresponding tests under [`tests/`](../tests/).
[`test_build.py`](../tests/test_build.py) and
[`test_package.py`](../tests/test_package.py) cover build and package contracts.
The catalogue records implemented changes; these tests do not individually
prove every controller timing case or compatibility with every PCW program.

## Retained upstream files

The paths below are relative to the repository root. Each modified C/header file
has a dated port notice in addition to its original ZEsarUX licence block.

| Modified file | Scope of the port changes |
|---|---|
| [`src/audio/audio.h`](../src/audio/audio.h) | Select libretro audio and frame-sized buffers; remove unused host drivers. |
| [`src/contend.h`](../src/contend.h) | Retain PCW timing declarations and remove other-machine contention paths. |
| [`src/cores/core_pcw.c`](../src/cores/core_pcw.c) | Integrate frame pacing, input, loader progress and interrupt/FDC fixes. |
| [`src/cpu.c`](../src/cpu.c) | Start the native C bootstrap; retain PCW initialization and concise CPU notes. |
| [`src/cpus/z80_codprddfd.c`](../src/cpus/z80_codprddfd.c) | Retain PCW Z80 behaviour and remove extensions for other machines. |
| [`src/cpus/z80_codpred.c`](../src/cpus/z80_codpred.c) | Retain PCW Z80 behaviour; remove other-machine paths and imported prose. |
| [`src/cpus/z80_codsinpr.c`](../src/cpus/z80_codsinpr.c) | Retain PCW Z80 behaviour; remove other-machine paths and imported prose. |
| [`src/dsk.c`](../src/dsk.c) | Integrate embedded OpenPCW-OS, autorun, diagnostics and DSK format fixes. |
| [`src/dsk.h`](../src/dsk.h) | Expose the retained PCW disk interfaces after dependency pruning. |
| [`src/machines/pcw.c`](../src/machines/pcw.c) | Integrate video modes, input, status, loader handoff and FDC behaviour. |
| [`src/machines/pcw.h`](../src/machines/pcw.h) | Expose the port loader integration and autorun interfaces. |
| [`src/operaciones.c`](../src/operaciones.c) | Retain PCW memory/port operations, diagnostics and concise CPU notes. |
| [`src/operaciones.h`](../src/operaciones.h) | Retain declarations used by the PCW CPU and memory/port operations. |
| [`src/settings.h`](../src/settings.h) | Remove an unused declaration for the standalone memorial display. |
| [`src/soundchips/ay38912.c`](../src/soundchips/ay38912.c) | Retain PCW AY emulation and replace imported prose with concise notes. |
| [`src/soundchips/ay38912.h`](../src/soundchips/ay38912.h) | Retain declarations used by PCW AY audio and save states. |
| [`src/start.h`](../src/start.h) | Retain the declarations used by the PCW libretro runtime. |
| [`src/storage/pd765.c`](../src/storage/pd765.c) | Correct PCW FDC command, status, write and rotational timing behaviour. |
| [`src/storage/pd765.h`](../src/storage/pd765.h) | Expose the Write Deleted Data command handler added by the port. |
| [`src/zesarux.h`](../src/zesarux.h) | Provide portable path limits for the Windows build. |
| [`src/LICENSES_info`](../src/LICENSES_info) | Replace the standalone application's data inventory with the resources actually retained by this PCW core and their notices. |

The retained `src/cpu.h` and `src/cores/core_pcw.h` are unchanged
from the pinned ZEsarUX source, including their notices. They do not carry a
claim that the port modified them.

The generated tree omits other machine targets, standalone menus and host
backends, networking, auxiliary application tools and their unused data. Its
retained units are pruned to the dependencies of the PCW/libretro runtime.
[COMMENT-SOURCES](COMMENT-SOURCES.md) records the upstream documentation replaced
with concise notes; [PROVENANCE](PROVENANCE.md) identifies retained data.

## Port glue and replacements

`src/libretro/` contains the frontend bridge, video/audio drivers, disk controls,
keyboard and gamepad mapping, on-screen keyboard, crop, drive sound and save-state
support. The port also supplies `src/compileoptions.h`, build files, generators
and the public test harness. Port-authored code carries GPLv3 notices.

The compact `pcw_engine.c`, `pcw_audio.c`, `pcw_screen.c` and `pcw_only_impl.c`
provide the PCW equivalents of the original startup, audio, screen, diagnostic,
filesystem and contention helpers. `pcw_debug.h`, `pcw_screen_api.h` and
`pcw_only_impl.h` expose those smaller interfaces. `pcw_keyboard.c/.h` keep the
upstream key-code interface used by the port. `pcw_zsf.c/.h` implement the PCW
subset of ZEsarUX's `src/snap/snap_zsf.c` format; `pcw_state_rle.c/.h` supply the
compatible RAM encoder and unchanged-page cache. The original emulator work is
credited to Cesar Hernandez Bano; the PCW replacements and port changes are by
retrodiv. These are included as maintained port sources, not unmodified upstream
files.

## Imported and generated components

The canonical `src/libretro/libretro.h` is unmodified and retains its own
permissive licence. It is not covered by the port's file-modification claims.

The disk, font and metadata from OpenPCW-OS retain their MIT notices. Their C
headers identify the data owners separately from the port's GPLv3 templates.
The upstream notices and source manifest are copied verbatim. The reconstruction
tool is `sources/openpcw-os/rebuild.py`; [SOURCE](../sources/openpcw-os/SOURCE.md)
records its inputs, published source repository and immutable revision.

The floppy WAV sources and generated PCM data retain David Colmenero's
GPLv2-or-later notice, using GPLv3 for this distribution. MAME-derived catalogue
metadata retains its CC0-1.0 notice; port-authored bindings and annotations follow
GPLv3. The native PCW bootstrap is port-authored GPLv3 code.
These resources are described in [PROVENANCE](PROVENANCE.md) and
[MAPPINGS](MAPPINGS.md); generating C arrays does not replace their original terms.

## Disk-state corrections, 2026-09-08

`src/storage/pd765.c` now preserves in-flight commands, transfer buffers and
seek/search timing in a validated, fixed-width state block. `src/machines/pcw.c`
preserves the associated clocks, interrupt latches and OpenPCW-OS handoff.
`src/dsk.c/.h` select the embedded helper through a dedicated internal API;
external paths always load their own file contents. The state codec and
integration in `src/libretro/` are port work under GPLv3. The published wrapper
format starts at v1, independently of the core's release version. It requires
complete device and native bootstrap state; earlier development formats are
rejected before mutation, without legacy readers or migrations.

## Native bootstrap, 2026-09-09

`src/libretro/pcw_boot.c` replaces the inherited Amstrad firmware with a native
C implementation of the PCW 8256/8512 sector-loading contract. CPU reset and
soft boot arm it, the PCW core advances it alongside controller/scanline events,
and the state runtime preserves its progress. No firmware array is shipped.
The explicit boot-failure callback uses the existing OpenPCW helper-disk path.
A new content session clears any unfinished helper-disk substitution before
booting. Tests cover controller phases, checksum failure, banking, soft boot
and repeated sessions on both machine models.
See [BOOTSTRAP](../docs/BOOTSTRAP.md) for provenance and compatibility limits.
