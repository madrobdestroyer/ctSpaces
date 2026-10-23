# ctSpaces Feature Reference

Applies to ctSpaces 6.3.5.0. The [User Guide](USER_GUIDE.md) explains everyday
workflows; [release validation](RELEASE_VALIDATION_6.3.5.0.md) records completed
checks and known limits. Historical release reports retain their original scope.

## Core Purpose

ctSpaces launches independent browser profiles for each client/browser pair. It provides persistent separation between client accounts and also lets one client keep independent Edge, Chrome, Brave, and Firefox spaces.

## Supported Browsers

| Browser | Detection | Notes |
|---|---|---|
| Microsoft Edge | Standard Program Files locations | Default selection when available. |
| Google Chrome | Program Files and current-user installation locations | Uses an independent Chromium slot. |
| Brave Browser | Program Files and current-user installation locations | Uses an independent Chromium slot. |
| Mozilla Firefox | Program Files and current-user installation locations | Uses an independent Firefox profile with `--no-remote --new-instance`. |

The browser menu disables entries that are not found. If the saved browser is unavailable, ctSpaces falls back to another detected browser. The saved selection is the default for the next launch; it does not cause clients to share browser data.

## Launch Behavior

Chromium browsers are launched with a dedicated `--user-data-dir`, first-run UI disabled, browser sync disabled, and sync-promotion UI disabled. Firefox is launched with its dedicated `--profile`, `--no-remote`, and `--new-instance` arguments.

| Profile | Data directory | Session behavior | Cleanup behavior |
|---|---|---|---|
| Standard client | New layout: `Sites\<ClientName>\Browsers\<browser>\Profile`; a bound legacy Chromium profile may remain at the client root | Existing slots request last-session restore by default. `Restore tabs` can disable it per client/browser pair. New slots start fresh. | Retained until whole-client Delete. |
| Default editor | `Default` | Chromium-only; never restores a prior session. | One singleton for Edge, Chrome, or Brave; Firefox selection is rejected with guidance to choose a Chromium browser. Removed after close. Approved sanitized changes are saved into `Default.7z`. |
| Temporary | `Temp` | Always starts fresh. Chromium uses `Default.7z`; Firefox uses a clean Firefox profile. | One singleton across all browsers; removed before launch and after close. |

New Edge, Chrome, and Brave slots, the Default editor, and Chromium temporary profiles are created from the current `Default.7z` starter template. New Firefox slots and Firefox temporary profiles are clean Firefox profiles and do not receive Chromium starter bookmarks, extensions, or preferences.

Each new client root carries the v2 client marker and can hold separate `edge`, `chrome`, `brave`, and `firefox` slots below `Browsers`; slots are created as their browsers are first used. A legacy client root is never moved merely to adopt this layout. Its existing Chromium data is durably bound in place to the first selected Chromium browser; if Firefox is selected first, Firefox receives a clean nested slot and the legacy root remains unbound until a Chromium browser is selected. Every additional browser receives its own clean nested slot.

## Main Launcher Controls

| Control | Behavior |
|---|---|
| `New` tab | Selects launch mode for opening another client. |
| Client tab | Displays only the client name for an open client/browser pair, changes the main action to `Show`, and can be dragged to reorder open sessions for the current launcher session. |
| Client tab close icon | Requests a normal close of that client's browser window. |
| Overflow button | Lists hidden client tabs and provides separate show and close commands. |
| Pinned client shortcut | Clicking opens the client or switches to its existing tab. Dragging reorders visible pins and saves the order. The context menu offers Select, Open, Open copied link, Restore tabs, Create desktop shortcut, and Client Notes. |
| Pinned overflow | Lists pinned clients beyond the four compact visible shortcuts. Right-click selects a hidden favorite for editing without opening it. |
| Client combo | Filters existing names by a case insensitive substring without completing or replacing typed text. Explicitly selecting a match fills its full name. Enter runs the primary action for the exact current name. |
| Close all | Requests normal closure of tracked client browsers while keeping the launcher open. Delayed or refused closes remain tracked and are not silently forced. |
| Notes | Opens the selected client's notebook or returns to the remembered notebook without launching a browser. Its independent window has one searchable client picker. |
| Integrated client logo | Displays the selected client's `client.ico` inside the editable selector; clicking the logo opens the profile folder. |
| Pushpin toggle | Adds or removes an existing client from the Pinned row and is disabled for a new client name. |
| `Create` | Creates and launches a new client. |
| `Open` | Launches an existing client. |
| `Show` | Brings the selected open client's browser window forward. |
| `Restore tabs` | Saves a per-client/browser choice for that slot's next launch. Off starts on a normal new tab without clearing any persistent profile data. |
| Temporary-profile icon | Opens the disposable `Temp` profile. |
| Settings icon | Opens the full configuration and profile-tools menu. |
| Browser selector | Displays and directly changes the default browser for the next launch. Choosing another browser selects that client's independent slot. |

