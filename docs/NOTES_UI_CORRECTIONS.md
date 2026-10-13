# Notes frame, overflow and resize corrections

Date: 2026-10-09. Local review build, Windows file/product version **6.3.0.0**.
These corrections are not published to GitHub and do not replace the previously
published ZIP or the installed application.

## Changes

- The native tab overflow child now paints with the selected notes theme. Its
  two chevrons have hover and disabled states and retain native scrolling and
  accessibility. Navigation occupies the full tab row; labels are clipped before
  the buttons. Overflow appears and disappears as the available width changes.
- Notes layout no longer synchronously repaints the whole window and every child
  control on each resize step. Position changes queue normal Windows paints.
- Removed whole-window `WS_EX_COMPOSITED`; the notes frame, tabs and navigation
  each paint into a local buffer. This preserves native RichEdit painting without
  repeatedly compositing hidden cached editors. The custom frame now preserves
  existing client pixels during resizing; child positioning also preserves pixels
  instead of discarding them with `SWP_NOCOPYBITS`.
- Reproduced the thick white outer border by clicking the launcher while notes
  was open. The installed executable matched the previous review build exactly;
  this was not a stale installation. Notes now handles `WM_NCPAINT` and
  `WM_NCACTIVATE` without passing them to the default native frame painter.
  DWM remains enabled, and `WS_THICKFRAME` remains present so actual corner
  resizing still works. Setting only the DWM border color, or delegating
  activation to `DefWindowProc` with `lParam=-1`, did not fix the live reproduction.
- Every newly created and cached RichEdit receives the selected window theme, so
  switching notes does not introduce a white scrollbar in a dark theme.
- Version policy is recorded in `DEVELOPER_GUIDE.md`: correction and test
  iterations do not automatically bump versions or publish releases.

## Validation

- Release x64 production build: zero warnings/errors. Log:
  `build/notes-630-frame-build.log`.
- `tests/Test-NotesWorkspace.ps1`: **102 checks passed**. Added checks
  cover themed overflow creation, dark pixels, full-row alignment, both scroll
  directions, live light/dark switching, overflow visibility at wide/narrow sizes,
  and 30 consecutive resize steps. Resize observers verified zero synchronous
  `WM_PAINT` or `WM_DRAWITEM` calls and zero RichEdit stream operations inside
  those layout steps; formatted content and active note were preserved. New
  frame checks cover DWM, blocked native frame painting/activation, preserved
  client pixels, retained native resize style, corner/caption hit tests, and the
  clickable close button. Log: `build/notes-630-frame-native.log`.
- `tests/Test-Release.ps1`: passed against the final executable. Log:
  `build/notes-630-frame-release.log`.
- Microsoft Defender custom scan of the exact executable: no threats. Log:
  `build/notes-630-frame-defender.log`.
- Computer Use review used an isolated QA executable/data directory. Reproduced
  the original white overflow arrows, exercised both corrected arrow buttons,
  switched away from and back to a long formatted note, and verified the themed
  scrollbar. This follow-up reproduced the outer white border on deactivation,
  then verified the corrected inactive frame at 1600x750 and 900x540. Actual
  corner drags changed 1600x750 to 1100x644 and then 900x540; maximize/restore
  also succeeded. The screenshots confirm resulting layouts;
  this is not a continuous high-frame-rate flicker measurement or a second-DPI
  qualification. No live client data was used.
- Previous storage/migration and unrelated browser suites were not repeated:
  notebook storage and browser behavior were unchanged.

Visual artifacts: `build/notes-630-white-frame-before.png`,
`build/notes-630-frame-inactive.png`,
`build/notes-630-frame-compact-inactive.png`, and
`build/notes-630-frame-restored-inactive.png`.

The earlier review captured active notes and missed the inactive border. Static
screenshots and paint-message checks do not establish continuous drag smoothness;
the change removes identified redraw causes, but a high-frame-rate recording of
the user's drag has not been obtained. The earlier request-preparation failures
remain unrelated and unresolved; this work does not claim to diagnose them.

## Review package

`build/notes-frame-review/ctSpaces6.3.0.0.zip` contains exactly one root
`ctSpaces.exe`, verified by `tools/New-ReleasePackages.ps1`. It is a local
review artifact with the same version as the existing release, identified by its
separate path and hashes:

- EXE SHA256: `75221C9B5A688BD2A510FA4B9547B7D145F84884E3A37B19AE819CF936C26014`
- ZIP SHA256: `39237D7FD05E5CB211E5D0C141029968DC57C13AD372ECD1B795B0FB8BA2B914`

Package log: `build/notes-630-frame-package.log`. Machine-readable result:
`build/notes-630-frame-result.json`. The earlier review ZIP in
`build/notes-ui-review` is preserved for comparison.

The published ZIP remains unchanged at
`dist/x64/Release/ctSpaces6.3.0.0.zip`, SHA256
`12527A5E45F4A27315458170888D484AEE62998F424C228964B857546C7EDA16`.
