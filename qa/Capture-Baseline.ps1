param([Parameter(Mandatory)][string]$ArtifactDirectory,
      [string]$Manifest=(Join-Path $PSScriptRoot '..\build\retained\retention.json'),
      [switch]$RefreshQaInputs)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Retention.psm1') -Force
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$retention = Read-Retention ([IO.Path]::GetFullPath($Manifest)) $workspace
New-RetentionBaseline $retention ([IO.Path]::GetFullPath($ArtifactDirectory)) -RefreshQaInputs:$RefreshQaInputs |
    ConvertTo-Json -Depth 8
