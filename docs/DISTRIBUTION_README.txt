ctSpaces 6.3.5.0 - Coworker release
Cumulative changes since the team's 6.1.1.0 version

The runtime ZIP contains exactly one file: ctSpaces.exe. This readme, guides,
release history, source and license notices remain in the GitHub repository.

6.3.5.0 fixes valid backups failing when temporary extraction paths are longer
than their final client locations. It improves extraction error details and
handles an extraction allocation failure safely. The backup format is unchanged.

1. Make a full client backup before updating, then close the old application.
2. Extract the ZIP to a folder.
3. Run ctSpaces.exe and follow the setup/update prompts for your Windows account.
4. Choose a browser before creating a new client.

The biggest change is Client Notes: a formatted notebook with saved notes,
reorderable tabs, All notes search, archive/Trash, templates, snippets, work
updates, tables, Find/Replace, spelling, print/PDF and Markdown interchange.
Open Notes beside Close all. It stays available when the launcher is minimized
or closed to the tray. Autosave stays quiet unless there is a problem.

Choose a regular PC browser for ticket/note links in Details > Open links in.
These links open outside isolated client profiles and do not change your
Windows default browser. Right-click a link to copy its complete address.

Settings offers global Vertical, Horizontal and Keep browser's choice, with a
client-wide override for new windows. Horizontal explicitly sets the vertical
tab preference to off. Running browsers are not changed.

Change Edge's layout using ctSpaces:
1. Bookmark old tabs you need, then close the client browser window normally.
2. Select that same client and Edge in ctSpaces and turn Restore tabs OFF.
3. Choose Vertical or Horizontal in the global/client Tab layout menu, then Open.
4. After closing this new window normally, you can turn Restore tabs ON to
   request that newly saved session.

Restore OFF starts a new session; the older saved tabs are not reopened.
Cookies, sign-ins and other profile data remain. With Restore ON, Edge reopens
the saved session's layout rather than changing that old window's layout.

Back Up All Client Data now lets you leave clients out and shows progress by
stage. Restore replaces the active client collection; it does not merge it.
The previous collection is kept in a pre-restore safety folder. A client left
out of the backup will not appear in that restored active collection.

Existing notes remain readable, but saving uses a notebook format that
6.1.1.0 cannot read. Keep a backup before downgrading. Notes
are local and unencrypted; snapshots/history aid recovery but are not backups.

Supported application choices are Edge, Chrome, Brave and Firefox. This is not
a claim that every browser/version/policy or SSO setup was tested. Native
vertical tabs depend on browser support and organization policy. Browser
security can keep protected sign-ins tied to a Windows user or computer.
See the release validation for actual browser checks and remaining limits.

This application is unsigned. Follow your organization's software policy.
Delete and cleanup permanently remove the selected clients after confirmation.

Repository starting points:
- docs/CHANGES_SINCE_6.1.1.0.md - Shareable team change list
- docs/USER_GUIDE.md - Everyday use, notes, browser settings and backup/restore
- docs/COLLEAGUE_QUICK_START.md - First steps
- RELEASE_NOTES_6.3.5.0.md - Cumulative release notes
- docs/RELEASE_VALIDATION_6.3.5.0.md - Completed checks and retained boundaries
- LICENSE - Application license
- 3p/7zip/DOC/ - Bundled 7-Zip notices
