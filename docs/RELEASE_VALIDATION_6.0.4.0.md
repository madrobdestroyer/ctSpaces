# ctSpaces 6.0.4.0 validation

Date: September 21, 2026.

## Scope

Delete Multiple Clients and Clean Up Inactive Clients now inspect candidates
on a worker thread. A themed progress dialog keeps the main message loop
responsive and permits cooperative cancellation before the selection preview.
Cancellation never opens a partially scanned preview and never starts deletion.
Existing activity tracking initialization can still occur during inspection.

The busy guard prevents nested cleanup and client launches during this
operation. Actual confirmed deletion retains its existing fresh browser,
activity, path, layout, and recursive filesystem checks. This release does not
add cancellation to an actual deletion after confirmation or change profile
formats, browser behavior, the guide, or the Quick Tour.

## Reproduced issue and comparison

The previous 6.0.3.0 executable was tested with isolated local client fixtures.
With 100 clients, its main message loop stopped responding for approximately
3.56 seconds before showing the selection list. With 200 clients, the
continuous stall was approximately 7.53 seconds. It recovered after discovery;
this test reproduced a scan stall, not a permanent deadlock. The exact cause
on the reporting colleague's computer remains unconfirmed.

Repeating the same fixture and heartbeat probe with 6.0.4.0 produced:

| Local clients | Time until selection preview | Failed heartbeat probes | Longest observed stall |
| --- | --- | --- | --- |
| 1 | 140 ms | 0 of 2 | 0 ms |
| 100 | 4,321 ms | 0 of 66 | 0 ms |
| 200 | 9,321 ms | 0 of 143 | 0 ms |

Heartbeat probes used a 200 ms timeout. These results demonstrate UI
responsiveness, not a scan speed improvement. Cancellation from the completed
selection preview restored the main window, and fixture markers were unchanged.

## Tested executable

File and product version: `6.0.4.0`. Display and About version: `6.0`.

Size: 5,575,680 bytes.

SHA256: `DD3C1D185EA3D2CB6705515ED4696F6A1F29C22B36ECB4B84E11EBEBD673A066`.

The Release x64 build passed without warnings or errors after correcting a
geometry integer type mismatch found by the initial compile. Release checks
passed. Bundled 7 Zip remains version 26.02. Windows Defender reported no
threats in this exact executable.

## Completed regression checks

`Test-ClientActivity.ps1` passed calendar boundaries, history, atomic write
failure, rename, corruption, and unsafe marker tests. Its integration checks
now also require cancellation to reach both preview validation functions.

`Test-InactiveClientCleanup.ps1` passed whole client deletion in disposable
fixtures, archived client inclusion, activity recording, preview cancellation,
reopened client protection, recent and future activity protection, corrupt
and unsafe record protection, Default and Temp protection, managed shortcut
removal, metadata pruning, multiple selection, manual deletion without the
inactivity timer, and unselected client preservation. Theme, compact result,
About, and simplified text menu checks also passed. Live configuration was
unchanged.

`Test-CleanupDialogFaultAndDpi.ps1` passed dialog creation failure recovery and
actual monitor checks at 96 and 144 DPI. Owner drawn font and layout unit
tests passed at 96, 144, 192, and 240 DPI, including bounded fallback output
for 100 clients.

`Test-CleanupScanUi.ps1` passed on the frozen executable above. Deliberately
slowed scans remained responsive. Cancel, Escape, the progress close button,
and a close request to the owner all cancelled within two seconds and
preserved the launcher. Every cancellation route allowed a subsequent fully
validated preview. Nested cleanup commands and a valid client launch request
were rejected while inspection was active. Unsafe folders remained excluded.

Injected scan dialog, timer, and worker creation failures reported safe
failure and recovered without deletion. Missing, empty, and invalid Sites
collections were handled safely. Client fixture snapshots and live
configuration were unchanged. No QA process remained afterward.

Complete progress window captures in Marine and Dark Gothic were visually
reviewed for readable status text, visible Cancel, themed animation, and
uncropped bounds. The new harness required corrections to cross process
control text reads, startup readiness polling, and DPI aware capture before
its final successful run. These were harness defects; the production binary
was unchanged throughout these runs.

The first cleanup regression invocation used PowerShell 7, whose different
System.Drawing assembly forwarding prevented the existing harness from
compiling. Running that harness in Windows PowerShell passed. This was a test
host mismatch, not an application failure.

## Evidence and boundaries

Local logs are under ignored `build/cleanup-scan-6.0.4.0/`. The comparison
fixtures and results are in ignored
`build/cleanup-freeze-diagnosis/run-052be30671/` for 6.0.3.0 and
`build/cleanup-freeze-diagnosis/run-f416dd63ad/` for 6.0.4.0. Test fixtures and
screenshots are not published or packaged.
The final focused scan evidence is `ui4.log` and `ui4/summary.json` with both
theme captures in `ui4/`.

This focused correction does not repeat the whole browser or guide matrix.
Previous evidence remains in the earlier release validation reports. Tests
used separate QA instances and disposable project data; the installed
application and real client collection were not upgraded or deleted.

## Distribution

`dist/x64/Release/ctSpaces6.0.4.0.zip` contains exactly one root entry,
`ctSpaces.exe`. Packaging reopened the ZIP and verified the embedded
executable against the tested SHA256 above. No documentation, license files,
logs, or source files were added to the colleague ZIP.

ZIP SHA256:
`EF1AB9AD60E0C4980FD192294DA5BA2BB405C0473341F9AD960A5716A54B5FA8`.

The 6.0.3.0, 6.0.2.0, 6.0.1.0, and 6.0.0.0 ZIP hashes were checked and remain
unchanged. Earlier release assets are retained.