`Options > Quick tour` opens a manually replayable 23 step overlay with 13
inert workflow illustrations that
highlights these real controls and illustrates existing workflows without operating them. Its owned themed callout
supports Back, Next, Skip, Done, Escape, and close while the launcher is
disabled. It preserves the current client and browser selections, pins,
sessions, Restore tabs preferences, and client data. Empty pin and session
states point to the actual pushpin or New tab without creating fake data. The
folder step uses the visible CLIENT field when the folder icon is hidden, and
explains that an existing client must be selected first. The
same tour can be started from the full guide or fresh-user welcome, but it is
never started automatically and does not mark guide topics as read. Its inert
illustrations cover browser-slot choice, pinned-menu actions, copied links, both
shortcut paths, pin/session reordering, rename, archive/restore, client-first
titles, Default save/discard, inactive cleanup, and manual bulk deletion; the tour
does not read the clipboard, create clients, launch browsers, reorder state, or
run cleanup or deletion.

Pinned client names and their order are stored in `config.ini`, survive launcher restarts, and are pruned automatically only after the client's container is conclusively absent. Up to eight may be saved; four are shown directly in a slim row to keep the launcher compact. Per-client/browser tab-restore exceptions are stored separately under `[restore_tabs]` using browser-qualified keys and use the same conclusive-existence rule. An unreadable or indeterminate configuration/profile state aborts pruning instead of being treated as empty. Closing the browser window directly releases the selected profile process so a later ctSpaces launch can restore its saved tabs.

Configuration mutations are written to a unique sibling stage and committed atomically. Config-file inspection distinguishes Regular, Missing, and Unavailable; client-directory probes distinguish Present, Missing/invalid, and Indeterminate. Operations stop on an unavailable/indeterminate state rather than overwriting or partially pruning saved state.

Dropping a visible pinned client on the Windows Desktop creates one `.lnk` launcher for that client. The same action is available in the pinned right-click menu and Settings. Its visible Desktop label is exactly the client name (`Client.lnk`). The link targets the current ctSpaces executable, records the browser selected when it was created through validated `--client` and `--browser` arguments, and uses `Sites\<ClientName>\client.ico` when available. Recreating it after choosing another browser updates that same verified ctSpaces-managed link rather than adding a second browser-named shortcut; unrelated shortcuts remain untouched. This one-link Desktop policy does not limit independent browser slots or simultaneous different-browser sessions for the client inside ctSpaces. A shortcut launch hands the request to an already-running copy of the same executable through bounded `WM_COPYDATA` instead of opening another launcher.

`Open copied link` accepts only a complete HTTP or HTTPS URL from Unicode clipboard text. The URL is validated with the Windows URL parser and safely quoted before launch. It opens in the selected client/browser slot when that slot can accept the request, or accompanies that slot's normal launch and saved Restore tabs choice when closed. An already-running isolated Firefox slot cannot reliably accept a second command-line URL, so ctSpaces explains that boundary instead of starting another writer.

## Settings Menu

