# Guided walkthrough and feature updates

Applies to ctSpaces 6.0.3.0.

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

## Guides and feature updates

**What's new** lists announced guidance that you have not acknowledged yet.
A small indicator is accompanied by **New** text so it does not depend on color
alone. A future feature can have its own indicator and explanation.

Opening a menu, selecting a topic, or closing the guide does not silently mark
everything read. **Next/Done** acknowledges the page currently displayed and
clears only that page's related New indicator. Skipping leaves other announced
topics available for later. For the current colleague migration, New is
relative to the supplied last-shipped 5.2.0.14 ZIP, not merely to the 6.0.3.0
patch. Guidance is tracked per feature and content revision, independently of
the application's patch version.

For colleagues coming from the supplied 5.2.0.14 ZIP, What's new announces
eight change-only topics with dedicated titles: **Firefox & browser slots**,
**Reorder open tabs**, **Links, shortcuts & ordering**, **Client-first window
titles**, **Rename & archive clients**, **Two bulk-delete tools**, **Improved
themed popups**, and **Guides & visual tour**. Temporary and starter profiles
are still explained in the full guide, but are not marked New in this release.
Existing actions are explained in the full walkthrough without being falsely
presented as newly added functionality in 6.0.3.0.

The 6.0.3.0 update leaves the 20-step, 12-illustration Quick tour unchanged.
It separates the dedicated change-only What's-new copy from the full guide and
removes the Default/starter-profile New marker. Reopen the full guide from
**Options > Guided walkthrough** or press **F1** to review every topic; the
newly labelled guidance is not a claim that the underlying actions first
appeared in 6.0.3.0. The 6.0.1.0 nine-step tour remains a historical release
reference.

## Coverage since the supplied 5.2.0.14 baseline

The supplied ZIP contains one `ctSpaces.exe` entry (ZIP SHA-256
`C84556D416873E0256D6F93CC19E7586706A6759F58C6B515DAE5CF71DC38500`); its
extracted executable is SHA-256
`4FB86BCF08E23FBCC7C57EE58D82C9EE26455FEDC443A4F89F98CB3798956DBF` and
reports internal file/product version `5.2.0.2`. The supplied bytes are the
baseline authority. This is still a documentation/static-evidence map, not a
runtime comparison: the baseline executable was not executed. Familiar 5.2 basics—browser selection,
pinning, session restoration, backup/restore, and themes—remain explained but are not
pretended to be newly introduced by this release.

| Post-5.2 capability | Evidence | Full guide | Quick tour / New |
|---|---|---|---|
| Independent Firefox/browser slots | [USER_CHANGELOG.md, Previously Shipped In 5.3](../USER_CHANGELOG.md) | Browsers and Restore tabs | Browser-slot illustration; **Firefox & browser slots — New** |
| Existing open tabs/overflow plus later clean labels and reordering | [USER_CHANGELOG.md, Previously Shipped In 5.3](../USER_CHANGELOG.md) | Open browser windows | Session illustration; **Reorder open tabs — New** |
| Pinned links, copied URLs, managed shortcuts, and ordering | [USER_CHANGELOG.md, Previously Shipped In 5.3](../USER_CHANGELOG.md) | Favorites, links, and shortcuts | Pin/menu/shortcut illustrations; **Links, shortcuts & ordering — New** |
| Existing custom icons plus later client-first titles/live updates | [USER_CHANGELOG.md, Previously Shipped In 5.3](../USER_CHANGELOG.md) | Icons and window titles | Title illustration; **Client-first window titles — New** |
| Rename and archive/restore | [USER_CHANGELOG.md, Previously Shipped In 5.3](../USER_CHANGELOG.md) | Rename and archive clients | Dedicated illustrations; **Rename & archive clients — New** |
| Three-month cleanup and immediate bulk deletion | [USER_CHANGELOG.md, Previously Shipped In 5.3.0.8](../USER_CHANGELOG.md) | Cache and permanent deletion | Cleanup/deletion illustrations; **Two bulk-delete tools — New** |
| Themed cleanup/popup improvements and text menus | [USER_CHANGELOG.md, Previously Shipped In 5.3.0.8](../USER_CHANGELOG.md) | Appearance plus written reference | **Improved themed popups — New** |
| Read-only walkthrough, revisioned What's new, theme/DPI/keyboard help | [USER_CHANGELOG.md, Previously Shipped In 5.3.0.10](../USER_CHANGELOG.md) | Guides and feature updates; Appearance and keyboard | **Guides & visual tour — New** |

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
