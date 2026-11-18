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
    if ($item.LinkType -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Linked fixture parent refused.' }
}
$fixture = Join-Path $artifact 'fixture'
[IO.Directory]::CreateDirectory((Join-Path $fixture 'build\retained')) | Out-Null
$checks = [Collections.Generic.List[string]]::new()
function Need([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Reject([scriptblock]$Action,[string]$Name,[string]$Pattern) {
    $rejected=$false
    try { $null=& $Action } catch {
        Need ($_.Exception.Message -match $Pattern) "Unexpected rejection for $Name`: $($_.Exception.Message)"
        $rejected=$true
    }
    Need $rejected "Expected rejection: $Name"
    $checks.Add($Name)
}
function Record([string]$Name,[string]$Text) {
    $path=Join-Path $fixture $Name
    [IO.File]::WriteAllText($path,$Text)
    [pscustomobject]@{Path=$path; Bytes=(Get-Item -LiteralPath $path).Length; Sha256=(Get-FileHash -LiteralPath $path).Hash}
}
$fallback=Join-Path $fixture 'dist\x64\Release\ctSpaces.exe'
Need ((Resolve-RetainedExecutable -Workspace $fixture) -eq $fallback) 'Fresh checkout default changed.'
Need (!(Test-Path -LiteralPath $fallback)) 'Missing fallback was unexpectedly created.'
$checks.Add('fresh checkout resolves built dist without requiring an EXE for native-only tests')
$selected=Record 'build\retained\current.exe' 'current executable fixture'
$other=Record 'build\retained\rollback.exe' 'rollback executable fixture'
$metadata=Record 'build\retained\metadata.json' '{}'
$source=Record 'App.cpp' 'int main() { return 0; }'
$manifest=Join-Path $fixture 'build\retained\retention.json'
$plan=[ordered]@{SchemaVersion=1; Version='6.3.5.0'; Workspace=$fixture;
    Current=@{Build=$selected.Sha256.Substring(0,8); Exe=$selected; Pdb=$metadata; ReviewMetadata=$metadata};
    Rollback=@{Build=$other.Sha256.Substring(0,8); Exe=$other; Pdb=$metadata; ReviewMetadata=$metadata};
    PublicRelease=@($metadata); BuildInputs=@($metadata); QaInputs=@(); ProductionSourcePaths=@($source.Path)}
ConvertTo-Json -InputObject $plan -Depth 10 | Set-Content -LiteralPath $manifest -Encoding utf8NoBOM
Need ((Resolve-RetainedExecutable -Workspace $fixture) -eq $selected.Path) 'Selected current EXE was not resolved.'
$checks.Add('local retention selects the exact working EXE instead of public dist')
Need ((Resolve-RetainedExecutable -Workspace $fixture -ExePath $other.Path) -eq $other.Path) 'Explicit candidate did not win.'
$checks.Add('explicit candidate keeps precedence')
$missing=Join-Path $fixture 'missing-explicit.exe'
Need ((Resolve-RetainedExecutable -Workspace $fixture -ExePath $missing) -eq $missing) 'Explicit missing-path handling changed.'
$checks.Add('explicit missing paths retain caller-controlled validation')
$text=[IO.File]::ReadAllText($selected.Path)
[IO.File]::WriteAllText($selected.Path,'tampered selected executable')
Reject { Resolve-RetainedExecutable -Workspace $fixture } 'tampered selected EXE is rejected' 'Retained file differs'
[IO.File]::WriteAllText($selected.Path,$text)
[IO.File]::WriteAllText($manifest,'invalid json')
Reject { Resolve-RetainedExecutable -Workspace $fixture } 'malformed local retention is rejected' 'JSON'
Need ((Resolve-RetainedExecutable -Workspace $fixture -ExePath $other.Path) -eq $other.Path) 'Explicit path unexpectedly used local retention.'
$checks.Add('explicit candidate does not depend on local retention being present or valid')
$actual=& (Join-Path $PSScriptRoot 'Resolve-Executable.ps1')
$localManifest=Join-Path $root 'build\retained\retention.json'
if (Test-Path -LiteralPath $localManifest) {
    $expected=Read-Retention $localManifest $root
    Need ($actual -eq $expected.Data.Current.Exe.Path -and
        (Get-FileHash -LiteralPath $actual).Hash -eq $expected.Data.Current.Exe.Sha256) 'Actual retained default differs.'
} else {
    Need ($actual -eq (Join-Path $root 'dist\x64\Release\ctSpaces.exe')) 'Actual fresh-checkout default differs.'
}
$checks.Add('normal project entry resolves the configured current build or fresh-checkout dist')
$public=Join-Path $root 'dist\x64\Release\ctSpaces.exe'
Need ((& (Join-Path $PSScriptRoot 'Resolve-Executable.ps1') -ExePath $public) -eq $public) 'Explicit public release selection changed.'
$checks.Add('explicit public release entry remains available')
$scripts = @(Get-ChildItem -LiteralPath (Join-Path $root 'tests') -Filter '*.ps1' -File | Where-Object {
    [IO.File]::ReadAllText($_.FullName) -match 'qa\\Resolve-Executable\.ps1'
} | ForEach-Object { [pscustomobject]@{Path=$_.FullName} })
foreach ($row in $scripts) {
    $text = [IO.File]::ReadAllText($row.Path)
    Need ($text -match "\[string\]\`$ExePath\s*=\s*''") "Default was not migrated: $($row.Path)"
    Need ($text -match 'qa\\Resolve-Executable\.ps1') "Resolver entry is missing: $($row.Path)"
    $tokens=$null; $errors=$null
    $null=[Management.Automation.Language.Parser]::ParseFile($row.Path,[ref]$tokens,[ref]$errors)
    Need (!$errors.Count) "Wrapper syntax differs: $($row.Path)"
}
Need ($scripts.Count -eq 19) 'The active wrapper scope differs.'
$checks.Add('all nineteen active wrappers retain ExePath parameters and parse with shared resolution')
$release=[IO.File]::ReadAllText((Join-Path $root 'tests\Test-Release.ps1'))
Need ($release -match 'if\s*\(!\$SourceOnly\)\s*\{[^}]+Resolve-Executable') 'SourceOnly now depends on an application payload.'
$checks.Add('source-only release checking preserves its source-only path')
$result=[pscustomobject]@{Status='Passed'; AtUtc=[DateTime]::UtcNow.ToString('o'); Checks=$checks.Count; Cases=@($checks);
    DefaultExecutable=$actual; PublicExecutable=$public; AppsLaunched=$false; InstalledFilesWritten=$false}
ConvertTo-Json -InputObject $result -Depth 6 | Set-Content -LiteralPath (Join-Path $artifact 'result.json') -Encoding utf8NoBOM
$result | ConvertTo-Json -Depth 6
