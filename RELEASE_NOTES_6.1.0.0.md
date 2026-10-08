# ctSpaces 6.1.0.0

## Three requested improvements

1. **Close All Clients.** Request normal closure of tracked client browsers
   together while keeping ctSpaces open. Respond to any browser prompts.
   Refused or delayed closes remain tracked and are not silently forced.
   The Default editor, Temporary profile, and unrelated browser windows are
   not part of the request.
2. **Filter clients while typing.** Match any part of an existing client
   name without replacing the text you type. Choose a result explicitly,
   or keep your exact new name and use Create. Names that overlap with an
   existing client no longer trigger automatic completion.
3. **Client Notes.** Open a separate themed notes window from Settings or
   a pinned client's right click menu. Local reminders stay with the client
   through rename, archive, and full backup. Whole client deletion removes
   them. Notes are plain local text, not encrypted or synced.

The full guide, What's new, and the optional visual tour explain these
additions. Earlier acknowledged guidance remains acknowledged; each new
feature has its own unread indicator. The visual tour now has 23 steps and
does not perform client or browser actions.

## Distribution

The normal application ZIP remains unchanged in structure and contains
exactly one file, `ctSpaces.exe`. Source, tests, guides, and validation
information stay in the GitHub repository, not the colleague ZIP.

Existing browser profiles and settings remain in their current locations.
This release does not implement the other planned support, nickname,
sharing, download, or copied link improvements.
