# What's changed since ctSpaces 6.1.1.0

**For coworkers updating to 6.3.5.0:** this covers all changes since the last
version shared with the team, rather than just the latest patch.

The 6.3.5.0 patch fixes valid backups failing at longer temporary restore paths,
improves extraction diagnostics and safely handles an extraction allocation
failure. The backup format is unchanged; final client-path limits still apply.

## Notes is now a full notebook

- Open **Notes** beside **Close all**, then use one searchable client picker.
  Switching clients keeps the same Notes window and its placement.
- Keep several formatted notes per client. Create a tab, rename it later, drag
  tabs into order and reopen closed notes from **All notes**. Closing a tab does
  not delete the note.
- Search titles, ticket references and text. Archive notes, restore them from
  **Trash**, or confirm permanent deletion. Trash has no automatic expiry.
- Use headings, highlights, lists, checklists, labeled links, code boxes and
  editable tables. Formatting appears when needed, keeping the writing area
  full width and matched to your theme.
- Make reusable templates and snippets in their own editors. Add local status,
  tags and ticket details, capture quick notes, append work updates and export
  handovers. Templates/snippets are shared; each resulting note stays with its
  client.

## More tools without a crowded toolbar

- **Ctrl+F / Ctrl+H:** Find/Replace with options and counts; Replace all is one Undo.
- Word wrap, **Ctrl+G** to go to a line, and document/selection counts.
- **F7:** review spelling with installed Windows dictionaries.
- **Ctrl+P:** print, or choose Microsoft Print to PDF in the Windows dialog.
- Preview Markdown import/export; use RTF for formatting or notebook export for
  metadata, links and history.
- Simple tables, up to 12 by 12, with Tab/Shift+Tab movement between cells.

Search **More** for a command. Code boxes are notes, not executable code, and
status/ticket fields do not connect to or update a ticketing system.

## Notes can stay up while the launcher is out of the way

Notes has its own taskbar window and keeps working when the launcher is
minimized or closed to the tray. Autosave stays quiet unless there is a problem.
Save failures keep the draft open; snapshots, version history and conflict
previews aid recovery. Undo is an editing-session tool, not a history that
survives closing the note editor.

Choose **Details > Open links in** to use a regular PC browser for note and
ticket links. For example, use Edge for work while keeping Chrome as your
personal default. This does not use a client's isolated browser profile or
change your Windows default. Right-click links to copy their full addresses.

## Browser layout and backups

- Choose **Settings > Tab layout - all clients**: Keep browser's choice,
  Vertical tabs or Horizontal tabs for new windows. Horizontal explicitly sets
  the vertical-tab preference to off.
- Give one client a different layout, or let it Follow global setting. Its
  choice applies to all its browser slots on the next launch. Temp follows
  global; the Default editor keeps its own choice. Keep stops enforcement; it
  does not undo a layout the browser has already adopted.
- **Change Edge's layout using ctSpaces:** first bookmark old tabs you need and
  close the client window normally. Select that same client and Edge, turn
  Restore tabs off, choose Vertical or Horizontal in ctSpaces, then open it.
  Restore on reopens the saved session's layout. After closing the new window
  normally, you can enable Restore tabs to request that newly saved session.
  The Restore-off launch starts a new session rather than reopening the old
  tabs; cookies, sign-ins and other profile data remain.
- Exclude clients from an all-client backup using its searchable selection.
  Progress now explains scanning, compression, verification and final saving.
- Restore validates the backup and keeps the old collection in a safety folder.
  It **replaces**, rather than merges, the active collection, so clients left
  out of a backup will be absent from that restored collection.
- Reliability fixes strengthen client identity, rename/archive/delete, selected
  menu actions, Temp/Default cleanup, saving, link retention and window resizing.

## Before updating

Make a full backup and close the old app. Extract the ZIP and run its
`ctSpaces.exe`. Existing notes remain readable; saving uses a newer notebook
format that 6.1.1.0 cannot read. Keep a backup before downgrading.

Edge, Chrome, Brave and Firefox are supported application choices, not a promise
that every browser/version/policy has been tested. See the
[release validation](RELEASE_VALIDATION_6.3.5.0.md) for actual browser checks and
limits, and the [user guide](USER_GUIDE.md) for instructions. Notes are local and
unencrypted; ctSpaces is not a security sandbox or a cloud-sync service.
