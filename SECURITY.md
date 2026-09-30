<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (c) 2026 retrodiv <retrodiv@proton.me> -->

# Security policy

## Supported versions

Security fixes target the latest public release and the current development
branch. Older versions do not have a separate security maintenance branch;
users may be asked to upgrade. Reports affecting older versions are still
welcome, especially when the issue also affects current code.

## Reporting a vulnerability

Report vulnerabilities privately to [retrodiv@proton.me](mailto:retrodiv@proton.me),
or use the repository host's private vulnerability reporting facility when
available. Keep exploit details and sensitive attachments out of public issues.

Include:

- the core version or commit, platform, frontend version and relevant build flags;
- the affected input, such as a DSK image, M3U playlist, save state or libretro API call;
- minimal reproduction steps, the observed result and the expected behaviour;
- a small synthetic reproducer you can legally share, and a sanitizer trace if available.

Do not send commercial disc images, credentials or private files. Describe the
failure first if a reproducer contains sensitive data. Review logs and memory
dumps before sharing them.

Maintainers should keep reports and non-public reproductions private, reproduce
the issue on current code, assess its impact and coordinate a fix and disclosure
with the reporter. Request only the information needed to investigate and remove
unnecessary sensitive attachments after resolving the report. Agree disclosure
timing privately so users have an opportunity to update.

## Security boundary

DSK images, M3U playlists, save-state buffers, cheat strings and paths supplied
by the frontend are external inputs processed by native code. Treat content
and save states as potentially unsafe and load them only from sources you trust.
The core runs inside the frontend process with its permissions. Emulation,
input validation and save-state checksums do not provide a sandbox for hostile
inputs; a parser or memory-safety defect can affect the whole frontend process.

M3U entries can use absolute paths or paths relative to the playlist, including
parent directories. They are not confined to the playlist directory. Inspect
playlists before loading them. Archive extraction is handled by the frontend;
its security and the host's filesystem permissions are outside the core's control.

Guest disc writes stay in the mounted in-memory disc buffer and are not written
back to the original host image. Save states contain emulated RAM and the mounted
disc buffer, including guest writes, and may therefore contain private data or
software you cannot redistribute. Share a synthetic reproduction instead of a
session state containing such material.

## Diagnostics and privacy

Diagnostics use the frontend's libretro logger, with standard error as a fallback.
Messages can include local paths and disc names. The core does not automatically
upload logs or telemetry; the frontend controls any logging or sharing it offers.
Redact identifying information before sending a report.

See [the README](README.md#content-and-discs) for supported content and save-state
behaviour. Ordinary compatibility problems can be reported in public issues with
a minimal reproduction that contains no private data or restricted content.
