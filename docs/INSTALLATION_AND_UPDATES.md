# Installation And Updates

Applies to ctSpaces 5.3.

## Requirements

- A 64-bit Windows computer.
- Microsoft Edge, Google Chrome, Brave, or Mozilla Firefox installed in a standard location.
- Access to the current Windows user's Local AppData folder.
- Windows PowerShell only when restoring a backup ZIP created by an older ctSpaces version. New backup and restore operations use the built-in 7-Zip engine.
- Internet access only when using browser websites or `Auto-fetch Icon`.

ctSpaces is a per-user application. Its normal setup does not require writing to Program Files.

## First Run

When ctSpaces does not find an installed copy, it asks whether to set up the app for the current Windows account.

Choosing Yes:

1. Copies the executable to `%LOCALAPPDATA%\InfinitySys\ctSpaces\ctSpaces.exe`.
2. Optionally creates a Start Menu shortcut.
3. Optionally creates a Desktop shortcut.
4. Optionally creates a current-user startup entry.
5. Launches the installed copy.

Choosing No offers a second choice to run that copy once without installing it.

Client profiles are stored in the same Local AppData tree whether the executable is installed or run once.

## Installed Locations

| Item | Location |
|---|---|
| Installed executable | `%LOCALAPPDATA%\InfinitySys\ctSpaces\ctSpaces.exe` |
| Shared configuration | `%LOCALAPPDATA%\InfinitySys\ctSpaces\config.ini` |
| Standard client containers and browser slots | `%LOCALAPPDATA%\InfinitySys\ctSpaces\Sites` |
| Installed starter archive | `%LOCALAPPDATA%\InfinitySys\ctSpaces\Default.7z` |
| Start Menu shortcut | Current user's Programs folder as `ctSpaces.lnk` |
| Desktop shortcut | Current user's Desktop as `ctSpaces.lnk` |
| Optional startup entry | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\ctSpaces` |

## Install A Newer Version

1. Close all ctSpaces client browser windows.
2. Close the ctSpaces launcher.
3. Run the newer `ctSpaces.exe`.
4. Confirm `Update ctSpaces`.
5. The updater replaces the installed copy and relaunches it.

The update process changes the executable and may update the shared new-client starter template. It does not replace existing `Sites\<ClientName>` containers or move legacy Chromium data into new browser slots.

### Why Closing Matters

Windows cannot replace an executable that is still held open. Antivirus scanning can also briefly hold the file after the window disappears. The updater retries, stages the new file in the destination folder, and reports any still-running installed process IDs when it can.

## Version Prompt Meanings

| Prompt | Meaning |
|---|---|
| Update the installed copy | The external executable is newer. |
| Replace Installed Copy | Both files report the same version, but you are running a different physical copy. |
| Run this newer copy once | Use the external build without changing the installed executable. |
| Run this older copy once | The installed app is newer; launch the older external build only for troubleshooting. |
| ctSpaces Is Open | Another launcher instance is still active or Windows is still finishing shutdown. |

Every distributed code fix should increment the fourth version component so ordinary updates do not look like same-version replacement.

## If An Update Says ctSpaces Is Still Running

1. Close every client browser opened by ctSpaces.
2. Close the launcher and wait several seconds.
3. Open Task Manager and look for `ctSpaces.exe` only if the message names a process ID or the problem repeats.
4. Retry the newer executable.
5. If no process remains, allow time for antivirus scanning to release the file.

Do not delete client profile folders to solve an executable update problem.

## Portable Troubleshooting Mode

Maintainers can place an empty file named `ctSpaces.portable` beside a test executable. That exact build then skips install/update handoff and runs directly.

Portable mode by itself still uses the normal Local AppData data root; it is not a test-data sandbox. Automated launcher tests additionally pass a random `--qa-instance` and an explicit `--qa-data-dir` that the app constrains to an absolute child of the copied QA executable's folder. Shortcut tests also pass a fake `--qa-shortcut-dir` under that folder. The tests remove only those exact run-scoped paths afterward.

Never distribute the `ctSpaces.portable` marker with a normal release.

## Remove ctSpaces

There is currently no built-in uninstaller.

### Remove The App But Keep Client Data

1. Close all ctSpaces browser windows and the launcher.
2. Remove the Start Menu and Desktop shortcuts if present.
3. Remove the `ctSpaces` value from the current-user Run key if startup was enabled.
4. Delete only `%LOCALAPPDATA%\InfinitySys\ctSpaces\ctSpaces.exe`.
5. Keep the `Sites` folder and `Default.7z` for a later reinstall.

### Remove Everything

1. Export a backup first if any client may be needed later.
2. Close all ctSpaces browser windows and the launcher.
3. Remove shortcuts and the startup entry.
4. Delete `%LOCALAPPDATA%\InfinitySys\ctSpaces`.

Deleting the entire data folder permanently removes all local client browser profiles, sign-in state, bookmarks, history, settings, icons, and safety backups.
