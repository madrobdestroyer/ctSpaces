# Release validation: 6.3.0.0

Validated October 9, 2026 with isolated fixtures under `build`. No live client data or installed application was modified.

- Release x64 build passed without compiler warnings or errors. Build log: `build/notes-630-build.log`.
- `tests/Test-NotesWorkspace.ps1` passed 79 assertions using the production notes implementation and actual native RichEdit/tab controls. Coverage includes immediate new notes; close/reopen; preserved per-tab undo; duplicate/pin/reorder; archive/trash/restore/permanent-delete confirmation; zero-open-tab restart; find/replace and grouped undo; labeled links; zoom without formatting changes; checklist continuation; autosave; conflicting writes and preserved recovery snapshots; text and metadata-only conflict copies; crash recovery; history preview/restore; imports/exports; cross-client copy/move; theme colors; resize stability; hidden footer; and guards against destroying notes during a nested modal or unresolved save. Confirmation, input and file-picker boundaries are controlled by the harness. Log: `build/notes-630-native.log`.
- `tests/Test-ClientNotes.ps1` passed storage, atomic replacement, unsafe path, Unicode, rich-text validation and migration tests. New checks cover all truncations of a v2 sample, v1 migration, zero notes, lifecycle/view/ticket/link metadata, duplicate IDs, bounded history, and safe recovery-file replacement. Log: `build/notes-630-storage.log`.
- `tests/Test-GuidedWalkthrough.ps1` passed with the revised notes announcement. Log: `build/notes-630-guide.log`.
- `tests/Test-Release.ps1` passed on the final executable. The first-party ASCII gate now includes `.inl` sources. Log: `build/notes-630-release.log`.
- Microsoft Defender reported no threats in the final executable. Log: `build/notes-630-defender.log`.
- Live Windows review used the Computer Use skill against a copied QA executable with an explicit isolated data directory. Dark Gothic notes were inspected with real mouse/keyboard input: add without naming, close/reopen with Ctrl+Shift+T, undo after reopening, highlighter dropdown with swatches/current selection, checklist completion, corner resize, maximize/restore, saved dimensions on reopening, library content search, and readable previews. The library was corrected during review to use theme colors and a display-fitting responsive layout. Captures: `build/notes-630-gothic.png`, `build/notes-630-library.png`, and `build/notes-630-search.png`. The final metadata-only conflict correction was additionally covered by native assertions; it did not change the reviewed layout.
- ZIP verification confirms exactly one root `ctSpaces.exe`, matching the final executable hash. All 22 prior release ZIP hashes remain unchanged. Log: `build/notes-630-package.log`; baseline: `build/notes-630-prior-packages.json`.

The live visual pass used the current display at 100% scaling. Light/dark text switching and repeated resizing were also checked in the native suite. A second physical DPI environment, browser-engine launching, installer deployment, and unrelated browser workflows were not requalified for this release. The historical `Test-ClientProductivityUi.ps1` notes path targets the pre-6.3 single-editor/v1 notebook and was not used as the 6.3 acceptance gate; the new native suite plus live review cover the changed workspace. Existing active-Firefox URL limitations remain.

Notebook format version 2 is readable by 6.3 and later; older releases reject it. Complete notebook exports retain metadata and revisions; TXT/RTF exports do not include separate ticket/link metadata. Revision history is bounded and sampled, not a keystroke log. Deferred features are listed in `docs/notes-roadmap.json`.

Executable bytes: 5727232
Executable SHA256: 1B564221CF1027F56048F1181B0778CC1D5F12816AEC2AFB041E19AD6C712959
ZIP SHA256: 12527A5E45F4A27315458170888D484AEE62998F424C228964B857546C7EDA16
ZIP bytes: 2238823
