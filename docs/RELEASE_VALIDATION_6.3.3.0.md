# Release validation: 6.3.3.0

## October 21 browser tab-layout candidate — background checks passed, release held

The new global and per-client tab-layout settings are built in a separate,
unpublished **6.3.3.0** candidate:
`build/tab-layout-2026-10-21/final-pass2/verified-candidate/ctSpaces.exe`.
Its SHA-256 is
`5BE4606690561520F0D8DE70026950C7BEAD21FAE5B9F29D0FD870B4533E5F14`.
The normal review copy described below remains unchanged at `4861ACDD…`.

Settings offers Keep browser's choice, Vertical tabs and Horizontal tabs for
all clients. Each client can follow the global setting or override it with
Vertical or Horizontal. Horizontal explicitly turns vertical tabs off.
Changes apply at the next managed launch. Open browsers are not modified;
Temp follows global and the Default editor keeps its own layout. Client
overrides follow the client through rename, archive, backup and restore.

The clean final-pass2 checkpoint passed all **58 available Core suites** and
**18 headless Notes workspace groups / 4,852 checks**. The four new layout
suites are included in Core: client settings 221, Chromium preferences 124,
Firefox preferences 332 and guarded menu/launch integration 57 checks.
Production compilation had **zero warnings and zero errors**. Release checks
passed against the exact candidate above. All 340 frozen project inputs,
488 SDK inputs and eight protected version/package/review files matched.

Two integration defects were reproduced and repaired before this clean pass:
the settings validator now reuses the original client deletion authority, and
Chromium launch retains the actual `Default` preference directory and its
ancestors through preparation and process creation. Original failed proofs
are retained. The first broad pass also exposed two outdated test assumptions;
the release assertion and pinned-context fixture were corrected, independently
reviewed, and followed by the wholly fresh final-pass2 scan. Earlier failed
results contribute no passing results to this checkpoint.

No mouse, foreground activation, real browser launch or normal-review update
was used. Native fixtures intercept presentation and process boundaries;
real browser startup, live UI and compositor/flicker review remain pending.
Native harness warnings remain in their logs; the zero-warning claim applies
to production compilation. Previously blocked ArchiveRoundTrip, extra archive
mutation and cleanup operations were not retried. Nothing was installed,
packaged, committed, tagged or published.

See the [checkpoint report](../build/tab-layout-2026-10-21/REPORT.html) and
[exact evidence record](../build/tab-layout-2026-10-21/result.json) for the
candidate identity, fresh runs, before/after proofs and remaining limitations.

## Normal review copy updated — October 20; publication held

At the user's explicit request, the background refresh updated
`build/notes-format-on-demand/app/ctSpaces.exe` to the exact tested final7
candidate, SHA-256
`4861ACDDB58BDF1AD35D8B56A28E8688E301ED66438940E2D5E7D660FB1EC851`.
The matching PDB is
`29CF9BF669479925B44AF5028DFF344A5C36985ACE4B084AB216A1A3A776AD9C`.
File version remains **6.3.3.0**. The normal review copy was not running;
no mouse, foreground activation, app launch or process stop was used.

Only the existing normal review EXE, matching PDB and canonical `result.json`
were replaced. Exact prior files and atomic pre-write backups are retained in
the transaction directory named by the
[refresh receipt](../build/notes-adversarial-2026-10-20/review-refresh.json).
The new canonical record preserves the approved design, branch and base commit
and points to the existing completed validation and manual evidence. Its hash
is `0881EC07E432E3571114AA6BF317872964EE8EC3C0E02A3DE9A9226A9F98B62C`.
The real user configuration matches its pre-update hash; notes and settings
were not modified. The source, shared dependency and existing release packages
remain unchanged. No tests were repeated solely for this byte-identical copy.

The immutable reports below describe the successful pre-refresh checkpoint;
their historical `ReviewRefreshed=false` fields are intentionally unchanged.
The later refresh receipt records the authorized update and verified rollback
files. This local review update is not an install or public release. No package,
commit, tag or GitHub publication occurred. The recorded testing limits remain.

## October 20 actual final7 UI review complete — release held

The user renewed mouse and foreground permission. Root reviewed the isolated
ordinary app at
`build/notes-adversarial-2026-10-20/app/ctSpaces.exe`, SHA-256
`4861ACDDB58BDF1AD35D8B56A28E8688E301ED66438940E2D5E7D660FB1EC851`.
It is byte-identical to the final7 production candidate below. No production
source changes or additional build were needed during this live review.

Completed actual interactions:

- Normal launcher Notes opening with 14 open tabs; quiet autosave and compact,
  theme-based layout remain intact.
- Native keyboard growth through the overflowing-tab threshold, including
  preceding-tab backfill at width 980; height growth; actual diagonal corner
  shrinking from 980 × 599 to 933 × 568 and the 900 × 540 minimum.
