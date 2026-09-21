# ctSpaces 6.0 - What's New

ctSpaces 6.0 is a consolidated major milestone for the features previously shipped across 5.2 and 5.3. It does not add new behavior beyond 5.3.0.10. Existing client spaces, sign-ins, bookmarks, icons, browser history, and saved sessions remain intact during the update.

## Current 6.0.0.0 Milestone

- **One current release:** The 6.0 release gathers the launcher, browser isolation, profile-safety, backup, cleanup, client-management, and guided-help work described below under one current version.
- **No new breaking behavior:** Storage locations, client/browser profile layout, update prompts, shortcuts, settings, and supported workflows remain compatible with 5.3.0.10.
- **Same focused download:** The release ZIP contains only `ctSpaces.exe`; guides, source, license notices, and historical reports stay in the repository.

## Previously Shipped In 5.3.0.10 (Historical)

- **Optional guided walkthrough:** A skippable welcome for fresh users and a full guide available later from Options or F1. Existing users and client-shortcut launches are not forced through onboarding.
- **Precise, read-only help:** Jump between topics covering everyday work, browser separation, Default changes, cleanup and backups. The guide explains actions without performing them.
- **Feature update guidance:** What's new and labelled indicators track individual topic revisions, so future changes can be highlighted without repeating the whole tour.
- **Theme and keyboard support:** The walkthrough follows the selected theme and display scale, with keyboard navigation and readable scrollable explanations.

## Previously Shipped In 5.3.0.9 (Historical)

- **Retryable cleanup failures:** A file lock no longer removes the client identifiers needed to retry deletion. Partial cleanup preserves the activity date, so an unsuccessful cleanup does not restart the three-month timer.
- **Cleanup error recovery:** Failure to open a selection dialog deletes nothing and reports the problem. Failure to open a result dialog preserves the complete summary in themed, bounded pages.
- **Display-scale consistency:** Secondary buttons and text menus use the font and scaling of their own window.
- **Safe overlapping work:** The launcher stays disabled until both an in-progress client launch and any Default-profile processing have finished.

## Previously Shipped In 5.3.0.8 (Historical)

- **Clean up inactive clients:** Settings can preview clients that have not been opened for three calendar months and permanently delete only the clients you explicitly acknowledge. The preview includes archived clients, rechecks activity and browser use before deletion, and skips anything open, changed, unsafe, or unverifiable.
- **Delete multiple closed clients:** `Delete Multiple Clients` provides the same multi-selection preview and safety checks without the three-month waiting period. It starts with nothing selected and requires the permanent-deletion acknowledgement before the delete action is enabled.
- **Clearer cleanup results:** Cleanup previews, warnings, confirmations, scrollable results, and the About window now follow the selected theme. Empty results stay compact, detailed results preserve paragraph breaks, and the details text is not selected automatically.
- **Consistent popup menus:** Pinned-client and open-session overflow menus use the selected theme, and the cleanup broom remains visible in light, dark, and selected states.
- **Simpler text-field menus:** Right-click editable client-name and input fields for Undo, Cut, Copy, Paste, Delete, and Select All. Read-only result fields offer Copy and Select All. Availability follows the current selection, clipboard, undo, password, and read-only state; standard keyboard shortcuts and international text input are unchanged.
- **Cleanup idea credit:** About now credits Cameron Kincer for the Client Cleanup Idea.

## Previously Shipped In 5.3 (Historical)

