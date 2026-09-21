# ctSpaces Documentation

These documents describe ctSpaces 5.3 for users, support staff, and maintainers.

## For People Using ctSpaces

- [Guided Walkthrough](GUIDED_WALKTHROUGH.md): optional first-run help, replaying topics, and per-feature update indicators.
- [User Guide](USER_GUIDE.md): everyday use, independent browser slots, pins, tabs, shortcuts, rename/archive, icons, themes, temporary browsing, and profile tools.
- [Feature Reference](FEATURE_REFERENCE.md): complete list of current controls and behavior.
- [Installation and Updates](INSTALLATION_AND_UPDATES.md): first run, updating, running once, and removing the app.
- [Data, Backups, and Privacy](DATA_BACKUP_AND_PRIVACY.md): browser-profile isolation boundaries, what is saved, where it is stored, and how backup/restore works.
- [Troubleshooting](TROUBLESHOOTING.md): practical fixes for common problems.

## For Maintainers

- [Developer Guide](DEVELOPER_GUIDE.md): architecture, profile lifecycle, build, tests, and release process.
- [Release Validation](RELEASE_VALIDATION_5.3.0.10.md): current public validation scope, evidence, and retained boundaries.
- [Improvement Roadmap](ROADMAP.md): prioritized ideas that preserve the compact launcher.
- [User Changelog](../USER_CHANGELOG.md): the nontechnical update summary for distribution.

## Source Of Truth

When documentation and behavior disagree, use this order:

1. The current release code and automated tests.
2. The current [release validation report](RELEASE_VALIDATION_5.3.0.10.md) for tested behavior and known boundaries.
3. The other documents in this folder.

Update the affected document whenever a user-visible behavior, storage rule, installer prompt, or release step changes.
