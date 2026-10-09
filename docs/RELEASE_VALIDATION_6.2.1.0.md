# Release validation: 6.2.1.0

Validated October 9, 2026, using disposable client fixtures. Live client data was not changed.

- Release x64 build passed without compiler errors or warnings.
- Native notes QA passed: compact default, caption drag and corner resize hit tests, persistence of dimensions and maximized state across restart, 100% and 150% DPI, formatted notes, named ticket tabs, autosave, tab-switch and close flushing, undo/redo, large documents, failed-save draft retention and recovery, and two links opening in the same client Edge process.
- Additional checks passed for dark text on yellow highlights after tab reopening and struck-through completed checklist text. Native captures verify the integrated title, fixed-width ticket tabs, grouped toolbar, padded document and footer without action buttons.
- Release gates, guide checks, and Microsoft Defender scan passed on the exact executable below.
- ZIP verified to contain exactly one root ctSpaces.exe whose SHA256 matches the tested executable. All 19 previous ZIP hashes remain unchanged.

Final capture: build/client-productivity-ui/e9bfc9baf4-notes-gothic.png.
Logs: build/notes-polish-build.log, notes-polish-ui.log, notes-polish-release.log, notes-polish-guide.log, notes-polish-defender.log and notes-polish-package.log.

The browser integration check used Edge. Other browser engines and an additional deployment VM were not requalified for this UI patch. The existing active Firefox URL limitation remains. No live app installation was performed.

Executable bytes: 5644288
Executable SHA256: 6F81B53D2E356A96581C0A4B07489AEC904C6E43B606895719601B2D8D7EB4F3
ZIP bytes: 2196627
ZIP SHA256: 82AD09D10DC1D3399E8D7C5C8D8B3D46F30187D0E92433132962BDA6E99710C5

Final visual review completed October 9 in the existing Dark Gothic and Marine captures: integrated title and close X, slimmer fixed-width ticket tabs, grouped toolbar, padded document, readable highlights and checklist completion, and autosave footer without action buttons. No blocking layout issue was found. The tested executable and packaged ZIP retain the hashes above; no rebuild or repeat functional test run was needed. User-facing documentation was aligned with the final controls.
