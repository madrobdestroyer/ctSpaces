# ctSpaces User Guide

Applies to ctSpaces 6.0.

## What ctSpaces Does

ctSpaces opens separate browser profiles for each client. A client may have independent Microsoft Edge, Google Chrome, Brave, and Mozilla Firefox spaces. Each client/browser pair keeps its own:

- Website sign-ins and cookies.
- Browser tabs and session history.
- Bookmarks and website icons.
- Extensions and extension settings.
- Browsing history, site storage, and cached files.

This lets you work with several client accounts without signing out, mixing cookies, or relying on Incognito windows. It also lets the same client use more than one browser when a site works better in a particular browser.

The optional custom client icon is different: it is stored once for the client and shared by all of that client's browser slots.

ctSpaces profiles are normal persistent browser profiles. They are not Incognito windows, operating-system security sandboxes, encrypted containers, or protection from unsafe sites, extensions, processes, or Windows users. ctSpaces disables browser sync when it launches a Chromium profile; Firefox uses its isolated profile arguments without a ctSpaces sync-disable flag.

## Main Window

The launcher is divided into a few compact areas:

- **New tab:** returns to the client selection view.
- **Client tabs:** display only the client name for each space opened by the current ctSpaces session. If one client is open in two browsers, both tabs intentionally use that client name; ctSpaces still tracks each browser slot separately.
- **Overflow button:** appears when all open clients cannot fit. It can show or close hidden client tabs.
- **Pinned row:** shows favorite existing clients as compact customer-icon shortcuts. Left-click one to open it or switch to its open tab, drag pins to reorder them, or right-click one for more choices.
- **Client field:** type a new client name or select an existing one from the same editable control.
- **Pushpin button:** adds or removes the selected existing client from the Pinned row. It stays unavailable for names that have not been created yet.
- **Create button:** appears for a new typed name and creates that client when selected.
- **Open button:** opens the selected client. Pressing Enter in the client field does the same thing.
- **Show button:** appears when an open client tab is selected and brings that browser window forward.
- **Client selector:** keeps the selected client's logo inside the editable name field. Clicking the logo opens that client's profile folder in File Explorer.
- **Temporary profile button:** opens a fresh disposable browser profile.
- **Settings button:** opens profile tools, themes, browser selection, backup/restore, and About.
- **Browser selector:** shows the browser used for the next launch and opens the browser menu directly. Changing it selects another independent browser slot; it does not convert or share the current slot.
- **Restore tabs switch:** controls whether the selected existing client/browser slot reopens its previous tabs the next time that slot is launched. It is unavailable for a new client name.

## Open A New Client

1. Select the `New` tab.
2. Type a client name in the Client Profile field.
3. Select `Create` or press Enter.
4. For Edge, Chrome, or Brave, ctSpaces creates the new browser slot from the Chromium Default profile template. For Firefox, it creates a separate clean Firefox profile.
5. The browser opens a normal new tab.

A brand-new client/browser slot does not restore the Default editor's old tabs or another client or browser's session.

## Open An Existing Client

1. Select the client from the list.
2. Select `Open` or press Enter.

Existing standard clients request their previous browser session by default, so tabs from their last normal shutdown can reopen. The browser ultimately decides what can be restored.

Only one instance of the same client/browser pair can be opened through one ctSpaces launcher at a time. The same client may be open in different browsers simultaneously, such as Client A in both Chrome and Firefox.

## Choose Whether A Browser Slot Restores Tabs

1. Select an existing client in the Client field and choose the browser whose slot you want to change.
2. Leave `Restore tabs` on to request that slot's previous tabs on its next launch, or turn it off to start that slot on the browser's normal new tab.

The choice is saved separately for each client/browser pair and affects that slot's next launch. Chrome and Firefox may therefore have different Restore tabs settings for the same client. Turning it off does not clear cookies, sign-ins, bookmarks, history, extensions, site data, or saved browser-session files. New slots, the Default editor, and temporary profiles always start fresh, so the switch stays off and unavailable for them.

For a pinned favorite, first choose the intended browser, then right-click its shortcut and check or uncheck `Restore tabs` for that browser slot before opening it. `Select client` places it in the editable Client field without launching it. Right-click the pinned overflow control to select one of the hidden favorites and use the main switch.

## Pin Favorite Clients

