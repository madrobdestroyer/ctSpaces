# ctSpaces 5.3.0.8 release review

> Superseded by the subsequent adversarial test pass. That pass reproduced a
> locked-file deletion failure that could remove ownership markers and prevent
> a normal retry. The original approval below is historical, not the current
> release recommendation. Use the newer release evidence and package.

Review date: September 21, 2026.

For the current release, see [5.3.0.10 validation](RELEASE_VALIDATION_5.3.0.10.md).
Historical artifact paths below describe the original review layout; local
release ZIPs now live beside the executable in `dist/x64/Release`.

## Decision

Suitable for an internal colleague rollout using the tested Edge and Chrome
workflows. The bounded source review and current runtime checks found no
confirmed release-blocking product defect. Retain 5.3.0.8: this review made
documentation changes, not a new executable change.

Astra reviewed architecture and data-safety decisions. Sol reviewed the recent
UI and lifecycle code and updated release documentation. Luna ran component
checks and drafted the colleague guide. The main task reviewed their results,
ran the browser/UI checks, and verified the distribution artifacts.

## Application breakdown

| Area | Behavior reviewed | Evidence |
|---|---|---|
| Client spaces | Separate persistent profiles for each client/browser pair; one client can run different browsers at once | Simultaneous Edge/Chrome processes, distinct exact profile paths and isolated sentinel files |
| Starter and temporary data | New Chromium profiles use a sanitized starter; Firefox has its own creation path; Default changes require saving | Both sanitizer suites, archive privacy gates and targeted source review |
| Launch and identity | Client-only tabs/shortcut names; browser-qualified internal identities and configurable window-title order | Workflow and live multi-browser title tests |
| Desktop shortcuts and icons | Managed links preserve ownership, rename correctly, and keep browser taskbar identity separate from the launcher | Workflow, handle-bound rename and live shortcut/favicon tests |
| Session lifecycle | Restore-tabs choice per browser slot; closing a browser releases its profile | Toggle persistence, two close/reopen cycles, coordinated shutdown |
| Cleanup | Three UTC calendar months, whole-client deletion, manual multi-select, acknowledgement, protection for open/unverifiable clients | Native activity tests and disposable full cleanup workflows |
| Backup and restore | Validated archives, bounded extraction, corruption rejection and preserved recovery copy | Compiled archive/restore suite and external-profile-use blocking test |
| Interface | About, text menus, results and controls follow the theme; keyboard cancellation remains usable | Rendered cleanup/UI checks, Auto-fetch layout at 150% scale, theme stress test |

Profile separation is not an operating-system security sandbox or encryption.
Deleting a client removes its live container and verified managed shortcuts;
separately exported backups and `Sites_PreRestore_...` recovery copies remain.

## Current verification

- Release x64 build: passed, no reported warnings or errors; executable hash
  stayed identical to the packaged 5.3.0.8 release.
- Release gates, configuration persistence, shortcut-name boundaries, client
  activity, Preferences JSON, directory sanitizer, archive sanitizer, archive
  round trip, rename/shortcut transaction guards, and handle-bound rename:
  passed. Source guards are distinguished from executable tests in their
  scripts; they are not substitutes for the runtime checks below.
- Workflow: passed pin order, same-path shortcut browser changes, unrelated
  shortcut preservation, case-only and full rename, archive/restore and saved
  preferences.
- Same-client Edge + Chrome: passed coexistence, exact distinct profile paths,
  title-order change, slot preservation, and normal browser/launcher shutdown.
- Restore tabs: passed default/on/off persistence. Browser-close integration
  restored both local tabs after two close/reopen cycles in Edge.
- External browser use: passed backup/archive blocking while a profile was in
  use and permitted the operations after that process exited.
- Live icon: launched through a real disposable client shortcut, confirmed
  distinct launcher/browser AppIDs, and updated the browser icon through an
  actual favicon fetch before its success dialog was dismissed.
- Auto-fetch layout: passed field sizing, themed buttons and Escape at 144 DPI.
- Theme responsiveness: passed 240 rapid changes, with GDI object growth of 7
  within the test's limit of 32.
- Coordinated browser shutdown: passed with Edge.
- Full cleanup workflow: Gothic passed current activity, selection, Cancel,
  acknowledgement, active-client protection, metadata/shortcut cleanup,
  read-only/editable text menus and rendered theme checks. Marine passed the
  same complete workflow on the final rerun.
- Microsoft Defender custom scan of the exact executable: no threats found,
  exit code 0. Signature version 1.459.311.0, updated September 20, 2026.

## Limits and follow-ups

- Firefox and Brave were not installed on this machine. Their paths were
  source-reviewed, but simultaneous Chrome + Firefox and their browser-specific
  lifecycle were not exercised. Run a small pilot on those browsers before
  presenting them as verified by this review.
- Authenticode status is `NotSigned`, the previously accepted internal-release
  choice. Organization policy may require a signed executable.
- Installation/update code was reviewed and staged-copy invariants are gated;
  this pass did not replace the installed app or run a fresh-machine installer
  test. Test the colleague installation on a pilot machine.
- Two low-priority source observations remain: cleanup dialog-creation failure
  could discard a summary instead of displaying a fallback message; secondary
  dialog/menu fonts can inherit the main window's DPI on mixed-scale monitors.
  Neither was reproduced as an everyday failure. No speculative code changes
  were included at release time.
- The first Edge/Chrome test used Windows PowerShell 5.1 and failed while
  deleting a long-path test artifact. The complete PowerShell 7 run passed.
  The first Marine cleanup run stopped on its exact candidate-count assertion;
  that attempt is not counted as a pass. A complete Marine rerun passed; the
  initial count difference was not reproduced or conclusively diagnosed.
- No Git checkout metadata is present at the project root. A separate
  allowlisted source snapshot accompanies this handoff instead of a release
  commit/tag. Build outputs, real profiles, extracted historical templates,
  and private workspace backups are excluded from that source snapshot.

## Executable identity

- File/product version: 5.3.0.8; display version: 5.3.
- Executable size: 5,456,384 bytes.
- SHA-256: `3D24D6B21865E012572CDFC15198DF97327BEC19648726A9374247F6EDC98F97`.
- Original application-only ZIP SHA-256:
  `AD2790BA98D82C6156F48F1CAE46BA1C6FE4271B9ED75A72E8A5267545C2DE70`.

The colleague ZIP adds the quick-start guide, user documentation and license
notices to that same executable. Keep the maintainer source ZIP separate from
the colleague download. No real client profile or private workspace backup is
part of either package.
