param([string]$ExePath='')
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Retention.psm1') -Force
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Resolve-RetainedExecutable -Workspace $workspace -ExePath $ExePath
