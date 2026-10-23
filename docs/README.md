# ctSpaces Documentation

These documents describe ctSpaces **6.3.5.0** on the `cpp` branch. Start with the
[current project handoff](PROJECT_HANDOFF.md) for release identity, local review
paths, tested coverage, known limitations, standing decisions and continuation
instructions. Versioned local review reports describe their historical binaries.

## For People Using ctSpaces

- [Guided Walkthrough](GUIDED_WALKTHROUGH.md): optional first-run help, the read-only Quick tour over real controls, replaying topics, and per-feature update indicators.
- [User Guide](USER_GUIDE.md): everyday use, independent browser slots, pins, tabs, shortcuts, rename/archive, icons, themes, temporary browsing, and profile tools.
- [Notes Walkthrough](NOTES_WALKTHROUGH.md): step-by-step Notes usage with real screenshots, templates, tickets, trash and permanent deletion; also available inside Notes through Help.
- [Notes Productivity](NOTES_PRODUCTIVITY.md): quick capture, searchable Actions, snippets, status, tags, sorting, work logs, bulk actions and readable handovers.
- [Feature Reference](FEATURE_REFERENCE.md): complete list of current controls and behavior.
- [Installation and Updates](INSTALLATION_AND_UPDATES.md): first run, updating, running once, and removing the app.
- [Data, Backups, and Privacy](DATA_BACKUP_AND_PRIVACY.md): browser-profile isolation boundaries, what is saved, where it is stored, and how backup/restore works.
- [Troubleshooting](TROUBLESHOOTING.md): practical fixes for common problems.

## For Maintainers

- [Current Project Handoff](PROJECT_HANDOFF.md): start here before continuing work; current release, evidence, boundaries, product decisions and housekeeping receipts.
- [6.3.5.0 Release Validation](RELEASE_VALIDATION_6.3.5.0.md): exact backup-restore repair, fresh checks and version-only provenance from the preceding broader scan.
- [Changes Since 6.1.1.0](CHANGES_SINCE_6.1.1.0.md): cumulative coworker-facing changes, update cautions and the accepted Edge Restore tabs limitation.
- [Notes Tab Order Review](NOTES_TAB_ORDER_REVIEW.md): native drag handling, explicit move controls, reopening order and adversarial testing of the 6.3.0.0 review branch.
- [Notes Inspector Review](NOTES_INSPECTOR_REVIEW.md): local 6.3.0.0 Inspector on Demand design, temporary panels, searchable clients, isolated notebooks, native resize validation and package evidence.
- [Notes Spacing Review](NOTES_SPACING_REVIEW.md): compact 12-point Normal text, existing-note spacing correction, and local package validation.
- [Launcher and Notes Links Review](NOTES_LAUNCHER_LINKS_REVIEW.md): foreground restoration, optional tray minimization, right-click editing, unused draft cleanup, and all note links using the PC's default browser.
- [Compact Notes and Trash Review](NOTES_COMPACT_TRASH_REVIEW.md): smaller status/help dialogs, direct Trash access, and local 6.3.0.0 validation.
- [Notes Template Editor Review](NOTES_TEMPLATE_EDITOR_REVIEW.md): local 6.3.0.0 direct template editing, add/close/delete transitions, validation, and package evidence.
- [Developer Guide](DEVELOPER_GUIDE.md): architecture, profile lifecycle, build, tests, and release process.
- [6.3.0.0 Release Validation](RELEASE_VALIDATION_6.3.0.0.md): the October 9 release's validation scope, evidence, and retained boundaries; subsequent design reviews are recorded separately above.
- [6.1.0.0 Release Validation](RELEASE_VALIDATION_6.1.0.0.md): historical validation of filtering, notes, and Close All Clients.
- [6.0.0.0 Release Validation](RELEASE_VALIDATION_6.0.0.0.md): historical validation evidence for the previous release.
- [Improvement Roadmap](ROADMAP.md): prioritized ideas that preserve the compact launcher.
- [User Changelog](../USER_CHANGELOG.md): the nontechnical update summary for distribution.

## Source Of Truth

When documentation and behavior disagree, use this order:

1. The latest user instructions and the current handoff for task scope, permissions and candidate identity.
2. The identified release/candidate's code and automated tests for implemented behavior.
3. Its applicable validation report and exact artifact identity for coverage and known boundaries.
4. The current guides; dated design reviews and older memory are historical context.

Update the affected document whenever a user-visible behavior, storage rule, installer prompt, or release step changes.