1. Select an existing client in the Client field.
2. Select the pushpin beside the field.
3. The client and its customer icon appear in the Pinned row.

Left-click a pinned client to open it in the currently selected browser. If that client/browser slot is already open, ctSpaces switches to its existing tab and browser window. Right-click it for `Select client`, `Open client`, `Open copied link`, browser-specific `Restore tabs`, and `Create desktop shortcut`. Select the filled pushpin again to remove it from favorites. Up to eight clients can be pinned; the first four remain visible and additional pins appear under the overflow control.

Drag visible pins left or right to put them in your preferred order. The order is saved automatically. Dragging a visible pin onto an open area of the Windows Desktop creates that client's desktop shortcut.

## Open A Copied Link In A Client

1. Copy a complete `http://` or `https://` website address.
2. Right-click the client's visible pinned shortcut.
3. Select `Open copied link`.

If the selected client/browser slot is already open and can accept a new command-line URL, the link opens in that slot. If it is closed, ctSpaces launches the selected slot with its saved Restore tabs choice and opens the copied link. An already-running isolated Firefox slot cannot reliably accept a second command-line URL, so ctSpaces explains that limitation instead of starting another writer. Invalid text and non-web addresses are not offered.

## Create A Client Desktop Shortcut

Select an existing client and choose `Create Desktop Shortcut` in Settings, choose `Create desktop shortcut` from a pinned client's right-click menu, or drag a visible pin onto the Desktop.

The shortcut uses the customer's icon when available, and its visible Desktop label is exactly the client name (`Client.lnk`). It opens that client in the browser selected when the shortcut was created. There is one managed Desktop link per client; after choosing another browser in ctSpaces, create the shortcut again to update that same link. The ctSpaces launcher and opened browser remain separate taskbar buttons. If ctSpaces is already running, the request is handed to the existing launcher instead of opening a second copy. Recreating or renaming a ctSpaces-owned shortcut replaces it safely; unrelated shortcuts are left alone.

The clean shortcut does not restrict the client's browser spaces. Change the browser selector to open the same client in independent Edge, Chrome, Brave, or Firefox slots, including more than one browser at the same time. For a very long client name, the exact `Client.lnk` path may not fit the safe Windows path budget at the current Desktop location. In that case shortcut creation stops and leaves the Desktop unchanged instead of inventing a different visible label.

## Work With Several Clients

- Open additional clients from the `New` tab.
- Change the browser selector to open another independent browser slot for the same client.
- Select a client tab to bring its browser window forward.
- Drag open client tabs left or right to arrange them for the current ctSpaces session.
- Select the `x` on a client tab to close that browser normally.
- Use the overflow button when there are more open clients than the tab row can display.
- The overflow menu has one section for showing clients and another for closing them.

Closing a client normally gives the browser time to save its current session and sign-in state.

You may also close the slot from the browser window. If the browser asks whether to close all tabs, choose `Close all` to finish closing it. When that client/browser slot's `Restore tabs` switch is on, ctSpaces requests those saved tabs the next time that slot is opened. The browser does not need a separate "close and save" button.

## Make Browser Windows Easier To Identify

Open Settings and check `Client name first in window titles` to place the client name at the beginning of each ctSpaces browser title. This makes similar browser windows easier to distinguish in Alt+Tab. The setting applies immediately to already-open ctSpaces client windows and is saved until changed again.

## Profile Types

| Type | Saved after closing | Restores a prior session | Purpose |
|---|---:|---:|---|
| New standard Chromium client/browser slot | Yes | No on its first launch | Creates one persistent Edge, Chrome, or Brave space from the Chromium Default template. |
| New standard Firefox slot | Yes | No on its first launch | Creates a clean independent Firefox profile; Chromium starter bookmarks and extensions are not copied into it. |
| Existing standard client/browser slot | Yes | Yes by default; optional per client/browser pair | Returns to that browser slot's normal saved state. |
| Temporary profile | No | No | One-off browsing that is removed after closing; only one may run across all browsers. |
| Default editor | Only if you approve saving it | No | Changes the sanitized starter for future Chromium client/browser slots and Chromium temporary profiles; only one editor may run across all browsers. |

## Temporary Browsing

Select the temporary profile button for a clean one-off browser space.

