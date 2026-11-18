# Retained builds and QA baselines

The current retention manifest is `build/retained/retention.json`. It records
one working version, one rollback, their exact symbols and review records,
the public release files and the required build library. The working EXE
is the installed file. Project review uses that path from `Current.Exe`.

Active application test wrappers resolve their default EXE through
`qa/Resolve-Executable.ps1`. With a local retention manifest, this verifies
the configured working EXE's actual hash and length. Explicit `-ExePath`
arguments retain priority. A checkout without a retention manifest uses
`dist/x64/Release/ctSpaces.exe`, allowing the normal local build workflow.
Native-only test selections keep their existing handling of a missing
runtime EXE. `Test-Release.ps1 -SourceOnly` continues to inspect source only.

Run these commands from the project root in a background PowerShell session.

```powershell
pwsh -NoProfile -File .\qa\Verify-Retention.ps1
pwsh -NoProfile -File .\qa\Capture-Baseline.ps1 -ArtifactDirectory C:\Projects\ctSpaces\build\qa-next-baseline
pwsh -NoProfile -File .\qa\Stage-Rollback.ps1 -ArtifactDirectory C:\Projects\ctSpaces\build\qa-rollback-check
```

Verification compares the selected EXE/PDB pairs, review metadata, public
files, build inputs and preserved QA inputs against the manifest. Source
paths and their parents must be ordinary paths. A changed or missing
retained payload stops verification.

Baseline capture first verifies retention, then freezes the current Git
source inventory, production source paths, build inputs and preserved QA
scripts. It writes `protected-before.json`, `production-inputs.json`,
`validation-inputs.json` and `baseline.json` under the fresh artifact folder.
Source records contain their original path, snapshot path, length and
SHA-256. These records can fence the next build and test run. Capture
imports the selected current and rollback guards from the retention
manifest. Historical delivery guard lists remain historical evidence.

When intentionally editing a preserved QA runner, use `-RefreshQaInputs`
on baseline capture. The baseline records each changed QA input and its
old and new hash. This switch permits fresh QA source capture while all
application, public release and build dependency pins still verify.

Rollback staging copies the single retained rollback EXE, PDB and review
metadata into the fresh directory and verifies their actual bytes. It
creates a `result.json` receipt. The staging directory is disposable after
its receipt is sealed by the cleanup manifest.

Use the new baseline for future work. The scripts retained under the old
`build/qa12*` paths describe their completed runs and can contain historical
binary paths. A full replay requires reproducing the inputs appropriate to
that historical run. The prior restricted archive wrapper and mutation
scopes remain subject to the handoff's recorded limits.

For the next successful delivery, the formerly working version becomes
the single rollback only after the new candidate passes qualification and
delivery verification. Update the retention manifest to the exact tested
hashes and verify it before pruning the former rollback or disposable
compiler/test outputs. Preserve the current source, useful QA scripts,
small result records and the matching symbols for the two selected builds.

The focused helper regression uses an isolated fixture and records its
cases without launching either application.

```powershell
pwsh -NoProfile -File .\qa\Test-Retention.ps1 -ArtifactDirectory C:\Projects\ctSpaces\build\qa-retention-helper-check
```
