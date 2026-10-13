# Launcher and Notes links review — 6.3.0.0

Validated October 12, 2026 in `C:\Projects\ctSpaces`, branch `cpp`.
This continues the local Notes review without a version bump, installation,
GitHub publication, commit, or new branch. Earlier review packages are preserved.

## Resulting behavior

- A normal visible start raises the launcher. Launching the same executable
  again restores an existing launcher, including one hidden in the tray, and
  brings its active popup forward. Explicit hidden/minimized startup requests
  retain their requested state. The app does not become always on top.
- **Gear > Minimize to tray** is an optional, remembered setting, off by default.
  Minimizing registers the ctSpaces notification icon before hiding the launcher.
  Click or keyboard-select the icon to restore; its right-click menu offers
  Show ctSpaces and Exit ctSpaces. Close still uses the existing exit path.
  Failed registration/version negotiation leaves normal taskbar access. After
  Explorer recreation, a hidden launcher re-registers its icon or restores itself.
- Untouched, pristine, automatically named blank drafts are discarded when a
  tab or the Notes window closes. Renamed notes, written content, and notes with
  metadata remain saved. New drafts use the lowest available Untitled name;
  saved meaningful Untitled notes continue to reserve their names. Existing
  non-pristine saved notes are not automatically deleted.
- RichEdit right-click now reliably opens the themed Undo, Redo, Cut, Copy,
  Paste and Select all menu. Selection, available clipboard text and editability
  determine which commands are enabled. Keyboard context-menu access remains.
- **Every Notes HTTP/HTTPS link** uses Windows' default browser with no client
  profile arguments: saved tickets, pasted URLs, and labeled links. Confirmation
  explicitly describes the regular PC browser session. Active user documentation
  and in-app guidance now explain this route.

The approved theme-based Notes layout, slim tabs, autosave without footer
buttons, remembered dimensions, direct Templates/Status/Trash controls, and
previous editor transition changes remain in place.

Windows controls which processes may take foreground focus; the implementation
uses the supported [foreground permission API](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow)
for verified instance handoff. The tray uses Microsoft's
[version-4 notification icon contract](https://learn.microsoft.com/en-us/windows/win32/shell/notification-area).

## Validation and practical limits

- Release x64 build passed without compiler warnings or errors.
- **385 native production-dialog checks passed.** Added coverage exercises
  generic default-browser launch/cancel/unsafe-URL rejection, real Unicode Paste
  and menu availability, blank-draft close/reopen and lowest-free-name reuse,
  named/content/metadata preservation, tray preference persistence, callback
  restore, Explorer recreation, registration/version failure, and no always-on-top
  flag. A separate pass-through check uses real Windows notification icon
  registration, version negotiation and removal. Launch calls are intercepted
  in native tests to verify URL/verb and absence of profile arguments.
- **21 installer/startup runtime scenarios passed** after the instance handoff
  change. Isolated fixtures intercept installation effects and do not alter the
  live installation.
- Computer Use reviewed the final executable in a disposable client fixture:
  the tray setting and saved checkmark, actual mouse right-click menu, Paste and
  Undo, an ordinary URL's default-browser confirmation, closing an untouched
  Untitled 6 draft, reopening without it, and creating Untitled 2 as the lowest
  available name. No external URL was launched during the visible review.
  The desktop tray icon itself was not clicked in this visual review; native
  tests cover registration and callback behavior.
- Release gates, guided walkthrough source contract and Microsoft Defender scan
  passed on the final executable. Defender's first invocation failed with
  `0x80508023`; retrying with a native Windows file path completed and found
  no threats. Packaging verified exactly one root `ctSpaces.exe` with an identical
  extracted executable SHA256.
- This adds no continuous frame-rate or multi-monitor DPI qualification. Earlier
  chat request-preparation failures remain unresolved.

Captures: `build/notes-launcher-tray-setting.jpg`,
`build/notes-launcher-context-menu.jpg`,
`build/notes-launcher-default-browser.jpg`, and
`build/notes-launcher-untitled-reuse.jpg`. Logs use the `build/notes-launcher-`
prefix: `build.log`, `native.log`, `startup.log`, `release.log`, `guide.log`,
and `defender.log`. Machine-readable result: `build/notes-launcher-result.json`.

Local package: `build/notes-launcher-review/ctSpaces6.3.0.0.zip`.

- EXE SHA256: `107077ECAD36E2C8F9B4C14D9F517C0BABBA9DB8F3DDE004C7B0806E812C7079`
- ZIP SHA256: `A18DB64367D5E26D59A5D5C4C7DFA52C5878261BBA3F2A307394CF0F34E892C5`
- Published ZIP, unchanged: `12527A5E45F4A27315458170888D484AEE62998F424C228964B857546C7EDA16`
- Prior template ZIP, unchanged: `F98195434916FCB4CBAB433797175F5190ED9E764A6FE08883C64EC20C21B129`
- Prior compact/trash ZIP, unchanged: `09F1AF342A52307D5AE3B15A0327BAE3100B8013B5E1DAAE347E467DBD2C2C9C`
