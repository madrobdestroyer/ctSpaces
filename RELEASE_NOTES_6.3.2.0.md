# ctSpaces 6.3.2.0

The Notes client selector now reads like a dropdown: a left-aligned client name
and a separate, DPI-scaled chevron at the right edge. Long names truncate before
the chevron; ampersands display literally. The selector fits its client name
within a compact minimum and sensible maximum, rather than staying wide for
short names.

Autosave stays quiet during normal editing and after saving. There is no Saved
or Saving label, even briefly. Save failures and invalid Details drafts still
show a persistent problem indicator. The built-in quick tour describes this
behavior.

Switching clients reuses the actual same Notes window, including its header
controls, position, size and maximized state. It loads the selected client's
notebook without closing and reopening the window. A failed save still blocks
the switch and keeps the current notebook and drafts open.

The approved Notes Inspector layout, theme colors, slim tabs, remembered size,
and autosave remain intact. The existing real application walkthrough videos
have been kept as requested.

See [release validation](docs/RELEASE_VALIDATION_6.3.2.0.md) for the focused
checks and visual review. The completed whole-app coverage from 6.3.1.0 remains
documented in its [stress review](docs/FULL_APP_STRESS_REVIEW_6.3.1.0.md).
