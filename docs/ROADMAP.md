# Improvement Roadmap

The first section records recently completed work. The remaining items are proposed improvements ordered to improve reliability first and keep the main launcher compact.

## Current Planning Priorities

Recorded October 6, 2026. Filtering, Client Notes, and Close All Clients were
implemented in 6.1 on October 8. The other five accepted
items remain future work.

1. **Direct support reporting to GitHub.** Add a themed report window inside
   ctSpaces with a description, technical information preview, Send report,
   and submission confirmation. Reports go through a Cloudflare Worker and
   become GitHub issues without requiring colleagues to sign into GitHub.
   Use the existing Cloudflare domain and stay within free Cloudflare
   services. Store GitHub credentials with the Worker. Exclude client names,
   browsing data, and contact information from public issue content.
   The exact domain and reporting setup remain to be configured.
2. **Filter clients while typing.** Narrow the existing client list as the
   user types any part of a name. Preserve deliberate creation of new clients
   and make existing matches clear. Keep the launcher compact and follow
   the selected theme.
3. **Small client notes.** Add a separate themed notes window accessible
   from the client's context menu. Store local reminders with that client,
   preserve them through rename and archive, and remove them with whole
   client deletion. Keep the main launcher compact.
4. **Close All Clients.** Request a normal close of every open client browser
   while leaving ctSpaces open. Show progress and handle browsers that
   refuse or delay closing without losing tracking of their open profiles.
5. **Open copied links in an already open client.** Investigate the reported
   behavior where this action works only while the client is closed. Each
   invocation should open the copied URL in a new tab of the intended
   client and browser, including several links copied and opened in succession.
   Preserve the normal launch behavior when the client is closed and the
   isolation between client and browser profiles. Current source already
   attempts this handoff for Chromium browsers but explicitly rejects it
   for an active isolated Firefox profile. Confirm the reported browser and
   test the running client paths before implementing a reliable solution.
6. **Client nicknames.** Let users assign optional alternate names to an
   existing client from its context menu. The typing filter matches both
   the real name and nicknames, then opens the same existing client.
   Display the real client name in results and require a clear selection
   when matches are ambiguous. Nicknames are local metadata and do not
   create or rename browser profiles. Preserve them through client rename
   and archive, include them in client backups, and delete them with the
   client.
7. **Share Client Setup.** Export a selected client's setup metadata for
   another colleague to import: real name, nicknames, icon, notes, and saved
   links when that feature exists. Let the sender review what is included.
   The recipient receives a prepared client with fresh browser profiles for
   their own sign ins. Browser cookies, credentials, history, and live
   session data are not part of this setup export. Handle an existing client
   name through explicit import choices and preserve the normal application
   ZIP distribution.
8. **Separate downloads for each client.** Offer a client specific download
   destination for its browser slots and an Open Downloads action in the
   client menu. Make the destination visible and keep existing files in
   their current locations unless the user explicitly requests a move.
   Before implementation, settle whether downloads are stored inside the
   client container and included in its backup/deletion rules or retained
   in a separate user folder. Explain that choice clearly in the interface.

Items 2, 3, and 4 are implemented in 6.1. The other items are
accepted plans for future implementation. The additional proposals
below remain ideas for consideration.

## Implemented In 5.3 (Historical)

These items were present in the validated `5.3.0.3` project-local candidate.
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
