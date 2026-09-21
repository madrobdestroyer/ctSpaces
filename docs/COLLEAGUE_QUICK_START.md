# ctSpaces: Quick Start for Colleagues

ctSpaces opens separate browser spaces for each client. It is designed for
ordinary per-user Windows use; no administrator installation is required.

New to the app? The optional welcome starts a read-only walkthrough. You can
skip it and return through **Options (gear) > Guided walkthrough** or **F1**.
Use **What's new** for announced feature guidance. Reading Help never launches
a browser or changes a client. See [guide help](GUIDED_WALKTHROUGH.md).

## Start ctSpaces

Run `ctSpaces.exe`. On first run, answer **Yes** to **Set up ctSpaces for this
Windows account?** to copy the app to your current user's Local AppData. Setup
then separately asks whether to create Start Menu and Desktop shortcuts and
whether to start ctSpaces automatically when you sign in. You can answer **No**
to decline setup, then confirm the follow-up offer to run once. Client data is kept in the same
per-user location either way:

`%LOCALAPPDATA%\InfinitySys\ctSpaces`

Do not move, rename, or manually edit the `Sites` folder while ctSpaces or one
of its client browsers is open.

## Create or open a client

1. On the **New** tab, type a client name in the **CLIENT** field.
2. Select the browser you want: Microsoft Edge, Google Chrome, Brave, or
   Mozilla Firefox.
3. Choose **Create** (or press Enter). Creating immediately opens the new
   client in the selected browser.

To return to an existing client, select its name and choose **Open**. A client
has a separate profile for each browser. For example, `Acme` in Edge and
`Acme` in Chrome have independent cookies, sign-ins, bookmarks, history,
extensions, and tabs; they can be open at the same time. Changing the browser
selector chooses another slot—it does not merge or convert the current one.

New Edge, Chrome, and Brave slots start from ctSpaces' sanitized starter
profile. The starter contains no client sign-ins, cookies, saved passwords,
history, or private session state. Firefox slots are created cleanly and do not
use the Chromium starter. Do not sign in to a real client while editing the
starter profile.

## Restore tabs

For an existing client, choose the client and browser, then use the **Restore
tabs** switch. On means ctSpaces asks that browser slot to reopen its previous
session the next time it starts; off means it requests a normal new tab. The
setting is saved independently for each client/browser pair. Turning it off
does not erase cookies, sign-ins, bookmarks, history, or other browser data.

New clients, temporary profiles, and the starter-profile editor always begin
fresh, so the switch is unavailable for them.

## Shortcuts

Use **Create Desktop Shortcut** from Settings or a pinned client. The managed
shortcut is named exactly after the client, such as `Acme.lnk`, and opens that
client in the browser selected when it was created. Recreate it after choosing
another browser to update the same shortcut. ctSpaces leaves unrelated
shortcuts alone.

Pin a client to make it easy to open. A pinned client can also open a copied
`http://` or `https://` link in the selected client/browser slot.

## Back up and restore

Before backup or restore, close every browser using a ctSpaces client profile,
including browsers started manually with a ctSpaces profile path.

- **Back Up All Client Data** creates a validated `.7z` archive containing all
  client profiles. It may contain cookies, active sessions, history, bookmarks,
  and saved credentials, so store it only in an approved protected location.
- **Restore Client Data** accepts current `.7z` backups and older `.zip`
  backups. ctSpaces validates the archive before replacing `Sites` and keeps
  the previous folder in a timestamped `Sites_PreRestore_...` recovery copy.

Keep important backups and recovery copies until the restored data has been
checked. Backups are not encrypted by ctSpaces.

## Cleanup and deletion

**Vacuum (Clear Cache)** removes cache-related data while keeping cookies,
history, and saved logins. **Archive Client** hides a client without deleting
it; it can be restored from the archived-client menu.

**Clean Up Inactive Clients...** previews clients that have not been opened in
three calendar months, including archived clients. Clients with no activity
history receive a fresh three-month tracking baseline; folder dates are not
used to guess prior activity. Open clients are skipped. Review the preview,
select the clients to remove, check its acknowledgement box, and confirm the
deletion. The operation is not scheduled.

**Delete Multiple Clients...** is the immediate manual multi-select action. It
does not wait three months, includes archived clients, and skips open clients.
Review the selection and acknowledge before confirming.

**Delete Profile** and the cleanup commands are irreversible whole-client
operations. They remove the live client root, every browser slot, the shared
icon, activity record, and only Desktop shortcuts that ctSpaces can verify as
managed. They do not delete unrelated shortcuts, user-created backups, or
timestamped recovery copies. Export a backup first if the client may be needed
later, and close every browser slot before deleting.

Do not substitute manual folder deletion for these commands: deleting a folder
outside ctSpaces does not clean its managed shortcuts or saved settings.

If Windows or your organization's IT controls warn that an internal build is
unsigned, use only the approved internal distribution and support process. Do
not bypass security policy or disable protections.

For more detail, see the [User Guide](USER_GUIDE.md), [Installation and
Updates](INSTALLATION_AND_UPDATES.md), and [Data, Backups, and Privacy](DATA_BACKUP_AND_PRIVACY.md).
