# ctSpaces 5.2 Pre-Deployment Security Review

Date: 2026-07-23  
Initial reviewed build: `5.1.0.13`  
Initial SHA-256: `AD78A27F199B5A4860F849FE73B44A8629BC63B3B7D55983E37CD003C0C70A2E`  
Remediated candidate: `5.2.0.0` (`dist\x64\Release\ctSpaces.exe`)  
Candidate SHA-256: `7D14D682733BA5EDAF59E45FCC7B623951D231E84E31FAA071E9D66D9E7C757C`  
Decision: **HOLD until the remaining unsigned-release validation gates pass**

Historical note: this review is retained for its dated findings and evidence. It
is superseded for current release status by the public
[`docs/RELEASE_VALIDATION_5.3.0.10.md`](docs/RELEASE_VALIDATION_5.3.0.10.md)
report.

## Executive Summary

The dark-theme client-selector defect remains fixed in the installed
`5.1.0.13` baseline. The archive, image, QA-isolation, path-containment, process
launch, and PE-hardening findings from the initial review were remediated in
source and rebuilt as `5.2.0.0`. The new Release x64 candidate passed the
project release gate, in-process archive regression suite, isolated theme stress
test, and isolated auto-fetch layout test.

The installed AppData copy was deliberately not replaced during this work. It
remains the previously verified `5.1.0.13` executable with its original hash.
The new `5.2.0.0` file exists only in the project Release directory.

The owner chose an unsigned internal release on 2026-07-23. Authenticode is
therefore an explicitly accepted release risk, not a remaining blocker. Broad
deployment is still on hold because the current working tree has not yet been
converted into a clean tagged release commit and the excluded browser/live-icon
and pilot gates have not run. The current exact artifact passed a Microsoft
Defender custom scan with no threats found. These are release-control gates, not
unresolved compiler or functional-test failures.

## Remediation And Validation Update

### Implemented controls

- Backup creation rejects a reparse-point source root, reparse-point children,
  and reparse-point extra files. Enumeration is bounded to 500,000 items,
  256 GiB of declared source data, 128 path components, and bounded path depth.
- Backup compression rechecks each file immediately before opening it and fails
  if it became a reparse point or its size changed after preflight.
- Modern 7z restore rejects Windows reparse attributes and POSIX symlink
  attributes, applies item/depth/size limits, and requires a five-percent
  free-space reserve (minimum 5 GiB) plus per-item filesystem overhead.
- Legacy ZIP restore validates all entry paths, duplicates, device names,
  symlink/reparse attributes, item/depth/size limits, and disk reserve before
  extraction. It holds the archive open read-only through validation/extraction,
  runs the fixed System32 PowerShell executable under a kill-on-close job, and
  has a 30-minute timeout.
- Image import now caps encoded input at 16 MiB, dimensions at 8192 pixels, and
  decoded area at 32 million pixels. It no longer creates an unnecessary giant
  square intermediate. ICO parsing caps entry count, dimensions, and payload
  area.
- QA mode requires an explicit child `--qa-data-dir`; automated scripts use
  project-local disposable data and verify that the live config does not change.
- Client names reject overlong, control-character, and device-name input.
  Client operations require a direct, non-reparse child of `Sites` immediately
  before filesystem access or mutation.
- Favicon input is restricted to ASCII DNS-host syntax. Browser launch supplies
  the known executable as `CreateProcessW`'s application path. Executable/data
  path resolution handles long paths and fails closed.
- Release x64 builds enable compiler CFG instrumentation and link with
  `/GUARD:CF /CETCOMPAT`. The final PE reports High Entropy VA, ASLR, DEP/NX,
  Control Flow Guard, and CET compatibility.

### Build record and failures

All build operations were one-worker, local, offline, and limited to this
project. No installer/update step was run.

1. The first MSBuild launch stopped before compilation with `MSB6001` because
   the Codex process environment contained both `Path` and `PATH`. The build
   command now removes both aliases in the child process and creates one
   canonical `Path`; no machine/user environment setting is changed.
2. The first normalized build was stopped after `StdAfx.cpp` because the command
   wrapper had an accidental one-second timeout. A process check confirmed no
   `MSBuild` or `cl` process remained.
