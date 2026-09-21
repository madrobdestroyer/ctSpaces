# ctSpaces 5.3.0.10

This update adds optional, replayable in-app guidance.

- Fresh users receive a skippable welcome on an ordinary visible launch.
  Existing users are not forced through onboarding, and client shortcuts and
  minimized startup are not interrupted by an automatic walkthrough.
- **Options > Guided walkthrough** and **F1** reopen the full guide. Topics
  cover the launcher, separate browser profiles, sessions, shortcuts, Default,
  icons, archive, cleanup, backup/restore, themes and help.
- **What's new** and small, text-labelled indicators point to announced
  guidance. Read progress belongs to each topic revision, so future features
  can be explained without resetting the entire guide.
- The guide follows the selected theme, supports keyboard navigation and
  display scaling, and is read-only apart from saving its own preferences.
  It never performs client operations as part of a demonstration.

All 5.3.0.9 cleanup-recovery, dialog fallback, display-scale and overlapping-work
fixes are retained. Browser/profile isolation and deletion safeguards are not
relaxed by this update.

See [Guided walkthrough and feature updates](docs/GUIDED_WALKTHROUGH.md) for
usage details and [release validation](docs/RELEASE_VALIDATION_5.3.0.10.md)
for the exact tested build, evidence and remaining test boundaries.
