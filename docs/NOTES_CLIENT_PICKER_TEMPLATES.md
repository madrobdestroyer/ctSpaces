# Notes client picker and templates review

October 10, 2026. Local review build, version 6.3.0.0. No version bump,
GitHub publication, or installation over live client data.

The unused second input was hidden, but the common theme painter still drew
its border. The painter now skips hidden controls, and single-field prompts
reduce their height. Move/copy uses a dedicated editable dropdown containing
other validated, active clients. Typing filters names case-insensitively while
preserving the query and caret. The confirmation and destination/source save
ordering are unchanged. The dropdown uses the notes themed surface and items.

More offers Templates with descriptions and formatted previews, and Save
current note as template. The four built-in starters initialize the library;
users can save their own, edit names/hints, update content from the current
note, or delete a template. Existing notes remain independent. The shared
library is stored in the app data directory's NotesTemplates subdirectory
using the validated notebook format and atomic snapshot-checked writer.
Hints occupy the template record's ticket field internally and are cleared
when creating a note. Ticket URLs, revision history and per-note view/session
state are excluded from saved templates; labeled document links are retained.
Shared templates are not included in a single-client notebook export.

Recently deleted retention is unchanged: no automatic expiry. Entries remain
in their client's notebook until confirmed permanent deletion and count toward
the 256-note limit. Its preview now states this explicitly. Client deletion
still removes the entire client, including its notebook.

Validation: Release x64 build with zero warnings/errors; 124 native workspace
checks passed, including typed suggestions, canonical destination resolution,
hidden-input border painting, persisted custom templates, name/hint edits,
content updates, theme refresh, independent created notes, permanent template
deletion, stale-write rejection and trash retention text. Release validation
and Defender scan passed on the exact executable. Existing browser behavior
and client storage serialization did not change; unrelated suites were not
repeated. Logs: build/notes-templates-build.log, notes-templates-native.log,
notes-templates-release.log, notes-templates-defender.log and
notes-templates-package.log.

Computer Use review ran the exact packaged executable in a disposable QA
instance. Verified the single-field themed move dialog, actual keyboard input
and matching dropdown suggestion, selection of the destination, and the final
template descriptions, preview and editing buttons. Captures:
build/notes-client-picker-reviewed.jpg and build/notes-templates-reviewed.jpg.
The move was canceled after selecting the test destination. Template content
updates/deletion and cross-client persistence were verified by the native
production-dialog harness. No real client notebook was edited.

Final review package: build/notes-client-templates-review/ctSpaces6.3.0.0.zip.
It contains exactly one root ctSpaces.exe, with the tested executable hash.

- EXE SHA256: 2331876964DAE2A0988835950CC970136FA2DF91D0A83A4AFB9D525F7D244727
- ZIP SHA256: 8590B7679B1E63BF747F55FCEDAD2CFA6040741879E21ECB9C81FF8C8D47CE7C

Earlier local review packages and the published dist ZIP are preserved.