3. The next build reached the bundled 7-Zip target but MSVC 14.50 raised internal
   compiler error `C1001` while batch-compiling three non-PCH `/O2` units with
   CFG. Each unit compiled successfully by itself with the same CFG settings.
4. `Format7z.vcxproj` now gives the PPMd decoder and encoder distinct unused,
   Release-x64-only preprocessor definitions. This causes separate compiler
   invocations without changing optimization or hardening. The full
   `Release|x64` `/m:1` build then succeeded with no reported warning or error.
5. The owner promoted the completed release to display version `5.2` and Windows
   file/product version `5.2.0.0`. The version-only rebuild, full release gate,
   archive round-trip harness, and exact-file Defender scan all passed.

### Candidate and test evidence

- Candidate: `dist\x64\Release\ctSpaces.exe`
- File/product version: `5.2.0.0`
- Size: 5,090,816 bytes
- Built: 2026-07-23 11:27:58 local time
- SHA-256:
  `7D14D682733BA5EDAF59E45FCC7B623951D231E84E31FAA071E9D66D9E7C757C`
- Authenticode: `NotSigned` (accepted for this internal release)
- Microsoft Defender exact-file custom scan: passed with no threats found;
  signatures `1.455.279.0` dated 2026-07-22 18:45, `MpCmdRun` exit code `0`.
- `tests\Test-Release.ps1`: passed; bundled 7-Zip 26.02 and `Default.7z`
  SHA-256 `3BD17C30D50CD7C8F467DC87C99DEC116531F40D5BA187843C580F3966AA70DC`.
- `tests\Test-ArchiveRoundTrip.ps1`: passed in-process archive, validated
  backup, corruption rejection, restore, reparse, and resource-limit cases.
- `tests\Test-ThemeResponsiveness.ps1`: passed 240 rapid changes with GDI delta
  zero using an isolated QA data root.
- `tests\Test-AutoFetchDialogLayout.ps1`: passed at 144 DPI with themed
  owner-draw buttons and correct Escape/launcher behavior.
- The browser/network-dependent live-icon test was intentionally excluded from
  this bounded physical-PC validation phase.
- Installed copy preserved:
  `%LOCALAPPDATA%\InfinitySys\ctSpaces\ctSpaces.exe`, version
  `5.1.0.13`, 5,058,560 bytes, SHA-256
  `AD78A27F199B5A4860F849FE73B44A8629BC63B3B7D55983E37CD003C0C70A2E`.

## Scope And Method

Reviewed:

- Startup arguments, data-root selection, mutex behavior, and QA isolation.
- Client-name validation and destructive profile operations.
- Browser process creation and self-install/update behavior.
- Automatic favicon download and local image/icon parsing.
- Modern `.7z` backup creation, inspection, extraction, and commit/rollback.
- Legacy `.zip` restore and PowerShell command construction.
- Release project hardening, Authenticode state, PE mitigation flags, Git state,
  vendored 7-Zip source/version, and starter-profile privacy.
- Tracked source/docs/tests for common credential and private-key patterns.
- Current authoritative 7-Zip/NVD and Microsoft deployment guidance.

Not performed on this physical PC:

- No hostile-archive, archive-bomb, reparse-loop, oversized-image, fuzzing, or
  destructive proof-of-concept was executed.
- No rebuild, full test suite, archive round-trip suite, server, listener, GPU
  operation, installation, or download was started during this review.
- No antivirus submission, dynamic instrumentation, or external penetration test
  was performed.

## Deployment Blockers

### 1. High — Backup creation follows reparse points and has no traversal cap

Evidence:

- `InProc7z.cpp:245` detects only `FILE_ATTRIBUTE_DIRECTORY`.
- `InProc7z.cpp:281` records the full attributes, including a possible
  `FILE_ATTRIBUTE_REPARSE_POINT`, but does not reject or handle it.
- `InProc7z.cpp:289-291` recursively enters every directory.
- `InProc7z.cpp:811-839` stores the complete recursive inventory in memory before
  compression, with no item, depth, byte, or cycle limit.

Impact:

- A junction or directory symlink under `Sites` can make a backup read files
  outside the ctSpaces profile tree and place their contents in the archive.
- A file symlink can likewise cause the target file to be read.
- A self-referential or cyclic reparse path can produce runaway recursion,
  excessive memory use, a stack failure, or an apparent system hang.
