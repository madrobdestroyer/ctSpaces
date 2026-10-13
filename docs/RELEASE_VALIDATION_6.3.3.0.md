# Release validation: 6.3.3.0

Validated October 13, 2026. This patch consolidates the Notes quick actions into
one compact row and gives them the Notes toolbar's themed surfaces. It does not
change notebook storage, browser routing, autosave or client switching.

- The focused native run passed 348 checks:
  `build/notes-action-row/native-results-final.log`. It verifies every action
  fits in one separated row at 900, 1240 and 1500 DIP widths, including a return
  to the minimum width, at 100%, 150% and 200% layout scaling. All status and
  Link/Open ticket captions fit their real control fonts. The heading, problem
  indicator, ticket/tag context, formatting and expanded search remain separate;
  the editor keeps its full width, handle and draft text.
- Existing action, ticket/code, resize-paint, tab-overflow and text-wrapping
  checks are included in that run. Incremental resizing reflows paragraphs
  without changing their contents. The resource order follows the new action
  row for keyboard navigation.
- Native visual captures use the production Notes dialog, controls and built-in
  Gothic and Marine palettes at the host's 125% DPI, at minimum and wider window
  sizes. The wider Gothic capture includes the Templates hover state. Review
  confirmed a single compact row, readable captions and context, subtle borders,
  intact theme colors and a full-width editor. Captures are
  `build/notes-action-row/gothic-compact.png`, `gothic-wide-hover.png` and
  `marine-compact.png`; the capture receipt is `capture.log` in that directory.
- The desktop Computer Use helper failed twice with
  `foreground window did not report a process id`. The visual captures therefore
  come from the native test executable compiling the production UI, rather than
  a successful desktop automation session against the release executable.
  This limitation does not represent a passed live click-through review.
- The Release x64 build completed without compiler warnings or errors:
  `build/notes-action-row/release-build.log`. Release gates passed in
  `release-gates.log`. Defender reported no threats in the final executable:
  `defender-final.log`. Its first scan attempt used forward slashes and failed
  with `0x80508023`; retrying the same file with a Windows path succeeded.
- Packaging verified exactly one root `ctSpaces.exe`, identical to the release
  executable: `build/notes-action-row/package.log`. All 25 previous root release
  ZIP hashes match `previous-package-hashes.json` in that directory.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ctSpaces.exe | 6367232 | FF1D1B8564CC161C103EEBAB90BB5E617FA555B0C9FAE283337CBA17C5BA6798 |
| ctSpaces6.3.3.0.zip | 2629918 | 2B49F684C5EEBA3CA897D39845816B1129580E8C30B32DC2BE752F9D794A9B13 |

The completed 660-check Notes regression run and same-window client-switching
review remain documented in [6.3.2.0 validation](RELEASE_VALIDATION_6.3.2.0.md).
The 12 core and 16 integration suites from the
[6.3.1.0 whole-app review](FULL_APP_STRESS_REVIEW_6.3.1.0.md) were not repeated for
this layout patch. Tests used disposable clients. The user's installed app,
real notebooks and existing walkthrough recordings remain untouched.
