# ctSpaces: current project handoff

Updated October 23, 2026. Start here when continuing work in another chat.
This is a maintainer status record, not a claim that the application has no bugs.
Versioned validation reports describe their own historical binaries; an old
report's use of “current” does not override this document.

## Current release and local copies

| Item | Current state |
|---|---|
| Public release | [6.3.5.0 / v6.3.5.0](https://github.com/madrobdestroyer/ctSpaces/releases/tag/v6.3.5.0) |
| Published source | `ef08c9113ec30b2eb437cdbaf67d4433db7649e5`, branch `cpp` |
| Version source | `version.h`; Windows version **6.3.5.0**, display version **6.3** |
| Normal local review EXE | `build/notes-format-on-demand/app/ctSpaces.exe` |
| Review record | `build/notes-format-on-demand/result.json` |
| Frozen released candidate | `build/release-2026-10-23/verified-candidate/ctSpaces.exe` |
| Distribution | `dist/x64/Release/ctSpaces6.3.5.0.zip`, one root `ctSpaces.exe` |
| Normal installation | `%LOCALAPPDATA%\InfinitySys\ctSpaces\ctSpaces.exe` |

The review folder's old design name is only a directory name; it is not another
Git branch or an older current version. Its EXE and matching PDB were refreshed
from the verified 6.3.5.0 candidate. The installed EXE was independently read
during this handoff and also reported 6.3.5.0 with the released executable hash.
Housekeeping did not install, launch, stop, or update the installed app.
Recheck installation state before claiming its version in a later session.

| Released artifact | SHA-256 |
|---|---|
| EXE | `A953054DAF7876AE6EF6C35BF393041B9F0B956C0C055CB964FFDDDC044A1676` |
| Matching private PDB | `1DFABD5CAFA3CA7501D7986AA7AC4B3A1A18D31C38C438192C6035850A0AA23A` |
| ZIP | `493CF42CCC9FB0A471DAC230C0F1564A4F5499F1E6C9FF3D294DC0757784CB81` |

GitHub publication and unauthenticated download verification are recorded in
`build/release-2026-10-23/publication.json`. The remote branch and peeled tag
matched the source commit. The previous 6.3.4.0 tag and release assets were
preserved. This handoff/documentation update does not replace a published asset,
change the version, or publish another release.

This handoff, `AGENTS.md`, and the current guides are versioned on `cpp` so the
continuation record survives this chat. The branch can advance with these
documentation-only commits while `v6.3.5.0` remains on the released source commit
above. Use the release tag to reproduce the shipped binary. Private workstation
memory, build receipts, client data, recordings and untracked design files are
not part of this documentation push; the portable facts and evidence boundaries
are summarized here and in the versioned validation reports.

## What the latest repair addressed

A valid backup passed archive CRC checks but failed restore when the temporary
staging path made otherwise valid paths too long. The same unchanged archive
succeeded at a shorter staging location. Version 6.3.5.0 adds restore-only
extended Win32 I/O for extraction, ownership binding, verification and owned
cleanup. It preserves the final destination limits of 247 directory characters
and 259 file characters, identity/link protections and transactional recovery.

Extraction now preserves the actual error and item when available. Targeted
allocation failure returns safely instead of escaping a `noexcept` callback;
the pending output stream has a strong owner. This was extraction hardening,
not a complete allocation-failure audit of every compression/update path.

The coworker's actual archive and its original producing version were not
available. The reproduced app defect is fixed; successful restoration of that
particular archive still needs the coworker's result. A OneDrive-transferred
backup must be fully available locally before retrying. The backup format did
not change. See [release notes](../RELEASE_NOTES_6.3.5.0.md) and
[release validation](RELEASE_VALIDATION_6.3.5.0.md).

## Validation: distinguish the exact candidates

| Candidate | Completed coverage |
|---|---|
| Fresh released 6.3.5.0 | 74 RestoreStagingPath assertions; 52 checks using the unchanged reproducer archive; 20 headless installer scenarios; 70 installer-publication assertions |
| Prior unpublished repaired 6.3.4.0, EXE SHA `8EEE626D854BD42E51EB821D28BA6817006F888F0DE947AB33042AF6D8C8AAB5` | 59 background Core suites; 18 headless Notes selectors / 4,852 assertions; 716 extra client assertions / 11 native cases |

The latter is not the previously published 6.3.4.0 binary. Of its 345 frozen
inputs, only `version.h` changed before building 6.3.5.0. Adding the pinned
starter archive and static library gives the 347-input release fence. Do not
describe the 59/18 groups as fresh 6.3.5.0 executions. The production release
build had zero warnings/errors; that statement does not cover every harness.

The exact staged ZIP passed the release checks and a Defender custom scan, and
the public downloaded ZIP reproduced the tested EXE bytes. None of these proves
the absence of bugs or certifies every browser, organization policy or machine.

Evidence index:

- `build/release-2026-10-23/result.json`: release checkpoint and provenance.
- `build/release-2026-10-23/candidate.json`: EXE/PDB and source-fence identity.
- `build/release-2026-10-23/source-frozen.json`: 347 release inputs.
- `build/release-2026-10-23/release-qa/result.json`: fresh native checks.
- `build/release-2026-10-23/original-archive/result.json`: unchanged reproducer.
- `build/restore-2026-10-23/verification.json`: preceding broad repair scan.
- `build/release-2026-10-23/review-refresh-latest.json`: normal review refresh.

Build evidence is private/ignored workspace material, not content of the app ZIP.
Versioned reports and Git tags remain the portable historical references.

## Known boundaries and follow-up

- No fresh physical resize/compositor-flicker, cross-monitor DPI, real SSO,
  printer-device, or clean-machine installation certification was performed
  for 6.3.5.0. Earlier UI evidence belongs to the binary named in its report.
  Native layout assertions and still captures cannot establish that no flashing
  occurred between frames. Keep any such live review explicitly pending.
- The previously restricted `ArchiveRoundTrip` wrapper and extra mutation
  corpus were not retried or bypassed. The ordinary valid-backup defect tests
  are a separate scope. Do not silently remove this limitation from reports.
- Two earlier policy-rejected cleanup targets remain excluded: the
  `build/retest-post-housekeeping-2026-10-15/browser-launcher/Firefox-size-repro`
  subtree and the separately recorded Temp Firefox fixture. Its exact private
  path is retained in `PROJECT_MEMORY.md`; do not retry through another tool.
- A later audit can find more defects. A passing covered scan is not a promise
  of zero bugs. When a defect is repaired, rerun its reproduction, affected
  regressions, and the relevant scan against one identified candidate.

## Decisions to preserve

- **Clients own their notes.** A combined search/dashboard does not merge
  notebooks. Rename/archive/restore/deletion must target the intended client
  and must not change another client's data. Browser slots remain independent.
- **Keep Notes compact and theme based.** Preserve the full-width editor,
  searchable single client picker, tabs and on-demand inspector. No permanent
  large sidebar, duplicate client selectors, redundant note headings, forced
  accent color, or permanent success footer. Use the chosen layout; expose
  formatting when requested. Status is a quiet indicator edited in Details.
- **Quiet autosave and independent window.** Report save problems and retain
  unsaved drafts. Flush pending edits safely on transitions; respect native
  Undo. Client switching keeps the same Notes window and placement. Notes can
  remain open when the launcher is minimized/closed to the tray; exiting the
  application is a separate lifecycle action.
- **Closing a tab is not deletion.** Saved notes reopen from All notes. Trash
  is recoverable with no automatic expiry; permanent deletion is explicit.
  Pristine auto-named empty drafts may be pruned; renamed, edited or
  metadata-bearing notes are retained. Keep search, sorting and
  multi-select/bulk actions usable.
- **Templates and snippets have real editors.** Templates create starter notes;
  snippets insert reusable blocks into a note. The libraries are shared, while
  inserted or created content belongs to the selected client's notebook.
- **Keep storage limits distinct.** The per-client notebook supports up to
  10,000 notes including archived/deleted entries, with a 256 MiB notebook
  budget; template/snippet libraries have their separate 256-entry limits.
  Old 256-note builds reject larger notebooks. Notes are local and unencrypted;
  do not imply cloud sync or safe downgrade without a compatible backup/export.
- **Preserve link occurrence identity and format compatibility.** A note can
  contain the same link caption with different destinations. Do not collapse
  bindings by visible text when editing, copying, undoing or saving. The parser
  accepts notebook formats 1–5; saves use formats 2–5 as needed for metadata,
  revision links and occurrence-specific links. Older executables may reject
  newer notebooks. Preserve a compatible backup/export before downgrading.
- **Ticket fields/status are local metadata.** They do not fetch or update a
  ticketing service. Code boxes label and display source; they do not execute it.
- **Notes links use a regular PC browser.** Details > Open links in chooses
  System default or an installed supported browser for Notes. This is separate
  from client browser selection and never sends Notes/ticket links into an
  isolated client profile. Keep link-copy context-menu actions discoverable.
- **Browser layout has global and per-client choices.** Global Keep browser's
  choice / Vertical / Horizontal and client Follow global / Vertical /
  Horizontal apply on the next managed launch. Horizontal turns the vertical
  preference off; Keep stops enforcing a preference. Temp follows global;
  the Default editor is excluded.
- **Accepted Edge session limitation.** With Restore tabs off, choose Vertical
  or Horizontal in ctSpaces and open the client: the new session follows that
  choice. Restore tabs on can restore an older window's saved layout. After
  closing the desired new window normally, Restore tabs can request that new
  saved session. Do not promise that old tabs are carried into the Restore-off
  session; bookmark needed tabs first.
- **Restore replaces the active collection.** Backup exclusions do not delete
  clients during export, but an excluded client will be absent from a restored
  collection. Restore keeps the previous collection in a recovery folder.
  Cross-PC browser sign-ins can require authentication again.
- **Avoid build-number churn.** Do not change 6.3.5.0 for routine audits or
  documentation. A future release/version/publication needs the user's request;
  existing release authorization is not permission to replace historical assets.

## Workspace housekeeping, October 23

The measured workspace decreased from **79.938 GiB to 11.065 GiB**, reclaiming
**68.873 GiB net**. Cleanup removed 16,220 compiler/cache files, 667 obsolete
test PDBs, and 1,022 empty directories. All 512 protected hashes and the 667
paired test executable hashes passed verification. The review EXE still matches
the released 6.3.5.0 hash. The measurement preceded this documentation update.

Source, libraries, every test EXE, application PDBs, current/last test checkpoints,
sealed diagnostic symbols, fixtures, source snapshots, reports, release ZIPs,
walkthroughs, backups and selected designs remain. Old test PDBs can regenerate
from the retained projects. They are not required to run the retained EXEs.
The remaining size is mostly historical builds and regression fixtures/evidence;
it is not the size of the application. The released EXE is 6,881,280 bytes.

Cleanup receipts and exact removal lists are under
`build/housekeeping-2026-10-23/`, starting with `summary.json`. Nothing used the
mouse, brought a window forward, launched an app, rebuilt the program or ran a
test during housekeeping. Future cleanup must inspect active workers first,
use a bounded explicit manifest, reject reparse paths and preserve evidence.
Do not repeatedly accumulate compiler caches after completed, sealed QA rounds.

## Where the rest is documented

| Need | Start here |
|---|---|
| All changes since coworkers' 6.1.1.0 | [Cumulative changes](CHANGES_SINCE_6.1.1.0.md), [user changelog](../USER_CHANGELOG.md) |
| Everyday operation and current controls | [User guide](USER_GUIDE.md), [feature reference](FEATURE_REFERENCE.md) |
| Notes workflows and advanced editing | [Notes walkthrough](NOTES_WALKTHROUGH.md), [productivity](NOTES_PRODUCTIVITY.md) |
| Storage, backup/restore, exclusions and privacy | [Data and backups](DATA_BACKUP_AND_PRIVACY.md) |
| Architecture, tests and release process | [Developer guide](DEVELOPER_GUIDE.md), [6.3.5.0 validation](RELEASE_VALIDATION_6.3.5.0.md) |
| Unimplemented accepted ideas | [Roadmap](ROADMAP.md); do not treat acceptance as completed implementation |
| Existing real recordings/narration | [Walkthrough project](../walkthrough/6.3.0-Heart/README.md); local player at `walkthrough/6.3.0-Heart/output/index.html` |
| Private workstation paths and older context | `PROJECT_MEMORY.md`, when present; its archival sections are not current instructions |

The two existing walkthroughs are real app recordings with clickable chapter
seek points. They are historical demonstrations, not a new 6.3.5.0 UI test.
Do not regenerate them unless requested. The project narration worker reuses
Lumen's local Kokoro/Heart assets read-only; do not change shared runtimes/assets,
launch Lumen or overwrite the HomeLab Atlas walkthrough.

## Start the next work session

1. Read this handoff, the relevant current guide, and private memory if present.
2. Check Git branch/status, current version, app paths, hashes and active workers.
   Keep unrelated user files, especially `design/`, intact.
3. Respect the current background-only workflow. Do not take the mouse, bring
   windows forward or start visible capture tools without fresh user permission.
4. For testing, use private fixtures and at most two hidden BelowNormal compiler
   lanes with MSBuild `/m:1`. Never use live client profiles as disposable QA data.
5. Keep candidate identity and coverage boundaries in the report. Preserve failed
   reproductions and final results, and update this document when status changes.
