# Whole-app adversarial review for 6.3.1.0

Reviewed October 13, 2026. This extends the approved Notes design rather than
restarting it. Every fixture uses a copied executable or a native test process
with project-owned data below `build`; real client profiles are not test targets.

## Findings and corrections

- Close all could miss a tracked browser whose first window had not appeared.
  Pending requests now wait within the existing eight-second grace period and
  hold a process handle to preserve identity. A browser receives one normal
  close request; a veto is respected. There is no forced termination in this path.
- Native code-table borders left stale lines after scrolling or resizing.
  Geometry changes now queue an editor background repaint. The application
  does not reload the document or synchronously redraw the entire window.
  Pixel checks verify complete borders after scroll, repeated resize and a
  cached-editor switch, rather than merely checking saved RTF attributes.
- The integrated Notes caption treated new header actions as window-drag areas.
  Client selection, All notes, Trash, More, Help and Close now receive client
  mouse input. Empty header space remains draggable and edge resizing remains.
- Editor and button focus notifications could dismiss a newly opened inspector.
  Focus changes no longer execute workspace actions or dismiss the panel.
- Enter and Escape in an open status dropdown now belong to that dropdown;
  they do not prematurely dismiss Details.
- The built-in guide now describes the Notes button, client picker, direct
  template/snippet editors, Trash retention, and PC-default-browser link routing.

## Completed coverage

The 12 core suites passed. Retained results are in
`build/full-app-stress/Core-results.json`:

| Area | Suites / exercised behavior |
| --- | --- |
| Starter safety | DefaultProfileSanitizer, DefaultProfileArchiveSanitizer |
| Configuration | BrowserPreferencesJson, ConfigPersistence |
| Guidance | GuidedWalkthrough; revised topic revision separately rechecked |
| Client lifecycle | ArchiveRoundTrip, RenameShortcutTransactions, HandleBoundRename, ClientShortcutName, ClientActivity |
| Notebook storage | ClientNotes; 10,000-page round trip, corruption/truncation, Unicode, stale writes, atomic failure and unsafe paths |
| Installer | InstallerRuntime; 21 isolated production installer success/failure scenarios and installed-child handoff |

All 16 integration suites passed after the corrections recorded below:

| Suites | Latest passing evidence below `build/full-app-stress` |
| --- | --- |
| WorkflowFeatures, RestoreTabsToggle, ExternalBrowserProfileUse, ThemeResponsiveness, CoordinatedShutdown, CleanupDialogFaultAndDpi | Initial `Integration-results.json`; theme stress used 1,000 selections |
| QuickTourUi, SameClientMultiBrowser, RestoreAfterBrowserClose, DefaultEditorDiscard | `after-harness-fixes/Integration-results.json` |
| GuidedWalkthroughUi, AutoFetchDialogLayout, CleanupScanUi, InactiveClientCleanup | `ready-controls/Integration-results.json` |
| LiveIconRefresh, including two actual live icon changes | `live-icon-two-changes.log` |
| ClientProductivityUi launcher filtering, cancellation and zero/one/multiple Close all | `launcher-close-race.log` |

The final native Notes suite passed **531 checks**, including the new focus,
dropdown, caption, delayed-close and border regressions. Evidence:
`build/full-app-stress/dropdown-key-native.log`. It exercises production Notes
code with real native RichEdit/tab controls; confirmation, shell and file-picker
boundaries are isolated. A focused delayed-close check also verified the final
process-handle refinement. The walkthrough recordings provide additional real
mouse and keyboard acceptance coverage of the final release executable.

## Test-harness corrections and limits

WMI/CIM process enumeration failed on this host. Browser tests now read process
metadata through a bounded, read-only native query while retaining exact
fixture path and process-start checks. PowerShell 7 drawing references and stale
launcher-control IDs were corrected. Earlier failing logs are retained, and
passing evidence is identified explicitly above; an initial stage JSON is not
rewritten to hide failures. `Test-FullApp.ps1` runs the stages serially and records
each subprocess result.

Actual browser integration covered installed Edge and Chrome. Brave and Firefox
were absent at their standard executable paths and are not claimed as live
coverage. Native review used the host's 120/96-DPI monitors; cleanup layout also
tested synthetic 96/144/192/240 DPI. Installer tests exercised isolated
production paths, not deployment over the user's installed app. These results
do not establish that every possible browser, display or timing issue is absent.

The earlier chat's request-preparation failures remain unexplained. Application
regressions and recording-driver errors are tracked separately from that issue.
