# ctSpaces 5.3.0.9 adversarial validation

Date: September 21, 2026. This supersedes the earlier 5.3.0.8 review.

This is a historical report. The current build and test boundaries are in
[5.3.0.10 validation](RELEASE_VALIDATION_5.3.0.10.md). Historical package paths
below record where the artifacts were originally created; local distributions
now use the original single `dist/x64/Release` folder.

## Changes driven by this pass

The most important reproduced product defect was partial client deletion.
With a real Windows file handle denying deletion, the old implementation could
delete browser/profile identifiers before reaching the locked file. After the
lock was released, the remaining client was no longer recognizable enough for
a normal retry.

The new deletion path checks readiness first and preserves identifying files,
required directories and the activity date while removing payload. It then
revalidates the exact client and browser-use state. Only a verified metadata-only
remainder is moved to a no-replacement staging path outside `Sites`. The live
path and staging contents are checked again. Failures retain a retryable client
or report the exact retained path; unverified residual data is not reported as
a complete deletion. A warning can identify leftover validated metadata after
all browser data has been removed.

Other fixes handle failed cleanup dialog creation, paginate the complete result
when the normal details dialog is unavailable, select fonts/DPI from secondary
controls rather than the launcher, and keep the launcher disabled while either
a launch or Default-profile processing remains active.

## Evidence

- The native installer/runtime harness compiles the real production translation
  unit, not a copy of its algorithms. Coverage includes install/update choices,
  real version resources, a genuinely locked installed EXE, denied staging-file
  creation, launch failure, byte-for-byte replacement, unchanged configuration
  and client sentinels, the Default/launch busy-state invariant, deletion retry,
  and legacy/hybrid and Firefox-shaped client deletion.
- The locked-client regression acquires the lock **after** the deletion
  preflight. Identifying files and the old activity record survive the failure;
  releasing the lock allows a complete retry. The test does not merely rely on
  a static lock being caught before work starts.
- The installed-child test executes actual production startup in two processes,
  copies the executable, uses the real shell launch and parent-wait argument,
  observes parent exit, and requires the child's actual launcher controls to
  exist and respond before closing it normally. Test-only LocalAppData/mutex
  boundaries and denied registry writes keep this away from the real install.
  This is not a fresh-machine/VM test, and it does not certify autorun or Start
  Menu integration on every managed Windows configuration.
- Fault-injected preview creation must show an error and remove nothing.
  Fault-injected result creation must retain success, skip, changed-activity
  and Windows-error details, then restore usable launcher controls.
- Real dialogs were moved between available 144- and 96-DPI displays with
  control-font, rectangle and list-extent checks. Native scaled-font fixtures
  additionally exercise 192 and 240 DPI; those are not physical monitor tests.
  A 100-client/100-line fallback test preserves all text, including CRLF and
  UTF-16 character pairs, within page limits.
- Two real Edge Default-editor close/decline cycles verify an unchanged starter
  archive, removal of disposable editor data, an unchanged existing-client
  sentinel, an unchanged live configuration and restored launcher controls.
- Component checks cover configuration persistence, client-age boundaries,
  shortcut/title boundaries, Preferences JSON, both Default sanitizers, archive
  corruption/round trips, and handle-bound rename/no-overwrite behavior.

## Failed attempts retained honestly

Several new test-driver defects were corrected during this pass: a wrong
heading-control ID; treating a boxed null window handle as true; hidden-window
discovery; use of PowerShell's reserved PID variable; dismissing a native dialog;
and a test's own open PID-file stream preventing fixture cleanup. These are not
counted as product fixes. Preserved diagnostics stay in `build`, not in the
distribution.

The earlier 5.3.0.8 intermittent manual-preview count failure was not
conclusively reproduced. The subsequently introduced heading-ID mistake is not
its explanation. Cleanup now has bounded, QA-only per-client decision tracing,
and the harness records actual rows and control state on failure. Safety checks
were not relaxed to make a row-count assertion pass.

A later same-client Edge/Chrome run timed out while looking for the exact
Chrome profile window. The old harness removed that failed fixture, so the
cause cannot be established from the retained timeout alone. The harness now
preserves failures with exact QA-owned process/profile and dialog diagnostics;
it does not increase the timeout or change product behavior just to pass.
The diagnostic rerun and three consecutive additional stress runs passed on
the final executable, without a timeout increase or a production-code change.

One final Default-editor two-cycle attempt reached a safety-warning dialog
instead of the expected Save prompt in its second cycle. The original driver
recorded only the dialog caption, so its exact cause remains unestablished.
The improved driver records complete dialog text, exact QA-owned browser
processes and before-close/after-exit checkpoints, and waits for the launcher
to finish opening the browser. Three readiness-enforced two-cycle runs and
one deliberate early-close two-cycle run then passed. No speculative product
fix or weakened browser-use safety check was made. This is an unresolved
intermittent observation, not a claim that the warning has been fixed.

## Final-build status

The x64 Release executable is **5.3.0.9**, 5,488,640 bytes, built September 21,
2026. SHA-256:

`52391224CCC752F655389E25C1A76705AAF8D0720FAFCE058C463B214A3A0186`

The shipping build completed without compiler warnings or errors. Release
checks passed, and a Microsoft Defender custom scan of that exact executable
completed successfully with no threats. The test-only native harness has
warnings from compiling/intercepting the full production translation unit;
the clean-build statement applies to the shipping executable.

Final-version results:

| Area | Result and scope |
| --- | --- |
| Native installer/runtime | 21/21 scenarios, including actual installed-child handoff and deterministic post-preflight deletion race/retry |
| Core workflow | Create/open, rename, archive, client labels, shortcut ownership and configuration preservation passed |
| Same-client Edge + Chrome | Diagnostic rerun and three consecutive stress runs passed; earlier timeout retained above |
| Restore tabs | Toggle persistence and actual close/reopen with two local tabs passed |
| External profile use | Protection against operations on externally opened client profiles passed |
| Default discard | Four focused two-cycle runs passed; earlier unexplained warning retained above |
| Cleanup and themes | Full Gothic and Marine runs passed: age/manual multi-selection, cancellation, open/reopened/unsafe protection, managed shortcuts, metadata, text menus and About |
| Cleanup fault/DPI | Preview/result creation failures, complete fallback text and physical 96/144-DPI checks passed |
| Icons and taskbar | Real shortcut launch, distinct launcher/client identities, live icon/favicon change before success-dialog dismissal passed |
| Layout and stability | Auto-fetch dialog layout, 240 theme changes and coordinated shutdown passed |

Logs are retained locally under `build/adversarial-20260921`,
`build/final-multibrowser-stress` and `build/final-default-discard-diagnostic`.
These contain diagnostic fixture details and are excluded from distribution.
The release folder's checksum manifest identifies the executable and ZIPs;
archive entries are checked against their exact allowlisted source files.

All reproducible product defects found in this pass have been addressed and
their regressions passed. This is a tested internal release candidate, not a
guarantee of zero defects: the intermittent observations above were not
conclusively explained, and this was not a clean-machine or fleet-wide test.

Signing was explicitly outside this pass. Runtime browser coverage uses the
installed Edge and Chrome; Firefox and Brave were not installed or certified
here. Firefox is not Chromium-based. No real client, installed application,
Desktop shortcut or autorun value was changed by these tests.
