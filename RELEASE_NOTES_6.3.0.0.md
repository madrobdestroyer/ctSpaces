# ctSpaces 6.3.0.0

Client Notes now keeps saved notes separate from open tabs. Close a tab with its X and reopen it from All notes or Ctrl+Shift+T. The notes window preserves the approved theme-based design, slimmer ticket tabs, autosave without a footer, and remembered window size.

- All notes provides content/title search, previews, archive, Recently deleted, and confirmed permanent deletion.
- New Untitled tabs open immediately. Tab menus add duplication, pinning, ordering, close others/all, and archive/delete. Untouched blank tabs are discarded on close.
- Undo/redo survives tab switching and closing/reopening during the notes-window session. Active note, cursor, scroll and zoom persist across openings.
- The launcher stays usable while notes are open. Client lifecycle operations flush the relevant notebook before proceeding and stop when a draft cannot be saved.
- Crash snapshots, bounded version history, conflict previews and recovered copies protect drafts. Metadata-only changes are preserved during conflict recovery. Retry, copy and export remain available after save failures.
- Find/replace, labeled links, highlight swatches, checklist/list keyboard behavior, strikethrough, code style, timestamps and view zoom extend the editor.
- Templates, creation/edit dates, optional ticket details, TXT/RTF import/export, complete notebook import/export, and explicit copy/move between clients are included.
- All notes is resizable, fits the current display, and follows the selected theme. Optional checklist progress and always-on-top settings are remembered.

Images, attachments, tables, reminders, cloud sync, collaboration and password storage are recorded in the roadmap for later work.

## Compatibility

Existing notebooks and legacy TXT/RTF notes remain readable. Saving in 6.3 upgrades the notebook to format version 2; earlier ctSpaces releases cannot read it. Keep a full client backup before downgrading, or export individual notes as TXT/RTF. Complete `.ctn` export preserves ticket/link metadata and history; TXT/RTF exports contain document text/formatting only. Notes remain local and unencrypted.

## Validation

Release x64 build, 79 native interaction assertions, notebook storage/migration and malformed-file tests, release gates, guided walkthrough tests, and Microsoft Defender scan passed. Live Windows review used disposable clients and covered the themed editor, close/reopen, undo, highlighting, checklist completion, resizing, remembered dimensions, content search and library previews. No live client data or installed application was modified. See `docs/RELEASE_VALIDATION_6.3.0.0.md` for evidence and limits.

The release ZIP contains exactly one root `ctSpaces.exe`.
