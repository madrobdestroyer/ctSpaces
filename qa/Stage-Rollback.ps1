param([Parameter(Mandatory)][string]$ArtifactDirectory,
      [string]$Manifest=(Join-Path $PSScriptRoot '..\build\retained\retention.json'))
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Retention.psm1') -Force
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$retention = Read-Retention ([IO.Path]::GetFullPath($Manifest)) $workspace
New-RetentionRollbackStage $retention ([IO.Path]::GetFullPath($ArtifactDirectory)) | ConvertTo-Json -Depth 8
