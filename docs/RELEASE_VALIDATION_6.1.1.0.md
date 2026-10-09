# ctSpaces 6.1.1.0 validation

Date: October 9, 2026.

## Scope

Focused correction for unreadable Close all and Restore tabs hints. Both
tooltip windows now participate in the existing theme refresh and lifetime
handling. Close all also receives font updates. No client operation or
browser launch behavior changes in this patch.

## Reproduction and regression

The updated `Test-ClientProductivityUi.ps1` reproduced the defect against
the executable extracted from the unchanged 6.1.0.0 ZIP. At startup, an
owned tooltip reported background and text colors of zero, meaning black
text on black. The exact Marine palette assertion failed as expected.

The final 6.1.1.0 executable passed the same test. All eight owned tooltip
windows matched the Marine palette at startup and the Dark Gothic palette
after applying a theme change, with distinct text and background colors.
This checks actual native control colors rather than a source only match.
It does not claim a pixel screenshot of the displayed hover balloon.

The existing productivity UI checks also passed: substring filtering,
preserving typed names, explicit selection and dismissal, new client
creation, notes save and discard, and Close All with zero, one, and
multiple tracked client sessions. Cancellation kept the session open.
Real isolated Edge sessions closed normally while the launcher remained.
Live ctSpaces configuration was unchanged. QA used separate data fixtures.

## Build and distribution

Release x64 built without warnings or errors. `Test-Release.ps1` passed for
Windows version `6.1.1.0` and display version `6.1`. Windows Defender
reported no threats in the final executable.

`New-ReleasePackages.ps1` verified that `ctSpaces6.1.1.0.zip` contains
exactly one root file, `ctSpaces.exe`, matching the tested executable.
Historical ZIPs were not replaced. Documentation stays in the repository.

Executable SHA256:
`DC2A08E041B42BD53EAF27A496AAB08CB9E64F2CC40A0D137170D63293EEB700`.

Application ZIP SHA256:
`3E1732827921426EE11E76BD69917220DC15280A0F4F505CBE29A1CDE1AB3766`.

This patch does not repeat the full earlier release audit. Broader feature
validation and retained boundaries remain documented in the
[6.1.0.0 validation report](RELEASE_VALIDATION_6.1.0.0.md).