- Several maximize/restore cycles, long-note reflow with all 1,162 characters
  retained, complete plus-button outline and no thick white window border in
  the observed frames.
- Actual two-match Find/Replace, Unicode typing, single-step Undo/Redo, and
  the older independent typing Undo entry after resizing.
- Actual F7 spelling correction from `speling` to `spelling`, then Undo and
  Redo of that correction without changing the rest of the note.
- Same-window switching among Northstar, Southstar and the Unicode long-name
  client. The Notes HWND, 900 × 540 size and chosen position remained stable.
  Each client's own content returned; edits to Northstar and Southstar remained
  isolated. A read-only durable comparison checked every saved page's logical
  text, order and existing IDs. Visiting the Unicode client validly migrated
  its notebook from version 1 to version 2 without changing its logical text.
- Minimizing the launcher, then closing the launcher to the tray, left Notes
  visible and able to perform Undo/Redo in the same window.

Independent read-only probes verified timer autosaves while Notes remained
open and before close, client switch or an explicit save. One probe caught the
temporary undone spelling while root repeated Undo/Redo; its failed receipt is
preserved. With inputs held, the next read found the corrected spelling on disk
(write time 13:44:22 UTC), following the temporary-state read at 13:44:16 UTC.
This interleaving did not establish a saving defect. The full evidence is in
`build/notes-adversarial-2026-10-20/manual-final7-evidence/`.

Fresh Notes close/reopen persistence passed after the user restored the
launcher using its tray icon. The ordinary launcher Notes button created a new
Notes HWND, restored the exact corrected text, all 14 open tabs and active
Untitled 2, and retained the 900 × 540 window size. Fresh reopening centered the
new window around the launcher at origin 302,263; the implementation stores
dimensions and maximized state, not absolute position. The earlier same-window
client switches separately retained the chosen origin 180,150.

Root then closed Notes and the launcher through their ordinary X controls;
no isolated QA app process remained. The final independent disk read found all
three notebooks byte-for-byte identical to the preceding client-roundtrip
checkpoint, including each page's raw RTF hash, logical text, order and existing
ID. The saved QA configuration records 900 × 540, unmaximized, and Northstar as
the last client. The real user configuration still matches its baseline hash.
The completed [manual receipt](../build/notes-adversarial-2026-10-20/manual-final-live.json)
records 13 bounded observations, eight limitations, eight sealed evidence
references and the preserved interleaved temporary-Undo read. No normal-review
promotion, installation, packaging or publication occurred.

The existing strict report validator then passed and sealed the
[full HTML report](../build/notes-adversarial-2026-10-20/REPORT.html) and
[completed checkpoint](../build/notes-adversarial-2026-10-20/result.json).
It revalidated all 323 frozen inputs, the exact executable and PDB, the 54 Core
suites, 18 Notes groups / 4,852 checks, zero-warning build, exact-candidate
release checks, manual evidence and protected artifacts. This validates the
completed evidence; it does not repeat the test runs. The four feature wrappers'
947 checks are already included in Core and are not an additional app-wide
total. The normal review copy still differs from this private final candidate;
no local review refresh was performed. Release remains held at the user's
instruction, with continuous compositor flicker explicitly unproven.

The user was not watching the resize checks. Stepwise captured frames establish
layout at the sampled states, not absence of between-frame compositor flicker.
Outward corner dragging beyond the current target bounds, physical monitor DPI
transitions, client-search typing through the popup, printing/PDF and real
browser/SSO flows remain unverified by this live pass. The fresh Notes window
was reopened within the same app process; a full process relaunch was not
repeated after the user needed the computer. No OBS was launched. Mouse input
has ended; no app defect was newly established during this actual final7 pass.

## Earlier October 20 background checkpoint — historical pending-UI snapshot

The following section preserves the earlier background checkpoint status. Its
pending-UI wording is superseded by the completed actual review above.

The fresh **final-pass7** background scan passed all **54 available Core suites**
and **18 Notes groups / 4,852 checks**. The exact production candidate built with
**zero compiler warnings and zero errors**, and its release-file checks passed.
Version remains **6.3.3.0**. The user stopped computer control to work, so the
final ordinary-app resize and interaction review remains pending renewed
permission. This is a completed background checkpoint, not release approval.

The candidate is
`build/notes-adversarial-2026-10-20/final-pass7/verified-candidate/ctSpaces.exe`,
SHA-256 `4861ACDDB58BDF1AD35D8B56A28E8688E301ED66438940E2D5E7D660FB1EC851`.
Its matching PDB is SHA-256
`29CF9BF669479925B44AF5028DFF344A5C36985ACE4B084AB216A1A3A776AD9C`.
All **323 frozen inputs** match manifest
`14AD4D5A7363A53B6FE655D1326D490429E50B80081A774FED28B1AB27D8AD4A`.
The [background report](../build/notes-adversarial-2026-10-20/BACKGROUND-REPORT.html)
and [sealed checkpoint](../build/notes-adversarial-2026-10-20/background-checkpoint.json)
record the exact executables, logs, input hashes, seven production findings,
eight historical test corrections and 110 historical evidence references.

