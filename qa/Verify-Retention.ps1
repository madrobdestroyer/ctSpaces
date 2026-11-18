param([string]$Manifest=(Join-Path $PSScriptRoot '..\build\retained\retention.json'))
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Retention.psm1') -Force
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$retention = Read-Retention ([IO.Path]::GetFullPath($Manifest)) $workspace
$result = Confirm-Retention $retention
[pscustomobject]@{Status=$result.Status; Version=$result.Version; Current=$result.Current; Rollback=$result.Rollback;
    ProtectedFiles=$result.Protected.Count; Manifest=$result.Manifest} | ConvertTo-Json -Depth 5
