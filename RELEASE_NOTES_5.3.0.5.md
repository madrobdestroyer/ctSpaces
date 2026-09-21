# ctSpaces 5.3.0.5 — Cleanup credit and immediate multi-client deletion

- About thanks Cameron Kincer for the client cleanup idea.
- Inactive-client cleanup uses the broom icon, separate from the Delete
  Profile trashcan.
- Options (gear) > Delete Multiple Clients... lists safe, closed clients,
  including archived clients. Select any number for immediate deletion,
  without waiting for three-month activity history. Nothing starts selected.
- Both deletion screens support click-to-toggle selection, Select All, Clear
  Selection, and a Delete Selected button showing the selected count. Ctrl is
  not required. Inactive cleanup initially selects its eligible clients;
  users can deselect any they want to keep.
- Delete requires both a nonempty selection and acknowledgement of permanent
  data loss. Cancel remains the default action. Only selected clients are
  removed, along with every browser slot, icons, managed shortcuts, and saved
  client preferences. Exported backups and unrelated shortcuts remain intact.
- Existing whole-client layout/path/reparse/browser-use checks are shared and
  repeated before each deletion. Open or unsafe clients are excluded. Manual
  selection deliberately bypasses only the inactivity requirement, not the
  deletion safety checks.

Verification is scoped to the Release build, native activity tests, release
gates, existing shortcut/rename/archive workflow, and the extended disposable
real-browser cleanup workflow. The latter checks About credit, cancellation,
acknowledgement with/without selection, Select All/Clear Selection, immediate
deletion of selected recent/baseline clients, unselected-client preservation,
and open/unsafe-client exclusion. Installed app and real clients are unchanged.
