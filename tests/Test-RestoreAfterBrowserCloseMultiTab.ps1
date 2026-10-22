param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$BrowserPath = '',
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\restore-after-browser-close-multi-tab'),
    [int]$TimeoutSeconds = 15
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Test-RestoreAfterBrowserClose.ps1') `
    -ExePath $ExePath -BrowserPath $BrowserPath -TimeoutSeconds $TimeoutSeconds -ArtifactDirectory $ArtifactDirectory `
    -TabCount 2 -CloseMode SessionShutdown
