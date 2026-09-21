# Data, Backups, and Privacy

Applies to ctSpaces 6.0.

## What Profile Separation Means

ctSpaces gives each client independent Edge, Chrome, Brave, and Firefox browser profiles. A client can remain signed in to its own accounts without sharing cookies, history, bookmarks, extensions, site storage, or sessions with another client or with another browser slot for that same client.

This is browser-profile separation. It is not Incognito mode, encryption, an operating-system security sandbox, or protection from an unsafe website, extension, process, or Windows user. Anyone who can access the Windows account and its files may be able to access the profile data.

## Data Root

ctSpaces stores its installed app, shared settings, templates, and profile data under:

```text
%LOCALAPPDATA%\InfinitySys\ctSpaces
```

The important items are:

| Item | Purpose | Lifetime |
|---|---|---|
| `ctSpaces.exe` | Installed per-user application | Until manually removed or updated |
| `config.ini` | Default browser/theme, pin order, per-client/browser Restore tabs exceptions, archive list, and title preference | Retained across updates; mutations are staged and atomically committed |
| `Sites\<ClientName>` | Persistent client container holding every browser slot | Until whole-client Delete or full removal |
| `Sites\<ClientName>\Browsers\<browser>\Profile` | Independent Edge, Chrome, Brave, or Firefox profile in the v2 layout | Retained with that client |
| `Sites\<ClientName>\client.ico` | Optional custom client icon | Retained with that client |
| `Default.7z` | Sanitized Chromium starter copied into new Edge, Chrome, and Brave profiles | Retained and revisioned |
| `default-template-revision.txt` | Installed starter-template revision | Retained across launches |
| `Default` | Temporary working folder for the Chromium Default editor | Recreated for one singleton Edge, Chrome, or Brave editing session; Firefox selection is not accepted |
| `Temp` | Disposable browser profile | One singleton across all browsers; removed before and after use |
| `_DefBak` | Sanitizer-produced and integrity-tested starter recovery archives | Retained for recovery |
| `Restore_Temp_<timestamp>` | Restore extraction staging area | Normally removed after restore |
| `Sites_PreRestore_<timestamp>` | Entire prior `Sites` folder preserved before restore | Retained for recovery |

Existing legacy Chromium clients are not moved into the v2 tree. Their original client root is durably bound to the first selected Chromium browser and keeps all private data in place. If Firefox is selected first, it receives a clean nested slot and the legacy root remains unbound until a Chromium browser is selected. Additional browsers always receive separate clean nested slots.

## What A Client Profile Contains

A normal client profile may contain sensitive browser data, including:

- Cookies and active website sessions.
- Saved sign-in information and browser-protected credentials.
- Browsing and download history.
- Bookmarks and favicon databases.
- Extensions and extension settings.
- Local storage, IndexedDB, service-worker data, and cached files.
- Session files used to reopen browser tabs.
- The optional `client.ico` identity icon.

Normal updates replace the app executable and may revise the new-client starter. They do not rewrite existing folders under `Sites`.

## Rename, Archive, And Shortcuts

Renaming is available only while every browser slot for that client is closed. It moves the complete client container and all browser slots to the new client name and updates ctSpaces references; it does not rebuild or sanitize any browser profile. The custom icon moves with the folder.

Archiving changes only ctSpaces organization data. The client remains under `Sites`, including all browser slots, its shared icon, and browser-specific Restore tabs choices, but is omitted from the normal client list until restored from Settings. `Delete Profile` is the separate destructive action.

Delete is a whole-client action. After an explicit warning, ctSpaces freshly revalidates the exact client path and recognized v2/hybrid layout, rejects Default/Temp and reparse points, checks every browser slot again, and removes the entire client container. It reports success only after proving the path is absent. Cleanup removes only Desktop shortcuts verified as ctSpaces-managed; unrelated shortcuts are left untouched. Reset is separately scoped to a selected browser and remains disabled/fail-closed pending separate authorization.

A client Desktop shortcut has the exact visible client label (`Client.lnk`) and contains the ctSpaces executable path, client and selected-browser arguments, description, and client icon path. There is one managed Desktop link per client; recreating it after selecting another browser updates that link. This does not merge or limit the client's isolated browser slots or simultaneous different-browser sessions inside ctSpaces. The shortcut does not contain cookies, credentials, history, or bookmark contents.

## Default Profile

`Default.7z` is copied only when ctSpaces creates a new Edge, Chrome, or Brave slot, the Chromium Default editor, or a Chromium temporary profile. New Firefox slots and Firefox temporary profiles are created cleanly through Firefox's own profile path and do not receive Chromium starter bookmarks, extensions, or preferences. Editing Default affects future Chromium profiles only. Only one Default editor may run at a time, and ctSpaces asks for Edge, Chrome, or Brave when Firefox is selected.