- A very large legitimate tree can also exhaust memory because every entry is
  accumulated before compression.

Required remediation:

- Reject all source entries with `FILE_ATTRIBUTE_REPARSE_POINT` by default.
- Add strict maximum item count, recursion depth, aggregate source bytes, and
  pathname length during enumeration, before adding entries to memory.
- Fail closed with the exact offending relative path.
- Add regression tests for directory junctions, file symlinks, cycles, and limits.

### 2. High — Restore resource limits can still exhaust the system drive

Evidence:

- `BackupEngine.cpp:18-20` allows 2,000,000 archive items and reserves only
  64 MiB of free space.
- `BackupEngine.cpp:180-196` compares only declared uncompressed file bytes with
  free disk space.
- Filesystem allocation units, directory/MFT metadata, decoder memory, and the
  operational free space Windows needs are not included.

Impact:

- Millions of zero-byte or small entries can consume substantial filesystem
  metadata even when declared uncompressed bytes appear safe.
- A valid-by-current-rules archive can leave the system volume with roughly
  64 MiB free or exhaust it completely, risking application failures and Windows
  instability.
- Crafted compression settings may demand excessive decoder memory even when the
  output byte total is small.

Required remediation:

- Replace the 2,000,000-entry ceiling with a deployment-appropriate limit.
- Keep a meaningful absolute and percentage-based free-space reserve.
- Add an absolute expanded-size ceiling and account conservatively for per-entry
  metadata/allocation overhead.
- Bound decoder memory/dictionary settings or reject archives above a defined
  operational policy.

### 3. High — Legacy ZIP restore has no size/item limits and waits forever

Evidence:

- `ctSpaces.cpp:4869` waits for PowerShell with `INFINITE`.
- `ctSpaces.cpp:4879-4906` invokes the pinned system Windows PowerShell and
  `Expand-Archive`.
- `ctSpaces.cpp:5139-5145` sends modern `.7z` files through validated staging but
  sends legacy ZIPs directly through the unbounded compatibility path.

Impact:

- A malformed ZIP or compression bomb can consume disk/CPU indefinitely and
  appear to freeze ctSpaces.
- The modern archive item, path, expanded-size, and free-space inspection is not
  applied before legacy extraction.

The PowerShell executable path is pinned and literal-path arguments correctly
escape apostrophes; direct shell injection was not found.

Required remediation:

- Safest option: remove legacy ZIP restore from the deployed build and provide a
  separate, controlled migration tool.
- Otherwise, inspect ZIP entries in process before extraction, apply the same
  strict path/resource policy as `.7z`, add a bounded timeout/cancellation path,
  and extract under an enforceable disk quota.

### 4. High — Image conversion and ICO loading allow memory exhaustion

Evidence:

- `ctSpaces.cpp:6242-6255` decodes an image and allocates a square intermediate
  whose side is the source's largest dimension, with no dimension/pixel cap.
- `ctSpaces.cpp:6249-6252` converts unsigned decoder dimensions to `int` without
  first bounding them.
- `ctSpaces.cpp:8525-8535` reads the complete ICO file into one memory buffer with
  no file-size ceiling.

Impact:

- A narrow but very tall/wide image can request gigabytes for the square
  intermediate bitmap.
- An oversized manually selected or restored `client.ico` can cause severe memory
  pressure or OOM when the launcher or watcher parses it.
- A small compressed image can still decode to extreme dimensions.

Required remediation:

- Enforce encoded-file, width, height, and total-pixel ceilings before allocation.
- Never create an original-dimension square; decode/downscale directly to the
  required maximum of 256×256.
- Add checked integer conversions and RAII cleanup for partially created icons.
- Reject oversized `client.ico` files before copying or loading them.

### 5. High — QA mode can run concurrently against live production data

Evidence:

- `ctSpaces.cpp:3139-3173` changes to a QA-specific mutex whenever
  `--qa-instance` is present, but requires/validates `--qa-data-dir` only if that
  second argument happens to be supplied.
- `tests/Test-AutoFetchDialogLayout.ps1:10-14,178-180` deliberately creates its
  profile under live `%LOCALAPPDATA%\InfinitySys\ctSpaces` and supplies only
  `--qa-instance`.
- `tests/Test-LiveIconRefresh.ps1:15-19,670-672` does the same.
- `tests/Test-Release.ps1:72` checks only that the source contains the
  `--qa-data-dir` feature, not that every QA launcher supplies it.