Seven confirmed Notes defect groups have been repaired:

- Find replacements are checked against save and link limits before editing;
  an oversized friendly-link label cannot silently lose its saved destination.
- Failed Find, history restore, snippet, table and link edits recover their
  original document without leaving rejected content available to Redo or autosave.
- Spelling treats Shift+Enter as a line boundary when excluding technical text.
- Replacing part of a friendly link preserves the surviving prefix and suffix.
- A refused partial Code insertion restores the unsaved draft and earlier Undo.
- A refused partial spelling correction restores the original draft before saving.
- Widening an overflowing tab strip shows preceding tabs instead of leaving a
  large empty gap, while preserving the active note, editor and Undo history.

| Fresh completed coverage | Result |
| --- | --- |
| Core catalog | 54 unique passing suites, partitioned 27 + 27 |
| Notes background groups | 18 passing groups, 4,852 checks |
| Notes geometry, included above | 223 checks, including viewport growth preservation |
| Normal Notes opening, included above | 3,552 checks |
| Main chrome, included in Core | 409 checks, including 6,000 native pinned-hover frames |
| Find/Replace, included in Core | 422 checks across 8 scenarios |
| Spelling, included in Core | 89 UI checks and 66 helper checks |
| Markdown, included in Core | 58 parser checks and 222 native checks across 8 groups |
| Editing Extras, included in Core | 90 checks across 5 scenarios |
| Link context, included in Core | 872 checks, 78 cases, 8 glyph variants |
| Separate scalar-only recovery control | 162 checks across 10 supported native cases |
| Separate native render selector | 232 checks on 6 surfaces |

The four feature wrappers contribute **947 checks already within Core**; these
overlapping counts are not combined into a larger app-wide total. All 16 emitted
Notes native-event banners across 15 groups were checked, including the final
whole-geometry audit. Three legacy headless groups do not emit a JSON banner;
their actual event coverage is not invented. The harnesses compile production
routines and do not all execute inside the ordinary production EXE.

The fresh six Find/Details and two spelling PNGs are byte-identical to the eight
native images root directly inspected during final-pass6. Their exact hashes
and reused visual observations are sealed in final-pass7's
`native-visual-review/inspection.json`. These are hidden native memory renders;
they do not establish continuous compositor flicker or physical drag resizing.
No mouse, actual keyboard/clipboard interaction, foreground activation, real
browser launch or OBS capture was used in this background checkpoint.

The repeated scan also corrected retained test-fixture allocation and a positive
restore fixture that exceeded the existing new-client path budget. The actual
application notice was `0x800700CE`; original and staged data stayed intact.
A shorter private root and an explicit precondition retain all original restore
assertions; the fresh full scan passes 29 checks across 12 restore scenarios.
Three C5055 compiler warnings were repaired with explicit float casts; an early
candidate-build gate now rejects warnings before starting a final scan. A CRLF
report-parser correction is separately recorded and did not change app tests.

The actual review still needs the exact final candidate: overflowing tabs during
growth and shrink, corner dragging, maximize/restore, remembered geometry,
autosave and Undo after layout changes, same-window client switching, Notes
remaining usable with the launcher hidden, and fresh reopening. The earlier C7
live review found the viewport defect and remains historical. Physical monitor
DPI transitions and between-frame flicker are not proved by the new background
results; printing/PDF and real browser/SSO flows were not repeated.

The normal review copy remains the October 19 **63AE** executable below. Its PDB,
canonical metadata, existing dist EXE/ZIP, version header, `Default.7z` and shared
7-Zip library are unchanged. The real configuration retains SHA-256
`E71FA7F4E7D12A32FB9560A1199B4231DE256FD63B813C27621431D621A3819B`.
No review promotion, install, package, commit, tag or GitHub publication occurred.
The mandatory full manual-report gate and review-refresh gate remain unexecuted.
Previously blocked ArchiveRoundTrip, extra archive-mutation corpus and cleanup
actions were not retried or bypassed.

## October 19 final live-review checkpoint — prior and unpublished

The current local review executable remains **6.3.3.0**, SHA-256
`63AE6AA10302B40E3D5991EA9904C20416C0EF68C2B55782949BABA336041F90`.
The exact rebuilt candidate is
`build/notes-live-review-2026-10-19/final-pass2/verified-candidate/ctSpaces.exe`.
The actual final live review ran the identical isolated copy at
`build/notes-live-review-2026-10-19/app/ctSpaces.exe`. After all gates passed,
the normal review copy at `build/notes-format-on-demand/app/ctSpaces.exe` was
refreshed to that same hash. Its matching PDB has SHA-256
`777F210FFB6B14E65C62B9B00C9E7A5E7DE2A6BD131B526E9C6D4442978DFDB0`.
The prior B4 executable, PDB and exact canonical metadata are preserved in
`build/notes-live-review-2026-10-19/prior-review-backup`.

