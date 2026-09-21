# Troubleshooting

Applies to ctSpaces 5.3.

## A Client Name Is Rejected

Use a non-empty Windows folder name. ctSpaces removes invalid filename characters and trailing periods or spaces. It rejects Windows-reserved names such as `CON`, `PRN`, `AUX`, `NUL`, `COM1`, and `LPT1`, plus the internal names `Default` and `Temp`.

If a typed name changes after invalid characters are removed, use the cleaned name shown in the Client Profile field.

Very long names can also be rejected because the final client and browser-profile paths must fit the safe legacy Windows path budget at the current Local AppData location. Use a shorter client name. Existing historical folders are not renamed or deleted automatically; operations on a path Windows cannot inspect safely stop without changing it.

## ctSpaces Says The Profile Is Already Open

Select the tab for that client and browser and use `Show`. A single ctSpaces launcher will not open the same client/browser slot twice because two browser instances writing the same data directory can corrupt profile data.

This does not prevent the same client from using a different browser. Change the browser selector to open that client's independent Edge, Chrome, Brave, or Firefox slot; for example, Client A may have Chrome and Firefox open together.

If no tab is visible, check the overflow menu. If the browser was closed outside ctSpaces, allow a moment for the launcher to notice that its process ended.

## A New Client Opens Old Websites

A brand-new client should open the browser's normal new tab. It should not restore Default's last session or another client's websites.

Confirm that the selected client/browser slot did not already exist. Existing slots request session restore when their browser-specific `Restore tabs` switch is on. If a genuinely new slot restores unrelated sites, record the ctSpaces version, selected browser, and exact client name before changing anything.

## A New Client Opens `about:blank`

Version 5.0.0.1 and later do not add `about:blank` to the browser command line. Confirm that the installed copy reports the current version in About. Browser policy or a browser extension can still control the new-tab experience.

## An Existing Client Does Not Restore Tabs

ctSpaces passes `--restore-last-session` only for an existing standard client/browser slot whose `Restore tabs` switch is on. The browser still decides whether a session is recoverable.

- Select the client and browser and confirm `Restore tabs` is on for that pair before opening it.
- Close the client normally with its ctSpaces tab close button or the browser's own close button. If the browser asks to close all tabs, choosing `Close all` still allows ctSpaces to request that saved session later.
- Avoid force-ending the browser in Task Manager.
- Check whether the browser is configured to clear session data on exit.
- If the profile came from an older build where Reset was available, confirm that it was not reset to a fresh starter.

Default and temporary profiles never restore sessions by design.

For a pinned client, choose the intended browser and right-click the pin to view or change that slot's checked `Restore tabs` setting without opening it.

## Bookmark Icons Show Globes

Open the bookmark's website once and allow the page to finish loading. If icons remain generic for many existing and newly created bookmarks:

- Confirm the installed ctSpaces version is current.
- Check whether security software is preventing writes to the client's `Default\Favicons` database.
- Test a newly created disposable client before changing a real client.
- Do not delete the Favicon database as a session-repair step.

The privacy-clean Chromium starter includes a proof-pruned favicon database only for its saved Bookmarks. It contains exact bookmark URL mappings and their referenced icon/bitmap rows, with unrelated URLs, orphan records, compatible timestamps, freelist pages, and SQLite sidecars removed. It is used for new Edge, Chrome, and Brave slots, not clean Firefox profiles. Existing clients keep their own browser-created favicon databases; ctSpaces never replaces them with the starter database.

## A Client Window Or Taskbar Icon Does Not Change

First confirm that `Sites\<ClientName>\client.ico` changed and that the correct client is open.

- Use `Set Profile Icon` or `Auto-fetch Icon` again.
- Allow a few seconds for the icon watcher.
- Confirm About reports 5.1 or later. Version 5.1 rebuilds the existing Windows taskbar button after changing its versioned icon resource, while retaining the crash and hang protections added in 5.0.0.9.

Client tabs do not display icons by design. The icon appears in the launcher preview, browser window, and taskbar.

## Auto-fetch Icon Fails

- Enter a domain such as `microsoft.com`, without a page path.
- Confirm the computer has internet access.
- Check whether a firewall, proxy, DNS filter, or security policy blocks `www.google.com`.
- Try `Set Profile Icon` with a local image as a fallback.

Auto-fetch depends on Google's favicon service. A site can return a generic, tiny, or missing icon even when the download succeeds.

## The Auto-fetch Window Is Too Small Or Uses The Wrong Colors

Version 5.0.0.7 and later make this window DPI-aware and apply the selected ctSpaces theme to its title bar, background, input, focus border, and buttons. If an older appearance remains, open About and make sure the installed executable was actually updated.

## The Selected Theme Is Not Kept

Select Settings, `Themes`, choose the theme, and select `Apply`. `Cancel` intentionally restores the prior theme. The selection is stored in `%LOCALAPPDATA%\InfinitySys\ctSpaces\config.ini`.

If the file cannot be written, check folder permissions and security-software activity. Do not delete `Sites` to repair a theme setting.

## Open Copied Link Is Unavailable

Copy one complete address beginning with `http://` or `https://`, then right-click a visible pinned client. Plain search text, file paths, browser-internal addresses, and copied text containing extra lines are deliberately rejected.

## A Client Desktop Shortcut Is Not Created