| Command | Selection required | Can run while selected client is open | Result |
|---|---:|---:|---|
| Set Profile Icon | Yes | Yes | Copies an ICO or converts another supported image into `client.ico`. |
| Remove Custom Icon | Yes | Yes | Deletes the custom client icon. |
| Create Desktop Shortcut | Existing client | Yes | Creates or updates the client-named Desktop link for the browser currently selected, using the customer icon when available. |
| Client Notes | Existing client | Yes | Opens that client's themed notebook without requiring a browser launch. Blank Untitled notes are usable immediately; ticket metadata is optional. Changes autosave quietly, with problems reported. The independent window remembers its bounds and maximized state. |
| Tab layout - all clients / this client | A client is needed only for an override | Yes, for the next idle launch | Keeps the browser's choice, requests vertical tabs, or explicitly requests horizontal tabs. A client override follows that client through its lifecycle. Restored Edge windows can retain their saved layout. |
| Rename Client | Existing client | No | Renames the complete client container and migrates its pinned entry, browser-specific Restore tabs choices, shared icon, and ctSpaces-managed client shortcut. |
| Archive Client | Existing client | No | Hides the client from the normal list without removing profile data. |
| Archived Clients | No | Yes | Restores a hidden client to the normal list. |
| Reset (Nuke) | Yes | No | Disabled and fail-closed pending separate authorization for selected-browser destructive behavior. |
| Vacuum (Clear Cache) | Yes | No | Removes selected cache directories while preserving logins, cookies, and history. |
| Auto-fetch Icon | Yes | Yes | Downloads a favicon for a supplied domain and creates `client.ico`. |
| Delete Profile | Yes | No browser slot for that client may be open | Revalidates and permanently removes the entire client container, including every browser slot, after explicit confirmation. |
| Clean Up Inactive Clients... | No | Open clients are skipped | Previews clients not opened in three calendar months, including archived clients, then permanently deletes the listed whole clients after explicit acknowledgement. |
| Delete Multiple Clients... | No | Open clients are skipped | Select several closed clients for immediate whole-client deletion, regardless of activity date. Includes archived clients and requires explicit acknowledgement. |
| Edit Default profile | No | Only one editor; Edge, Chrome, or Brave must be selected | Opens the future Chromium-slot starter for editing. Firefox profiles use a separate clean creation path. |
| Themes | No | Yes | Previews and saves the launcher theme. |
| Client name first in window titles | No | Yes | Toggles whether ctSpaces browser titles begin with the client name for easier Alt+Tab identification and refreshes already-open client windows. |
| Browser Selection | No | Yes | Changes the saved default browser for the next launch; each client/browser slot remains independent. |
| Back Up All Client Data | No | Selected client profiles must be closed; indeterminate relevant process inspection also blocks it | Starts with every client checked, including archived clients. Choose which clients to include in the verified manifested `.7z` backup. Excluded clients may remain open. |
| Restore Client Data | No | No supported browser process may be using a profile below `Sites`; indeterminate inspection also blocks it | Validates and stages a backup before transactionally replacing `Sites`; older ZIP backups remain supported. |
| Quick tour | No | Yes | Highlights real launcher controls in a read-only, manually navigated overlay without performing their actions. |
| About | No | Yes | Shows product, version, author, and project information. |

## Client Notes

The launcher **Notes** button sits beside **Close all**. Notes has its own
taskbar window and remains usable when the launcher is minimized or sent to the
tray. Switching clients saves the original notebook and reuses the same window,
preserving its position, size and maximized state. Each notebook stays with its
client through rename, client archive and backup; whole-client deletion removes
that client's notes without merging them with another client's notebook.

The editor has slim reorderable tabs and one compact row for **Find**,
**Snippets**, **Templates**, **Add update**, **Format** and **Details**. Text
selection or Format opens an on-demand formatting palette. There is no permanent
formatting toolbar, Save/Cancel footer, or normal Saved/Saving indicator. Save,
load and invalid-details problems remain visible until resolved.

