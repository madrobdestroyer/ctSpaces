# Ticket links, code boxes, and top actions review

October 10, 2026. Local review build at version 6.3.0.0. Extends the prior Notes
productivity work; no implementation restart, version bump, publication, or
installation over live client data. Prior packages are preserved.

## Behavior

Ticket URLs open through Windows' default browser association with no client
profile arguments. Confirmations explain the regular sign-in session. Editor
and All notes buttons use this route, as does clicking the active note's saved
ticket URL in its body. Other note links retain client-browser behavior. All
notes now flushes through its owning editor rather than its preview dialog,
which previously prevented a ticket launch when draft capture failed there.

The former 256-note cap was an implementation guard. Notebook capacity is now
10,000 notes per client, including archived/deleted notes, within the unchanged
256 MiB total byte bound. Validation no longer compares every name/ID against
every earlier page; ordered sets retain case-insensitive name and identity
checks. Shared template/snippet libraries keep their own 256-entry limit.
Older builds may reject larger notebooks safely; export before downgrading.

Choosing Code opens a language dropdown and plain source editor. Insertion
creates a bordered native RTF table with a language caption and Consolas source.
Selected prose pre-fills the source and is replaced on insertion. The caption
is editable text; the language labels do not provide syntax highlighting. Code
is never executed. The box fits the minimum Notes window and keeps fixed width
across resize; long lines wrap. Automatic background and theme text preserve
contrast. Nested boxes are blocked, insertion is one Undo action, and normal
prose can continue after it. Source supports 131,072 characters per block.
Notebook/history/RTF exports retain the box; readable HTML handovers keep the
caption and text with normalized native line/cell separators.

Find, Snippets, Templates, Status, and Add update have direct top buttons.
Windows narrower than 1240 logical pixels place these in one compact row below
the slim tabs. Wider windows place them beside the title. Status displays the
current local work status; it does not synchronize the ticketing system.
Templates offers Create note, Name / hint, Update from note, Delete template,
and Save as new. Save as new creates a starter from the current note. Its layout
keeps all actions on the same baseline at smaller sizes. User Guide and the
14-topic in-app walkthrough explain the changed controls and browser route.

## Validation

- Release x64 build passed without compiler warnings or errors.
- 257 native production-dialog checks passed, including existing Notes features
  and new tests for browser routing/cancellation/errors, current Status label,
  direct Find, responsive button separation at 900/1240/1500 logical widths,
  template creation/reload from the manager, language choices, code literal
  syntax/Unicode/surrounding prose, real table formatting, nested insertion
  rejection, one-step Undo, disk round trip, readable handover, and light theme.
- Storage tests passed at exactly 10,000 notes and rejected the 10,001st. Existing
  format migration, duplicate identities/names, malformed input, Unicode,
  bounded files, atomic failure, and unsafe-path checks passed.
- Guided walkthrough source/resource contract passed.
- Computer Use reviewed the real isolated QA app, including the language
  dropdown, multiline source entry, inserted box, save/close/reopen, direct
  Status editor and updated top label, Templates controls, and maximize/restore
  between wrapped and single-row actions. Live client data was untouched.
- The final link included updated actual workspace/template screenshots for
  the walkthrough. Native checks preceded only this screenshot resource update;
  implementation was unchanged. No reason existed to repeat the entire suite.
- Final release gates and Microsoft Defender passed on the packaged executable.
  ZIP verification confirmed one root executable and an identical extracted hash.

This review does not qualify continuous resize frame timing, multi-monitor DPI
transitions, or interactive performance with 10,000 notes displayed at once.
No real external ticket was launched; routing was checked at the shell boundary
to avoid touching ticket sign-ins. Earlier chat request-preparation failures
remain unresolved.

## Evidence

Actual captures: docs/screenshots/notes-code-picker.jpg,
docs/screenshots/notes-code-block.jpg, docs/screenshots/notes-workspace.jpg,
docs/screenshots/notes-templates.jpg, and build/notes-code-wide-review.jpg.

Logs: build/notes-code-build.log, build/notes-code-native.log,
build/notes-code-storage.log, build/notes-code-release.log,
build/notes-code-defender.log. Package and final hashes are recorded in
build/notes-ticket-code-result.json.

Local package: `build/notes-ticket-code-review/ctSpaces6.3.0.0.zip`.

- Executable SHA256: `B405017F3CC921784FC63352599C1FB417E6D168C6A67FBE1086A456706DA94D`
- ZIP SHA256: `2EC0590E2DC46325EDC69D965D90DAC6FC580199E6DF3CE8138E13CA86DBDF7A`
- Published ZIP remains unchanged:
  `12527A5E45F4A27315458170888D484AEE62998F424C228964B857546C7EDA16`.
- Prior productivity review ZIP remains unchanged:
  `AD80397FB7139EEC1ECF41F78ED240077242E2D30130254BD6F898339D4762B0`.
- Prior walkthrough review ZIP remains unchanged:
  `5D8624F4A829D475C3E424E9A24294970253B3771E41FB29D5B18C787212CE8A`.

Implementation references: Microsoft's [ShellExecuteW documentation](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutew)
and [RichEdit table cell attributes](https://learn.microsoft.com/en-us/windows/win32/api/richedit/ns-richedit-tablecellparms).
