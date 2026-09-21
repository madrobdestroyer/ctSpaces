# ctSpaces 5.3.0.10 validation

Date: September 21, 2026.

Status: scoped validation passed; prepared for internal colleague distribution.

## Scope

This pass first repeated the previously intermittent workflows on the frozen
5.3.0.9 executable, then added optional guided onboarding and feature-specific
update guidance. Astra specified the behavior and review criteria; Sol and
Luna implemented and tested the changes. Tests use disposable, constrained
profiles and do not update the real installed application or user clients.

## Repeat adversarial baseline

The baseline executable remained SHA-256
`52391224CCC752F655389E25C1A76705AAF8D0720FAFCE058C463B214A3A0186`.

- Same-client Edge/Chrome coexistence: three consecutive passes at the unchanged
  timeout, checking exact profile windows, separate processes, isolated
  sentinels, live title changes and clean shutdown.
- Default editor: five readiness-enforced close/discard cycles and one
  deliberate early-close cycle passed. The starter archive, existing-client
  sentinel and live configuration remained unchanged.
- Full cleanup tests passed in Dark Gothic and Marine, including exact
  candidate counts, selection/acknowledgement/cancellation, open/reopened and
  unsafe-client protection, managed shortcuts, metadata, About and text menus.
- Shortcut-name and path/UTF-16 boundary tests passed.
- Configuration persistence, activity/calendar boundaries, browser Preferences
  JSON, both starter sanitizers, in-process backup/archive corruption/restore,
  and handle-bound no-overwrite rename checks passed.

The earlier Default warning, Chrome-window timeout and cleanup row discrepancy
did not recur. Their causes remain unproven; these passes are not presented as
evidence of a new product fix. Logs are retained under
`build/walkthrough-20260921/baseline` and `components`, with the configuration
run in `build/walkthrough-config-tests.log`.

## New guide verification

The final release executable is Release x64 version **5.3.0.10**, 5,524,992 bytes:

`D92F1BD5075FCE0F86CB9099D6EDE164E458E0E79F9F017A592769552A0D1173`

The production build completed with zero warnings and errors. The guide unit
suite passed catalog integrity (12 topics), conservative freshness detection,
pending/handled state, announcement filtering and future-revision preservation.

The native UI suite passed on this exact executable:

- Fresh Start, Skip and Escape, durable handling and no automatic replay after
  restart; existing 5.3.0.9-shaped settings receive no forced welcome.
- Fresh minimized startup retains pending state without a modal. The next
  ordinary visible launch offers the welcome and saves Skip durably.
- F1/manual replay, direct topic selection, per-page Next acknowledgement,
  announced-only What's New, Done acknowledgement and compact caught-up state.
- Options New labels, a stored future revision of 999 remaining intact, and
  unchanged browser selection, Restore-tabs and unrelated settings.
- Locked configuration produces a visible save failure without false
  acknowledgement or premature dialog closure.
- An actual WM_COPYDATA client launch dismisses the open guide without marking
  it read and opens the exact disposable Edge profile.
- Gothic and Marine rendering, live Marine-to-Gothic repaint while the guide
  remains open, and real moves between this host's 96- and 144-DPI monitors.
  All required controls stay inside the window and footer controls do not
  overlap. Final complete-window screenshots were visually reviewed.
- Four repeated replay cycles had a GDI-object delta of zero. Real client
  data/configuration remained unchanged; no QA-owned process remained.

Final runtime evidence: `build/walkthrough-20260921/final-guide-after-caption/`.
That folder contains `manifest.txt`, `final-guide-after-caption-rerun.log`, and
the visually reviewed `1e2f95052f-*` screenshots. The earlier passing guide run
is retained separately. Unit evidence is `guide-unit-final.log`.

