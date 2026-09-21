# ctSpaces 6.0.0.0 validation

Date: September 21, 2026.

Status: focused release validation passed.

## Scope

This is a version-and-documentation milestone consolidating the previously
shipped 5.2 and 5.3 work. The only application-source change since 5.3.0.10 is
the shared version header: Windows file/product version `6.0.0.0`, display
version `6.0`. Application behavior, profile formats, and guide content are
unchanged. The broader prior regression evidence remains in
[5.3.0.10 validation](RELEASE_VALIDATION_5.3.0.10.md); that entire matrix was
not repeated for this metadata-only release.

Astra reviewed the transition and release evidence; Sol performed the
version/documentation changes, build, and isolated runtime checks. Testing
did not update the real installed app or modify real client data.

## Exact executable

- Release x64 build: zero warnings and zero errors.
- File/product version: `6.0.0.0`.
- Size: 5,524,992 bytes.
- SHA-256: `D897702E04A167F685CAC9C7AE8DEB0F5D80D54B8AFDA149EA1A6546854EECE1`.
- Live launcher title: `ctSpaces v6.0`.

## Checks repeated for this release

- `Test-Release.ps1`: passed source, resource, version, executable, and bundled
  dependency checks; bundled 7-Zip remains 26.02.
- `Test-GuidedWalkthrough.ps1`: passed catalog integrity, conservative fresh-data
  detection, pending state, announced revisions, and future-revision preservation.
- `Test-InstallerRuntime.ps1`: all 21 scenarios passed, including production
  install/update paths and an isolated installed-child handoff. Registry,
  known-folder, profile, and shell effects remained fixture-bound or intercepted.
  Its instrumented test harness emits expected unused/unreachable-code warnings;
  the shipping application build does not.
- `Test-WorkflowFeatures.ps1`: passed pin ordering, shortcut replacement and
  browser retargeting, unrelated-shortcut preservation, rename including
  case-only names, archive/restore, and preference/profile preservation.
  Live configuration and client data remained unchanged.
- `Test-GuidedWalkthroughUi.ps1`: passed welcome/skip/restart behavior, existing-user
  suppression, minimized deferral, F1 replay, announcement labels, current-page
  acknowledgement, future revisions, locked-settings failure handling, and a
  real isolated client-launch handoff. Live theme refresh and layouts at this
  host's 96/144 DPI passed; repeated opening had a GDI-object delta of zero.
  Live application state remained unchanged.
- `Test-LiveIconRefresh.ps1 -AutoFetchDomain microsoft.com`: passed actual
  client-shortcut launch, separate launcher/browser taskbar identities, and
  live favicon refresh on the disposable client.
- Windows Defender scanned the exact executable and reported no threats.

Local logs and guide screenshots are retained under `build/release-6.0/`.
These generated test artifacts are not part of the repository or application ZIP.

## Compatibility review

Source review confirmed that numeric update comparison orders 6.0.0.0 after
5.3.0.10. The install/data location, single-instance identity, launcher/client
taskbar identities, and profile layout do not derive from the display version.
Backup format validation remains unchanged: recorded application versions are
not required to match the running version. This is source evidence, not a new
historical-backup-fixture restore certification.

Guide progress uses stable topic identifiers and revisions rather than the
application version. Renumbering the release does not reset read topics or
announce unchanged features as new.

## Distribution

`dist/x64/Release/ctSpaces6.0.0.0.zip` contains exactly one root entry,
`ctSpaces.exe`. Packaging verified that entry's SHA-256 against the executable
above and refused to overwrite an existing versioned ZIP.

ZIP SHA-256:
`A8A740FCF5C3D12E2F5253F727D268C69CE809F397983FEF697DF3A36B9899E5`.

Documentation, license notices, source, and release reports remain in the
repository, not in the colleague ZIP. The old 5.3.0.10 ZIP remains unchanged
(SHA-256 `B844218F553C8B25191758972DD0E552C337AD2726FEB610A68A8AD974662D33`).

## Boundaries

This is not a fresh-machine VM, fleet-wide, additional-browser, or screen-reader
certification. Signing remains outside scope. Passing these checks is evidence
for the tested cases, not a guarantee that no bugs exist.

The unchanged 12-topic guide covers the main workflows, not every control or
interaction. Secondary omissions include opening the profile folder, removing
a custom icon, explicit unpinning instructions, dragging a pin to the Desktop
to create a shortcut, and detailed session-overflow Show/Close instructions.
No guide expansion is included in this version-only release.
