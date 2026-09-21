# Guided walkthrough and feature updates

Applies to ctSpaces 6.0.2.0.

The built-in guide explains ctSpaces without performing any client operations.
Reading a page does not create a client, launch a browser, change a profile,
toggle Restore tabs, fetch an icon, or delete data.

## Starting or skipping the guide

On a fresh, ordinary launch, ctSpaces offers a welcome. Choose **Start** to
begin or **Skip** if you already know the application. Skipping does not disable
Help. Existing installations are not forced through the welcome after updating.

Open **Options (gear) > Guided walkthrough** or press **F1** to return later.
Use the topic list to jump directly to the subject you need. **Back** and
**Next** move through the guide, and **Done** finishes it. You can leave with
**Skip/Close**, Escape, or the window's close button.

## Quick tour of the real controls

Open **Options (gear) > Quick tour**, or choose **Quick tour** from the full
guide or fresh-user welcome, for a 20-step visual tour with 12 inert workflow
illustrations. It outlines the actual
CLIENT field, browser selector, primary Create/Open/Show button, pushpin or
pinned row, session tabs, client-folder icon, Restore tabs control, Temporary,
and Options, then illustrates existing copied-link, shortcut, reordering,
rename, archive/restore, client-title, Default save/discard, browser-slot,
inactive-cleanup, and bulk-deletion workflows. If there are no pins or open clients, it points to the real
pushpin or New tab and explains the empty state without creating fake data.
If the client-folder icon is hidden because no existing client is selected,
that step highlights the CLIENT field and explains what to select first.

Use **Back**, **Next**, **Skip**, and **Done**; Escape and the callout's close
button also exit. The launcher controls cannot be clicked while the tour is
open. The tour does not change the selected client or browser, toggle a pin or
Restore tabs, create or open a profile, read the clipboard, run cleanup/deletion,
reorder pins or sessions, or mark walkthrough topics as read. Inert illustration
panels perform no actions. It
is manually replayable and is never started automatically.

Launching a client through its shortcut must not be interrupted by automatic
onboarding. A fresh user's pending welcome is deferred until an ordinary visible
launch. Background/minimized startup is also kept free of an automatic guide.

## What the guide explains

- How client and browser profiles stay separate, and why this is workflow
  isolation rather than an operating-system security sandbox.
- Creating a client, opening an existing one, and showing an open window.
- Opening an existing client's folder in File Explorer from the client icon at
  the left of the CLIENT field (not from the Temporary button). The folder
  contains all browser slots; do not edit or delete its profile files while a
  client browser is open.
- Selecting a browser and using the independent Restore tabs setting.
- Open-session tabs and their overflow menu: client-label entries show windows,
  while `Close <client>` entries below the divider close sessions.
- Pinning and unpinning clients (which removes only the favorite), pinned-menu
  Select/Open/Restore-tabs choices, the normal click-to-open-or-show action,
  copied links, and desktop shortcuts made from the menu or by dragging a pin
  onto the Windows Desktop.
- Temporary browsing and explicitly saving or discarding Default changes.
- Client icons, favicon fetching, removing a custom icon to return to the
  default without removing browser data, and client-first window titles.
- Renaming, archiving, and bringing archived clients back.
- Cache clearing, three-calendar-month cleanup, and immediate multi-client
  deletion, including what is permanently removed and what is protected.
- Backups, restore, retained recovery copies, and the privacy limits of backups.
- Themes, keyboard navigation, and returning to Help.

Each topic gives the relevant control/menu location and explains important
consequences. Management actions still require their normal selection,
confirmation and safety checks outside the guide.

## New-feature indicators

**What's new** lists announced guidance that you have not acknowledged yet.
A small indicator is accompanied by **New** text so it does not depend on color
alone. A future feature can have its own indicator and explanation.

