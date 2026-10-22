# Release validation: 6.3.4.0

This record describes the October 22, 2026 release candidate and its verified
package. The publication target is
[v6.3.4.0](https://github.com/madrobdestroyer/ctSpaces/releases/tag/v6.3.4.0);
the tag identifies the corresponding source commit. Validation preceded
publication. Build/log links below identify retained local workspace evidence;
these private artifacts are not included in the repository or runtime ZIP.

The exact production candidate is
`build/release-2026-10-22/verified-candidate/ctSpaces.exe`. Its Windows file and
product versions are **6.3.4.0**; the application's major/minor display remains
**6.3**. Its SHA-256 is
`26E6228B9BE2BDF685ADA4E529972EA9435E2662323285E935FFA5D1F404EFDA`.
The matching private PDB has SHA-256
`74A00C36CA996A12F034E49D7198D8813B3CAFA591F8F69CA96303E779435232`.

The candidate was built afresh with **zero production compiler warnings and
zero errors**. `tests/Test-Release.ps1` passed against this exact executable,
checking the current source, Windows version resources and bundled starter
archive. The recorded starter archive SHA-256 is
`54A0FE4B18C681353852DD86363306AC5E475784111FD76C949E829DC8464EA5`;
the bundled 7-Zip version is 26.02. Candidate and direct release receipts are
[candidate.json](../build/release-2026-10-22/candidate.json),
[candidate-build.log.json](../build/release-2026-10-22/candidate-build.log.json)
and [release-check.log.json](../build/release-2026-10-22/release-check.log.json).

The preceding **6.3.3.0** candidate, SHA-256
`5BE4606690561520F0D8DE70026950C7BEAD21FAE5B9F29D0FD870B4533E5F14`,
passed the wholly fresh October 21 checkpoint: **58 unique available Core
suites** and **18 headless Notes workspace groups / 4,852 checks**. Its four
tab-layout suites are included in the 58: client settings 221, Chromium JSON
preferences 124, Firefox preferences 332 and guarded menu/launch integration
57 checks. These overlapping groups are not added into a new assertion total.
The exact results remain in the
[earlier checkpoint record](../build/tab-layout-2026-10-21/result.json).

Between that completed checkpoint and the 6.3.4.0 build, **339 of the 340 frozen
inputs were identical**. Only `version.h` changed, from SHA-256
`B754BBC58A71A64B0A675FC197D256BE23962AD62E07FD59F856F24350950B08`
to `2EBBFF98BCFFDFFA9E89E4AF88540FD7D2E984BCE279D773747B8632201278C5`.
The new [340-input source manifest](../build/release-2026-10-22/source-frozen.json)
has SHA-256
`0437070DC7713DF6C64795A69A1D29E66435CC533DA11C6EC3764BFD76A86131`.
The 58 Core and 18 Notes groups were **not rerun on 6.3.4.0** and are not
relabeled as such. They provide behavioral evidence for the unchanged source;
the following installer, exact-candidate and live checks are fresh 6.3.4.0
evidence. The cumulative release source set includes **20 new production
files and 205 test/support files** alongside the existing source. These source
files are separate from the runtime ZIP contents.

Fresh production-linked installer harnesses passed **20 headless runtime
scenarios** and **70 publication-boundary checks**, sequentially in isolated
fixtures. Both harnesses have actual 6.3.4.0 VERSIONINFO resources. The runtime
suite calls the real production version reader before exercising install,
newer/equal/older update, copy and launch-validation paths. Its observed invalid
paint, presentation, window requests, windows, native show/focus/foreground and
device-input guards were all zero. The foreground installed-child handoff was
excluded; these were not real user installations or updates. Native harness
builds retained 29 and 58 warnings respectively, with zero errors; the
zero-warning claim above applies to the production candidate. Exact binaries,
logs, hashes and unchanged source fences are recorded in
[installer-checks/result.json](../build/release-2026-10-22/installer-checks/result.json).

The live review used a portable, isolated copy with a unique QA instance and
data and shortcut directories beneath its own executable folder. The
[launch receipt](../build/release-2026-10-22/live-qa-launch.json) and
[live UI record](../build/release-2026-10-22/live-ui-result.json) bind all actions
to the same `26E6228B...` executable. Fictional clients exercised ordinary Edge
launch, Temp creation and normal close, normal Notes opening, typing,
Undo/Redo, autosave and close/reopen. An actual corner drag changed Notes from
**960 × 640 to 900 × 540**; maximize reached **1707 × 1067**, followed by restore
to **900 × 540**. Text, layout and edges remained intact in the observed states.
Client switching reused the same Notes window and retained its geometry;
notebooks remained separate and Undo did not cross clients. Closing the
launcher left Notes visible. Sampled captures and these actions do not prove
continuous compositor or flicker absence.

Actual tab-layout review used **Microsoft Edge 155.0.4283.62**:

| Requested behavior | Live result |
|---|---|
| Global Vertical, new client/profile | Opened vertically |
| Global Vertical, Restore tabs unchecked, next launch | Opened vertically |
| Client Horizontal override, Restore tabs unchecked | Opened horizontally |
| Vertical requested while restoring a saved horizontal window | Restored window remained horizontal despite the saved startup preference |
| Horizontal requested while restoring a saved vertical window | Restored window remained vertical despite the saved startup preference |
| Vertical selected natively in Edge, saved and restored | Native vertical layout restored correctly |

The accepted scope is explicit: **the app's startup layout choice applies to
new/fresh windows; restoring an Edge session retains the saved window layout**.
The user accepted this behavior after clarification. The two restored-window
override cases are a documented limitation, not passing enforcement tests or a
repaired defect. No binary-session modification, forced disabling of Restore
tabs, or automatic browser-UI toggle was added. Chrome, Brave and Firefox had
native adapter coverage in the earlier checkpoint but were not live-tested in
this round. The bounded QA observations and primary-source corroboration are
retained in the private
[Edge investigation](../build/release-2026-10-22/edge-layout-investigation.json).

An ordinary authorized GUI backup/restore smoke used the exact release
candidate and synthetic client data. Empty selection disabled backup; selecting
the client enabled it. The app created and verified a backup, a later note edit
was made, and the app restored the selected archive. The notebook and client
layout-settings bytes matched their pre-backup hashes exactly. The later note
edit remained in the app-created pre-restore recovery folder. The archive
SHA-256 was
`A039C7C5450C379C8651F51ADB43B14795681ABC589DB580A0B53AA3E38EB3A3`.
Exact before/after and recovery hashes are in
[gui-backup-result.json](../build/release-2026-10-22/gui-backup-result.json).
This was the normal application workflow, distinct from the previously
unsigned-policy-blocked `ArchiveRoundTrip` wrapper. That wrapper, the blocked
extra archive-mutation corpus and previously blocked cleanup were **not
retried or bypassed**; no passing result is claimed for them.

The staged **ctSpaces6.3.4.0.zip** has SHA-256
`EE1FCB7E1932F62156CC113F120F367CC14DFE76128BDEF6237637A2212D691F`.
It contains exactly **one root entry, `ctSpaces.exe`**. The extracted entry is
byte-identical to the candidate and isolated live-review executable, with the
same 6.3.4.0 Windows version. PDBs, static libraries, client/config data,
backups, QA markers, logs and source are not separate archive entries. The
[private package receipt](../build/release-2026-10-22/stage-6.3.4.0-20261022T182026478Z-3ab284417e314f5881492e74b2f1e807/staging-result.json)
records its entry/hash verification and reviewed ordinary Windows PE imports.
Import inspection is not a clean-machine runtime proof. This document does not
claim actual browser/SSO coverage beyond the isolated Edge review, an actual
installed-child foreground handoff, or universal absence of bugs.

The verified EXE, private PDB and ZIP were promoted to `dist/x64/Release` with
exact hash equality. Independent verification rechecked all 340 source inputs,
488 SDK originals and retained copies, and all 26 historical release ZIPs.
The old distribution EXE/PDB were retained separately before replacement;
historical ZIPs and the static library remained unchanged. The final private
receipt is `build/release-2026-10-22/final-preflight.json`. Microsoft Defender's
custom scan of the exact promoted executable completed with exit code 0 and
reported no threats, recorded in `build/release-2026-10-22/defender-scan.log`.
