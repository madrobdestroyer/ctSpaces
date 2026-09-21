# ctSpaces 6.0.3.0 validation

Date: September 21, 2026.

Status: scoped guide/announcement correction validated.

## Scope and baseline

What's New now has dedicated change-focused titles and bodies instead of
reusing whole manual chapters. Eight announcements distinguish additions from
existing basics and popup/theme improvements. Temporary/Default is no longer
announced or given an Edit Default menu marker. The full guide retains its 12
stable topics; the 20-step Quick tour and its 12 inert illustrations are
unchanged. No browser, profile, deletion, backup, or installation logic changed.

The user-supplied `ctSpacesV5.2.0.14.zip` is the colleague baseline. Its exact
hashes, differing embedded version, static evidence, and comparison limits are
recorded in [the baseline note](BASELINE_5.2.0.14.md). The old executable was
not executed or installed. Static evidence was checked against current source
and recorded development history; this is not an exhaustive runtime diff.

## Tested executable

- File/product version: `6.0.3.0`; display/About version: `6.0`.
- Size: 5,568,512 bytes.
- SHA-256: `4CB5499090E18E3EABD0BD80270DC49807535A966A0EF4067E3D90AE3059EF65`.
- Release x64 build passed with no warnings or errors.
- Release checks passed; bundled 7-Zip remains 26.02.
- Windows Defender reported no threats in this executable.

## Completed checks

- Guide unit tests require dedicated, nonempty announcement copy distinct
  from full-guide copy, exact announcement titles/IDs/revisions, and no
  announcement copy on unannounced topics. They verify progress saved by
  6.0.2.0 exposes exactly the corrected eight announcements, plus future
  revision non-downgrade and existing fresh-data/state behavior.
- The isolated native guide UI suite passed on the executable above. It
  compared all eight announcement bodies against the corresponding full-guide
  chapters and checked the specific new-feature instructions and old-feature
  distinctions. It verified the new dialog caption and that Edit Default no
  longer has a New marker.
- Skip preserves unread progress; Next/Done acknowledge only the displayed
  topic; unrelated markers remain; the caught-up notice stays compact; and
  future revision 999 is preserved. Locked settings report an error without
  falsely acknowledging a page.
- Fresh welcome, Skip/Escape, restart suppression, minimized deferral,
  existing-user suppression, F1 replay, live theme preview, and 96/144-DPI
  layout checks passed. Repeated guide opens had GDI delta zero.
- An isolated Edge client-shortcut handoff dismissed the guide without
  acknowledging it and launched only its test profile. No test launcher or
  matching Edge process remained afterward.
- Screenshot review confirmed readable announcement titles, list labels, and
  bodies in Marine, plus the full guide's dark-theme rendering. The guide
  harness reported live settings unchanged and no test client in live Sites.

Local evidence is under ignored `build/baseline-help-6.0.3.0/`: `msbuild.log`,
`guide-unit-final.log`, `release-gate.log`, `defender.log`, `guide-ui.log`,
`packaging.log`, and `guide-ui/641f5d1d97-*`. Screenshots and test fixtures are
not published or packaged.

This focused correction does not repeat the whole browser/workflow matrix or
unchanged Quick-tour suite. Prior evidence remains in
[6.0.2.0 validation](RELEASE_VALIDATION_6.0.2.0.md). No live application or
client data was upgraded by these tests.

## Distribution

`dist/x64/Release/ctSpaces6.0.3.0.zip` contains exactly one root entry,
`ctSpaces.exe`. Packaging reopened the archive and verified that the embedded
executable matches the tested SHA-256 above. No documentation, licenses,
logs, source, or baseline executable are added to the colleague ZIP.

ZIP SHA-256:
`1AC65F1AE2C277DD54EDCE8E2FCDEA5226DC8DC7BEA0086DCEB0971038E3EA99`.

The previous 6.0.2.0, 6.0.1.0, and 6.0.0.0 ZIP hashes were rechecked and are
unchanged. Historical tags and release assets are retained.
