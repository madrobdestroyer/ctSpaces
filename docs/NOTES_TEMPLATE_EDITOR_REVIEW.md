# Direct template editing and Notes transition review

October 12, 2026. Local review build at version **6.3.0.0**, continuing the
existing Notes work in `C:\Projects\ctSpaces` on branch `cpp`. No version bump,
publication, installation, commit, or new branch was made for this review.

## Behavior

Templates now opens one combined create, edit, and use window. Saved starters
appear on the left; their name, usage hint, and editable formatted content appear
on the right. **New template** starts an independent draft without requiring an
existing client note. The editor offers normal text, headings, subheadings, bold,
italic, underline, and bullets.

**Save template** saves the reusable starter and keeps the manager open.
**Use template** saves pending changes first and creates an independent note for
the current client. **From current note** remains an optional starting point.
The top Templates button and corresponding Actions entries use this same window.
Changing a starter does not change notes previously created from it.

Switching templates or closing with changes offers Save, Discard, or Cancel.
Duplicate names, bounded content, conflicting library writes, and failed saves
keep the draft available instead of silently replacing it. Theme changes retain
unsaved template content. New blank drafts reset inherited heading/list styles.
Keyboard paste now routes Ctrl+V and Shift+Insert through the existing bounded
plain-text paste handler in both the template editor and client Notes. This fixes
the RichEdit keyboard path bypassing WM_PASTE while unsafe object imports remain
blocked.

The reported add/delete glitch had two visible sources: a newly created editor
could be shown at its temporary 100 by 100 size before its document and layout
were ready, and tab rebuilding exposed intermediate empty states. Editors now
load and lay out while hidden, with the previous document remaining visible
until the final swap. Tab updates batch redraw, then restore redraw before native
selection and overflow layout. Pristine closed/deleted editors retire after the
replacement is ready. Closing or deleting the active tab selects its adjacent
open tab, preventing the previous jump to the first tab. The document outline
invalidates after the visibility swap.

The existing theme-based Notes design, slim tabs, Notes autosave without footer
buttons, remembered window size, ticket/browser routing, code boxes, productivity
features, and walkthrough remain in place. Templates use explicit Save because
they are shared starters, separate from each client's autosaved note drafts.

## Validation

- Final Release x64 build passed without compiler warnings or errors.
- **344 native production-dialog checks passed**, including the previous Notes
  checks and direct template creation, formatting, save/reload, independent
  client content, save-before-use, unique names, navigation cancellation,
  discarded drafts, failed/conflicting saves, theme draft retention, keyboard
  paste, blank-draft formatting, readable fonts, and responsive action spacing.
- Repeated add/delete/close checks observe actual RichEdit windows. They reject
  visible document streaming and visible temporary geometry, retain cached
  documents/undo editors, verify adjacent selection and retirement, and test
  closing every tab and restarting into the empty state.
- Command timings for the final repeated native fixture: adding notes had a
  median of **38.4441 ms** and maximum of **43.9285 ms**; delete/close commands had
  a median of **30.5143 ms** and maximum of **42.0443 ms**. These are synchronous
  command timings, not display-frame measurements or performance guarantees.
- Computer Use reviewed the actual isolated app: authored a new template with
  name, hint, and body directly in the manager, saved it, and used it to create a
  note. It also exposed the blocked keyboard paste and first-tab selection jump,
  which were corrected before the final native run. The exact final executable
  was reviewed for add/close with adjacent selection, Templates, and
  maximize/restore. Its SHA256 matches the packaged executable.
- Guided walkthrough source/resource checks passed. The in-app template topic,
  User Guide, Notes walkthrough, and developer documentation describe the new
  combined editor. The walkthrough includes the actual updated Templates image.
- Final release gates passed. Microsoft Defender found no threats in the final
  executable. Packaging verified exactly one root `ctSpaces.exe` and an entry
  SHA256 identical to the final executable.
- Published and prior ticket/code, productivity, and walkthrough ZIP hashes
  remain unchanged. Prior storage validation at 10,000 notes is retained; the
  storage format/parser was unchanged, so that completed suite was not repeated.

This review does not qualify continuous resize frame timing or multi-monitor
DPI transitions. A resize-drag attempt through Computer Use was rejected because
its endpoint lay outside the captured window; maximize/restore was inspected.
Delete behavior was exercised in disposable native fixtures, without deleting
live client notes through the UI. Earlier chat request-preparation failures
remain unresolved.

## Evidence and package

Actual captures:

- `docs/screenshots/notes-templates.jpg`: combined editor used by the walkthrough.
- `docs/screenshots/notes-template-created.jpg`: directly authored and saved starter.
- `build/notes-template-final-review.jpg`: exact final executable, reopened manager.

Logs: `build/notes-template-build.log`, `build/notes-template-native.log`,
`build/notes-template-release.log`, and `build/notes-template-defender.log`.
Machine-readable result: `build/notes-template-editor-result.json`.

Local package: `build/notes-template-editor-review/ctSpaces6.3.0.0.zip`.

- Executable SHA256: `1DE746A53B01860104E796AD184B4BF4D2106A45DA29B226A29BC5FB3061D051`
- ZIP SHA256: `F98195434916FCB4CBAB433797175F5190ED9E764A6FE08883C64EC20C21B129`
- Published ZIP: `12527A5E45F4A27315458170888D484AEE62998F424C228964B857546C7EDA16`
- Prior ticket/code ZIP: `2EC0590E2DC46325EDC69D965D90DAC6FC580199E6DF3CE8138E13CA86DBDF7A`
- Prior productivity ZIP: `AD80397FB7139EEC1ECF41F78ED240077242E2D30130254BD6F898339D4762B0`
- Prior walkthrough ZIP: `5D8624F4A829D475C3E424E9A24294970253B3771E41FB29D5B18C787212CE8A`
