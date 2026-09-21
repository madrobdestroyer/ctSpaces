# Improvement Roadmap

The first section records recently completed work. The remaining items are proposed improvements ordered to improve reliability first and keep the main launcher compact.

## Implemented In Current 5.3 Work

These items are present in the validated `5.3.0.3` project-local candidate.
Automated release controls are complete; installation and the manual deployment
pilot remain separate decisions.

- Saved drag ordering for pinned clients.
- Runtime drag ordering for open client tabs.
- Open a copied HTTP/HTTPS link from a pinned client's right-click menu.
- One customer-icon Desktop shortcut per client, with the exact client-name label (`Client.lnk`) and the browser selected when it was created; recreating it updates that managed link.
- Drag a pinned client onto the Desktop to create its shortcut.
- Safe rename that retains every browser slot, shared icon, pin position, browser-specific Restore tabs choices, and the generated client shortcut.
- Reversible client archive and restore without deleting profile data.
- Optional client-first browser window titles for Alt+Tab.
- Independent Edge, Chrome, Brave, and Firefox profile slots for each client, including simultaneous different-browser sessions for one client; the one-link Desktop naming policy does not limit these slots.
- In-place binding for legacy Chromium roots without moving their private browser data.
- Cross-browser singleton Default/Temp sessions and sanitized bookmark-linked starter favicons.
- Whole-client Delete with repeated path/layout/activity validation and exact managed-shortcut cleanup; Reset remains separately scoped and disabled pending authorization.
- Atomic tri-state configuration persistence and hardened starter-archive sanitization.

## Product Direction

ctSpaces is strongest when it does one job quickly: open the correct persistent client browser space without mixing accounts. New features should support that workflow without returning ticket numbers, screenshot controls, or profile-management bulk to the main window.

Use a separate Profile Manager or focused dialogs for larger workflows. Keep the launcher optimized for selecting, opening, showing, and closing clients.

## Recommended Next Three

### 1. Track Browsers By Profile Path

Identify running browser processes and windows by their normalized `--user-data-dir`, with PID tracking as a fast hint rather than the only identity.

Why it matters: Chromium can hand work to an existing process or change which process owns a window. Profile-path tracking would improve tab cleanup, close behavior, active-profile detection, updates, and all-browser compatibility.

### 2. Add A Privacy-Safe Diagnostics Report

Provide a `Copy Diagnostics` command containing ctSpaces version, Windows version, display scale, selected browser, detected browser paths and versions, relevant folder existence, active profile names, and recent ctSpaces errors.

It must exclude URLs, cookies, history, credentials, bookmark contents, and browser databases. This would make support far easier without asking users to send sensitive profiles.

### 3. Add A Separate Profile Manager

Create a compact management window with search, alphabetical/recent sorting, archived-client filtering, open status, last-used time, disk size, and the existing Vacuum/Icon/Delete commands. Reset should appear only after its separate scope and authorization work is complete.

The main launcher remains unchanged. The manager becomes the place for infrequent organization work.

## Prioritized Ideas

| Priority | Idea | User value | Estimated effort | Design note |
|---|---|---|---|---|
| P0 | Profile-path process tracking | More reliable open/show/close and update behavior | High | Build before adding more session features |
| P0 | Privacy-safe diagnostics report | Faster support and clearer bug reports | Medium | Never include browsing data |
| P0 | Broader integration-test matrix | Prevents Edge/Chrome/Brave/Firefox and DPI regressions | High | Use only GUID-named disposable profiles |
| P0 | Code signing and published checksums | Clearer trust and fewer security prompts | Medium | Requires a release-signing process |
| P1 | Separate Profile Manager | Search and organization without launcher clutter | Medium | Best home for future profile tools |
| P1 | Selective export and restore | Move or recover one client without replacing all clients | High | Add backup manifest and collision choices |
| P1 | Clone profile | Faster setup from a similar existing client | High | Must remove account/session state unless explicitly retained |
| P1 | Recoverable delete/reset quarantine | Easier undo after destructive actions | Medium | Add retention limits to avoid silent disk growth |
| P1 | Disk usage and Vacuum preview | Shows what will be removed and space recovered | Medium | Keep logins/history clearly marked as preserved |
| P1 | Backup retention manager | Makes `_DefBak` and `Sites_PreRestore` understandable | Medium | Never silently remove the newest recovery copy |
| P2 | Close All command | Faster end-of-day shutdown | Low | Request normal close and show progress |
| P2 | System tray quick switcher | Faster access when the launcher is minimized | Medium | Optional, not enabled without user choice |
| P2 | Better icon fetch | More reliable icons and clearer previews | Medium | Normalize domains, add provider fallback, preview before applying |
| P2 | Explicit per-client start page | Optional repeatable workflow for selected clients | Medium | Never guess or open random sites automatically |
| P2 | Default-editor safety banner | Makes starter-only behavior harder to misunderstand | Low | Keep it inside the Default editor workflow |
| P2 | Accessibility audit | Better keyboard, high-contrast, scaling, and screen-reader support | Medium | Test at 100, 150, and 200 percent DPI |

## Suggested Delivery Order

### Reliability Release

- Profile-path process tracking.
- Diagnostics report.
- Edge, Chrome, Brave, and Firefox lifecycle tests.
- Update and shutdown regression coverage.

### Data Safety Release

- Selective restore.
- Retention management for recovery folders.
- Optional encryption for backups stored outside approved protected locations.

### Workflow Release

- Separate Profile Manager.
- Search, archive filtering, recent clients, size, and open status.
- Clone with transactional rollback and clear privacy choices.

## Ideas To Avoid

- Do not put ticket numbers, screenshot capture, notes, or support-case fields back into the main launcher.
- Do not automatically open a guessed website for a new client.
- Do not add icons to client tabs; the selected identity is already visible in the launcher and browser taskbar.
- Do not make Default-session restore available. Default is a clean starter editor.
- Do not migrate or sanitize existing `Sites` profiles as part of a starter-template update.
- Do not share, copy, or merge profile state between a client's browser slots as an automatic convenience.
- Do not make backups appear portable without warning about Windows-protected credentials.
- Do not add background network update checks without an explicit release, trust, and privacy design.

## Definition Of A Good Addition

A proposed feature should answer yes to all of these:

1. Does it make switching or maintaining client browser spaces safer or faster?
2. Can it preserve the compact main launcher?
3. Does it avoid exposing or transmitting browser data?
4. Can destructive behavior be staged, validated, or reversed?
5. Can the behavior be tested with a disposable profile?