The current HTML report and machine-readable evidence are
`build/notes-live-review-2026-10-19/REPORT.html` and `result.json`.
The separate completed canonical metadata transaction is
`build/notes-live-review-2026-10-19/canonical-refresh.json`; it preserves the
approved design, branch and base commit identity. The earlier checkpoint below
is historical and does not identify the current executable.

The final production build has zero warnings and errors. Exact-candidate
source/PE/default-archive release gates passed. All **309 frozen input hashes**
match; the manifest is SHA-256
`EAEBE70EDFF8F7D452C52F40914931A08FD05D6EDDDBA50D5D95FBB23243DE03`,
and its included `ctSpaces.cpp` is SHA-256
`5627DF7334B5E855EFB5B2C5208662D48CDF8698FE16E4C6B54176082DA3EDC7`.

| Completed final coverage | Result |
| --- | --- |
| Available Core catalog | 54 unique passing suites, partitioned 27 + 27 |
| Notes background groups | 18 passing groups, 4,502 checks |
| Normal Notes opening, included above | 3,552 checks |
| Notes geometry, included above | 198 checks |
| Main chrome, included in Core | 409 checks, including 6,000 native pinned-hover frames |
| Find/Replace, included in Core | 120 checks across 8 scenarios |
| Spelling, included in Core | 53 UI and 66 helper checks |
| Markdown, included in Core | 58 parser and 156 native checks |
| Editing Extras, included in Core | 90 checks across 5 scenarios |
| Link context, included in Core | 872 checks, 78 cases, 8 glyph variants |
| Separate native visual selector | 232 checks on 6 surfaces |

The four editor wrappers contribute **543 checks already included in Core**.
Link-context checks are also part of Core. These overlapping counts are not
combined into a larger app-wide total. Six final Find/Details renders and two
spelling renders were visually inspected and sealed in
`final-pass2/native-visual-review/inspection.json`. They show actual hidden
native controls; they are not visible compositor captures. Background guards
record zero actual visibility, foreground, focus, keyboard and clipboard effects
where the suite exposes those guards. These source/native harnesses compile
production routines; they do not all run inside the ordinary production EXE.

The live review reproduced and repaired dark-theme native edit borders,
unstable Find/Replace accessible names, navigation from filled table cells, and
link context menus missing on the right half of a visible URL glyph. Link
regressions also cover wrapping, adjacent destinations, combining characters
and compound Unicode clusters while rejecting blank areas. A defensive parent
paint repair requires both logical search-panel state and native visibility
before outlining Find/Replace. The first final scan stopped on that outline
regression under hidden visibility suppression; its failed logs and 48CD
candidate remain intact. The fresh `final-pass2` scan above passed after the
repair and stronger per-rectangle assertions, with no further reproducible
defect found in its covered scope.

The final ordinary EXE was also exercised with actual input in isolated demo
clients: physical Shift+Tab from the end of a filled cell, marker insertion and
Undo; stable Find/Replace labels and theme borders; F7 spelling panel; normal
Notes opening; width changes from 900 to 932 and back to the 900-by-540 minimum;
maximize to 1920 by 1080 and restore; same-HWND client switching with retained
position and size; exact URL copy/paste and Undo at the original failed hit
position; and ordinary launcher pinned-client menu/hover and dismissal.
Microsoft Print to PDF produced a real one-page table output. Its extracted
text and rendered page were inspected; printing and a separate native Print
Cancel each preserved the complete live note. The Windows print host reports
that in-dialog preview is unavailable. No physical printer was used.
`manual-live-evidence.json` distinguishes these final 63AE observations from
the historical B4 and intermediate 48CD actions. The isolated app is closed,
and the real user configuration retains SHA-256
`E635CF984E4851F5718DE5CBBBFCA1F8EEBA59DEAE2CB3B2CFE07E210FC8B19C`.

Live resizing and stepwise captures **do not prove absence of between-frame
flicker**. No OBS or continuous desktop recording was used. A historical launcher
restore capture fringe disappeared after restart/restore or a native caption
drag; capture bounds versus an actual nonclient-frame issue remains
inconclusive, and it is not counted as a fixed product defect. Physical
multi-monitor/DPI transitions, physical printing, actual browser/SSO launch,
tray restoration and installed-child foreground handoff remain untested here.
Markdown remains a disclosed bounded subset; use RTF for formatting and `.ctn`
for metadata/history. Spelling depends on installed dictionaries.

