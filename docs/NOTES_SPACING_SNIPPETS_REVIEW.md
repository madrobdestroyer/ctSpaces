# Existing-note spacing and snippet authoring — 6.3.0.0

Validated October 13, 2026 in `C:\Projects\ctSpaces`, branch `cpp`.
This continues the local review without changing the version, installing the app,
publishing, committing, or creating a branch. Earlier ZIPs remain unchanged.

## Existing notes

Changing only the new-note defaults did not correct paragraphs already saved in
RTF. Opening an existing rich note now removes the exact former normal-paragraph
signature: zero space before, 80 twips (four points) after, and single line spacing.
It keeps character formatting, explicit blank lines, heading/subheading spacing,
and other paragraph or line spacing. A matching four-point gap imported from
another editor is indistinguishable from that old app default and is also corrected.

The original RTF is remembered as a forced version-history revision before the
new bytes replace it. History retains its existing 20-revision/32-MiB limits.
The change uses the existing autosave and conflict/recovery path; a concurrent
notebook write cannot be overwritten. Reopening a corrected note produces no
additional spacing revision. Only opened notes are migrated, avoiding a startup
rewrite of every saved, archived or deleted entry. Notes already using compact
spacing bypass the paragraph scan.

The explicit Normal text command still provides a correction for other gaps and
also applies 12-point body text. Automatic migration does not change the font size.

## Snippets

The top Snippets button now opens the direct reusable-content editor, with visible
New snippet, From selection, Delete snippet, Save snippet, Insert in note and Close
actions. The name, optional hint and formatted body can all be edited there.
Creation and editing work with every note tab closed; capture/insertion require
an active note. The Actions capture command opens the same editor with a new draft.

Snippet saving exports the real character range rather than the implicit terminal
paragraph included in a whole-document RTF export. This avoids adding an unexpected
newline when inserting an inline snippet. Save/insert failure preserves the draft;
library concurrency checks remain in place. Deleting or editing a snippet does not
change text already inserted into notes. The snippets and templates stores remain
separate, with their existing limits.

The walkthrough text, tooltip, searchable action hint, productivity/user guides and
embedded Snippets screenshot were updated. The screenshot was captured from the
production app in a disposable fixture after creating and saving Rollout sign-off.

## Validation

- Release x64 build passed without compiler errors or warnings.
- **437 native production-dialog checks passed.** Added coverage includes exact
  old-default migration, preserved font/headings/custom spacing/blank lines,
  caret/zoom restoration, autosave/reopen idempotence, pre-migration history,
  concurrent-save protection and recovery drafts; direct snippet authoring with
  no open note, formatted persistence, cancelled navigation, selection capture,
  insertion without an extra newline or tab, deletion preserving inserted text,
  and concurrent library-save draft retention.
- Interactive production-app review verified New snippet, direct name/hint/body
  entry, Save snippet, the new library entry and its saved state. The QA app closed
  cleanly. No live client data was changed.
- Release gates and guided-walkthrough source contracts passed.
- Microsoft Defender found no threats in the packaged executable.
- ZIP verification confirmed exactly one root executable with matching bytes.

Earlier startup/installer validation was retained; those paths were not changed.
During development a clipboard test intermittently found Paste enabled but inserted
no text. Shared clipboard contention during UI review is a possible cause, not a
confirmed diagnosis. The final native suite ran separately from clipboard-using UI
automation and passed.
An initial insertion test found the terminal-paragraph issue described above.

Logs: `build/notes-spacing-snippets-{build,native,release,guides,defender}.log`.
Package: `build/notes-spacing-snippets-review/ctSpaces6.3.0.0.zip`.
Executable SHA256: `0615A8AE54F69AFA44C805A9DC9C8EF5D122019D3F8E144E9EC48A2EB04436CF`.
ZIP SHA256: `E3A29534A2E68F40180A86A6888FFAA38F9CD2EDBB660C69D2615C01C88558DB`.

## Separate design proposal

Notebook and Focus are two conversation mockups of a possible future layout.
They propose a collapsible searchable library, a large writing area, compact
formatting, metadata near the title and one reusable-content panel with direct
creation. The existing application layout was not replaced. The fragment passed
JavaScript syntax checks and the bundled visualization wrapper validated it.
An automated browser preview was blocked by the browser's file-URL policy; rendered
browser layout and interaction checks were not completed. The app visual review
above concerns the actual native Snippets editor, not the proposed mockups.

The previous chat's request-preparation failure remains unresolved.
