# ctSpaces 6.1.0.0 validation

Date: October 8, 2026.

## Scope

This release implements the three requested client productivity features:
substring filtering without autocomplete replacement, local Client Notes,
and Close All Clients while retaining the launcher. Other roadmap features
remain future work.

The notes file is bounded to 128 KiB of UTF8 text at the client root.
Unreadable files, invalid Unicode, embedded NUL, directory and reparse
targets, hard links, and failed saves must not erase previously saved notes.
The editor keeps a draft after a failed save. Saved notes belong to the
whole client, not a browser slot.

Close All Clients requests ordinary browser closure rather than silently
terminating refused closes. The existing coordinated launcher exit remains
a separate workflow. Filtering uses a complete cached list rather than
enumerating client folders on each keystroke. Archived clients remain hidden.

## Unit and archive checks

`Test-ClientNotes.ps1` passed Unicode and mixed newlines, empty notes,
stale snapshot detection, bounded length, invalid Unicode and embedded NUL,
failed path revalidation, locked replacement preserving prior content,
unreadable file rejection, unsafe file and root types, rename, recursive
copy, and deletion fixtures.

`Test-ArchiveRoundTrip.ps1` passed validated backup creation, staging,
transactional restore, corruption, and existing path safety checks. A new
UTF8 client notes fixture is verified byte for byte through actual validated
7z backup, restore staging, and live collection commit.

`Test-GuidedWalkthrough.ps1` passed fifteen topic coverage, eleven accumulated
announcements, independent read state, first launch behavior, and revision
handling. A user who read all previous topics receives exactly the three
new announcements. Existing read revisions are not reset.

`Test-ClientActivity.ps1` and `Test-ConfigPersistence.ps1` passed their
existing calendar, persistence, corruption, and safety regressions.

## Release evidence

The Release x64 build completed without warnings or errors. Release checks
passed for Windows file and product version `6.1.0.0` and display version
`6.1`. Bundled 7 Zip remains 26.02. Windows Defender reported no threats
in the current candidate executable.

Executable size: 5,595,136 bytes.

Executable SHA256:
`2D2D764CC13435CAD86EFA02A1B2C7538A57EEEEEC05DB4FCE123ED6D809534A`.

Focused review identified and corrected stale dropdown navigation after
dismissal and the target snapshot interval around Close All confirmation.
The first native UI run also reproduced an Enter/F4 selection interference
path, which was fixed before packaging. Notes lifecycle messages are deferred
while its modal editor is open so a Default save prompt cannot cover it.
Closing the editor restores controls and processes the deferred lifecycle
work. Ordinary Windows shutdown is refused while notes editing is active.

## Native UI and workflow checks

`Test-ClientProductivityUi.ps1` passed on the final executable. It checks
two visible substring results, case insensitive matching, preserved typed
text during browsing and dismissal, explicit selection, and new client
creation through F4 and Enter. Notes discard, save, and reopen passed in
Marine and Dark Gothic. Close All was tested with zero, one, and two real
isolated Edge sessions, including cancellation that preserves the session.
The launcher remained open, saved notes were unchanged by browser closure,
and live configuration was untouched.

The productivity, guide, and tour UI scripts were run with PowerShell 7.
The notes, workflow, cleanup, archive, and unit checks also use their
supported Windows PowerShell hosts where applicable.

`Test-WorkflowFeatures.ps1` passed pinned ordering, managed shortcuts,
independent browser slots, case only and normal rename, archive and restore,
and exact whole client deletion in disposable data. New notes assertions
verify preservation through both rename forms and archive, followed by
removal through the app's actual Delete Profile command. Live configuration
and Sites were unchanged.

`Test-InactiveClientCleanup.ps1` passed whole client deletion, multi selection,
archived inclusion, active/reopened client protection, activity history and
unsafe record checks, Default/Temp protection, managed shortcut cleanup,
metadata pruning, cancellation, unselected client preservation, themed
messages and About, compact empty results, and simplified text menus.

`Test-GuidedWalkthroughUi.ps1` passed final executable checks for welcome
deferral, skip/replay, dedicated announcements, independently acknowledged
topics, future revision preservation, theme changes, locked configuration,
and external launch handoff. Actual monitor DPI values were 96 and 144.

Notes captures in Marine and Dark Gothic and the compact Close All layout
were visually reviewed. The tests use copied applications and isolated data,
not the installed application or real client profiles.

`Test-QuickTourUi.ps1` was rerun on the final executable and passed all 23
distinct steps and 13 inert illustrations, including the real Close All
anchor. Marine and Dark Gothic were exercised at actual 96 and 144 DPI.
Guide handoff, empty and populated pin/session guidance, overlay lifetime,
minimize/close cleanup, external launch handoff, and preserved configuration
and client state passed. The final notes illustration was visually reviewed.

The initial UI harness needed corrections for edit modification semantics,
capture DPI/timing, inadvertent client launch during a selection test, and
the existing legacy path budget in a long temporary fixture directory.
These harness corrections did not change production behavior. The separate
F4/Enter issue described above was a production fix and was rerun successfully.

Real browser checks in this feature pass used Edge. Other browser families
were not independently requalified here. Refusal handling is bounded and
does not contain forced termination, but an unsaved page refusal was not
artificially induced in the browser UI.

## Runtime package

`ctSpaces6.1.0.0.zip` is 2,157,831 bytes and contains exactly one root entry,
`ctSpaces.exe`. Packaging reread and verified the inner executable hash.
No guide, license, changelog, checksum sidecar, or source file is in this ZIP.
Historical 6.0 ZIP hashes were checked and remain unchanged.

ZIP SHA256:
`D96282E7A4A46135DB163F31ECE2491EF7C378E386801658DF8B4F16DE426E6C`.