The version header, existing packaged EXE/ZIP, `Default.7z` and reused 7-Zip
library retain their protected hashes. No install, package, commit, tag or
GitHub publication was performed. The previously blocked `ArchiveRoundTrip`,
extra archive-mutation corpus and generated-artifact cleanup were not retried
or bypassed. Earlier request-preparation failures still have no established
root cause. This checkpoint is not a claim of universal bug freedom.

## Earlier October 19 editing upgrade checkpoint — historical, superseded

The current local review executable is **6.3.3.0**, SHA-256
`B4AD1D989D6FA342EAE3C6DD930A8C1D7F491E6EDD36EF1C278FF8B85BBE2218`.
The exact production candidate is
`build/notes-replacement-2026-10-19/final-pass2/verified-candidate/ctSpaces.exe`.
The identical review copy is `build/notes-format-on-demand/app/ctSpaces.exe`;
its matching PDB is SHA-256
`FE556E808171F686E2FF1382E7A04409D83FB45AE977890FA3A677E0ADA8D901`.
Previous review binaries and the exact prior metadata were preserved under
`build/notes-replacement-2026-10-19/prior-review-backup` before refresh.
The canonical review metadata now points to this checkpoint; its separate
transaction receipt is `build/notes-replacement-2026-10-19/canonical-refresh.json`.

The production candidate passed the source/PE/default-archive release gates and
built with zero compiler warnings or errors. All 308 frozen source inputs match
without test-only exceptions. The source manifest is SHA-256
`F1DFBF2B7FEED3A14C7D106DCFC9675AA1F5FDA654C6BC9251B18964690A10C0`;
the included `ctSpaces.cpp` is SHA-256
`3437AB27DA24C9449425BA63BC859A824987CE844B4E987AD042B121B5BF682B`.
The version header, existing packaged EXE/ZIP, `Default.7z`, and reused 7-Zip
library retain their protected hashes. No install, package, commit, tag or GitHub
publication was performed. Live client data was not used by the private tests.

The editing upgrade adds temporary inline Find/Replace with options and counts,
remembered word wrap, logical Go to line, document/selection statistics, Windows
spelling review, printing from a separate rich-text copy, previewed Markdown
interchange, and simple native tables. The selected theme-based layout, slim
tabs, independent client notebooks, quiet autosave and remembered geometry are
retained. These tools use shortcuts or searchable More actions; no permanent
sidebar or footer was added. The user guide and release notes describe access
and format limitations.

| Completed final coverage | Result |
| --- | --- |
| Available Core catalog | 54 unique passing suites, partitioned 27 + 27 |
| Notes background groups | 18 passing groups, 3,542 checks |
| Normal Notes opening, included above | 2,592 checks |
| Notes geometry, included above | 198 checks |
| Main chrome, included in Core | 409 checks, including 6,000 native pinned-hover frames |
| Find/Replace, included in Core | 95 checks across 7 scenarios |
| Spelling, included in Core | 47 UI and 66 helper checks |
| Markdown, included in Core | 58 parser and 118 native checks |
| Editing Extras, included in Core | 90 checks across 5 scenarios |
| Separate native visual/accessibility run | 152 checks on 6 surfaces using the exact final Find executable |

The four editor wrappers contribute 474 checks already included in the Core
catalog; these overlapping groups are not combined into an app-wide assertion
total. Eight saved native memory renders were inspected: the six Find/Details
surfaces plus dark Gothic and light Marine spelling panels. Their labels,
controls, explanation text and button edges fit. Native observers measured zero
actual owned-window visibility, foreground or focus events in the guarded
runs; physical input, real clipboard, browser and printer effects were blocked
or intercepted. Windows spelling uses the installed provider, which can employ
an operating-system COM process.

Eleven grouped production repairs are recorded in the report: wrap reset during
layout; missing soft-line navigation/counting; narrow Details text clipping;
link destinations after replacement; refused replacement selection; dark-theme
Find checkboxes; spelling and long-row dropdown rendering; multiline/trailing
paragraph Markdown decoding; mixed-case HTTP/HTTPS schemes; retained-link
memory amplification; and dialog-hosted native table Tab navigation. Native
regressions cover exact RTF, save/Undo/Redo, stale contexts, resource bounds and
unrelated-client preservation as applicable.

The full-scan failure and all targeted failed proofs are retained. The first
scan stopped on obsolete Details geometry assumptions; the added actual-font
measurement then proved a real defect: 280 pixels of wrapped text in a
171-pixel control at a 235-pixel width. The repaired hint receives 280 pixels,
and successive native scroll intervals cover its complete content. The normal
502-DIP panel and minimum 114-DIP hint remain. Repeated narrow-panel layout
does not toggle the scrollbar, reposition the popup or stream the editor.
Fixture lifetime, capture composition, stale assertions and compile corrections
are distinguished from product repairs. After the last repair, the complete
available scan above found no further reproducible defect in its covered scope.