The pass caught and corrected the guide's Windows-blue topic selection and
bright native borders in Gothic. Focused review also corrected the initial
welcome-message argument, passive-startup suppression, live control-theme
refresh and favicon-service explanation. Existing client safety rules were
not relaxed. Test-driver-only fixes included DPI-aware capture, the GDI API
import, PowerShell variable handling, and waiting for a message box's child
text to initialize within the existing timeout. Failed attempts are retained;
these are not counted as app bugs.

The guide is explanatory and read-only apart from its own progress settings.
It does not click through live features or execute example operations. The
catalog maps future announced topic revisions to their relevant menu commands;
this release announces the guide itself, not old features as if they were new.

## Final release checks

The source/release gate passed again on the final executable. Windows Defender
scanned that exact executable and reported no threats. Evidence is
`release-after-caption.log` and `defender-after-caption.log`. Shortcut-transaction
checks also passed in `rename-shortcut-final.log`.

All 14 serial regression runs passed on candidate
`AADEF28EEF526D52922ABE5F08AB852D522E3300BF694BD7B7F47190209C3D45`:

- Installer/runtime: 21 scenarios, including real production startup and an
  isolated installed-child handoff, plus deterministic locked-delete retry.
  Test-only known-folder, registry and shell boundaries protect the real install.
  The harness has expected warnings from intercepted/unreachable production
  paths; the shipping application build has no warnings.
- Workflow/pins/shortcut persistence and same-client Edge/Chrome coexistence.
- Restore-tabs preferences, restoration after closing the browser twice, and
  protection against an externally opened client profile.
- Auto-fetch layout, 240 rapid theme changes (GDI delta 7), coordinated shutdown.
- Full inactive/manual cleanup and themed About/text-menu workflows in both
  Gothic and Marine.
- Real client shortcut launch, distinct launcher/browser taskbar identities,
  and live favicon refresh using `microsoft.com`.
- Two Default editor discard cycles, with starter archive, existing-client
  sentinel and live configuration unchanged.
- Cleanup dialog failure recovery, complete bounded 100-client fallback,
  scaled font metrics at 96/144/192/240 DPI, and actual 96/144-DPI monitors.

Logs, exit codes and executable identity are retained in
`build/walkthrough-20260921/final-regression`. No product failure occurred in
these final legacy runs; no timeouts or safety checks were weakened.

A final visual review subsequently caught the open guide's title bar retaining
Marine color during a Gothic preview. The correction is restricted to applying
window-chrome refresh to the guide during preview. All other windows and the
legacy workflows above keep their previous behavior. The complete guide UI
suite, guide unit suite, release gate and antivirus scan passed again on the
final executable. Its live-preview screenshot confirms the dark title bar now
matches the guide. The legacy-suite identity above is deliberately retained
rather than relabelled; the broad matrix was not rerun after that guide-only fix.

## Distribution

The distribution archive is `dist/x64/Release/ctSpaces5.3.0.10.zip`, beside the
validated executable and the earlier versioned ZIPs. Optional source packaging
creates `ctSpaces5.3.0.10-source.zip` in that same folder. Versioned checksum and
package-metadata files identify the executable and the archives created by each
packaging run without colliding with another release.

The release packager refuses to overwrite any versioned output, checks version
consistency, and verifies every ZIP entry against its mapped source before
exposing it. Optional source packaging also checks required project dependencies;
its selection excludes private profiles, backups, failed QA fixtures and generated
builds. The distribution archive includes this report, the user guides and license
notices. Validation did not install over the real app or send any colleague
messages. Repository synchronization and GitHub release publication are separate
distribution steps, not additional runtime certification.

## Boundaries

Signing and additional browser runtime certification remain outside the
requested scope. This is not a fresh-machine VM or fleet-wide certification.
Screen-reader certification and guide layouts on displays unavailable to this
host were not performed. Hidden/no-activate launch suppression and operation
guards were source-reviewed; minimized deferral and client handoff were also
runtime-tested as described above. Passing tests are evidence for these
specific cases, not a claim that no bugs can exist.
