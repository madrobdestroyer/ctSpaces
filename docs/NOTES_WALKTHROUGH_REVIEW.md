# Notes walkthrough review

October 10, 2026. Local review build, version 6.3.0.0. Continues the verified
Notes fixes documented in NOTES_GUIDANCE_REVIEW.md; no implementation restart,
version bump, GitHub publication or installation over live data.

Help retains the quick overview and adds Step-by-step walkthrough. More also
offers Notes walkthrough, including when no note is open. The dedicated themed
dialog has ten numbered topics, Next, Back, Finish and Close, a scrollable
read-only instruction pane, and real screenshot examples embedded as resources
in the executable. It requires no browser, network request or companion files.
Screenshots are explicitly labeled as dark-theme examples, while the dialog
uses the selected application theme. The window can resize, and its fonts and
layout handle DPI changes; no multi-monitor DPI transition was requalified.

Topics cover creating a note without a required title, autosave, formatting and
checklists, tab organization and closing, All notes search/filter/reopening,
optional ticket metadata, reusable/custom templates, client copy/move,
archive/trash/restore, permanent deletion, history and export/recovery.
The permanent-deletion step has a persistent bold warning: Empty trash deletes
notes and their history and cannot be undone. Instructions explain all deleted
notes in the selected client's notebook, hidden search results, total count,
No as the default confirmation choice, and backup/export before removal.
Browsing the guide performs none of the actions it describes.

Six actual screenshots from disposable QA notes are retained in docs/screenshots
and embedded as RCDATA. The user-facing docs/NOTES_WALKTHROUGH.md provides the
same everyday workflow with screenshots and explicit permanent-deletion warning.
User Guide and documentation index link to it. The existing first-use overview
remains optional and replayable; the editor gains no permanent footer.

Validation:

- Release x64 build passed without warnings/errors.
- 165 native production-dialog checks passed, including the previously verified
  frame, editor, picker, templates, tickets and bulk trash behavior. New checks
  verify the Help entry and dedicated command, ten topics, Next/Back boundaries,
  direct selection, all embedded image decodes, the deletion warning and scope,
  read-only text/no deletion control, invalid selection, minimum-size separation,
  Finish label, and unchanged notebook/editor after browsing every step.
- Computer Use reviewed the exact final executable in the isolated QA instance.
  Verified Help launch, Next, Back, all topics in the dropdown, jumping to the
  permanent-deletion step, final recovery instructions and Finish returning to
  the workspace. No live client notes were touched or deleted.
- Final release gates and Microsoft Defender passed on these executable bytes.
- ZIP contains exactly one root ctSpaces.exe matching the build and reviewed app.
  Published ZIP and previous review packages remain intact.

Logs: build/notes-walkthrough-build.log, notes-walkthrough-native.log,
notes-walkthrough-release.log, notes-walkthrough-defender.log and
notes-walkthrough-package.log. Captures: build/notes-walkthrough-step1.jpg,
notes-walkthrough-deletion.jpg and notes-walkthrough-finish.jpg.

Package: build/notes-walkthrough-review/ctSpaces6.3.0.0.zip.

- EXE SHA256: A73FC13FFA4CB11DE21AB661844572727AF7FC7A866D73F1C944AA9A2EC42D7E
- ZIP SHA256: 5D8624F4A829D475C3E424E9A24294970253B3771E41FB29D5B18C787212CE8A
- Published ZIP SHA256 remains: 12527A5E45F4A27315458170888D484AEE62998F424C228964B857546C7EDA16

The preceding chat's request-preparation failure remains unresolved. This
walkthrough does not diagnose it. Point-in-time UI review does not establish
continuous resize frame timing or flicker absence.
