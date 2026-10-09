# ctSpaces 6.2.0.0 validation

Date: October 9, 2026.

## Scope

Client Notes now has formatted documents, user-named ticket tabs, autosave,
and links that open in the note client's selected browser space. Existing
notes migrate into the initial Notes tab without changing the legacy files.
The approved layout uses a client heading, readable tabs, a grouped icon
toolbar with hover hints, and a padded editor. The initial window is
960 by 640 logical pixels. Resizing and maximized state are persisted in
the existing atomic configuration store and fitted to the current display.

## Validation performed

- Release x64 build completed without warnings or errors.
- `tests/Test-ClientNotes.ps1` passed storage and native RichEdit tests:
  Unicode and newline preservation, legacy TXT/RTF migration, formatted
  notebook roundtrips, independent tabs, name validation, size boundaries,
  malformed data, unsafe paths and hard links, atomic save failure, stale
  snapshot detection, and native URL export normalization.
- `tests/Test-ClientProductivityUi.ps1` passed against the final executable
  using disposable clients. It exercised text and formatting autosave,
  immediate close, named tabs, Unicode rename, duplicate names, switching
  and reopening, all formatting controls, undo/redo, a document exceeding
  1 MiB of RTF, and draft retention/recovery while the file was locked.
- The same native UI run verified compact initial dimensions, larger and
  smaller resizing, toolbar fit at the minimum width, saved dimensions on
  reopen and after restarting ctSpaces, and maximized/restore persistence.
  Formatting remained intact when moving between 96 and 144 DPI displays.
  Marine and Dark Gothic window captures were inspected.
- Actual clicks on two local HTTP links opened the note client's isolated
  Edge profile. The second request reached the local server through the
  same browser process; another client was not opened and notes stayed open.
- The full UI run also passed existing substring filtering, explicit
  selection, creation of a new similarly named client, and Close All with
  zero, one, and multiple Edge sessions. Cancellation preserved the session.
- `tests/Test-WorkflowFeatures.ps1` passed on the final executable. Legacy
  notes and formatted notebooks survived case-only and ordinary rename,
  archive and restore. Whole-client deletion removed them with the client.
- `tests/Test-ArchiveRoundTrip.ps1` passed the real in-process archive,
  validated backup, corruption rejection, and restore tests with a two-tab
  formatted notebook fixture.
- `tests/Test-GuidedWalkthrough.ps1` passed catalog, revision announcement,
  pending state, fresh-data detection, and non-downgrade tests.
- `tests/Test-Release.ps1` passed for Windows version 6.2.0.0 and display
  version 6.2. The bundled Default.7z hash remains
  `54A0FE4B18C681353852DD86363306AC5E475784111FD76C949E829DC8464EA5`.
- Windows Defender scanned the final executable and reported no threats.

The final native UI run was `e9aac89d3d`; local evidence is retained in the
ignored `build/notes-*.log` files and `build/client-productivity-ui` captures.
All GUI and lifecycle checks used isolated QA data. Live application
configuration and client folders were not changed.

## Boundaries

Actual browser link and close tests used Edge. Chrome, Brave, and Firefox
were not newly requalified. An already-running isolated Firefox session
cannot receive a command-line URL; ctSpaces reports that existing browser
boundary. Closed Firefox clients can use the normal launch-with-URL path.
No artificial browser unsaved-page refusal was induced. The broader guide
UI suite was not repeated for the notes text update.

Autosave uses a 250 ms typing debounce, with immediate flush on tab changes
and close. Failure preserves the visible draft and reports the failure.
Notes remain bounded to 32 MiB text and 64 MiB RTF per tab, 256 tabs, and
256 MiB per notebook. Incoming RTF does not accept embedded objects,
pictures, or external fields; paste inserts plain text.

## Distribution

`tools/New-ReleasePackages.ps1 -Version 6.2.0.0` verified that
`dist/x64/Release/ctSpaces6.2.0.0.zip` contains exactly one root entry,
`ctSpaces.exe`, with the same hash as the tested executable. All 18 prior
release ZIP hashes were unchanged. Documentation stays in the repository.

Executable: 5,635,584 bytes. SHA256:
`BD0CE99AFDAC9EE30EB7AA23659934BA5FD3A2707640F7F06ED6E8FCBD5C8C6E`.

ZIP SHA256:
`E4EA223AC2A94FEA6744E271909CA71708434E9ED91A6FDAA6734CCFB1E289F4`.
