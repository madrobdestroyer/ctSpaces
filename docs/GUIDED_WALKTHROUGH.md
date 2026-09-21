# Guided walkthrough and feature updates

Applies to ctSpaces 6.0.

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
guide or fresh-user welcome, for a short visual tour. It outlines the actual
CLIENT field, browser selector, primary Create/Open/Show button, pushpin or
pinned row, session tabs, client-folder icon, Restore tabs control, Temporary,
and Options. If there are no pins or open clients, it points to the real
pushpin or New tab and explains the empty state without creating fake data.

Use **Back**, **Next**, **Skip**, and **Done**; Escape and the callout's close
button also exit. The launcher controls cannot be clicked while the tour is
open. The tour does not change the selected client or browser, toggle a pin or
Restore tabs, create or open a profile, or mark walkthrough topics as read. It
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
everything read. **Next/Done** acknowledges the page currently displayed.
Skipping leaves other announcements available for later. Guidance is tracked
per feature and content revision, independently of the application's patch
version: an update can flag only the changed feature instead of resetting the
whole walkthrough.

This release announces the updated Help topic, including the new optional Quick
tour. Existing actions are explained in the full walkthrough without being
falsely presented as newly added functionality.

The 6.0.1.0 update adds the optional Quick tour and revises the full guide to
document existing folder, icon, pin, shortcut, and session-menu actions more
completely. Reopen the full guide from **Options > Guided walkthrough** or
press **F1** to review every topic; the newly documented actions are not
presented as new product features.

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
