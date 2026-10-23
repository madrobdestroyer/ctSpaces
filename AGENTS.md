# Working on ctSpaces

Read `docs/PROJECT_HANDOFF.md` before changing this project. It records the
current release/candidate, local review paths, test evidence, known limitations,
standing product decisions and the documentation map. When available, read the
current section of private `PROJECT_MEMORY.md`; sections marked historical are
context, not current release or work instructions.

- Inspect Git status, the version source and active workers before building,
  cleanup or changing an existing candidate. Preserve unrelated user files.
- Keep the user's requested version. Audit fixes and documentation are not
  automatic authorization for version bumps, installation or publication.
- Respect the latest interaction permission. Background work must not take the
  mouse, raise windows or start visible capture tools without authorization.
- Use isolated fixtures for QA. Do not mutate real client/browser data to test.
  Preserve client ownership, notebook isolation, save recovery and Undo.
- After fixing a defect, rerun its reproduction, affected regressions and the
  relevant scan. Identify the exact candidate; report untested boundaries.
  A passing covered scan is not a claim that the app has no bugs.
- Use at most two hidden BelowNormal compiler lanes and MSBuild `/m:1`.
  Retain frozen inputs and final evidence; remove disposable compiler caches
  after completed, sealed rounds rather than accumulating repeated builds.
- Cleanup needs an explicit checked workspace-bound manifest. Preserve current
  review/release files, libraries, source, test evidence, backups and recordings.
  Reject reparse paths. Do not retry policy-rejected operations through another
  tool; the handoff/private memory identifies the prior restricted scopes.
- Update the handoff and affected current guides when behavior/status changes.
  Keep versioned validation reports historical and bind results to their actual
  binary, especially the distinct published and repaired 6.3.4.0 candidates.