Before saving a changed Default profile, ctSpaces keeps only Bookmarks, a sanitized `Favicons` main database, narrowly allowlisted Preferences/Secure Preferences fields, and intended extension packages/install metadata. The closed favicon database is checked with Windows SQLite, reduced to page URLs that exactly occur in the sanitized Bookmarks file, stripped of orphan icon records and timestamps, vacuumed, and integrity-checked. SQLite journal/WAL/SHM files are never packaged. If that proof cannot be completed, saving fails. `Bookmarks.bak`, cookies, logins, account/sign-in state, history, sessions, network data, site storage, IndexedDB, WebStorage, caches, and extension runtime/site settings remain excluded. Existing `Sites` clients keep their own browser-created databases; starter sanitization never repairs, injects, or rewrites their favicons.

Do not sign in to a client account while editing Default. Never distribute an old or locally captured `Default.7z` unless it has passed the release safety test.

Revision 4 and later place only sanitizer-produced, integrity-tested archives in `_DefBak`. Older `_DefBak` files are inactive and are never launched or extracted automatically, but they may contain local starter/browser data from an earlier version. Keep them private and inspect or sanitize them manually before recovery use.

The starter archive wrapper rejects traversal and absolute/device paths, case-insensitive duplicate entries, reserved Windows names, archive links, extracted reparse points, non-file entries, and excessive declared entry/total sizes. It sanitizes in a sibling staging area and atomically commits only the verified result, so a failed in-place update leaves the prior archive unchanged.

## Temporary Profile

The temporary profile is removed before each launch and again after the browser closes. Chromium temporary profiles start from the sanitized Chromium starter; Firefox temporary profiles start as clean Firefox profiles. Neither restores a previous session, and only one temporary profile may run at a time across all browsers.

Temporary browsing is not guaranteed secure deletion. Browser processes, antivirus tools, Windows indexing, crash files, or storage behavior can delay deletion or leave recoverable disk traces. Use it for convenience, not for handling data that requires forensic-grade removal.

## Export All Data

`Back Up All Client Data` creates a validated 7-Zip archive containing the complete `Sites` folder and a ctSpaces manifest.

- Every supported browser process using a profile below `Sites` must be closed first, including one started manually with a ctSpaces profile path. An unreadable or indeterminate relevant process blocks export, and ctSpaces repeats the check immediately before archive creation.
- The `.7z` backup contains all clients; selective export is not currently available.
- Hidden profile files and files larger than 2 GB are supported by the built-in archive engine.
- ctSpaces rejects skipped or unsafe source items, verifies the completed archive's paths, item types, sizes, and CRCs, and atomically installs the finished destination file.
- The backup is not encrypted by ctSpaces.
- The backup may contain active sessions, history, bookmarks, saved credentials, and client-identifying information.
- The suggested filename includes a timestamp.

Store backup files in an approved protected location. Do not send them through ordinary chat or email unless your organization explicitly permits that handling.

## Restore Data

`Restore Client Data` accepts current manifested `.7z` backups. It also accepts older `.zip` backups through a backward-compatible legacy path.

The restore process:

1. Requires every supported browser process using a profile below `Sites` to be closed; an unreadable or indeterminate relevant process blocks restore.
2. Inspects a current backup for traversal/absolute/device paths, duplicates, unsafe item types, unreasonable item counts and declared sizes, and available staging space.
3. Extracts into `Restore_Temp_<timestamp>` while checking archive integrity.
4. Verifies the ctSpaces manifest and profile count.
5. Moves the current `Sites` folder to `Sites_PreRestore_<timestamp>`.
6. Moves the staged data into place and rolls back the folder swap if replacement fails.

The pre-restore folder is intentionally retained. After the restored clients have been checked, a maintainer can archive or remove old pre-restore folders according to the organization's retention policy.

## Moving To Another Computer

A backup preserves browser files, but Chromium and Windows can protect credentials with keys tied to a Windows account or computer. After moving a backup:

- Bookmarks, icons, extensions, and much profile state should copy normally.
- Some cookies, passwords, or sign-ins may no longer decrypt.
- A user may need to sign in again.
- Browser-version or policy differences can affect restored extensions and settings.

Keep the original backup until the moved profiles have been checked.

## What ctSpaces Sends Over The Network

ctSpaces does not contain an analytics or telemetry client. Normal website traffic, including a copied link opened for a pinned client, belongs to the selected browser.

The `Auto-fetch Icon` command sends the domain entered by the user to Google's S2 favicon service and downloads the returned image. Opening the project link in About launches that link in the normal Windows browser.

## Recovery Order

When data appears missing, stop making destructive changes and check in this order:

1. The current `Sites\<ClientName>` folder.
2. A recent `Sites_PreRestore_<timestamp>` folder.
3. A user-created ctSpaces `.7z` backup or an older backup ZIP.
4. `_DefBak` only when recovering the new-client starter, not a client profile.

Reset is disabled and is not a recovery tool. Delete Profile and manual folder deletion are irreversible; make a copy or export first when the data may still be needed.

## Configuration Safety

Config-file inspection distinguishes Regular, Missing, and Unavailable results. An unreadable, unsafe, directory, or reparse-point destination is Unavailable and is not treated as an empty configuration. Client-directory probes separately distinguish Present, Missing/invalid, and Indeterminate. Pin, archive, and Restore-tabs pruning aborts as an all-or-nothing operation on an unavailable/indeterminate state. Every settings mutation is written to a unique sibling stage and committed atomically, so a staged-write failure preserves the original bytes.