**Details** edits optional ticket information, local work status and tags.
The small status icon and label beside it are read-only, and No status hides
them. Status does not synchronize with a ticket system. Templates and snippets
each combine creation, editing, saving and use in their own shared library;
they use explicit Save buttons while client notes autosave.

All note and ticket URLs open, after confirmation, in the chosen regular PC
browser session outside client profiles. Choose **Details > Open links in** or
**More > Link browser**: System default, or an installed Edge, Chrome, Brave or
Firefox. This one Notes preference applies to every client notebook and does not
change the launcher browser or Windows default. An unavailable chosen browser
is reported instead of silently falling back.

The notebook limit is **10,000 notes per client**, counting closed, archived and
trashed notes, and **256 MiB** including history. Shared template and snippet
libraries have separate **256-entry** limits. Trash has no automatic expiry;
Restore recovers a note, while Delete forever and Empty trash require explicit
confirmation. See the [Notes walkthrough](NOTES_WALKTHROUGH.md) and
[productivity guide](NOTES_PRODUCTIVITY.md) for recovery, reusable content,
search, sorting, bulk actions and handover exports.

## Browser Tab Layout

Settings > **Tab layout - all clients** offers Keep browser's choice (the
default), Vertical tabs and Horizontal tabs. The selected client's **Tab layout
- this client** menu, also available from its pinned context menu, offers Follow
global setting or an explicit vertical/horizontal override. Horizontal explicitly
requests vertical tabs off. Keep releases enforcement; it does not restore an
earlier layout. Temporary profiles follow the global setting; the Default editor
is excluded. Firefox also removes only ctSpaces' own `user.js` override when
enforcement is released.

