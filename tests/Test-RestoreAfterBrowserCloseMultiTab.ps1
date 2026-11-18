param(
    [string]$ExePath = '',
    [string]$BrowserPath = '',
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\restore-after-browser-close-multi-tab'),
    [int]$TimeoutSeconds = 15
)
$ErrorActionPreference = 'Stop'
$ExePath = & (Join-Path $PSScriptRoot '..\qa\Resolve-Executable.ps1') -ExePath $ExePath
& (Join-Path $PSScriptRoot 'Test-RestoreAfterBrowserClose.ps1') `
    -ExePath $ExePath -BrowserPath $BrowserPath -TimeoutSeconds $TimeoutSeconds -ArtifactDirectory $ArtifactDirectory `
    -TabCount 2 -CloseMode SessionShutdown
