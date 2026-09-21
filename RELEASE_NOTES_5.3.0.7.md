# ctSpaces 5.3.0.7 — Consistent popup styling

- About now themes its content, link, background, buttons, and window chrome.
  Its link stays inside the dialog's content area.
- All app message-box calls share palette handling, including their button
  band, while retaining Windows' native layout, button results, default-button
  behavior, and cancellation semantics. Fatal errors before palette
  initialization and Windows-owned file pickers keep Windows styling.
- Pinned-client and session overflow menus now use the same owner-drawn menu
  renderer as Options and other client menus. Tooltip colors no longer get
  overridden by Windows visual styles.
- Cleanup's broom is a scalable vector glyph painted in the current menu text
  color, so it stays visible in dark themes and when selected.
- Nothing-to-delete messages are compact and do not use a large selectable
  details box. Completion details preserve paragraph breaks, start with focus
  on OK rather than selected text, and hide the scrollbar when the text fits.
  Needed list/detail scrollbars use light/dark control styling.
- Client selection, the three-calendar-month cutoff, acknowledgement, and
  deletion safety checks are unchanged. Installed app and real clients are
  not replaced or modified by this build.

Verified: Release x64 build, release gates and activity integration source
guards, plus the isolated real-browser cleanup workflow in Dark - Gothic,
Dark - Crimson, and Marine. Rendered checks cover About content/link,
compact no-client messages and button bands, cleanup palettes, no initial
details selection, and visible dark-scrollbar brightness. Themed No cancels
exit; existing acknowledgement, multi-selection, cancel and whole-client
deletion protections pass. Some earlier QA attempts encountered conservative
process-inspection skips during browser startup; no deletion checks were
relaxed. Package contains one executable, verified against the build hash.
No new all-app audit or Defender scan was performed.
