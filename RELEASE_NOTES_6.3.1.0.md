# ctSpaces 6.3.1.0

Client Notes now opens from the launcher beside Close all, with one searchable
client picker and a full-width writing area. Details and searchable More actions
appear on demand and close when you click outside. Each notebook continues to
belong to its client through rename, archive, backup and deletion.

- Create and edit templates and snippets directly in their shared editors.
- Organize notes with ticket details, local status, tags, sorting and bulk actions.
- Capture quick notes, append dated work updates and export readable HTML handovers.
- Access Trash directly. Deleted notes remain until restored or permanently removed.
- Use language-labeled, editable code boxes. Code is never executed.
- Drag tabs, use their context menu, or press Ctrl+Shift+PgUp/PgDn to reorder.
  Reopened notes go to the end of their pinned or ordinary group.
- Note and ticket links open in the PC's default browser, outside client profiles.
- Keep theme colors, compact text spacing, autosave and remembered window size.
  The launcher comes forward on startup and can optionally minimize to the tray.

Adversarial review also fixed stale code-border pixels after scrolling/resizing,
header buttons being treated as drag areas, inspector focus/dismissal problems,
status-dropdown Enter handling, and Close all missing a browser still creating
its first window. Browser close vetoes and unsaved-work prompts remain respected.

Validation includes 12 core suites, 16 integration suites, 531 native Notes
checks, real Edge/Chrome workflows, and real-input walkthrough recordings.
See [the detailed review](docs/FULL_APP_STRESS_REVIEW_6.3.1.0.md) and
[release validation](docs/RELEASE_VALIDATION_6.3.1.0.md) for evidence and limits.

This is one deliberate patch release after review. Intermediate fixes did not
each consume a new version number.