- **Reorder pinned clients:** Drag visible pins left or right. The new order is saved for the next time ctSpaces opens.
- **Clean open-client labels:** Each open-session tab shows only the client name, without a browser suffix or separator. Drag active client tabs into the order that works best for the current session.
- **Open a copied link in the right client:** Copy a website address, right-click a pinned client, and choose `Open copied link`.
- **Clean client desktop shortcuts:** Create one customer-icon shortcut per client from Settings, the pinned-client menu, or by dragging a visible pin onto the Windows Desktop. Its visible label is exactly the client name (`Client.lnk`) and it records the browser selected when created. Recreate it after selecting another browser to update that same managed link. This does not limit the client's independent browser slots or simultaneous different-browser sessions inside ctSpaces.
- **Safe client rename:** Rename a closed client while keeping all of its browser slots, shared customer icon, pinned position, browser-specific Restore tabs choices, and ctSpaces-created client shortcut.
- **Archive inactive clients:** Hide a closed client from the normal list without deleting any browser data. Restore it later from `Archived Clients` in Settings.
- **Easier Alt+Tab identification:** Turn on `Client name first in window titles` to place the client name at the beginning of ctSpaces browser titles.
- **Single-launch shortcut handling:** A client shortcut hands its request to ctSpaces when the launcher is already running instead of opening a second copy.
- **Private starter bookmark icons:** New Chromium spaces keep the intended Default bookmark icons, while unrelated sites visited during Default editing are removed from the saved icon database. Firefox receives a separate clean Firefox profile rather than Chromium starter data. Existing client spaces and their icon history are never changed by this cleanup.
- **Independent browsers for each client:** A client can now keep separate Edge, Chrome, Brave, and Firefox spaces. For example, Client A may have Chrome and Firefox open together without either browser sharing the other's cookies, logins, history, or sessions.
- **Private legacy upgrade:** An existing Chromium profile stays exactly where it is and is bound to its first selected Chromium browser. ctSpaces does not move its private browser data merely to adopt the new multi-browser layout; additional browsers receive separate clean slots.
- **Clean transient spaces:** Default editing and temporary browsing remain one-session-at-a-time operations across every browser. They never reuse a standard client's browser slot.
- **Safer whole-client deletion:** Delete now names and removes the entire client, including every browser slot, only after fresh path/layout checks and a second open-browser check. Shortcut cleanup removes only links verified as ctSpaces-managed and leaves unrelated Desktop shortcuts untouched.
- **Reset fails closed:** Reset remains disabled while its selected-browser destructive behavior awaits separate authorization. It does not silently substitute whole-client deletion.
- **Safer saved settings:** Configuration changes are staged and committed atomically, and Unicode client names remain intact when an older settings file is migrated. If an existing configuration cannot be decoded or read reliably, ctSpaces preserves it instead of treating it as empty and pruning pins, archived clients, or Restore-tabs choices.
- **Hardened starter archives:** Default-profile archive handling rejects unsafe paths, duplicates, Windows device names, links/reparse points, unexpected item types, and excessive declared sizes before committing a sanitized archive.
- **Reliable deep-path rename:** Client and shortcut names are checked against the real Windows path budget. Case-only rename safely preserves even the deepest supported browser-profile files instead of using an overlong temporary name.
- **Fresh browser-use checks:** Rename, archive, Delete, export, and restore inspect every relevant browser profile and fail closed if a process is active or cannot be identified reliably. Restore repeats that check immediately before replacing live client data.
- **Safer updates and shortcuts:** Executables and `.lnk` files are written to verified same-folder stages and committed without overwriting a raced or unrelated item. Failed transactions preserve the prior owned file or report the exact retained path.
- **Reliable existing client shortcuts:** Recreating a ctSpaces-owned client shortcut safely updates its recorded browser and still works correctly when Windows carries the old file's creation time into the replacement.
- **Separate taskbar buttons:** ctSpaces keeps its launcher separate from the client browser opened by a Desktop shortcut, while each client/browser pair retains its own taskbar identity.
- **Reliable live client titles:** Normal Edge and Firefox captions are recognized correctly, and changing `Client name first in window titles` updates browser windows that are already open.

These additions keep the main launcher compact. Rename, archive, shortcut, and title controls live in Settings or the pinned-client right-click menu rather than adding another permanent row of buttons.

The separation provided by ctSpaces is browser-profile isolation for everyday account workflows. It is not an operating-system security sandbox, encryption boundary, or protection from malicious websites or extensions.

## Updating

Close ctSpaces, then run the ctSpaces 6.0 executable and approve the update when prompted. Client browser profiles are stored separately and remain available afterward.

## Previous 5.2 Changes (Historical)

ctSpaces 5.2 refreshed the launcher and improved session, icon, theme, update, and backup reliability.

### Highlights

- **Redesigned launcher:** A compact dark-friendly layout makes it easier to choose a client, see its customer icon, and understand whether ctSpaces will create, open, or show it.
- **Pinned clients:** Favorite clients can be pinned with their customer icons for one-click opening or switching. Right-click a pin to select it, open it, or change its `Restore tabs` setting before launch.
- **More visible favorites:** The Pinned label now sits above the shortcuts, allowing four pinned clients to remain visible before overflow.
- **Integrated client selector:** The customer logo has its own clean space inside the editable client field, keeping long names, typing, and the client list from colliding.
- **Modern client list control:** The dated boxed arrow has been replaced by a subtle themed chevron while keeping the familiar type-or-select behavior.
- **Better spacing:** When no clients are pinned, the unused Pinned/None row disappears and the launcher becomes shorter. Pinned shortcuts return automatically when the first favorite is added.
- **Clearer pin control:** The old bookmark ribbon has been replaced by the angled pushpin used in the new design.
- **Open-client tabs:** Each open client appears in a compact tab. Use the tab to return to that client or close its browser window.
- **Overflow menu:** When several clients are open, extra tabs move into a menu instead of being squeezed together. Clients can also be closed from that menu.
- **Saved client sessions:** Existing clients can reopen their previous browser session, including the tabs they were using.
- **Per-client tab choice:** A new `Restore tabs` switch lets each existing client either reopen its previous tabs or start on a normal new tab. Turning it off does not remove sign-ins, cookies, bookmarks, history, or other saved client data.
- **Browser-close restore:** Closing a client from the browser itself now releases that client cleanly, so tabs can be restored the next time it is opened through ctSpaces. If the browser asks to close all tabs, choosing `Close all` still saves the session used by `Restore tabs`.
- **Temporary browsing:** Temporary spaces always start fresh and are removed after they are closed.
- **Browser choice:** ctSpaces can use Microsoft Edge, Google Chrome, or Brave, and the browser can now be changed directly from the footer.
- **Themes:** Existing themes remain available. New dark choices include Gothic, red-and-black Crimson, Graphite Copper, Black Cherry, Midnight Moss, Ink Blue, and Violet Ash.
- **Softer dark themes:** The modern dark themes now use lighter charcoal surfaces for better visibility while keeping their original accent colors.
- **Clearer wording:** The former `Color` option is now named `Themes` throughout the app.
- **Backup and restore:** All client spaces can be exported to a backup file and restored later.

