# Compact Notes dialogs and direct Trash review

October 12, 2026. Local review of ctSpaces **6.3.0.0** in
`C:\Projects\ctSpaces`, branch `cpp`. Continues the previous template review;
no version bump, publication, installation, commit, or new branch.

Status and tags now uses a smaller dialog, compact dropdown rows, a shorter
single-line tags field, smaller Apply/Cancel buttons, and concise labels. Its
resource size changes from 380 by 174 to 300 by 130 dialog units; the text font
remains unchanged. Validation, status choices, tag limits, and save behavior are
unchanged.

The quick Help overview changes from 560 by 328 to 460 by 218 dialog units. It
retains six short topics with less empty space and compact navigation buttons.
Its ticket guidance now says default browser and local status, and its template
guidance explains writing/editing starters directly in Templates.

**Trash** is a direct button beside All notes, a quick Help button, and a
searchable action available with no active note. All enter the existing library
with the deleted-note filter selected immediately. The library caption and
filter say Trash; the former Recently deleted wording is replaced in active
Notes UI, exported handovers, walkthrough text, and user documentation. Switching
back to another filter restores the All notes caption.

Trash remains recoverable indefinitely. Restore, selected permanent deletion,
and Empty trash retain their existing confirmation and persistence paths. No
storage format, retention rule, or deletion semantics changed.

## Validation

- Release x64 build passed without compiler warnings or errors.
- **356 native production-dialog checks passed**. Added checks cover direct Trash
  routing, initial filter/caption, exclusion of kept notes, Empty trash
  visibility, quick Help navigation and corrected guidance, searchable access
  without an active note, and top-button separation at 900/1240/1500 widths.
  Existing save, restore, permanent deletion, bulk trash, template, editor
  transition, and theme checks remain in the suite.
- Computer Use reviewed the isolated app's compact Status dialog and all five
  dropdown choices, compact Help, Help-to-Trash navigation, and top Trash button.
  The exact final executable was reviewed for the tightened Help layout and
  direct Trash navigation. The unchanged status layout was captured before the
  final Help-only resource adjustment. No live client notes were deleted.
- Guided walkthrough source contract passed. Final release gates and Microsoft
  Defender passed on the final executable. Package verification confirmed one
  root `ctSpaces.exe` with an identical extracted executable SHA256.
- The published ZIP and prior template-editor review ZIP retain their hashes.
  Storage tests were not repeated because storage and retention were unchanged.

Actual captures: `build/notes-compact-status.jpg`,
`build/notes-compact-help.jpg`, `build/notes-trash-button.jpg`, and
`build/notes-direct-trash.jpg`. Logs use the `build/notes-compact-trash-` prefix:
`build.log`, `native.log`, `guide.log`, `release.log`, and `defender.log`.
Machine-readable evidence: `build/notes-compact-trash-result.json`.

Local package: `build/notes-compact-trash-review/ctSpaces6.3.0.0.zip`.

- EXE SHA256: `2C2CF71A0DD99DE3D6956368ACCDA6B9D87E132B253E34C9AE317F259CD6291F`
- ZIP SHA256: `09F1AF342A52307D5AE3B15A0327BAE3100B8013B5E1DAAE347E467DBD2C2C9C`
- Published ZIP, unchanged: `12527A5E45F4A27315458170888D484AEE62998F424C228964B857546C7EDA16`
- Previous template-editor ZIP, unchanged: `F98195434916FCB4CBAB433797175F5190ED9E764A6FE08883C64EC20C21B129`

This review does not add frame-rate or multi-monitor DPI qualification. Earlier
chat request-preparation failures remain unresolved.
