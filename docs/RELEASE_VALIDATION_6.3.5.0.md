# Release validation: 6.3.5.0

This record describes the October 23, 2026 backup-restore repair and its exact
release package. The publication target is
[v6.3.5.0](https://github.com/madrobdestroyer/ctSpaces/releases/tag/v6.3.5.0).
The tag identifies the corresponding source commit. Workspace evidence links
refer to retained private build artifacts, which are not included in Git or the
runtime ZIP.

## Exact candidate and package

The production candidate is
`build/release-2026-10-23/verified-candidate/ctSpaces.exe`. Its Windows file and
product versions are **6.3.5.0**, and its display version remains **6.3**.

| Artifact | SHA-256 |
|---|---|
| ctSpaces.exe | `A953054DAF7876AE6EF6C35BF393041B9F0B956C0C055CB964FFDDDC044A1676` |
| Private ctSpaces.pdb | `1DFABD5CAFA3CA7501D7986AA7AC4B3A1A18D31C38C438192C6035850A0AA23A` |
| ctSpaces6.3.5.0.zip | `493CF42CCC9FB0A471DAC230C0F1564A4F5499F1E6C9FF3D294DC0757784CB81` |

The fresh Release/x64 production build had **zero compiler warnings and zero
errors**. `Test-Release.ps1` passed against this exact candidate, including its
Windows version resources and bundled starter archive. The starter archive
remains SHA-256
`54A0FE4B18C681353852DD86363306AC5E475784111FD76C949E829DC8464EA5`;
the pinned static 7-Zip library remains
`452507481B0080F673ECAC76BCEE6F4913599B5515EC16F91626BD43A3AA38E2`.
The bundled 7-Zip version is 26.02.

The ZIP contains exactly one root `ctSpaces.exe`. Extracting it reproduced the
candidate's exact bytes and version. Ordinary PE imports were checked against
the established Windows-system dependency list. The package includes no PDB,
client/config data, QA marker, source, logs or walkthrough output. A Windows
Defender custom scan of the exact staged ZIP returned exit code 0. This is not
a clean-machine runtime or universal malware-safety certification.

Evidence: [candidate](../build/release-2026-10-23/candidate.json),
[build](../build/release-2026-10-23/candidate-build.log.json),
[release checks](../build/release-2026-10-23/release-check.log.json),
[package](../build/release-2026-10-23/package-stage/staging-result.json), and
[Defender scan](../build/release-2026-10-23/defender-scan.json).

## Reproduced failure and repair

A valid ordinary backup passed full CRC testing but failed staging with
`0x80004005` when the temporary folder made its paths longer. The same unchanged
archive succeeded at a shorter staging path. This establishes an app-side bug;
it does not establish the condition of the coworker's particular archive.

Restore-only extended Win32 I/O now covers extraction, ownership binding,
verification and owned cleanup. Sealed inventory names remain ordinary paths.
Final budgets are mapped from the verified inventory and rechecked after the
last safety callback, before bound directory moves. Existing client limits,
identity checks, reparse/link rejection, content verification and recovery
behavior remain in place.

The new regression includes actual final directory/file boundaries of 247/259
characters, which succeed, and 248/260, which are refused without changing live
data or the verified staged payload. An occupied ordinary extraction output
returns its Windows error and item rather than a checksum accusation.

The targeted allocation test initially exposed a real extraction callback
termination: allocation escaped a `noexcept` boundary. The repaired callbacks
contain and translate exceptions, and a strong member reference protects the
pending output stream. The final fault returns `E_OUTOFMEMORY`, closes leases,
leaves archive/source/live bytes unchanged, and permits a successful fresh
retry. An unrecorded partial output may be safely retained rather than deleted
without verified ownership. This is extraction hardening, not a global audit
of every compression/update allocation path.

## Completed automated checks

Fresh checks on the frozen **6.3.5.0** source:

| Check | Result |
|---|---|
| RestoreStagingPath | 74 assertions, zero failures |
| Original unchanged reproducer archive | 52 assertions, zero failures; exact-byte commit and recovery |
| InstallerRuntime, HeadlessOnly | 20 scenarios passed |
| InstallerPublication | 70 assertions passed |

Both installer harnesses have actual 6.3.5.0 PE VERSIONINFO resources. The
runtime suite exercises the production version reader and install/update
comparison paths. Its invalid-paint, presentation, window-request, window,
native-show/focus/foreground and device-input counters were all zero. Installed
foreground handoff and real user installation were excluded. Installer harness
build warnings remain in their logs; the zero-warning claim applies to the
production build. Receipts:
[release QA](../build/release-2026-10-23/release-qa/result.json) and
[original archive proof](../build/release-2026-10-23/original-archive/result.json).

The preceding **unpublished repair candidate**, version 6.3.4.0 with SHA-256
`8EEE626D854BD42E51EB821D28BA6817006F888F0DE947AB33042AF6D8C8AAB5`,
passed **59 unique available background Core suites**, **18 headless Notes
selectors / 4,852 assertions**, and **716 additional client assertions / 11
native cases**. This is the repaired review build, not the published 6.3.4.0
binary. Exact scope is retained in the
[repair checkpoint](../build/restore-2026-10-23/verification.json).

All 345 unique inputs from that completed checkpoint were compared before the
release build. **344 were unchanged; only `version.h` changed.** Adding the
explicitly pinned starter archive and static library gives the 347-input
[release source fence](../build/release-2026-10-23/source-frozen.json), SHA-256
`9C1E72760E42AE3C7FEFA311E90F1BD7C85550DBDE28719B5421F5A046BB09CB`.
The 59/18 larger groups were not rerun on the new version and are not relabeled
as fresh 6.3.5.0 evidence. Version-only provenance is recorded in the
[release plan](../build/release-2026-10-23/release-plan.json).

Three obsolete version pins were removed from native behavioral tests; actual
executable/header version validation remains in the release checks. Unused
stale-version receipt fields were removed without changing historical records.

## Boundaries and retained behavior

- The coworker's archive and its exact producing version were unavailable.
  Test archives are ordinary valid backups created through the production API;
  no actual historical-producer backup is claimed.
- The previously restricted `ArchiveRoundTrip` wrapper and mutation corpus
  were not retried or bypassed. Valid-backup long-path tests are separate
  ordinary defect reproductions.
- This release review used no mouse, foreground window control, live browser,
  or actual installation. Native tests were compiled from the frozen source;
  the production executable was build/release checked and inspected as data.
- No fresh compositor, resize-flicker, cross-monitor DPI, SSO or browser-policy
  certification is claimed. Earlier live checks remain in the historical
  [6.3.4.0 validation](RELEASE_VALIDATION_6.3.4.0.md).
- The Edge layout choice applies to a new session with Restore tabs off. An
  already saved window can restore its prior layout. After the desired new
  window is closed normally, Restore tabs can be enabled for that saved
  session. This accepted limitation is unchanged.
- Backup format and notebook layout are unchanged. Restore still replaces the
  active client collection and retains the previous collection in recovery.
  Windows/browser-protected sign-ins may require authentication on another PC.

The prior 6.3.4.0 tag, versioned documentation and ZIP remain historical and
unchanged. No older release asset is replaced by this patch publication.