- Any previous temporary folder is removed before launch.
- In Edge, Chrome, or Brave, the temporary profile starts from the current Chromium Default template. In Firefox, it starts as a clean Firefox profile.
- It does not restore a previous session.
- Its folder is removed after the temporary browser closes.
- Only one temporary profile may be open at a time, even if the browser selector is changed.

Do not use the temporary profile for work that must remain available later.

## Choose A Browser

Select the browser control in the lower-left corner, or open Settings and then `Browser Selection`, and choose:

- Microsoft Edge.
- Google Chrome.
- Brave Browser.
- Mozilla Firefox.

Unavailable browsers are disabled in the menu. The selected browser is saved as the default for future launches, but each client keeps a different profile slot for each browser. Selecting Chrome and later Firefox for the same client does not copy or mix either browser's cookies, logins, history, bookmarks, extensions, or sessions. Both slots may be open together; selecting a client/browser pair that is already open switches to that existing session.

Existing clients created before the multi-browser layout keep their legacy Chromium profile in place. The first Chromium browser selected for that client is durably bound to the legacy root; private data is not moved. Firefox and additional Chromium browsers receive separate clean nested slots.

The Default profile editor is Chromium-only because `Default.7z` contains Chromium profile data. If Firefox is selected, ctSpaces asks you to select Edge, Chrome, or Brave before editing Default. Firefox client and temporary profiles are still supported and are created cleanly without copying Chromium starter data.

## Choose A Theme

Open Settings and select `Themes`.

- `System (Auto)` follows the Windows app theme.
- `System (Light, Default)` forces the light style.
- `System (Dark)` forces the dark style.
- The remaining entries are built-in color themes.
- `Dark - Gothic` is the featured dark style for fresh installations.
- `Dark - Crimson` provides the dedicated red-and-black style.
- Additional modern dark choices include Graphite Copper, Black Cherry, Midnight Moss, Ink Blue, and Violet Ash.

Selecting a theme previews it immediately. `Apply` saves it; `Cancel` returns to the previous theme. Existing installations keep their saved theme, while a fresh installation starts with Gothic.

## Client Icons

The selected client icon is stored once at the client root and shared by that client's browser slots. It appears in ctSpaces and on the client's browser windows and taskbar buttons.

### Set An Icon

1. Select a client.
2. Open Settings and choose `Set Profile Icon`.
3. Select an `.ico` file or another image format supported by Windows imaging.

Non-ICO images are converted into a multi-size Windows icon. If that client is already open, the browser and taskbar icon refresh without restarting the browser.

### Fetch An Icon From A Website

1. Select a client.
2. Open Settings and choose `Auto-fetch Icon`.
3. Enter a domain such as `example.com`. The field starts blank so ctSpaces does not guess a website from the client name.

ctSpaces downloads a 64-pixel website icon through Google's favicon service and converts it to `client.ico`. This action requires internet access.

### Remove An Icon

Choose `Remove Custom Icon` to delete `client.ico` and return to the browser's default icon.

## Profile Tools

Profile-management tools require a selected client. Whole-client actions such as Rename, Archive, and Delete require every browser slot for that client to be closed.

- **Reset (Nuke):** deliberately remains disabled and fails closed. Its selected-browser destructive behavior requires separate authorization before it can be enabled; it does not delete or recreate anything in the current build.
- **Vacuum (Clear Cache):** removes browser cache, code cache, GPU cache, service-worker cache, and shader cache. Cookies, history, and saved logins are preserved.
- **Rename Client:** changes a closed client's name while retaining its complete set of browser slots, shared customer icon, pinned position, browser-specific Restore tabs choices, and ctSpaces-managed client shortcut.
- **Archive Client:** hides a closed, inactive client from the normal list without deleting any browser slot or its browser-specific Restore tabs choices.
- **Archived Clients:** lists hidden clients and restores the selected client to the normal list.
- **Delete Profile:** permanently removes the entire selected client, including its legacy data and Edge, Chrome, Brave, and Firefox slots. ctSpaces revalidates the exact client path/layout and checks again that every browser slot is closed immediately before deletion. It removes only shortcuts verified as ctSpaces-managed and leaves unrelated shortcuts untouched.

Export a backup before Delete when the client may be needed again. Delete cannot be undone.

## Edit The Default Profile

The Default profile is the Chromium starter used for future Edge, Chrome, and Brave client slots and temporary profiles. It is not copied into Firefox profiles.