These are next-launch native preferences, not commands to change an open or
restored browser window. Edge can retain a saved session's layout when Restore
tabs is on. To establish a new layout, preserve needed tabs, close that client's
Edge window normally, select the same client/browser, turn Restore tabs off,
choose the desired layout and launch. See the [User Guide's layout workflow](USER_GUIDE.md#choose-vertical-or-horizontal-browser-tabs)
for the restore boundary and the scope of completed checks.

## Inactive Client Cleanup

Available from the Options (gear) button as `Clean Up Inactive Clients...`.
Both cleanup tools first show a themed progress dialog while inspecting
clients in the background. Cancel, Escape, or closing that dialog stops the
inspection safely without deleting clients or showing a partial selection
list. Large collections can still take time to inspect, but the interface
remains responsive. This cancellation applies before the selection preview,
not to an actual deletion after confirmation.

The preview has a scrollable multi-selection client/date list, Select All and
Clear Selection, an acknowledgement checkbox, a `Delete Selected (count)`
button, and Cancel (the default action). Click a row to toggle selection; Ctrl
is not required. Eligible inactive clients start selected, but can be deselected.
Nothing is
deleted just by opening the preview; cleanup is manual, not scheduled.

`Options > Delete Multiple Clients...` uses the same selection screen without
the three-month activity requirement. It lists safe, closed clients, including
archived clients, and starts with nothing selected. Recent clients, clients
with only an upgrade baseline, and clients with unverified activity dates can
be selected for immediate deletion. Only selected clients are deleted, and
the same exact-path/tree/layout/OS-process protections are repeated before each
removal. The inactivity-specific timestamp/cutoff check is not applied to this
explicit manual selection. Default/Temp and unsafe/open clients remain excluded.

Inactive cleanup uses the broom icon rather than the Delete Profile trashcan.
About credits Cameron Kincer for the client cleanup idea.
Both selection screens and cleanup results/no-client/error screens use the
selected app palette for window chrome, backgrounds, text, list selection,
checkbox, borders, and buttons. Completion details are scrollable read-only text
with preserved paragraph breaks, no initial selection, and no scrollbar when
the text fits. Nothing-to-delete notices use a compact themed message instead
of a large details panel. The broom glyph uses the current menu text color,
including the selection color, for light/dark contrast. Checkbox state, selection,
acknowledgement, and default Cancel behavior are unchanged.

Opening a client in any supported browser records one durable, shared client
activity date at `Sites/<Client>/ctSpaces-client-activity`. It travels with a
rename and is removed with the client. The date is saved immediately before
starting a browser; a failed launch can conservatively extend retention, never
shorten it. If tracking cannot be saved, the launch is blocked with an error.
Default and temporary launches do not create client activity records.

Three calendar months are subtracted in UTC, with month-end clamping (May 31
to February 28/29). Clients at or before the cutoff are eligible. Existing or
imported clients without history start a new three-month grace period when
first discovered by this version; NTFS folder dates are never used to guess
prior activity. Corrupt/unreadable records and future-dated activity are not
eligible. Archived clients are included, not exempted.

Before each deletion, cleanup freshly rechecks the exact activity record,
eligibility, recognized client layout, complete tree for reparse points, and
tracked/OS-level browser use. Changed, open, or unverifiable clients are
skipped. Confirmed deletion removes all browser slots, client files/icons,
activity records, verified managed Desktop shortcuts, and prunes saved client
preferences. Unrelated shortcuts and separately exported backup files are
not erased. A partial deletion or shortcut removal failure is reported.

## Vacuum Details

Vacuum targets these locations under the browser's `Default` directory when present:

- `Cache`
- `Code Cache`
- `GPUCache`
- `Service Worker\CacheStorage`
- `Service Worker\ScriptCache`
- `DawnCache`
- `GrShaderCache`
- `ShaderCache`

It does not intentionally delete cookies, history, login databases, bookmarks, sessions, or client icons.

## Reset And Delete Boundaries

Reset is deliberately disabled and fails closed. Its intended selected-browser destructive scope is separate from whole-client Delete and must not be enabled without separate authorization and validation.

Whole-client Delete performs a fresh safe-path lookup, requires an exact recognized v2 or hybrid legacy layout, rejects Default/Temp and any reparse point in the tree, confirms every browser slot is closed, presents an explicit irreversible all-browser warning, then repeats validation and the all-browser activity check immediately before removal. Success is reported only after filesystem absence is proven. Shortcut cleanup removes only links whose target, executable, and arguments verify that ctSpaces manages them; unrelated shortcuts are preserved.

## Default Profile Contents

When Default changes are approved, ctSpaces keeps starter-safe browser material such as:

- Bookmarks (not `Bookmarks.bak`).
- Narrowly allowlisted Preferences and Secure Preferences fields.
- Installed extension packages and allowlisted install metadata needed by the starter.

It retains only favicon mappings whose page URLs exactly occur in sanitized Bookmarks and their referenced icon/bitmap rows. Orphan rows, unrelated URLs, compatible timestamps, freelist pages, and SQLite sidecars are rejected or removed. Other login/account data, history, sessions, cookies/network data, local storage, IndexedDB, WebStorage, cache data, and extension runtime/site settings are excluded.

The Default editor affects only Chromium profiles created afterward. It never rewrites existing clients and is never copied into Firefox profiles.

## Client Icons

- One custom icon is stored as `Sites\<ClientName>\client.ico` and shared by that client's browser slots.
- ICO files are copied directly.
- Other image formats recognized by Windows GDI+ are converted to a multi-size icon.
- Auto-fetch uses `https://www.google.com/s2/favicons` and requires internet access.
- New icons are staged and validated before `client.ico` is atomically replaced.
- The icon watcher detects `client.ico` size or write-time changes.
- Open browser windows receive small and large icons through `WM_SETICON`.
- A stable launcher AppUserModelID keeps ctSpaces separate from browser buttons, while each per-client/browser AppUserModelID keeps browser groups separate.
- Each distinct icon content hash gets a taskbar resource filename so Explorer cannot reuse stale icon pixels.
- ctSpaces sets the relaunch-icon resource and recreates the open taskbar button after an icon change.
- Client tabs intentionally do not display icons.

## Themes

ctSpaces includes:

- System Auto, System Light, and System Dark modes.
- A catalog of built-in named color themes.
- Immediate preview in the Themes dialog.
- Apply and Cancel behavior.
- Saved theme selection in `config.ini`.
- DPI-aware launcher, menus, controls, and dialogs.
- Text-field right-click/keyboard menus use the app palette and show only
  normal editing actions; read-only details offer Copy and Select All, without
  Windows' reading-order, Unicode-control or IME menu entries.
- Themed About content/link/buttons, app-owned messages and confirmations,
  pinned/session overflow menus, and tooltip colors. Windows retains native
  message-box button results, default-button and cancellation behavior.
- Windows-owned file pickers and early fatal errors before theme initialization
  keep Windows styling. Required list/detail scrollbars use light/dark control
  styling; short cleanup results hide their scrollbar entirely.

## Backup And Restore

### Export

- Starts with every client selected, including archived clients. Search and Select shown/Clear shown make a smaller backup possible without changing any client's data.
- Requires the selected clients' browser profiles to be closed. An unreadable or indeterminate relevant process blocks export; excluded clients may remain open.
- Repeats the selected clients' external profile-use check immediately before archive creation.
- Uses the bundled in-process 7-Zip engine.
- Adds a root manifest and top-level `Sites` folder containing the selected complete client containers, including their Notes and browser data.
- Fails if any source file is skipped, then inspects and CRC-tests the staged archive before saving it atomically.
- Suggests a timestamped `ctSpaces_Backup_...7z` filename.

### Restore

- Requires every supported browser process using a profile below `Sites` to be closed. An unreadable or indeterminate relevant process blocks restore.
- Inspects current `.7z` backups for unsafe paths, excessive item counts, expanded size, and available disk space.
- Rejects traversal, absolute/device paths, case-insensitive duplicates, links/reparse points, unexpected item types, and unsafe declared sizes before committing extracted data.
- Extracts into a temporary folder while checking archive integrity, then validates the ctSpaces manifest and profile count.
- Accepts older ZIP backups through a Windows PowerShell compatibility path and normalizes them into the same staging layout.
- Moves the current `Sites` folder to `Sites_PreRestore_<timestamp>` before replacement.
- Attempts rollback if the folder swap fails.

## Installation And Updates

- Per-user install under `%LOCALAPPDATA%`; administrator rights are not required for the normal path.
- Optional Start Menu, Desktop, and current-user startup entries.
- A short display label `6.3`, backed by the current four-part Windows version `6.3.5.0` used for install/update comparisons.
- Newer, same-version, and older external copies have distinct prompts.
- Updates stage the new executable beside the installed copy and retry replacement.
- Relaunch waits for the updater process to exit before acquiring the single-instance mutex.
- A `ctSpaces.portable` marker bypasses install/update handoff for controlled testing only.

## Session And Shutdown Handling

- Open clients are tracked in a case-insensitive `(client, browser)` active-profile map, allowing different browsers for one client while preventing two writers to the same slot. The launcher tab intentionally displays only the client name; this does not collapse the underlying browser-qualified identity.
- A background watcher updates client icons and normalizes Edge, Chrome, Brave, and Firefox titles with the client name. The saved title option chooses client-first or page-first order and applies to windows that are already open.
- A reaper thread notices when each launched browser process exits.
- Closing ctSpaces with active clients asks permission to close them normally.
- The launcher exits after all requested browser closes complete.

## Current Boundaries

- The browser selector is a saved global default for the next launch, while every client keeps independent per-browser profile slots.
- Backup can include all or selected clients. Restore replaces the complete current `Sites` collection with the archive's contents; it does not merge selected clients into the live collection. Current data is retained in a pre-restore recovery folder first.
- There is no built-in uninstaller or automatic internet update checker.
- There is no profile search, tagging, or size display. Client activity dates are recorded for manual three-month cleanup; the normal selector does not display them.
- Profile separation is organizational browser-profile isolation, not an operating-system sandbox, encryption boundary, or malware isolation.
- Browser-protected credentials may not remain usable after moving a profile to another Windows account or computer.
- Auto-fetch depends on an external favicon service.