Source/native suites compile production routines with process-local guards;
they do not all execute inside the ordinary production EXE. Exact-candidate
release gates separately validate the identified executable. Visible live
resize/compositor flicker, physical monitor/DPI transitions, actual printer and
Microsoft Print to PDF workflows, real keyboard/mouse/clipboard, browser/SSO,
tray behavior and installed-child foreground handoff remain pending. Printing
used real RichEdit pagination into private memory with dialog/spooler calls
intercepted; no physical print or PDF file was produced. Markdown is a bounded,
disclosed subset; use RTF for formatting or notebook export for metadata/history.
Spelling depends on installed dictionaries and skips code, native tables, links
and technical tokens.

The previously blocked `ArchiveRoundTrip`, extra archive-mutation corpus and
generated-artifact cleanup were not retried or bypassed. Earlier request
preparation failures still have no established root cause. This checkpoint is
not a claim of universal bug freedom or a completed visible UI review.

Full evidence is in `build/notes-replacement-2026-10-19/REPORT.html`,
`result.json`, `final-pass2/source-frozen.json`, the two final Core result sets,
the Notes results, `final-pass2/native-visual-review/result.json` and `inspection.json`,
and `canonical-refresh.json`. Earlier checkpoints below are retained historical
records; their references to a current executable apply to their own dates.

## October 15 final background audit checkpoint — unpublished

The current review executable remains **6.3.3.0**, SHA-256
`0FD0FB373E409F0CD2362E6EEB00FE96983F3FE53A29F2215221CAC81A462F7C`.
The isolated candidate is
`build/app-audit-round15-2026-10-15/verified-candidate/ctSpaces.exe`.
The identical normal review copy is
`build/notes-format-on-demand/app/ctSpaces.exe`; its matching PDB was copied too.
The previous review EXE/PDB were preserved in
`build/app-audit-round15-2026-10-15/prior-review-backup`.
The exact candidate passed the release source/PE/archive gates. Its Release x64
build had zero compiler warnings and errors. Version, published EXE/ZIP,
`Default.7z`, the reused static library and live configuration retain their
recorded original values. No package, install, commit, tag or GitHub publication
was performed.

The final production source is SHA-256
`C954E8D8C59CEBCA42F7900D62C3828355895CBB30767F531470EBD4C4282B22`.
The 238-input source manifest has three exact, separately recorded test-only
allowances: one new legacy-preference expectation and two stale wrapper source
recognizers. No production change followed the candidate rebuild. The selected
Notes design, theme-derived controls, autosave, tab layout and remembered window
geometry are retained.

The completed available background scan found no further reproducible production
defect after the repairs. It passed:

| Coverage | Final result |
| --- | --- |
| Available Core catalog | 45 unique suites, all passed: final Core A22 and Core B23 |
| Notes background groups | 18 groups, 3,488 checks; normal opening contributes 2,592 |
| Main chrome | 409 checks, including 6,000 native pinned-hover frames |
| Client preferences and rename, also included in Core | 1,427 checks / 121 private scenarios, including 17 rename scenarios |
| Independent collection reproductions and matrix | 151 checks / 22 scenarios |
| Generated startup parser | 3,073 checks / 1,024 requests against the final compiled native suite |

Overlapping regression groups are not added into a single assertion total.
Core includes fresh Chromium Temp/Default preparation, safe cleanup and isolation
coverage (106 checks / 32 lifecycle scenarios), Default save transactions
(163 checks / 11 scenarios), archive interruption recovery, browser preference
publication, client identity/isolation, saving/recovery, command lifetime and
guarded launcher/Notes workflows. Browser and desktop external effects are
intercepted; actual browser sessions were not launched.

The repairs cover directory ownership during Temp/Default lifecycle operations;
archive SDK leases surviving callback exceptions; bounded browser preference
output and conflicting publication; ambiguous startup arguments; null editor and
launcher repaint targets; and stale settings writes. Pin/archive/Restore-tabs
actions now apply their captured intent to current settings. Full-list reorder
and pruning refuse a changed original snapshot. Rename transforms only the
selected client's entries in current staged lists in one transaction. Unknown
fields and unrelated clients survive. A known unsaved pin drag uses its own
pre-drag saved baseline without accepting independent changes. Canonical
2,048-entry archive updates took 3,062 ms for the measured sequence, compared
with about 57 seconds for a superseded per-row variant; the corresponding
Restore snapshot took 1,531 ms. Valid noncanonical formatting retains the native
INI fallback.

Initial failures remain evidence. The first Core A run had 20 passes and two
stale source-inspection failures before their native tests; the complete final
22-suite rerun passed after correcting the wrappers. A newly added legacy test
initially expected an unnormalized alias; the corrected test verifies preserved
effective preferences and browser independence. An earlier legacy guard variant
recorded four actual focus and four foreground events with no visible windows
and failed. Reviewed final guarded runs measured zero actual events; suppressed
requests are not reported as actual events. The final generated parser selector
has desktop-action interceptors but no native WinEvent observer.

