# Selected notes design: Inspector on Demand

Design decisions recorded October 13, 2026. Implemented in the local 6.3.0.0 review build. See NOTES_INSPECTOR_REVIEW.md for native validation and review boundaries.

The user selected **Inspector on Demand**. The editor keeps its full width. Ticket details, tags, and secondary tools open on demand. There is no permanent large sidebar. Retain compact tabs, automatic saving without footer buttons, theme support, remembered window size, compact paragraph spacing, and the previously completed notes features.

## Client ownership is a requirement

- Notes belong to their client and remain in that client's notebook. A common dashboard must not turn the independent notebooks into one shared notebook.
- Each client owns its own notes, archive, trash, history, and saved note metadata. Global reusable snippets and templates remain reusable across clients under their existing behavior.
- Deleting one client must not delete, rename, renumber, reassign, or otherwise alter another client's notes or history. Preserve the existing client-deletion behavior for the deleted client's own notebook; do not introduce a different retention policy through the dashboard redesign.
- Preserve the application's existing client identity and per-client storage boundaries. Do not route mutations by a filtered row number or assume that a display name alone is a safe new identity scheme.
- Client switching saves pending changes to the original client before loading the destination notebook. Do not carry the previous client's selected note, metadata, or bulk selection into the destination.
- The combined **All clients** view is for search, preview, and navigation. Archive, restore, and delete operations require opening the relevant client's notebook first. Mutation controls identify the client they affect.
- Before implementation is considered complete, verify that deleting client A leaves client B's notes intact, that switching clients saves to the correct notebook, and that stale or cross-client selections cannot mutate another client's data.

## Entry point and client picker

Add a visible **Notes** button to the main app. Open the currently selected client, or the last notes client when no client is selected. Retain the existing client context-menu entry as a convenient shortcut.

The client picker in Notes supports searching all clients by typing, with pinned and recent clients shown first. Display a short list of matches; a large client collection must not require scrolling through a long dropdown. Support keyboard selection. This changes navigation, not notebook ownership.

The main-app Notes launch button belongs on the existing main ctSpaces window. Do not add a second main-app header, launch button, or duplicate client selector inside the Notes window. Notes has one client selector in its own header.

## Writing and management

Keep status and ticket context near the title. Keep frequently used writing tools, snippets, and templates readily accessible. The **Details** popover opens only when needed and does not resize the editor.

**All notes** and **Trash** retain substantial, separate management windows rather than becoming small plain menus. They should be resizable and remember their size. Their default scope is **This client**.

The manager has Notes, Archive, and Trash views; search across titles, content, ticket numbers, and tags; status and tag filters; useful sorting; a readable preview; and explicit selection boxes for bulk operations. Results show client ownership when searching across clients.

Trash exposes **Restore selected**, **Delete selected forever**, and **Empty trash**. Permanent deletion requires a confirmation naming the client and number of notes affected. Trash has no automatic expiry under the existing notes behavior. Empty Trash affects the selected client's Trash, not every client's Trash.

Group less frequent actions by purpose and support searching them. Do not replace all discoverable actions with an undifferentiated long submenu. Preserve the walkthrough and explanatory labels that help new users understand tickets, local status, templates, snippets, and recovery.

Ordinary note links and ticket URLs continue to open in the PC's default browser outside client browser profiles.

## Temporary panels

Switch client, Details, Status, and More use compact native panels. Their owner remains enabled. An outside click or Escape closes them; opening them never changes editor dimensions. Details autosaves valid metadata. More uses a plain Windows ellipsis label, with no Command symbol. All notes and Trash are separate management windows with normal close controls.

## Review boundary

The native review build is version **6.3.0.0**, on the existing **cpp** branch. No version bump, GitHub release, or installation is part of this design trial. Previous packaged builds are preserved. Visual review uses disposable demo clients, not live client data.
