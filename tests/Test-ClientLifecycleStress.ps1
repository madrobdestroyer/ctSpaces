param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\build\notes-format-on-demand\app\ctSpaces.exe'),
    [ValidateRange(1, 2000)][int]$Cycles = 200,
    [ValidateRange(0, 20000)][int]$UnrelatedBrowserExitChurn = 0,
    [string]$EvidenceDirectory = (Join-Path $PSScriptRoot '..\build\client-lifecycle-stress'),
    [switch]$KeepFixture
)

$ErrorActionPreference = 'Stop'
# Reuse the real workflow fixture, native message driver and client assertions;
# failed runs preserve their exact data, last requested names and app errors.
& (Join-Path $PSScriptRoot 'Test-WorkflowFeatures.ps1') -ExePath $ExePath `
    -LifecycleStressCycles $Cycles -UnrelatedBrowserExitChurn $UnrelatedBrowserExitChurn `
    -EvidenceDirectory $EvidenceDirectory -KeepFixture:$KeepFixture
