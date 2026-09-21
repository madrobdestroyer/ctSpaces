# ctSpaces 5.3.0.6 — Themed cleanup screens

- Inactive-client cleanup and immediate multi-client deletion now use the
  selected application palette for their background, text, list rows and
  selected-row highlights, checkbox, borders, buttons, and window chrome.
- Cleanup completion, no-client, and inspection-error messages now use a
  themed dialog with scrollable read-only details instead of a bright Windows
  message box.
- Selection, acknowledgement, default Cancel, activity cutoff, and whole-client
  deletion protections are unchanged.

Verified with the Release x64 build, release gates and activity integration
guards, plus the disposable real-browser cleanup workflow in Dark - Gothic,
Dark - Crimson, and Marine (light). Rendered palette checks cover dialog/list
backgrounds, selected rows, checkbox fill, buttons, and completion details.
The existing selection/cancel/activity/open-client/deletion safety assertions
also pass. The render harness is DPI-aware and waits for initialization before
capturing controls. Installed app and real clients remain unchanged.