Impact:

- A QA instance can bypass the production mutex while reading/writing the live
  config, template, and profile root.
- Tests can race with the installed application and risk live-data corruption.

Required remediation:

- Make `--qa-instance` invalid unless a valid isolated `--qa-data-dir` is also
  present.
- Derive QA data under the QA executable folder by default, or require the
  explicit absolute child path.
- Update both scripts to use their disposable QA directory exclusively.
- Make the release check inspect every QA `Start-Process` argument set.

### 6. High operational — The release is not reproducible from Git

Evidence:

- Current branch: `cpp`
- Current commit: `56e13ed6eb1c6b636949340e72862a3b833a2e3d`
  (`Update README.md`, 2026-01-15).
- The working tree contains many modifications/deletions, while the current
  backup engine, 7-Zip wrapper, progress UI, tests, docs, version header, and
  complete `3p/7zip` tree are untracked.

Impact:

- A clean clone does not reproduce the reviewed source or executable.
- Security-sensitive changes have no durable review/history boundary.
- Recovery, code review, release comparison, and dependency provenance are
  fragile.

Required remediation:

- Intentionally review and commit the complete source/dependency/test/doc set.
- Exclude only generated QA/build artifacts.
- Build the final artifact from a clean tagged commit and record commit, toolchain,
  dependency commit, hash, and test results.

### 7. Accepted operational risk — The executable is unsigned

Evidence:

- `Get-AuthenticodeSignature` reports `NotSigned`, with no signer or timestamp.
- The self-update flow (`ctSpaces.cpp:7371-7428`) trusts the embedded version and
  user confirmation; it does not verify an approved publisher/signature.

Impact:

- Recipients cannot use Windows publisher verification to distinguish the
  approved build from a modified executable.
- SmartScreen reputation cannot transfer across unsigned releases, and enterprise
  policy may block execution.

Owner decision and compensating controls:

- The owner chose an unsigned internal release on 2026-07-23; obtaining and
  applying a publisher certificate is not part of this release.
- Publish the executable through one controlled location and publish its exact
  SHA-256 as the primary integrity check.
- The exact-file Microsoft Defender scan completed with no threats found. Repeat
  it if the executable is rebuilt or otherwise changes.
- Complete the pilot before publishing the link.
- The Teams message warns recipients that Windows may show **Unknown publisher**
  and tells them not to run a file whose hash differs.
- Organization policy can still override this accepted project risk and require
  a signed executable.

Microsoft states that unsigned files must build SmartScreen reputation separately
for each version and may be blocked by enterprise policy:
<https://learn.microsoft.com/windows/apps/package-and-deploy/smartscreen-reputation>

## Medium And Low Findings

### Medium — Client input and directory enumeration need stronger bounds

- `SanitizeName` (`ctSpaces.cpp:5321-5353`) blocks separators, ADS colons, quotes,
  trailing spaces/dots, and common reserved names, but has no explicit length or
  control-character ceiling.
- `UpdateClientsComboBox` (`ctSpaces.cpp:5295-5307`) performs filesystem
  enumeration without exception handling.
- Destructive operations construct paths from sanitized names but do not perform
  a final strict-child check immediately before mutation.

Recommendation: reject rather than transform invalid names, cap them well below
the NTFS component limit, reject all characters below U+0020, handle enumeration
errors, reject reparse profile roots, and verify every mutation target is a strict
child of the expected root.

### Medium — Release hardening is incomplete

The shipped PE has:

- High-entropy VA: enabled.
- ASLR (`DYNAMIC_BASE`): enabled.
- DEP/NX: enabled.
- Control Flow Guard: not enabled.

The project uses `/sdl`, but release settings do not explicitly enable CFG,
`/CETCOMPAT`, or Spectre mitigation. Microsoft documents that `/guard:cf` is off
by default and recommends enabling it across compiled code:
<https://learn.microsoft.com/cpp/build/reference/guard-enable-control-flow-guard>

Recommendation: enable and verify CFG for the app and static library, evaluate
`/CETCOMPAT` for x64, raise warnings to `/W4`, and make new warnings fail CI after
the current baseline is cleaned up.

### Low — Favicon domain input is not URL-encoded