### Profile Improvements

- Client icons now appear on the client browser window and taskbar.
- Changing a client icon now updates the already-open client browser and taskbar without restarting that client.
- Website icons in bookmarks have been restored.
- Client profiles can be reset more safely. If the reset cannot finish, the existing profile is kept.
- The default new-client profile can still be edited without affecting existing clients.
- New clients start with a normal browser new tab instead of restoring an unrelated session.

### Fixes

- Fixed saved-login and autofill suggestions briefly appearing and immediately closing while browsing in a client space.
- Closing ctSpaces now asks once and closes all open client browsers without making you approve a second close-all-tabs message in every browser window. Each client receives a brief opportunity to save its session and sign-ins first.
- Fixed new clients unexpectedly opening random websites or `about:blank`.
- Fixed bookmark entries showing a generic globe instead of website icons.
- Fixed updates sometimes reporting that ctSpaces was still running after it had been closed.
- Improved the install and update messages so it is clearer whether ctSpaces is being installed, updated, or launched temporarily.
- Fixed client tabs becoming too wide or cramped when several clients were open.
- Vertically centered the client name and rebuilt the selector as one themed surface, preventing Windows from repainting a white outline, hiding the customer logo, or restoring the old dropdown face.
- Removed the extra block that could appear beside the client-list arrow while typing or opening the dropdown.
- Fixed the white native dropdown button reappearing after the list opened by restacking the themed selector after Windows completes each open/close transition.
- Prevented the lower native combo from painting through the overlapping themed selector and editor, which could still leave the entire client field white in 5.1.0.12.
- Tightened the dropdown lane and enlarged its chevron so the right side no longer looks like unused whitespace.
- Added close controls for clients listed in the overflow menu.
- Fixed the Themes picker becoming slow or unresponsive when moving quickly through several themes. Previewing is now lightweight, and the heavier menu and icon updates happen once after Apply.
- Made the Auto-fetch Icon window larger, cleaner, and consistent with the selected theme and Windows display scaling.
- Fixed an intermittent issue where changing an open client's icon saved the new icon but did not immediately update its browser window or taskbar button.
- Fixed the remaining Windows taskbar cache issue so an open client's button is rebuilt with the new icon immediately.
- Fixed a crash or failed update that could occur when replacing an icon while that client's browser was already open.
- Auto-fetch Icon now starts with a blank website field instead of guessing the client name as a `.com` address.
- Icon downloads and conversions are prepared safely before replacing the client's current icon.
- Improved closing behavior so browser sessions and sign-ins have time to save normally.
- Improved backup reliability by preventing a backup while client browser windows are still open.
- Updated the built-in archive engine to 7-Zip 26.02 for current security and reliability fixes.
- Replaced PowerShell backup creation with the built-in 7-Zip engine. New `.7z` backups include a ctSpaces manifest, preserve hidden and very large files, fail if any source file is skipped, and are integrity-tested before being saved.
- Restore now checks archive paths, item count, disk space, CRCs, manifest, and profile count before replacing data. Older `.zip` backups remain restorable, and the previous `Sites` folder is still retained for recovery.

### Simplified Interface

- Removed the ticket-number and screenshot controls that made the launcher feel crowded.
- Kept the client tabs, profile icon, temporary-space button, settings menu, and all previous theme choices.
- Added a single editable client field, a clean pushpin toggle, and a slim pinned row with compact logo-and-name shortcuts.

### Updating To 5.2 (Historical)

Run the new ctSpaces 5.2 file and approve the update when prompted. Your existing client spaces and saved browser information will remain available after the update.
