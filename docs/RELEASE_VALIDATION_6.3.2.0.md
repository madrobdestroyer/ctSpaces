# Release validation: 6.3.2.0

Validated October 13, 2026. This patch addresses the Notes client selector,
routine autosave messages and client switching without changing the approved
Inspector layout. The existing walkthrough videos remain unchanged.

- The focused native review passed 129 checks:
  `build/notes-header-polish/native-results-reuse.log`. It covers left-aligned
  short/long/ampersand names, ellipsis and chevron separation at three widths,
  normal/hover/focus/disabled drawing, bounded name-based sizing, actual notebook
  writes with no Saved/Saving label, invalid details and a real write conflict.
- Twelve client switches preserve the exact window and header-control handles,
  position and dimensions, while loading only the selected client's contents.
  Maximized, minimized-to-maximized and restored states remain correct. Opening
  the current client again also preserves its maximized state. A failed save
  blocks the switch and leaves the original window, notebook and draft intact.
- The complete native Notes suite passed 660 checks:
  `build/notes-header-polish/notes-regressions.log`. This was rerun because client
  switching now exchanges notebook state inside the existing window, affecting
  cached editors and notebook loading. The 12 core and 16 integration suites
  completed for 6.3.1.0 were not repeated; their evidence and limits remain in
  [the full-app stress review](FULL_APP_STRESS_REVIEW_6.3.1.0.md).
- The final Release x64 build completed without compiler warnings or errors:
  `build/notes-header-polish/release-build-reuse.log`.
- The final executable passed the release gates:
  `build/notes-header-polish/release-gates-reuse.log`.
- Defender found no threats in the final executable:
  `build/notes-header-polish/defender-reuse.log`.
- The actual final executable was launched in an isolated portable fixture and
  opened through the launcher Notes button. Visual review confirmed that the
  short `rob` selector is compact, with the name at the left and a separate
  chevron at the right, and that the normal save-status area is empty. The
  fixture executable SHA-256 matches the release executable; its receipt is
  `build/notes-header-polish/ui-fixture.json`. This visual check used the desktop's
  current DPI and Gothic theme; it does not claim live coverage of every display
  or theme combination. Native drawing checks use the production theme colors.
- Packaging verified exactly one root `ctSpaces.exe`, identical to the release
  executable: `build/notes-header-polish/package.log`. All prior root release ZIP
  hashes still match `build/notes-header-polish/previous-package-hashes.json`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ctSpaces.exe | 6367232 | C8A811EA699B2CF0A6A24AD9BF53264F8319EE2C6DF7D8A0F47C94A2A6F434BC |
| ctSpaces6.3.2.0.zip | 2629924 | 08757F3CD9CDA213DDB7B86A5E80F08D49E7E3E110C7C492CEDE765E8B5B8DAF |

This batch uses one patch version, 6.3.2.0, because 6.3.1.0 was already published.
The selector, autosave and window-reuse iterations shared that version. Tests
used disposable client data; the user's real installation and client profiles
were not changed.
