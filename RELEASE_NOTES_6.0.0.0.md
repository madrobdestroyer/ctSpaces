# ctSpaces 6.0.0.0

ctSpaces 6.0 is a consolidated major milestone for the work already shipped in
5.2 and 5.3. It introduces no new application behavior beyond 5.3.0.10 and no
intentional breaking change. Existing client profiles, browser slots, settings,
shortcuts, backups, and update behavior remain compatible.

## Consolidated Highlights

- A compact, theme-aware launcher with pinned clients, draggable pin and
  open-session ordering, client icons, overflow handling, and independent
  Edge, Chrome, Brave, and Firefox slots for each client.
- Per-client/browser session restore, clean temporary browsing, sanitized
  Chromium starter profiles, and clean Firefox profile creation.
- Safe client rename, archive, whole-client deletion, inactive-client cleanup,
  multi-client deletion, copied-link opening, managed Desktop shortcuts, and
  optional client-first browser titles.
- Profile-preserving updates, atomic settings and shortcut writes, fresh
  browser-use checks, hardened archive handling, and guarded destructive
  operations that fail closed when a target cannot be verified.
- In-process `.7z` backup, validated transactional restore, and compatibility
  with older `.zip` backups.
- A skippable and replayable guided walkthrough, feature-specific update
  guidance, keyboard navigation, theme support, and display-scale support.

The detailed historical 5.2 and 5.3 changes remain in the
[user changelog](USER_CHANGELOG.md). This version change does not imply a new
profile format, a reset of user data, or removal of supported workflows.

## Download And Documentation

The release asset is the app-only
[`ctSpaces6.0.0.0.zip`](https://github.com/madrobdestroyer/ctSpaces/releases/download/v6.0.0.0/ctSpaces6.0.0.0.zip)
from [ctSpaces 6.0 / tag v6.0.0.0](https://github.com/madrobdestroyer/ctSpaces/releases/tag/v6.0.0.0).
It contains exactly `ctSpaces.exe`; documentation, release history, source, and
license notices remain in the repository.

See the [User Guide](docs/USER_GUIDE.md),
[Guided Walkthrough](docs/GUIDED_WALKTHROUGH.md),
[Installation and Updates](docs/INSTALLATION_AND_UPDATES.md),
[Data, Backups, and Privacy](docs/DATA_BACKUP_AND_PRIVACY.md), and
[6.0.0.0 release validation](docs/RELEASE_VALIDATION_6.0.0.0.md) for current
usage, update, safety, and verification details.
