# ctSpaces 6.3.3.0 — unpublished review

Browser tab layout now has an all-client choice and a client-wide override.
Settings > Tab layout - all clients offers Keep browser's choice, Vertical tabs
and Horizontal tabs. Settings > Tab layout - this client and the pinned-client
context menu offer Follow global setting, Vertical tabs and Horizontal tabs.
Horizontal explicitly turns vertical tabs off. Choices apply on the next
managed-browser launch; open browsers and normal PC browser profiles are left
alone. Client overrides stay with the client through rename, archive and backup.
Native browser support and applicable organization policies still govern the
visible result. This is separate from Restore tabs.

Client Notes keeps its full-width editor, searchable client picker, slim tabs,
theme colors, remembered window geometry and automatic saving. Find, Snippets,
Templates, Add update and Format stay in one compact row. Details opens when
needed; the status indicator is read-only, and a linked ticket opens in the
chosen regular PC browser outside client profiles. All notes, Trash, More and
Help remain in the header. Saving only draws attention when there is a problem.

This review adds the everyday editing tools needed for longer notes and logs:

- Temporary Find/Replace rows with Match case, Whole word, Wrap, match counts and
  inline feedback. Replace all forms one Undo action.
- Remembered word wrap, Go to line (**Ctrl+G**), and document/selection counts.
  Deliberate soft breaks and lines inside code boxes count as logical lines.
- Windows spelling review (**F7**) using installed dictionaries. Corrections are
  explicit; code, native tables, links and technical tokens are skipped.
- Printing (**Ctrl+P**) from an independent rich-text copy. Choose Microsoft
  Print to PDF in the Windows printer dialog for PDF output. The native Windows
  print host does not provide an in-dialog preview for this app.
- Previewed UTF-8 Markdown import/export. Import creates a separate client note;
  conversion warnings explain unsupported formatting. RTF preserves formatting,
  and notebook export retains metadata and history.
- Simple native editable tables, up to 12 rows and 12 columns. Tab and Shift+Tab
  move between existing cells without adding rows.

Search **More** for these tools. They add no permanent sidebar or footer.
The selected layout, quiet autosave and same-window client switching remain.
The existing application walkthrough videos remain unchanged.

Adversarial checks also repaired replacement/link binding and rejected native
selections, wrap being reset by layout, spelling dropdown rendering, bounded
Markdown conversions, dialog-hosted table navigation, code-line navigation, and
Details explanation clipping at narrow widths. The Details scroll range follows
the actual wrapped text while unchanged layout avoids scrollbar and position
changes.

The subsequent live review also repaired native edit-field borders in dark
themes, stable Find/Replace accessibility labels, navigation between populated
table cells, and link context menus when clicking the right half of a glyph.
The final review exercised the real app's input, resizing, same-window client
switching, exact URL copy/paste with Undo, PDF output and print cancellation.
Stepwise captures do not establish the absence of between-frame flicker.

See the [user guide](docs/USER_GUIDE.md) for access and format limits, and
[release validation](docs/RELEASE_VALIDATION_6.3.3.0.md) for exact candidates,
completed checks and the remaining visible/physical review boundaries. Version
remains 6.3.3.0; this local review is not installed, packaged or published.
