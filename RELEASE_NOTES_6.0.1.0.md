# ctSpaces 6.0 — Guide Update (6.0.1.0)

ctSpaces 6.0.1.0 adds an optional visual Quick tour and completes the built-in
guidance for existing launcher actions. It does not introduce a new profile
format, change browser behavior, or alter client data. Existing profiles,
browser slots, settings, shortcuts, backups, and update behavior remain
compatible.

## Optional Quick Tour

- Start **Options > Quick tour** at any time, or choose **Quick tour** from the
  full guided walkthrough, including the fresh-user welcome.
- Nine short steps outline the actual CLIENT field, browser selector,
  Create/Open/Show button, pushpin or pinned row, session tabs, client-folder
  icon, Restore tabs control, Temporary button, and Options button.
- The themed callout supports Back, Next, Skip, Done, Escape, and close. It
  disables launcher interaction while open and never clicks a highlighted
  control, changes a selection or preference, creates a fake client, or launches
  a browser.
- Empty pinned and session states are explained using the real pushpin or New
  tab. The tour is manually replayable and is never forced at startup.
- External client-shortcut requests dismiss the tour before the requested
  launch proceeds.

## Updated Guide

- The Create and open topic now explains that the client icon at the left of
  the CLIENT field opens the selected existing client's folder in File
  Explorer. The folder contains all browser slots, it is separate from the
  Temporary button, and profile files should not be edited or deleted while a
  client browser is open.
- The Icons topic now explains how **Remove Custom Icon** returns a selected
  existing client to the default icon without removing browser data.
- The Favorites topic now covers unpinning—which removes only the favorite—and
  creating a Desktop shortcut by dragging a visible pin onto the Windows
  Desktop.
- Pinned-client help now distinguishes **Select client**, which fills the
  CLIENT field without launching; **Open client**, which opens the currently
  selected browser or shows its existing session; and **Restore tabs**, which
  changes the setting for that client and selected browser without opening it.
  It also explains that a normal click opens the pin in the selected browser or
  shows its existing window.
- The Sessions topic now explains that overflow entries are client labels used
  to show windows, with `Close <client>` commands below the divider.

These actions already existed; this patch documents them more completely. Open
**Options > Guided walkthrough** or press **F1** to replay the full guide.

## Download And Documentation

The release asset is the app-only
[`ctSpaces6.0.1.0.zip`](https://github.com/madrobdestroyer/ctSpaces/releases/download/v6.0.1.0/ctSpaces6.0.1.0.zip)
from [ctSpaces 6.0 / tag v6.0.1.0](https://github.com/madrobdestroyer/ctSpaces/releases/tag/v6.0.1.0).
It contains exactly `ctSpaces.exe`; documentation, release history, source, and
license notices remain in the repository.

See the [User Guide](docs/USER_GUIDE.md),
[Guided Walkthrough](docs/GUIDED_WALKTHROUGH.md),
[Installation and Updates](docs/INSTALLATION_AND_UPDATES.md),
[Data, Backups, and Privacy](docs/DATA_BACKUP_AND_PRIVACY.md), and
[6.0.1.0 release validation](docs/RELEASE_VALIDATION_6.0.1.0.md) for current
usage, update, safety, and verification details. The
[6.0.0.0 release notes](RELEASE_NOTES_6.0.0.0.md) and
[validation report](docs/RELEASE_VALIDATION_6.0.0.0.md) remain available as
historical records.
