# ctSpaces 6.0.1.0 validation

Date: September 21, 2026.

Status: scoped release validation passed.

## Scope

This update adds a manually started, read-only Quick tour highlighting nine
real launcher controls. The existing detailed guide now explains the previously
omitted folder, icon-removal, unpinning, desktop-drag shortcut, session-overflow,
and pinned-menu actions. Normal clicking of a pinned client still opens or
shows it; only the right-click Select client command selects without launching.

The same 12 guide topic identifiers are retained. Five revised topics advance
from revision 1 to 2; the other seven retain their revisions. Only the updated
Help topic is announced, including the new optional tour. Client profile,
browser, backup, and installation formats are unchanged.

## Executable

- Release x64 build: zero warnings and zero errors.
- File/product version: `6.0.1.0`; display version: `6.0`.
- Size: 5,540,352 bytes.
- SHA-256: `39DDF683C1C76D3CB5B8652065E4DA1FBDF934A709E4630DE321E9FD1631C652`.

## Completed checks

- Release gate passed; bundled 7-Zip remains 26.02.
- Guided-walkthrough unit tests passed, including old revision-1 progress,
  unchanged-topic preservation, announcement filtering, and non-downgrade of
  future revision 999.
- Full guided-walkthrough UI suite passed on the exact executable above:
  startup/skip/restart, existing-user suppression, minimized deferral, F1,
  revised content, per-page acknowledgement, locked-settings failures, an
  isolated browser-launch handoff, and live theme preview. The Quick tour
  button fits without overlapping navigation. The expanded favorites page's
  final character is visible at 96 and 144 DPI; its complete text fits without
  requiring a scrollbar. Repeated guide opening had a GDI-object delta of zero.
- Existing client-management and shortcut workflow suite passed, including
  shortcut replacement/retargeting, rename, archive/restore, and preservation
  of profiles and settings. Live configuration and client data were unchanged.
- Windows Defender scanned the exact executable and reported no threats.
- Quick tour UI suite passed on the same executable: all nine distinct steps,
  Back/Next/Done/Skip/Escape/title-bar close, guide-button handoff, and main-window
  re-enabling after exit or minimization. Checks verified the hollow highlight's
  actual placement around the CLIENT field, primary action, and Options control,
  plus removal of the highlight when the tour closed.
- Quick tour captures in Marine and Dark Gothic at 96 and 144 DPI were visually
  reviewed. Text and navigation fit; the callout and title bar followed the
  selected theme. An actual isolated Edge shortcut request dismissed the tour
  before opening the exact requested profile. Ordinary tour navigation created
  no client data and preserved configuration. Repeated opening had a GDI-object
  delta of -1, with no remaining QA launcher or QA Edge process after cleanup.

Evidence is retained locally under `build/guide-completion-6.0.1.0/`.
The final guide UI result is `guide-ui-final.log`; screenshots are in
`guide-ui-final/`. Earlier guide-test attempts exposed a test-driver list-box
message constant error and mismatched wording assertions, not a missing
application feature. Those checks were corrected and the final suite passed.

The final Quick tour result is `build/final-quick-tour-ui.log`; reviewed captures
have prefix `ab4fb07fa5` under `build/quick-tour-ui/`. Earlier test-driver fixes
corrected fixture-path constraints, a dialog-initialization race, screenshot
rectangle aggregation, and initial theme-key normalization in the fixture.
No production failure was hidden by loosening timeouts or isolation checks.

## Review and boundaries

Focused source review covered owner-window enable/disable restoration, tour
cleanup, keyboard routing, guide-to-tour transition, and client-shortcut
handoff. Review corrected placement refreshes that could raise an inactive
overlay above another application; refreshes now preserve window stacking.

The previous broader evidence remains in
[6.0.0.0 validation](RELEASE_VALIDATION_6.0.0.0.md) and
[5.3.0.10 validation](RELEASE_VALIDATION_5.3.0.10.md). This update does not claim
that the entire historical matrix was repeated. Fresh-machine, additional
browser, screen-reader, and signing certification remain outside scope.

## Distribution

The output is `dist/x64/Release/ctSpaces6.0.1.0.zip`, containing only
`ctSpaces.exe`. No documentation, license files, source, or sidecars belong in
the colleague ZIP. Packaging verified the single entry and its executable hash.
Existing release ZIPs remain untouched. The 6.0.0.0 ZIP still has SHA-256
`A8A740FCF5C3D12E2F5253F727D268C69CE809F397983FEF697DF3A36B9899E5`.

6.0.1.0 ZIP SHA-256:
`91C7D21D156CE65311E4294EB6D59978EEE95C9A0B967ADD333984775BA2B526`.

Generated logs, screenshots, QA fixtures, and private notes are not published
as source or included in the application ZIP. The real installation and client
data were not updated during this task.