- `NormalizeFaviconDomain` (`ctSpaces.cpp:4217-4238`) removes schemes/paths and
  whitespace but permits query delimiters such as `&` and `=`.
- `GuiAutoIcon` (`ctSpaces.cpp:4649-4651`) concatenates the value directly into a
  Google S2 query string.

The request origin is fixed to Google's HTTPS favicon service, the response is
capped at 5 MiB, and this is not a general arbitrary-URL downloader. Still,
percent-encode a strictly validated DNS/IP host value and reject query delimiters.
The existing privacy guide correctly discloses that the entered domain is sent to
Google.

### Low — Browser launch should pin `lpApplicationName`

`ctSpaces.cpp:5275-5285` fully quotes the selected fixed browser path and no
current command injection was found. Pass that known executable path as
`CreateProcessW`'s `lpApplicationName` anyway, leaving only browser arguments in
the mutable command line.

### Low — Long executable paths are not handled robustly

Several calls use `GetModuleFileNameW` with `MAX_PATH` and do not detect
truncation (`ctSpaces.cpp:3134-3137`, `7305-7307`, `7372-7374`). Use a resizing
buffer or a sufficiently sized long-path implementation and fail closed on
truncation.

## Controls That Passed Review

- No listener, server, analytics client, or telemetry endpoint was found.
- The only in-app download path is the fixed Google HTTPS favicon service, with
  connect/read timeouts and a 5 MiB response cap.
- Modern 7z entry validation rejects absolute paths, `.`/`..` components, drive
  colons/ADS, control characters, wildcard characters, common device names,
  trailing dots/spaces, and overlong path components before extraction.
- Modern restores use a staging folder, manifest/profile-count check, CRC/error
  handling, transactional folder swap, and rollback.
- Backup creation fails if an enumerated file cannot be opened; skipped source
  files do not silently produce a successful backup.
- Legacy PowerShell restore uses the exact System32 Windows PowerShell path,
  `-NoProfile`, `-NonInteractive`, literal paths, and apostrophe escaping.
- Browser executable discovery is restricted to standard Edge/Chrome/Brave
  locations, and the current command line quotes both executable and profile.
- The starter `Default.7z` static privacy inspection passed:
  - 285 entries.
  - No forbidden cookie, history, login, session, network, or site-storage paths.
  - No private account/sign-in/sync/password/autofill keys in `Preferences`.
  - All four expected extension settings were present.
- The tracked-file secret scan found no common API-key, access-token, private-key,
  or password-assignment patterns in the reviewed source/docs/tests.
- Bundled 7-Zip source is a clean detached checkout at
  `f9d78aff31a5f2521ae7ddbdc97c4a8855808959`, version 26.02.
- The static library compiles only the 7z handler. Recent NVD entries concerning
  RAR5, UDF, AR, SquashFS, NTFS, WIM, and similar stock-7-Zip handlers are not
  present in this narrow build. NVD's recent RAR5 entry is:
  <https://nvd.nist.gov/vuln/detail/CVE-2026-58052>
- The privacy guide accurately states that profile separation is not encryption,
  Incognito mode, or a security sandbox, and that backups are unencrypted and may
  contain active sessions and credentials.

## Remaining Release Gates

Completed in this bounded physical-PC phase:

- Safe client-name/target-containment implementation and source checks.
- Reparse/resource-bound archive regression coverage.
- Bounded legacy ZIP hostile-entry validation.
- Image/ICO size and dimension guards plus source checks.
- QA isolation, Release gate, archive round trip, theme stress, and dialog/DPI
  checks.
- One-worker `Release|x64` build and PE mitigation verification.
- Exact-file Microsoft Defender custom scan of SHA-256
  `7D14D682733BA5EDAF59E45FCC7B623951D231E84E31FAA071E9D66D9E7C757C`;
  no threats found.

Still required before broad deployment:

1. Put the intended source set in a clean reviewed commit/tag and rebuild from
   that exact revision.
2. Record and publish the SHA-256 of the exact unsigned release file.
3. If step 1 creates a different executable, repeat the exact-file Defender scan
   and record its new SHA-256.
4. Run the excluded live-icon/browser check and a controlled pilot from a
   disposable Windows account or deployment VM.
5. Replace the remaining link/support placeholders in
   the internal announcement only after those gates pass.
