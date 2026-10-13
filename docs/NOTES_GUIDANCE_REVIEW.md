# Notes guidance and bulk trash review

October 10, 2026. Local review build, version 6.3.0.0. This iteration does not
bump the version, publish to GitHub, or install over the live application.

All notes now starts with the active note selected when present in its results.
Ticket numbers appear beside note titles and on their own line in the preview;
a valid ticket URL enables the labeled Open ticket link action. The ticket
dialog explains that the fields label/link a note and do not fetch or sync
ticket data. Current, open, archived and deleted views explain their contents
and actions. Closing a tab is distinguished from archiving or moving to trash.

A theme-aware first-use guide covers writing, autosave, finding notes, optional
tickets, shared templates and trash. It offers direct routes to templates and
All notes and remains available from Help and More. Dismissal is persisted in
the existing app configuration. No permanent editor footer was added.

Recently deleted now offers Empty trash. Its confirmation names the client and
total deleted-note count, includes notes hidden by the current search, explains
that revision histories are removed, and defaults to cancellation. The command
works with no selected result. It rejects read-only views and other filters,
saves pending edits before erasing pages, and preserves current/archived notes
and other clients' notebooks. Deleted notes retain the existing no-expiry policy
until the user confirms permanent removal. The library minimum width is 800
logical pixels to keep the additional action clear of Close.

Validation: final Release x64 build passed without compiler warnings/errors;
151 native production-dialog checks passed. New checks include visible ticket
references, ticket searches, initial selection, help persistence/navigation
controls, explanatory input layout, bulk deletion of 100 deleted notes, default
cancel scope, filtering with one or zero matches, read-only rejection, locked
notebook save failure, history removal, persistence, other-client isolation and
minimum-width button separation. Existing notes, frame, picker and template
checks remain part of that focused harness. The final release checks and
Microsoft Defender scan passed on the packaged executable.

Computer Use reviewed the actual executable in the existing isolated QA
instance. It caught unreadable native group-box headings in the dark theme;
these were replaced with themed static headings and reviewed again. The review
verified ticket 44553 in All notes, the explanatory ticket dialog, the Help
button and guide, the guide's Open All notes route, and the final Recently
deleted labels, retention and bulk-scope text. No live client data was changed.
Minimum-size button separation and bulk erasure were verified by the native
harness. Interactive captures are point-in-time observations, not continuous
frame-time measurements of resize animation.

Captures: build/notes-guidance-help.jpg, notes-guidance-library.jpg,
notes-guidance-ticket.jpg and notes-guidance-trash.jpg.
Logs: build/notes-guidance-build.log, notes-guidance-native.log,
notes-guidance-release.log, notes-guidance-defender.log and
notes-guidance-package.log.

Package: build/notes-guidance-review/ctSpaces6.3.0.0.zip, verified to contain
one root ctSpaces.exe matching the tested and visually reviewed executable.

- EXE SHA256: 33FA8ECD013CD6952859515C7BC0085673A17D0F45124D1146CD0A16E690E141
- ZIP SHA256: E3DA2654CF8AABC9424C6A87DCCCE8FFCBF185C2A159611292E96B69F8DB3353
- Published ZIP SHA256 remains: 12527A5E45F4A27315458170888D484AEE62998F424C228964B857546C7EDA16

Previous review ZIPs are preserved. The earlier chat's request-preparation
failure remains unresolved; this application change does not diagnose it.
