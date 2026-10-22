# ctSpaces 6.3.4.0

This is the cumulative update for coworkers whose last version was **6.1.1.0**.
It brings together the Notes redesign, everyday editing tools, browser layout
choices and reliability work from the 6.2 and 6.3 series.

## Highlights since 6.1.1.0

- A full client notebook with formatted notes, reorderable tabs, searchable
  All notes, recoverable Trash and version history.
- Templates, snippets, quick capture, work updates, tables, Find/Replace,
  spelling, printing/PDF and Markdown support.
- Notes stays available when the launcher is minimized or closed to the tray.
  Ticket and note links use a chosen regular PC browser outside client profiles.
- Global vertical/horizontal browser layout choices, with a client-wide
  override. **Turn Restore tabs off when applying a new layout in Edge.**
- Client exclusions and clearer progress for backups, plus stronger checks
  around client identity, lifecycle actions and saving.

## Client Notes

Open **Notes** beside **Close all**. One searchable client picker switches
notebooks in the same window, retaining its position and size. The full-width
editor uses the selected theme, slim tabs and formatting that appears on demand.

- Create notes immediately, rename them when useful and drag tabs to reorder.
  Closing a tab keeps the saved note in **All notes**; it does not delete it.
- Search titles, ticket numbers and content. Preview, sort, archive and manage
  several notes together. **Trash** offers restore and confirmed permanent
  deletion, with no automatic expiry.
- Use headings, highlights, lists, checklists, labeled links and language-labeled
  code boxes. Code is stored as text and never executed.
- Add local status, tags and optional ticket details in **Details**. These
  organize notes locally; they do not synchronize a ticketing system.
- Create and edit templates and snippets. These libraries are shared across
  clients; the notes created from them belong to their client.
- Capture quick notes, append dated work updates and export HTML handovers.
  TXT/RTF and complete notebook import/export, plus explicit copy/move between
  clients, provide ways to share or transfer work.

## Everyday editing tools

- **Ctrl+F / Ctrl+H:** temporary Find/Replace rows with counts, Match case,
  Whole word and Wrap. Replace all is one Undo action.
- Remembered word wrap, **Ctrl+G** to go to a logical line, and document or
  selection counts for text and logs.
- **F7:** explicit spelling review using installed Windows dictionaries.
  Code, native tables, links and technical tokens are excluded from prose checks.
- **Ctrl+P:** print the note or selection. Choose Microsoft Print to PDF in the
  Windows printer dialog for PDF output. There is no in-dialog print preview.
- Editable tables, up to 12 rows and 12 columns. Tab/Shift+Tab move between
  existing cells without adding rows.
- UTF-8 Markdown import/export with preview and conversion warnings. Import
  creates a separate note. Use RTF for formatting or notebook export for the
  metadata, links and history that Markdown cannot retain.

Find tools through searchable **More** without adding a permanent sidebar.

## Saving, recovery and links

Autosave stays quiet unless there is a problem. Failed saves retain the draft;
client switching and related lifecycle actions stop when a pending note cannot
be saved. Snapshots, bounded version history and conflict previews aid recovery.
Native Undo belongs to the open editor; closing a tab or Notes releases its queue.

Notes has its own taskbar window and stays available when the launcher is
minimized or closed to the tray. Use **Exit ctSpaces and Notes** to close the app.

Choose **Details > Open links in** or **More > Link browser** for ticket and note
links. For example, use Edge for work while keeping Chrome as your Windows
default. This does not change Windows defaults or the client's isolated browser.
Right-click a URL or labeled link to open it or copy its complete address.

## Browser layout, backups and reliability

**Settings > Tab layout - all clients** offers Keep browser's choice, Vertical
tabs and Horizontal tabs. **Horizontal explicitly sets the new-window vertical
tab preference to off.**
Each client can Follow global setting or choose Vertical/Horizontal across its
browser slots.
The preference is prepared at the next managed launch; running browsers are not
changed.
Temp follows global and the Default editor keeps its layout. Keep stops
enforcement rather than restoring a historical choice; Firefox also removes
only ctSpaces's own `user.js` override.

**Applying the layout in Edge:** the selection in ctSpaces works when opening
a new session. To use it:

1. Bookmark any currently open tabs you need, then close that client's Edge
   window normally.
2. Select the same client and Microsoft Edge in ctSpaces, then turn **Restore
   tabs off**.
3. Choose **Vertical tabs** or **Horizontal tabs** in ctSpaces, then open the
   client. It opens with the selected layout.

After closing that new window normally, you can turn **Restore tabs on** again
for later launches. Edge restores the most recently saved session, including
its layout. You do not need to change the layout inside Edge.

**The limitation concerns already saved windows:** with Restore tabs on, Edge
can bring back their previous layout despite a different ctSpaces selection.
Turning Restore tabs off starts a new session instead of reopening the old
tabs, so bookmark any old tabs you need before doing this. It keeps the client's
sign-ins, bookmarks and other profile data.

**Back Up All Client Data** now starts with a searchable selection, including
archived clients. Uncheck clients to leave them out. Progress explains scanning,
compression, verification and final saving, with item/byte counts. Full browser
profiles can contain many files even when there are few clients.

Restore validates the backup and keeps the previous collection in a pre-restore
safety folder. **It replaces the active client collection; it does not merge it.**
Clients excluded from a backup will be absent from that restored collection.

Reliability fixes cover client-name identity, stale menu targets,
rename/archive/delete ownership, Temp/Default lifetimes, save conflicts,
rejected edits, link preservation and resize/tab behavior. Notes stay with their
client through rename, archive and backup; whole-client deletion removes them.

## Updating and boundaries

Make a full backup, close the old app, extract the ZIP and run `ctSpaces.exe`.
The ZIP contains that one executable; documentation and license notices remain
in the repository. Existing notes remain readable, but saving upgrades notebook
storage. Version 6.1.1.0 cannot read the newer notebook format, so keep a backup
before downgrading. Notes remain local and unencrypted.

Edge, Chrome, Brave and Firefox are supported application choices, not a promise
that every browser version, organization policy or SSO setup was tested. Native
vertical tabs depend on browser support/policy. Browser-protected sign-ins may
remain tied to their Windows user or computer after backup/restore.

See the [user guide](https://github.com/madrobdestroyer/ctSpaces/blob/cpp/docs/USER_GUIDE.md),
[shareable change list](https://github.com/madrobdestroyer/ctSpaces/blob/cpp/docs/CHANGES_SINCE_6.1.1.0.md) and
[release validation](https://github.com/madrobdestroyer/ctSpaces/blob/v6.3.4.0/docs/RELEASE_VALIDATION_6.3.4.0.md) for instructions, browsers
actually exercised and remaining test boundaries. Native automated checks do
not establish continuous compositor behavior or certify SSO compatibility.

## Download verification

The Windows x64 ZIP contains one `ctSpaces.exe`.

| File | SHA-256 |
|---|---|
| ctSpaces6.3.4.0.zip | `EE1FCB7E1932F62156CC113F120F367CC14DFE76128BDEF6237637A2212D691F` |
| ctSpaces.exe | `26E6228B9BE2BDF685ADA4E529972EA9435E2662323285E935FFA5D1F404EFDA` |
