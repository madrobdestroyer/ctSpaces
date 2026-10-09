# Release validation: 6.2.2.0

Validated October 9, 2026, with disposable client fixtures. No live installation or client data changes were made.

- Release x64 build passed without warnings or errors.
- Visual review used the user's corrected reference image. Native captures cover shaded tabs and dropdowns, continuous window backgrounds, refined icons, checklist outlines, document padding, and footer status without action buttons. System Dark retains its blue accent; Gothic retains its rose accent; Marine retains its light palette.
- Final native notes QA passed: compact default, caption drag and corner resizing, minimum toolbar width, dimension and maximized-state persistence, named tabs, formatting, autosave, tab-switch/close flushing, undo/redo, large documents, failed-save draft retention/recovery, and repeated note links in the same isolated Edge process.
- A new reopen check caught RichEdit importing its own wide-space RTF token as an ordinary space. The correction preserves explicit Unicode spacing on export and handles earlier saved tokens on import. Native round-trip units and the final UI test pass. Reopened headings, body text, and links retain Segoe UI.
- Final visual checks include the same populated document in Gothic and System Dark, and an empty System Dark note. Final functional and visual runs observed 100% DPI. Earlier rendering iterations also exercised 150% DPI; the final serialization correction was requalified at 100% only because the available displays changed.
- Release gates and Microsoft Defender passed on the executable below.
- Packaging verified one root ctSpaces.exe matching the tested executable. All 20 historical ZIP hashes remain unchanged.

Final visual captures: build/client-productivity-ui/6cbd52fdc2-notes-gothic.png, 6cbd52fdc2-notes-default-dark.png, 6cbd52fdc2-notes-empty-dark.png, and 6cbd52fdc2-notes-marine.png.

Evidence: build/notes-reference-build.log, notes-reference-unit.log, notes-reference-ui.log, notes-reference-visual.log, notes-reference-release.log, notes-reference-defender.log, and notes-reference-package.log.

Other browser engines, a separate deployment VM, and unrelated launcher workflows were not requalified for this correction. The existing active Firefox URL limitation remains. Native rich-text highlights retain their rectangular rendering; colors follow the selected theme and existing document font sizes are preserved.

Executable bytes: 5646848
Executable SHA256: B70693DD6B5E62B24B06F5AAF42CD4FD45C05C70AA4F3BC3A21C3534FEC79AEE
ZIP bytes: 2198331
ZIP SHA256: 52BEDC8AA7DAB3BBB73C25795FD4E82DE96D9AED6C6BF01AE3BC98C7849C5004
