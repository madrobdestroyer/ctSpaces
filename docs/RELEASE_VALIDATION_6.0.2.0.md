# ctSpaces 6.0.2.0 validation

Date: September 21, 2026.

Status: scoped release validation passed.

## Scope

The colleague upgrade baseline is 5.2.0.14, not the most recent development
build. This update expands the optional Quick tour to 20 steps, with 12 inert
illustrations. Coverage includes independent browser slots, pinned context
menus, copied links, both Desktop-shortcut paths, pin and session reordering,
client-first titles, rename/archive, Default save/discard, and both cleanup
tools. The tour highlights real controls without performing their actions.

Eight existing guide topics now announce relevant post-5.2 guidance. Related
Options commands show the existing dot and `(New)` label until that topic is
acknowledged. Skipping does not acknowledge unseen guidance; ordinary tour
navigation does not mark written-guide topics read.

The same 12 stable topic identifiers remain. Profile layout, browser launching,
deletion operations, backup formats, and installation behavior are unchanged.
The historical feature inventory was checked against the repository changelog
and release notes; an exact 5.2.0.14 source snapshot is not present in this repo.

## Executable and completed checks

- Release x64 build: zero warnings and zero errors.
- File/product version: `6.0.2.0`; display version: `6.0`.
- Executable size: 5,561,344 bytes.
- SHA-256: `56670F18CD489950C35A37F7D6F8EB8B40BEEAC058518B30EF058DAF6D7AD58A`.
- Release gate passed; bundled 7-Zip remains 26.02.
- Guided-walkthrough unit tests passed: stable identifiers, exact revisions,
  eight announcement topics, old read-state filtering, and future-revision
  non-downgrade.
- Windows Defender scanned the executable above and reported no threats.

- Full guided-walkthrough UI suite passed on the final executable above: fresh-user
  welcome/skip, existing-user suppression, minimized deferral, F1 replay, all
  eight announcement topics, individual acknowledgement and menu markers,
  future revision 999, locked-settings errors, theme preview, 96/144 DPI, and
  an isolated Edge shortcut handoff. Repeated opening had GDI delta zero.
  Live configuration and client data were unchanged. Evidence:
  `guide-ui-final-56670f.log` and `guide-ui-final-56670f/d6402a08d1-*`.

- Final-build Quick tour UI passed: 20 distinct steps, 12 demonstrations, 48
  light/dark-theme captures at 96/144 DPI, strict title/body fit, empty and
  populated states, real-control anchors, guide-to-tour handoff, navigation,
  close/minimize cleanup, an actual isolated Edge shortcut request, and
  configuration/pin/client-data isolation. Repeated opening had GDI delta zero.
  Evidence: `quick-tour-ui-final-56670f.log` and
  `quick-tour-ui-final-56670f/9415343255-*`.
- Final-build workflow regression suite passed, including persistent pin order,
  client-only shortcut creation and retargeting, preservation of unrelated
  shortcuts, both browser slots, case-only/deep-path and ordinary rename,
  archive/restore, title preference persistence, and unchanged live data.
  Evidence: `workflow-features-final-56670f.log`.
- Screenshot review confirmed readable themed workflow illustrations and
  actual pin/session highlights. All Browser/Default/Archive footers were
  visually checked in both themes and at both DPIs after the footer fix. Root
  review also checked shortcut/drag, copied-link, reorder, title, deletion,
  and populated-session captures. Screenshots are not included in distribution.

All final GUI suites ran serially on the executable above. No QA launcher or
QA Edge process remained after cleanup. The final guide unit rerun is recorded
in `guide-unit-final.log`. No live installation or client data was updated.

## Review and boundaries

Source review is focused on guide content, announcement routing, inert drawing,
text fitting, owner-window behavior, and the read-only tour boundary. The
existing isolated native UI harnesses are used; no live client data or Desktop
shortcuts are test fixtures. Screen captures, logs, and QA fixtures stay under
the ignored `build/feature-tour-6.0.2.0/` area or existing QA fixture roots.

The release gate caught raw non-ASCII symbols in newly added C++ strings.
These were converted to equivalent Unicode escapes without weakening the
charset gate. UI testing also found missing `MIIM_STRING` metadata on
owner-drawn menu items. Adding the flag exposes their existing text to menu
inspection/accessibility APIs without changing their rendering or commands.
The empty-client tour test also caught a stale highlight: the folder icon is
hidden without an existing client selection. That step now falls back to the
real CLIENT field with an explicit explanation, instead of retaining the prior
session-tab highlight. A visible folder icon remains the preferred target.
Its empty-state copy was shortened to fit the compact card at 96 DPI. Visual
review also caught the Default illustration's wrapped footer losing its final
pixels despite the separate title/body assertions passing. The two-choice
painter now reserves the measured footer height plus padding rather than a
fixed 30 DIP. This is why screenshot review is separate from machine checks.

Test-harness corrections were kept separate from product fixes: guide content
checks were aligned with the final equivalent wording; a PowerShell expression
was corrected; and Quick tour text measurement stopped selecting a foreign
process's font handle. The latter failed `SelectObject` and measured the wrong
fallback font, producing a false clipping warning. The tester now creates the
same production font locally at the control's DPI. The 96/144-DPI screenshots
confirmed the browser-step body was visible; fit assertions remain enabled.
The test driver also normalizes comma-separated DPI arguments and waits for
the exact target rectangle after page changes, rather than sampling midway
through the UI handler. These changes retain strict coverage and target checks.

This release does not repeat the entire historical browser, installer, and
backup test matrix. Earlier evidence remains in
[6.0.1.0 validation](RELEASE_VALIDATION_6.0.1.0.md) and
[5.3.0.10 validation](RELEASE_VALIDATION_5.3.0.10.md). Fresh-machine testing,
screen-reader certification, and signing are outside this scoped update.

## Distribution

The output is `dist/x64/Release/ctSpaces6.0.2.0.zip`, containing exactly
one root entry, `ctSpaces.exe`. Documentation, source, license notices, logs,
and screenshots are not part of the colleague ZIP. Previous ZIPs and release
tags are preserved. Packaging reopened the ZIP and verified both the single
entry and its executable SHA-256 against the tested binary.

ZIP SHA-256:
`740B8002D33EE99C21EDEF8DC9F0262048FB1892CA4E1A751C92E8D3282E38DC`.

The previous 6.0.1.0 ZIP remains unchanged with SHA-256
`91C7D21D156CE65311E4294EB6D59978EEE95C9A0B967ADD333984775BA2B526`;
6.0.0.0 remains unchanged with SHA-256
`A8A740FCF5C3D12E2F5253F727D268C69CE809F397983FEF697DF3A36B9899E5`.

Generated evidence, screenshots, failure fixtures, and private project notes
remain excluded from source publication and distribution.