Opening a menu, selecting a topic, or closing the guide does not silently mark
everything read. **Next/Done** acknowledges the page currently displayed and
clears only that page's related New indicator. Skipping leaves other announced
topics available for later. For the current colleague migration, New is
relative to the last shipped 5.2.0.14 feature set, not merely to the 6.0.2.0
patch. Guidance is tracked per feature and content revision, independently of
the application's patch version.

For colleagues coming from 5.2.0.14, this release announces eight post-baseline
topics: Browsers and Restore tabs, Open browser windows, Favorites, links, and
shortcuts, Temporary and starter profiles, Icons and window titles, Rename and
archive clients, Cache and permanent deletion, and New since 5.2.0.14.
Existing actions are explained in the full walkthrough without being falsely
presented as newly added functionality in 6.0.2.0.

The 6.0.2.0 update expands the optional Quick tour to 20 steps, including 12
inert illustrations, and revises the full guide to
document existing folder, icon, pin, shortcut, and session-menu actions more
completely. Reopen the full guide from **Options > Guided walkthrough** or
press **F1** to review every topic; the newly documented actions are not
presented as new product features. The 6.0.1.0 nine-step tour remains a
historical release reference.

## Coverage since the colleagues' 5.2.0.14 baseline

The repository does not contain the colleagues' exact 5.2.0.14 source or tag,
so this is a documentation-based coverage map from the post-5.2 release notes
and changelog, not an exact source diff. Familiar 5.2 basics—browser selection,
pinning, Restore tabs, backup/restore, and themes—remain explained but are not
pretended to be newly introduced by this release.

| Post-5.2 capability | Evidence | Full guide | Quick tour / New |
|---|---|---|---|
| Independent browser slots and browser-specific Restore tabs | `USER_CHANGELOG.md:70-72` (Previously Shipped In 5.3) | Browsers and Restore tabs | Browser-slot illustration; New |
| Existing open-session tabs/overflow plus new clean labels and reordering | `USER_CHANGELOG.md:61-63` | Open browser windows | Session illustration; New |
| Pinned links, copied URLs, managed shortcuts, and Desktop drag | `USER_CHANGELOG.md:61-64` | Favorites, links, and shortcuts | Pin/menu/shortcut illustrations; New |
| Existing temporary/Default profiles plus post-5.2 starter-privacy hardening | `USER_CHANGELOG.md:69,72,76` | Temporary and starter profiles | Default save/discard illustration; New |
| Existing custom icons plus new client-first window titles/live title updates | `USER_CHANGELOG.md:67,82` (custom icons are a 5.2 basic) | Icons and window titles | Title illustration; New |
| Rename and archive/restore | `USER_CHANGELOG.md:65-66` | Rename and archive clients | Dedicated illustrations; New |
| Three-month cleanup and immediate bulk deletion | `USER_CHANGELOG.md:52-56` | Cache and permanent deletion | Cleanup/deletion illustrations; New |
| Read-only walkthrough, revisioned What's new, theme/DPI/keyboard help | `USER_CHANGELOG.md`, 5.3.0.10 section | New since 5.2.0.14; Themes and keyboard | Tour navigation; New |

Additional safety and maintenance details—legacy profile binding in place,
client-only shortcut names and taskbar handoff, edit-field right-click menus,
and retry-preserving cleanup failures—are covered in the full written guide,
User Guide, and Feature Reference rather than made into separate tour steps.

## Appearance and saved preferences

The guide uses the selected application theme and adapts to the display scale.
Long explanations remain scrollable; navigation does not require reading tiny
text. It uses ordinary keyboard navigation and the same simplified text-field
menu as the rest of ctSpaces.

Welcome and read-progress preferences are saved with ctSpaces' settings. If
Windows prevents that save, ctSpaces reports it instead of claiming that the
progress was stored. Client profiles and browser preferences are not changed by
guide navigation.

For full reference material, see the [User Guide](USER_GUIDE.md),
[Feature Reference](FEATURE_REFERENCE.md), and
[Data, Backups, and Privacy](DATA_BACKUP_AND_PRIVACY.md).
