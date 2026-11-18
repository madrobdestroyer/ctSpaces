param([Parameter(Mandatory)][string]$ArtifactDirectory)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Retention.psm1') -Force
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifact = [IO.Path]::GetFullPath($ArtifactDirectory)
if (!$artifact.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $artifact)) { throw 'Tests require a fresh artifact directory under build.' }
for ($part=[IO.Path]::GetDirectoryName($artifact);$part;$part=[IO.Path]::GetDirectoryName($part)) {
    if (!(Test-Path -LiteralPath $part)) { continue }
    $item=Get-Item -LiteralPath $part -Force
    if ($item.LinkType -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Linked artifact parent refused.' }
}
$fixture = Join-Path $artifact 'fixture'
[IO.Directory]::CreateDirectory((Join-Path $fixture 'build\retained')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $fixture 'public')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $fixture 'scripts')) | Out-Null
$checks = [Collections.Generic.List[string]]::new()
function Need([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Save([string]$Path,$Value) { ConvertTo-Json -InputObject $Value -Depth 10 | Set-Content -LiteralPath $Path -Encoding utf8NoBOM }
function Make([string]$Relative,[string]$Text) {
    $path = Join-Path $fixture $Relative
    [IO.File]::WriteAllText($path,$Text)
    [pscustomobject]@{Path=$path; Bytes=(Get-Item -LiteralPath $path).Length; Sha256=(Get-FileHash -LiteralPath $path).Hash}
}
function Reject([scriptblock]$Action,[string]$Name,[string]$Pattern) {
    $rejected = $false
    try { $null = & $Action } catch {
        Need ($_.Exception.Message -match $Pattern) "Unexpected rejection for $Name`: $($_.Exception.Message)"
        $rejected = $true
    }
    Need $rejected "Expected rejection: $Name"
    $checks.Add($Name)
}
$current = [ordered]@{Exe=(Make 'build\retained\current.exe' 'working payload');
    Pdb=(Make 'build\retained\current.pdb' 'working symbols');
    ReviewMetadata=(Make 'build\retained\current.json' '{"working":true}')}
$rollback = [ordered]@{Exe=(Make 'build\retained\rollback.exe' 'rollback payload');
    Pdb=(Make 'build\retained\rollback.pdb' 'rollback symbols');
    ReviewMetadata=(Make 'build\retained\rollback.json' '{"rollback":true}')}
$current.Build = $current.Exe.Sha256.Substring(0,8)
$rollback.Build = $rollback.Exe.Sha256.Substring(0,8)
$public = Make 'public\release.zip' 'public release'
$library = Make 'build\retained\7zip.lib' 'retained build library'
$qa = Make 'scripts\runner.ps1' 'write-output qa'
$source = Make 'App.cpp' 'int main() {return 0;}'
$null = Make '.gitignore' "/build/`n"
& git -C $fixture init --quiet
if ($LASTEXITCODE) { throw 'Could not initialize isolated Git fixture.' }
$manifest = Join-Path $fixture 'build\retained\retention.json'
$plan = [ordered]@{SchemaVersion=1; Version='6.3.5.0'; Workspace=$fixture; Current=$current; Rollback=$rollback;
    PublicRelease=@($public); BuildInputs=@($library); QaInputs=@($qa); ProductionSourcePaths=@($source.Path);
    HistoricalVerifiedUnchanged=@(@{Path=(Join-Path $fixture 'old-generated\gone.exe'); Sha256=('A'*64); Bytes=2})}
Save $manifest $plan
$retention = Read-Retention $manifest $fixture
$verified = Confirm-Retention $retention
Need ($verified.Protected.Count -eq 9) 'Unexpected selected guard count.'
Need (@($verified.Protected | Where-Object Path -Like '*old-generated*').Count -eq 0) 'Historical paths leaked into guards.'
$checks.Add('selected pair verifies without missing historical rollback paths')
$stage = Join-Path $fixture 'build\stage'
$staged = New-RetentionRollbackStage $retention $stage
Need ($staged.Files.Count -eq 3 -and !$staged.InstalledFilesWritten -and !$staged.AppsLaunched) 'Rollback stage scope differs.'
Need ((Get-FileHash -LiteralPath (Join-Path $stage 'ctSpaces.exe')).Hash -eq $rollback.Exe.Sha256) 'Wrong staged rollback bytes.'
$checks.Add('rollback stages exact EXE PDB and metadata into an isolated directory')
Reject { New-RetentionRollbackStage $retention $stage } 'rollback refuses an existing destination' 'Destination already exists'
$baseline = Join-Path $fixture 'build\baseline'
$captured = New-RetentionBaseline $retention $baseline
$frozen = @(Get-Content -LiteralPath $captured.ValidationManifest -Raw | ConvertFrom-Json)
Need (@($frozen | Where-Object Path -eq $source.Path).Count -eq 1) 'Root source is missing from baseline.'
Need (@($frozen | Where-Object Path -eq $qa.Path).Count -eq 1) 'Useful runner is missing from baseline.'
Need (@($frozen | Where-Object Path -Like '*old-generated*').Count -eq 0) 'Old generated payload was imported.'
foreach ($row in $frozen) { Need ((Get-FileHash -LiteralPath $row.Snapshot).Hash -eq $row.Sha256) 'Snapshot hash differs.' }
$checks.Add('fresh baseline freezes live inputs and excludes historical generated payloads')
Reject { New-RetentionBaseline $retention $baseline } 'baseline refuses an existing destination' 'Destination already exists'
Reject { New-RetentionBaseline $retention (Join-Path $fixture 'outside-build') } 'baseline refuses a destination outside build' 'Evidence must use a fresh directory under build'
Reject { New-RetentionRollbackStage $retention (Join-Path $fixture 'build\retained\stage') } 'rollback refuses retained payload scope' 'Evidence must use a fresh directory under build'
$original = [IO.File]::ReadAllText($public.Path)
[IO.File]::WriteAllText($public.Path,'tampered public')
Reject { Confirm-Retention $retention } 'changed public bytes are rejected' 'Retained file differs: .*public'
[IO.File]::WriteAllText($public.Path,$original)
$original = [IO.File]::ReadAllText($rollback.Exe.Path)
[IO.File]::WriteAllText($rollback.Exe.Path,'tampered rollback')
Reject { Confirm-Retention $retention } 'changed rollback bytes are rejected' 'Retained file differs: .*rollback.exe'
[IO.File]::WriteAllText($rollback.Exe.Path,$original)
$original = [IO.File]::ReadAllText($qa.Path)
[IO.File]::WriteAllText($qa.Path,'write-output next-regression')
Reject { Confirm-Retention $retention } 'runner changes require an explicit baseline refresh' 'Retained file differs: .*runner.ps1'
$refreshed = New-RetentionBaseline $retention (Join-Path $fixture 'build\baseline-refreshed') -RefreshQaInputs
Need ($refreshed.QaInputChanges.Count -eq 1 -and $refreshed.QaInputChanges[0].Path -eq $qa.Path) 'Source refresh was not recorded.'
$checks.Add('explicit runner refresh records the changed input')
[IO.File]::WriteAllText($qa.Path,$original)
$outside = Join-Path $artifact 'outside.exe'
[IO.File]::WriteAllText($outside,'outside workspace')
$originalCurrentExe = $current.Exe
$originalCurrentBuild = $current.Build
$plan.Current.Exe = [pscustomobject]@{Path=$outside; Bytes=(Get-Item -LiteralPath $outside).Length; Sha256=(Get-FileHash -LiteralPath $outside).Hash}
$plan.Current.Build = $plan.Current.Exe.Sha256.Substring(0,8)
Save $manifest $plan
Reject { Confirm-Retention (Read-Retention $manifest $fixture) } 'arbitrary external executable paths are rejected' 'Path outside the retained scope'
$plan.Current.Exe = $originalCurrentExe
$plan.Current.Build = $originalCurrentBuild
Save $manifest $plan
$null = Confirm-Retention (Read-Retention $manifest $fixture)
$junction = Join-Path $fixture 'linked'
$null = New-Item -ItemType Junction -Path $junction -Target (Join-Path $fixture 'public')
$plan.BuildInputs = @([pscustomobject]@{Path=(Join-Path $fixture 'linked\release.zip'); Bytes=$public.Bytes; Sha256=$public.Sha256})
Save $manifest $plan
try { Reject { Confirm-Retention (Read-Retention $manifest $fixture) } 'reparse ancestors are rejected' 'Linked path refused' }
finally {
    $link = Get-Item -LiteralPath $junction -Force
    Need ($link.LinkType -eq 'Junction' -and $link.Target -eq (Join-Path $fixture 'public')) 'Fixture junction identity changed.'
    Remove-Item -LiteralPath $junction -Force
}
$result = [pscustomobject]@{Status='Passed'; AtUtc=[DateTime]::UtcNow.ToString('o'); Checks=$checks.Count; Cases=@($checks);
    Fixture=$fixture; AppsLaunched=$false; InstalledFilesWritten=$false; PublicProjectFilesWritten=$false}
Save (Join-Path $artifact 'result.json') $result
$result | ConvertTo-Json -Depth 6