1. Open Settings and select `Edit Default profile`.
2. Add or change the bookmarks, extensions, and preferences that future Chromium slots should receive.
3. Close the Default browser window.
4. Choose whether to save those changes as the new starter.

When the Default profile is saved, ctSpaces keeps starter Bookmarks, intended extensions, a narrow set of preferences, and only favicon records exactly linked to those starter bookmarks. Unrelated favicon URLs, orphan rows, timestamps, and SQLite sidecars are removed. Bookmark backups, sign-ins, cookies, history, sessions, account state, local/site storage, caches, and other private browser databases remain excluded. If you choose not to save, or saving fails, the disposable editor profile is removed and the previously saved starter remains unchanged. Existing clients and all of their browser slots are never changed by editing the Default profile.

Only one Default editor may be open at a time. The editor supports Edge, Chrome, and Brave; if Firefox is selected, ctSpaces asks you to choose a Chromium browser first.

Do not sign into a real client account while editing the Default profile. Account data is not meant to become part of the starter.

## Back Up All Clients

1. Close every supported browser process using a ctSpaces client profile, including one started manually with a ctSpaces profile path.
2. Open Settings and select `Back Up All Client Data`.
3. Choose where to save the `.7z` backup.

Backup is blocked if ctSpaces detects any supported browser process using a profile below `Sites`, or if a relevant process cannot be inspected reliably.

The default filename includes the date and time. ctSpaces writes the backup to a temporary file, verifies its manifest and archive integrity, and only then places it at the selected location. The backup contains the full `Sites` collection and may contain sensitive client browser data. Store it appropriately.

## Restore A Backup

1. Close every supported browser process using a ctSpaces client profile, including one started manually with a ctSpaces profile path.
2. Open Settings and select `Restore Client Data`.
3. Select a validated ctSpaces `.7z` backup and confirm the restore. Older `.zip` backups can still be selected for compatibility.

Restore is blocked if ctSpaces detects any supported browser process using a profile below `Sites`, or if a relevant process cannot be inspected reliably.

Before replacing the current `Sites` folder, ctSpaces checks the archive paths and size, extracts into a staging folder, verifies the embedded manifest and profile count, and moves the current data to a timestamped `Sites_PreRestore_...` safety folder. A damaged or invalid backup never replaces the current `Sites` folder.

Browser security can tie some passwords or cookies to a Windows user or computer. A ctSpaces backup preserves the files but cannot guarantee that every protected sign-in will work on a different computer.

## Close ctSpaces

If no clients are open, closing ctSpaces exits immediately.

If clients are open, ctSpaces asks whether it should close all client browser windows and exit. Choosing Yes requests a normal browser shutdown so session and sign-in data can finish writing. Choosing No leaves ctSpaces and the clients open.

## Client Name Rules

- Leading and trailing whitespace is removed.
- Windows-invalid filename characters are removed: `\ / : * ? " < > |`.
- Trailing periods and spaces are removed.
- Windows-reserved names such as `CON`, `PRN`, `AUX`, `NUL`, `COM1`, and `LPT1` are not accepted.
- `Default` and `Temp` are reserved internal names.
- Client names are treated case-insensitively.
- A new client, rename target, or restored client must leave enough room under the current ctSpaces data-folder path for every managed browser slot. This limit is path-dependent, so an extremely long otherwise-valid name is rejected with a request to use a shorter name.

Use clear names that remain unique after invalid characters are removed.

The broader syntax check remains compatible with older profile and backup names. When an historical path is already beyond what this Windows build can inspect safely, ctSpaces fails the affected filesystem operation closed instead of partially changing or omitting that client.

## Where Data Is Stored

The installed app and shared settings are stored under:

```text
%LOCALAPPDATA%\InfinitySys\ctSpaces
```

Each standard client has a container under:

```text
%LOCALAPPDATA%\InfinitySys\ctSpaces\Sites\<ClientName>
```

New-format browser profiles are stored in that container under `Browsers\edge`, `Browsers\chrome`, `Browsers\brave`, and `Browsers\firefox`. A pre-existing legacy Chromium profile may remain at the client root and is bound in place rather than moved.

See [Data, Backups, and Privacy](DATA_BACKUP_AND_PRIVACY.md) for the complete folder guide and safety notes.
