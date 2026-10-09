# ctSpaces 6.2 (6.2.0.0)

Client Notes supports formatted text and separate notes for user-named tickets.

- Existing notes open in the initial **Notes** tab. **+ Ticket** creates a tab with its own content, and **Rename tab** changes its name.
- A compact, resizable editor has readable ticket tabs, one grouped formatting toolbar with hover hints, and padding around the document. Resize or maximize it for more writing space; your preferred size is remembered for the next opening.
- Format notes with headings, font sizes, bold, italic, underline, highlighting, bullets, numbered lists, checklists, links, undo, redo, and clearing formatting.
- Changes autosave after a brief typing pause. Switching tabs and **Close** finish pending saves. A failed save reports its status and keeps the draft open.
- Web links in a note open in that client's selected browser space after confirmation. Already open Chromium clients receive a new tab; the editor stays open.
- Notes retain the selected theme and follow client rename, archive, and full backup. Whole client deletion removes them.

The first notebook save preserves the old TXT notes. The bounded notebook stores up to 256 tabs, with 32 MiB of text and 64 MiB of formatted content per tab and 256 MiB total. Pasting accepts plain text; embedded objects, pictures, and external RTF fields are rejected. Links require confirmation before opening.

The download ZIP contains exactly one root file: `ctSpaces.exe`.

An already-open isolated Firefox client cannot accept another command-line URL. ctSpaces explains this rather than starting a second browser process for the same profile.