Select an existing, non-archived client, choose the intended browser, and use Settings > `Create Desktop Shortcut`, or use the same command from its pinned-client menu. The visible Desktop label is exactly the client name (`Client.lnk`), while the link records the browser selected when it was created. When dragging a pin, release it over an open area of the Windows Desktop rather than another application window. If Explorer keeps showing an older icon, refresh the Desktop; the shortcut already points at the client's current `client.ico` path.

If the exact `Client.lnk` name cannot fit the safe Windows path budget, shorten the client name or use a shorter Desktop folder location. ctSpaces stops without changing the Desktop rather than inventing another visible label or overwriting another shortcut.

Recreating the shortcut after selecting another browser safely updates that one reverified ctSpaces-managed client link, including when NTFS carries the old file's creation time into the replacement. It does not remove or combine the client's isolated browser slots: Edge, Chrome, Brave, and Firefox sessions can still run simultaneously for the same client inside ctSpaces. Unrelated shortcuts and reparse points are left unchanged. The ctSpaces launcher also has a taskbar identity separate from every client/browser identity, so opening a client shortcut does not merge the launcher and browser buttons.

## Rename Or Archive Is Unavailable

Close that client's browser window first. Rename and archive are disabled while the profile is active so browser databases and profile files are not moved or hidden mid-write.

Renaming keeps every browser slot, the shared custom icon, pinned position, browser-specific Restore tabs choices, and the ctSpaces-managed client Desktop shortcut. Archiving keeps the complete client container but removes the client from the normal list and Pinned row.

## An Archived Client Is Missing

Open Settings > `Archived Clients` and choose the client to restore it to the normal list. Archived entries are removed only when their matching profile folder no longer exists. Archive is not Delete Profile and does not remove the folder itself.

## Browser Titles Do Not Begin With The Client Name

Open Settings and check `Client name first in window titles`. Allow about a second for open ctSpaces browser windows to refresh after a page/tab or preference change. Current Edge branding variants, including its zero-width separator form, are normalized before the client name is added. This is one global display preference; it does not rename browser profiles or websites.

## A Browser Is Missing From Browser Selection

ctSpaces detects Edge, Chrome, Brave, and Firefox in their standard Program Files and current-user installation locations. A menu entry is disabled when its executable is not found.

Install the browser normally or confirm that organizational packaging did not put it in a custom path. Custom browser paths are not currently supported.

## Reset, Vacuum, Or Delete Is Unavailable

Close every browser slot for that client before a whole-client action. Delete rechecks all four browser slots immediately before removing data.

- `Vacuum` clears selected cache folders and preserves normal account state.
- `Reset` deliberately remains disabled and fails closed pending separate authorization for its selected-browser destructive behavior.
- `Delete Profile` permanently removes the entire client folder, including legacy data and all Edge, Chrome, Brave, and Firefox slots.

Export a backup before Delete when there is any doubt. Delete cannot be undone. ctSpaces removes only Desktop shortcuts it can verify as ctSpaces-managed; an unrelated shortcut with a similar name is left alone.

If Delete reports a partial or failed removal, do not assume the client is gone. The selection is retained so the exact reported path can be inspected manually.

## Export Or Restore Is Blocked

Close every supported browser process using a profile below the ctSpaces `Sites` folder, including a browser started manually with a ctSpaces profile path. Export and restore fail closed when a relevant process is active or cannot be inspected reliably; they do not rely only on tabs tracked by the current launcher.

If the browser looks closed, wait briefly for its background process to exit. Avoid ending unrelated browser sessions in Task Manager.

## A Restored Profile Asks For Sign-in Again

This can be normal on another computer or Windows account. Windows and Chromium can protect credentials with machine- or user-specific keys. The backup still preserves the profile files, but ctSpaces cannot make protected credentials portable.

## An Update Says ctSpaces Is Still Running

1. Close all ctSpaces client browsers.
2. Close the launcher and wait several seconds.
3. Retry the newer executable.
4. If the message includes a process ID, locate only that `ctSpaces.exe` in Task Manager.
5. If no process remains, allow antivirus software time to release the executable.

Do not delete client profiles to solve an executable lock.

## An Update Says It Is Another Version Or Offers A Temporary Run

Open About and compare the release version. ctSpaces uses its four-part Windows file version for update comparisons; `version.h` is the source of truth for a particular build.

- A newer external copy offers an update.
- An older copy offers a one-time troubleshooting run.
- A physically different copy with the same version offers replacement or temporary use.

Every distributed code change should increment the fourth number so the normal update path is clear.

## ctSpaces Cannot Find A Browser

Install at least one supported browser in a standard location. Current builds support Microsoft Edge, Google Chrome, Brave, and Mozilla Firefox.

## Temporary Data Cannot Be Removed

The browser or security software may still hold files in the `Temp` directory. Close the temporary browser, wait briefly, and start ctSpaces again. It attempts to remove the old temporary folder before the next temporary launch. Only one temporary session may run at a time across all browsers.

## Where To Look Without Changing Data

Useful read-only details for support are:

- ctSpaces version from About.
- Selected browser and browser version.
- Windows version and display scaling.
- The exact error message and operation being attempted.
- Whether the client is new, existing, Default, or temporary.
- Whether `%LOCALAPPDATA%\InfinitySys\ctSpaces\Sites\<ClientName>` exists.

Do not send a profile folder, ctSpaces backup, cookies database, password database, or browsing history as a routine support attachment.