Visible resizing/compositor flicker, physical monitor/DPI transitions, actual
browser/SSO and tray behavior, and installed-child foreground handoff remain
pending. Older visible evidence belongs to its identified earlier binary.
The extra platform-rejected storage corpus and unsigned-policy-blocked
`ArchiveRoundTrip` wrapper remain unexecuted; neither was retried or bypassed.
The earlier chat's request-preparation failure has no established root cause.
Passing this covered background scan does not establish universal absence of
bugs or a completed visible UI review.

Fresh R15/R16 compiler caches were removed only after all workers finished:
43 directories, 3.43 GiB. Executables, linked PDBs, logs, source snapshots,
fixtures, packages and walkthrough media remain. Previously rejected cleanup
targets were not retried. Full evidence is in
`build/app-audit-round15-2026-10-15/REPORT.html`, `result.json`,
`validated-checkpoint.json`, `Core-results.json`, `final-source-hashes.json`,
`canonical-refresh.json`, `protected-check-end.json` and `cleanup.json`.

## October 15 post-housekeeping background checkpoint — unpublished

The current local review executable is now SHA-256
`556BBE6B33B421729624E065740B65CE71A2AC581FB53A954292EBD6D21F23E8`,
still **6.3.3.0**. The candidate is
`build/retest-post-housekeeping-2026-10-15/verified-candidate/ctSpaces.exe`;
the identical normal review copy remains
`build/notes-format-on-demand/app/ctSpaces.exe`. Its fresh Release build and
exact-candidate source/PE/default-archive release gates passed without warnings
or errors. The unchanged 7-Zip static library was reused with its recorded hash.

This pass reproduced and fixed four defects:

- Empty-workspace layout could pass a missing editor HWND to `InvalidateRect`,
  requesting a desktop-wide repaint after the last tab closed or during resize.
- Empty-workspace theme/system-color/DPI changes could similarly pass a missing
  editor to `RedrawWindow`.
- Rename with no open note could index past the page vector; its reproduction
  failed with `bad allocation`. Inactive rename requests now stop before indexing,
  and tab double-click rename requires the selected active label, excluding its
  close button and surrounding whitespace.
- Updating Firefox's startup preference could grow a valid 4 MiB `user.js` beyond
  its own readable limit and cause subsequent launches to fail. Oversized output
  is now refused before staging, preserving the original bytes.

The empty-editor, theme and rename regressions failed before their fixes and
passed afterward. Final-source Notes/Main tests passed **3,753 assertions** across
19 hidden groups, including 6,000 native hover frames. Those groups exercise
normal Notes opening, saving/recovery, independent clients, bulk library actions,
import/export, templates/snippets, link Undo, geometry, empty-window theme/DPI,
tab tooltips and add-tab repaint. No native show/focus/foreground events were
observed in guarded runs.

Across the broader Core catalog, **42 of 43 suite families passed**; the
`ArchiveRoundTrip` wrapper was blocked before execution by Windows PowerShell's
unsigned-script policy and is not counted as passing. No bypass was attempted.
Storage, client and browser/launcher lanes recorded at least **11,448** additional
counted assertions, for **15,201** total across the reported passing runs; several
older suites do not emit assertion totals. Supplemental concurrent stress used
eight clients, 320 notebook saves, 320 activity saves and 16 stale/cross-client
write refusals. A stale ClientNotes test was corrected to distinguish first
migration conflicts from canonical CTN authority while preserving legacy bytes.

Per-run source and executable hashes identify earlier unrelated Core tests that
preceded the last Notes-only repairs. They are not relabeled as final-candidate
application execution. The final Notes/Main harnesses compile the final source;
the application candidate itself received release gates, not an ordinary visible
launch in this pass.

Visible resizing, continuous compositor/flicker smoothness, physical monitor/DPI
transitions, live browser/SSO sessions, actual tray interaction and installed-child
foreground handoff remain outside this background checkpoint. The earlier visible
UI evidence below belongs to its explicitly identified earlier binary. No mouse,
foreground interaction, package, security scan, commit, tag or GitHub publication
was performed. Original published artifacts and live configuration are unchanged.

Full evidence: `build/retest-post-housekeeping-2026-10-15/REPORT.html`,
`result.json`, the three lane reports, native before/after reproductions,
`final-source-hashes.json`, `verified-candidate-build.log` and
`verified-release.log`. Rebuildable compiler caches created by this pass were
removed after verification. Policy-rejected Firefox fixture deletions were not
retried; those private fixtures remain.

## October 15 final UI review checkpoint — unpublished

The local review executable is now SHA-256
`BC9669A3650E3EF848D785B364D47A89A2F26061E59ADC9CE0D7E3F2A8E6D18D`,
still file version **6.3.3.0**. The tested copy is
`build/final-ui-review-2026-10-15/app/ctSpaces.exe`, launched with isolated QA
data and a unique QA instance identifier. The identical normal review copy is
`build/notes-format-on-demand/app/ctSpaces.exe`.

