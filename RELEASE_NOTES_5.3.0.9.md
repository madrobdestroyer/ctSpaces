# ctSpaces 5.3.0.9

This maintenance update hardens cleanup recovery and secondary windows.

- A locked file can no longer cause normal client deletion to remove the
  identifying files needed for a retry. Deletion checks locks before making
  changes, preserves the client's identifying files and activity date while
  removing browser data, and verifies the final result. An incomplete deletion
  remains an error, with a retry path or an exact retained location.
- If a cleanup selection dialog cannot be created, ctSpaces reports the error
  and deletes nothing. If the result dialog cannot be created, a themed,
  paginated fallback preserves the outcome instead of losing the summary.
- Secondary buttons and text menus use their own control's font and display
  scale. The launcher's primary-button styling is retained.
- Finishing a Default-profile operation cannot unlock the launcher while
  another launch is still running, and a launch completion cannot unlock it
  while Default processing is still active.

The three-calendar-month rule, explicit deletion acknowledgement, multi-select
deletion, client/browser isolation, client-only shortcut names, favicon support,
and the requirement to save Default changes are unchanged. About retains
Cameron Kincer's credit for the cleanup idea.

See `docs/RELEASE_VALIDATION_5.3.0.9.md` for the test evidence and its boundaries.
