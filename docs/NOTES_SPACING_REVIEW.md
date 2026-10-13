# Notes spacing review — 6.3.0.0

The October 13 [existing-note spacing and snippet review](NOTES_SPACING_SNIPPETS_REVIEW.md)
supersedes the manual-only correction described below. This document retains the
October 12 validation history.

Validated October 12, 2026 in `C:\Projects\ctSpaces`, branch `cpp`.
Continues the local review without a version bump, installation, publication,
commit, or new branch. Earlier packages remain preserved.

Normal text previously used 14 points plus 4 points of extra space after every
paragraph. Existing blank lines consequently occupied more space than expected
in a plain note. The font-size dropdown changes character size alone, so choosing
12 points did not remove those gaps.

New blank notes, plain-text-generated notes/captures, blank template drafts, work
log text, and Clear formatting now use 12-point Normal text with single line
spacing and zero extra space before or after paragraphs. The built-in formatted
starters and saved rich formatting retain their explicit sizes and appearance.
Heading and subheading spacing remain unchanged.

Selecting text and choosing **Normal text** resets its paragraph spacing and
applies 12-point body text. This also works when Normal text is already the
selected dropdown item: its committed-selection notification is handled. The
template editor offers the same correction. Existing saved notes are not
automatically reformatted. No blank lines or note content are removed.

For an existing plain note: focus the body, press Ctrl+A, open the style dropdown,
and click Normal text. Select only body paragraphs to retain heading formatting.
Natural font metrics can still differ from another app or font family; the
change removes our additional paragraph gaps.

## Validation

- Release x64 build passed without compiler warnings or errors.
- **402 native production-dialog checks passed.** Added checks cover default
  font/paragraph metrics, applying Normal text to saved before/after and double
  spacing through committed selection, measured reduction of actual line
  positions with equally sized blank lines, exact text preservation, heading
  and subheading spacing, Clear formatting, close/reopen persistence,
  plain-text-generated notes, and template spacing reset.
- Computer Use reviewed the exact final executable in the disposable Notes
  Review client. The user's three-line example, with its original blank lines,
  renders at 12 points without the added paragraph gaps. Reselecting Normal text
  on a saved 14-point note visibly changes it to 12 points and preserves the text.
- Final release gates and Microsoft Defender passed on the packaged executable.
  ZIP verification confirms exactly one root `ctSpaces.exe` whose extracted
  SHA256 matches the tested executable. Published and previous launcher-review
  ZIP hashes remain unchanged.
- The previous 21 startup/installer scenarios are retained; they were not
  repeated because startup, tray, installation, and link routing are unchanged
  in this patch. Storage format tests and unrelated completed qualification
  were not repeated. This adds no multi-monitor DPI or frame-rate qualification.

Captures: `build/notes-spacing-compact.jpg` and
`build/notes-spacing-reapply-normal.jpg`. Logs: `build/notes-spacing-build.log`,
`build/notes-spacing-native.log`, `build/notes-spacing-release.log`, and
`build/notes-spacing-defender.log`. Result: `build/notes-spacing-result.json`.

Local package: `build/notes-spacing-review/ctSpaces6.3.0.0.zip`.

- EXE SHA256: `B87BC3AEA611EEC701A674EAA590F1B90CC7CD165E1E7CD3A84D8E7716359A0F`
- ZIP SHA256: `9598CD142D643A82983744754DE70C8DA8358A1FCB37F93CA58CD2C4ECF7C3FA`
- Prior launcher-review ZIP, unchanged: `A18DB64367D5E26D59A5D5C4C7DFA52C5878261BBA3F2A307394CF0F34E892C5`
- Published ZIP, unchanged: `12527A5E45F4A27315458170888D484AEE62998F424C228964B857546C7EDA16`

Earlier chat request-preparation failures remain unresolved.
