# ctSpaces

ctSpaces is a small Windows launcher for opening client-specific browser spaces. Each client can have an independent Microsoft Edge, Google Chrome, Brave, and Mozilla Firefox profile, so browser data stays separated both between clients and between browsers for the same client.

Version 5.3 adds saved drag ordering, client desktop shortcuts, open-copied-link actions, safe rename and archive tools, optional client-first browser titles, and all of the compact launcher, profile safety, backup/restore, icon, and theme improvements from 5.2.

Maintenance build **5.3.0.10** adds a skippable, replayable guided walkthrough
and per-feature update guidance while retaining the 5.3.0.9 safety fixes.
See [release notes](RELEASE_NOTES_5.3.0.10.md),
[guide help](docs/GUIDED_WALKTHROUGH.md), and the
[current adversarial validation](docs/RELEASE_VALIDATION_5.3.0.10.md).

This is a maintained fork of the
[original ctSpaces project](https://github.com/BiatuAutMiahn/ctSpaces) by
BiatuAutMiahn. Fork maintenance does not imply changes to or endorsement by the
original project.

## Download

Download `ctSpaces5.3.0.10.zip` from the
[latest GitHub release](https://github.com/madrobdestroyer/ctSpaces/releases/latest),
extract it to a folder, and run `ctSpaces.exe`. The archive includes the user
documentation and applicable license notices.

## What It Is Used For

Use ctSpaces when you need to jump between multiple client browser sessions without mixing accounts or browser state. Type or select a client name, choose a browser, and ctSpaces launches that client/browser pair with its own profile data. The same client may have Chrome and Firefox open at the same time without sharing their profile folders.

This is browser-profile isolation for workflow separation. It is not an operating-system security sandbox, encrypted container, or boundary against a malicious site, extension, process, or Windows user.

Each client has one container under:

```text
%LOCALAPPDATA%\InfinitySys\ctSpaces\Sites\<ClientName>
```

New-format clients keep independent profiles below `Browsers\edge`, `Browsers\chrome`, `Browsers\brave`, and `Browsers\firefox`. Existing legacy Chromium roots are bound in place to the first Chromium browser that uses them; ctSpaces does not move their cookies, logins, history, or other private data during the upgrade. Other browsers receive clean nested slots.

The app itself installs and stores shared settings under:

```text
%LOCALAPPDATA%\InfinitySys\ctSpaces
```

## Features

- Optional first-run walkthrough, replayable from Options or F1, with direct topic selection and independently tracked new-feature guidance. Existing users and client-shortcut launches are not interrupted by an automatic tour.
- Client profile launcher with separate data for every client/browser pair.
- Persistent sign-ins, cookies, bookmarks, history, site data, and sessions for each saved client.
- Per-client/browser `Restore tabs` switch. Leave it on to reopen that browser slot's previous tabs, or turn it off to start that slot on a normal new tab while keeping sign-ins and other profile data.
- Browser selector for Microsoft Edge, Google Chrome, Brave, and Mozilla Firefox. Choosing another browser opens that client's independent slot rather than reusing another browser's data.
- Pinned favorite clients with customer logos, one-click open/switch behavior, and saved drag ordering.
- Pinned-client right-click actions for selection, tab restore, opening a copied website link, and creating one customer-icon Desktop shortcut per client. Its visible label is exactly the client name (`Client.lnk`), it records the browser selected when created, and recreating it updates that managed link.
- Drag a pinned client onto the Windows Desktop to create its shortcut directly.
- Separate taskbar identities for the ctSpaces launcher and every client/browser pair.
- Live client-first or page-first Alt+Tab titles across Edge, Chrome, Brave, and Firefox.
- DPI-aware client selector with a reserved customer-logo area, editable name, and themed chevron.
- Open-client tabs with close buttons, drag ordering, and an overflow menu when too many clients are open.
- Safe client rename that keeps every browser slot, shared client icon, pinned position, browser-specific Restore tabs settings, and the ctSpaces-managed client shortcut.
- The single clean Desktop shortcut does not limit browser choice inside ctSpaces: the same client can still use isolated Edge, Chrome, Brave, and Firefox slots, including simultaneous sessions in different browsers.
- Reversible client archive that hides inactive clients without deleting their profile data.
- Optional client-first browser window titles for faster identification in Alt+Tab.
- Fresh temporary profile launcher that removes its data after closing. Only one temporary session may run at a time, regardless of browser.
- Chromium Default profile editor for changing the sanitized template used by future Edge, Chrome, and Brave slots and temporary profiles. It retains only bookmark-linked favicon rows and excludes cookies, history, logins, account state, sessions, site storage, and other private browsing data. Firefox slots and temporary profiles are created as clean Firefox profiles instead; the Default editor asks you to select a Chromium browser.
- Reset remains deliberately disabled and fails closed while its selected-browser destructive behavior awaits separate authorization.
- Whole-client Delete action that revalidates the target, requires every browser slot to be closed, removes all browser slots after confirmation, and leaves unrelated Desktop shortcuts alone.
- Manual inactive-client cleanup under the Options (gear) button: preview and permanently delete whole clients not opened in any browser for three calendar months. Includes archived clients, skips open/unsafe clients, and gives clients without history a fresh tracking grace period.
- Immediate multi-client deletion under Options > Delete Multiple Clients: select closed clients without waiting for activity tracking, then confirm permanent whole-client removal. Both cleanup screens offer Select All/Clear Selection and a selected count. About credits Cameron Kincer for the cleanup idea.
- Vacuum action to clear cache-heavy profile data.
- Custom profile icons, remove-icon support, and automatic favicon fetching.
- Theme picker with the existing catalog plus Gothic, red/black Crimson, Graphite Copper, Black Cherry, Midnight Moss, Ink Blue, and Violet Ash dark themes.
- About, app messages and confirmations, overflow menus, and tooltips follow the selected theme. Empty cleanup notices are compact; details are not automatically selected, and scrollbars appear only when needed. Windows-owned file pickers and fatal errors before theme initialization retain Windows styling.
- Verified in-process backup of all client data to a `.7z` file.
- Transactional restore with manifest, path, type, size, CRC, staging, and rollback checks.
- Backward-compatible restore for older `.zip` backups.
- Atomic configuration updates with tri-state reads so an unreadable or indeterminate configuration is not treated as empty and pruned.
- First-run/update flow that can install shortcuts for Start Menu, Desktop, or startup use.

## Basic Use

1. Enter a new client name or choose an existing client from the editable list.
2. Select `Create` for a new client or `Open` for an existing one.
3. Use the pushpin button to keep an existing client in the compact favorites row, then drag pins into your preferred order.
4. Use the tabs at the top to switch between currently open clients or drag them into a different order.
5. Use `Restore tabs` to choose whether the selected client/browser slot should reopen its last tabs on its next launch.
6. Select the browser control in the footer to choose the client/browser slot for the next launch, or use Settings for profile tools, themes, export, restore, and About.

The `New` tab is the launcher view. Each open-session tab displays only the client name. ctSpaces still tracks the browser internally, so the same client can have matching client-named tabs for simultaneous isolated sessions in different browsers.

## Documentation

- [User Guide](docs/USER_GUIDE.md): everyday use and profile tools.
- [Guided Walkthrough](docs/GUIDED_WALKTHROUGH.md): skippable onboarding, replay and new-feature guidance.
- [Feature Reference](docs/FEATURE_REFERENCE.md): complete current behavior.
- [Installation and Updates](docs/INSTALLATION_AND_UPDATES.md): setup, updating, and removal.
- [Data, Backups, and Privacy](docs/DATA_BACKUP_AND_PRIVACY.md): storage and data-safety details.
- [Troubleshooting](docs/TROUBLESHOOTING.md): practical fixes for common problems.
- [Developer Guide](docs/DEVELOPER_GUIDE.md): architecture, build, tests, and release process.
- [Improvement Roadmap](docs/ROADMAP.md): prioritized ideas for future work.

## Backup And Restore

Use `Back Up All Client Data` before large changes, before moving machines, or before testing a new build. Use `Restore Client Data` to bring profiles back from a validated ctSpaces `.7z` backup. Older `.zip` backups remain selectable for compatibility.

ctSpaces requires every supported browser process using a profile below the `Sites` folder to be closed before export or restore, including a browser started manually with a ctSpaces profile path. An active match or a relevant process that cannot be inspected reliably blocks the operation so browser databases are not copied while in use.

## Project Layout

```text
.
|-- ctSpaces.cpp              Main Win32 app
|-- ctSpaces.rc               Windows resources and version info
|-- version.h                 Shared app/file/product version
|-- Resource.h                Resource IDs
|-- BackupEngine.*            Validated backup, staging, and rollback logic
|-- InProc7z.*                In-process 7-Zip backup/default-profile helpers
|-- ProgressUI.*              Progress dialog helpers
|-- theme.h                   Built-in theme definitions
|-- Default.7z                Bundled default browser profile template
|-- docs/                     User, support, developer, validation, and roadmap documents
|-- icons/                    Icons used by the app
|-- tools/                    Starter-profile maintenance tools
|-- tests/                    Automated release checks
|-- 7zip/                     Visual Studio project for the static 7-Zip library
|-- 3p/7zip/                  Bundled 7-Zip SDK/source dependency
|-- build/                    Generated intermediate files
`-- dist/                     Generated build outputs
```

`build/` and `dist/` are generated and ignored by git.

## Building

Clone the public repository together with its pinned 7-Zip submodule:

```powershell
git clone --recurse-submodules https://github.com/madrobdestroyer/ctSpaces.git
cd ctSpaces
```

Open `ctSpaces.sln` in Visual Studio and build `Release|x64`.

The main executable is written to:

```text
dist\x64\Release\ctSpaces.exe
```

The solution also builds the bundled static 7-Zip 26.02 library from `7zip/Format7z.vcxproj`. That project depends on the pinned official sources in `3p/7zip`, so keep both folders with the project.

After building, run the non-browser release, starter, configuration, archive, and workflow checks:

```powershell
& .\tests\Test-Release.ps1
& .\tests\Test-DefaultProfileSanitizer.ps1
& .\tests\Test-DefaultProfileArchiveSanitizer.ps1
& .\tests\Test-BrowserPreferencesJson.ps1
& .\tests\Test-ConfigPersistence.ps1
& .\tests\Test-ArchiveRoundTrip.ps1
& .\tests\Test-WorkflowFeatures.ps1
& .\tests\Test-RenameShortcutTransactions.ps1
& .\tests\Test-HandleBoundRename.ps1
& .\tests\Test-ClientShortcutName.ps1
& .\tests\Test-RestoreTabsToggle.ps1
& .\tests\Test-ExternalBrowserProfileUse.ps1
```

Then run the browser lifecycle checks from PowerShell 7 (`pwsh`). These tests use long disposable native-browser paths and `System.Diagnostics.ProcessStartInfo.ArgumentList`; Windows PowerShell 5.1 can fail in test setup or cleanup even when the ctSpaces behavior being tested succeeds. `Test-SameClientMultiBrowser.ps1` requires both Edge and Chrome; the others use the first supported Chromium browser they can find unless a browser path is supplied.

```powershell
& .\tests\Test-SameClientMultiBrowser.ps1
& .\tests\Test-CoordinatedShutdown.ps1
& .\tests\Test-RestoreAfterBrowserClose.ps1
```

The PowerShell 7 recommendation applies to the full browser-test harness only. It does not change ctSpaces runtime behavior: Default-profile sanitization continues to use the validated fixed System32 Windows PowerShell path.

The live client-icon path can be checked with a randomly named, isolated QA instance and disposable profile that are removed when the test finishes:

```powershell
& .\tests\Test-LiveIconRefresh.ps1 -AutoFetchDomain microsoft.com
```

The Auto-fetch dialog can be checked for DPI-safe sizing and themed owner-draw controls without opening a browser:

```powershell
& .\tests\Test-AutoFetchDialogLayout.ps1
```

Rapid theme changes can be stress-tested against a disposable QA launcher:

```powershell
& .\tests\Test-ThemeResponsiveness.ps1
```

The 5.3 pin, shortcut, rename, archive, restore, and title-preference workflow runs above without touching live clients or the real Desktop.

## License

MPL-2.0. See `LICENSE`. The original project and author attribution are retained;
this fork's maintenance and distribution do not replace them.