The visible pass reproduced and repaired four defects: gaps in All notes/Trash
outlines after enlarging; an incorrect accessible label on Find; Alt+F4 opening
the launcher client dropdown; and a closed, reordered tab leaving its tooltip
over the neighboring note. The add-tab button also receives a complete,
non-erasing repaint when its bounds change. The user-supplied screenshot shows
its full outline; an apparent missing edge in the automated capture is not
counted as a fifth confirmed defect.

Seven current-source affected suites passed **3,044 checks**: Main chrome 409,
library borders 18, tooltip lifetime 14, add-tab repaint 15, geometry 118, normal
Notes opening 2,448 and Notes redraw/opening 22. Main chrome includes 6,000
native hover frames. The exact candidate passed `tests/Test-Release.ps1`.

Actual UI actions included all four Notes edges and a corner, the 900 × 540
logical minimum, large/maximized/restored states, twelve-tab overflow, client
switching with retained bounds, Unicode autosave across process restart,
full ticket IDs, Find, Details, highlighter, creating/reordering/closing notes,
and library/Trash presentation. The report identifies which intermediate
build supplied each visible observation; older frames are not labelled as
captures of the final binary.

Point-in-time captures do **not** certify continuous compositor smoothness or
the absence of flashing between frames. No observer sign-off or frame-time
measurement is claimed. Physical DPI/monitor transitions and the complete
core/integration catalog were not rerun. The user's installed application,
live configuration and the existing release EXE/ZIP remain unchanged. No new
package, security scan, commit, tag or GitHub publication was performed.

Full evidence: `build/final-ui-review-2026-10-15/REPORT.html`, `result.json`,
`edge-sweep.json`, `*-review.log` and `build-source-hashes.json`.

## Original packaged build — October 13

Validated October 13, 2026. This patch consolidates the Notes quick actions into
one compact row and gives them the Notes toolbar's themed surfaces. It does not
change notebook storage, browser routing, autosave or client switching.

- The focused native run passed 348 checks:
  `build/notes-action-row/native-results-final.log`. It verifies every action
  fits in one separated row at 900, 1240 and 1500 DIP widths, including a return
  to the minimum width, at 100%, 150% and 200% layout scaling. All status and
  Link/Open ticket captions fit their real control fonts. The heading, problem
  indicator, ticket/tag context, formatting and expanded search remain separate;
  the editor keeps its full width, handle and draft text.
- Existing action, ticket/code, resize-paint, tab-overflow and text-wrapping
  checks are included in that run. Incremental resizing reflows paragraphs
  without changing their contents. The resource order follows the new action
  row for keyboard navigation.
- Native visual captures use the production Notes dialog, controls and built-in
  Gothic and Marine palettes at the host's 125% DPI, at minimum and wider window
  sizes. The wider Gothic capture includes the Templates hover state. Review
  confirmed a single compact row, readable captions and context, subtle borders,
  intact theme colors and a full-width editor. Captures are
  `build/notes-action-row/gothic-compact.png`, `gothic-wide-hover.png` and
  `marine-compact.png`; the capture receipt is `capture.log` in that directory.
- The desktop Computer Use helper failed twice with
  `foreground window did not report a process id`. The visual captures therefore
  come from the native test executable compiling the production UI, rather than
  a successful desktop automation session against the release executable.
  This limitation does not represent a passed live click-through review.
- The Release x64 build completed without compiler warnings or errors:
  `build/notes-action-row/release-build.log`. Release gates passed in
  `release-gates.log`. Defender reported no threats in the final executable:
  `defender-final.log`. Its first scan attempt used forward slashes and failed
  with `0x80508023`; retrying the same file with a Windows path succeeded.
- Packaging verified exactly one root `ctSpaces.exe`, identical to the release
  executable: `build/notes-action-row/package.log`. All 25 previous root release
  ZIP hashes match `previous-package-hashes.json` in that directory.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ctSpaces.exe | 6367232 | FF1D1B8564CC161C103EEBAB90BB5E617FA555B0C9FAE283337CBA17C5BA6798 |
| ctSpaces6.3.3.0.zip | 2629918 | 2B49F684C5EEBA3CA897D39845816B1129580E8C30B32DC2BE752F9D794A9B13 |

The completed 660-check Notes regression run and same-window client-switching
review remain documented in [6.3.2.0 validation](RELEASE_VALIDATION_6.3.2.0.md).
The 12 core and 16 integration suites from the
[6.3.1.0 whole-app review](FULL_APP_STRESS_REVIEW_6.3.1.0.md) were not repeated for
this layout patch. Tests used disposable clients. The user's installed app,
real notebooks and existing walkthrough recordings remain untouched.
