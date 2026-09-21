# ctSpaces 5.3.0.8 — Simple themed text menus

- Replaced Windows' default text-field context menu with a compact menu using
  the app's existing owner-drawn palette and selection/disabled colors.
- Editable client-name and input-dialog fields offer Undo, Cut, Copy, Paste,
  Delete, and Select All. Read-only result fields offer only Copy and Select All.
  Availability follows selection, clipboard, undo, and read-only state.
- Removed reading-order, Unicode-control-character, and IME menu entries.
  Normal keyboard shortcuts and international text input remain unchanged.
- Handles both mouse right-click and keyboard context-menu requests. Native
  edit commands preserve undo, clipboard handling, input limits and change
  notifications. Menu item pointers remain valid during tracking; cancellation
  does not edit text, and password fields cannot expose Copy/Cut.
- Applies to the visible client field, the underlying combo edit, rename/icon
  input dialogs, and read-only details through shared edit subclassing.

Verification is scoped to the Release build/gates and disposable UI workflow:
actual text-menu entries, dark/light rendering, Select All/Delete/Undo menu
actions, read-only menu, and the existing cleanup protections. Test clicks
target the isolated QA popup and restore the pointer afterward; the user's
clipboard is not modified by the menu-action test. No all-app audit or new
Defender scan. Installed app and real clients are unchanged.
