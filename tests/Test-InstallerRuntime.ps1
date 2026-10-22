param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [switch]$HeadlessOnly,
    [string]$ArtifactDirectory = ''
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project = Join-Path $PSScriptRoot 'InstallerRuntimeTests.vcxproj'
$sevenZipLibrary = Join-Path $root 'dist\x64\Release\7zip.lib'
$executable = Join-Path $root `
    "build\tests\InstallerRuntime\$Platform\$Configuration\InstallerRuntimeTests.exe"

if ($HeadlessOnly) {
    $artifacts = if ($ArtifactDirectory) { [IO.Path]::GetFullPath($ArtifactDirectory) }
        else { Join-Path $root 'build\tests\InstallerRuntimeHeadless' }
    if (-not $artifacts.StartsWith($root + '\build\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Headless runtime artifacts must stay below workspace build.'
    }
    New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
    $sourceFiles = @(Get-ChildItem -LiteralPath $root -File | Where-Object Extension -in '.cpp','.h','.inl','.rc')
    $sourceFiles += @('InstallerRuntimeTests.cpp','InstallerRuntimeTests.vcxproj','Test-InstallerRuntime.ps1' |
        ForEach-Object { Get-Item -LiteralPath (Join-Path $PSScriptRoot $_) })
    $sourceFiles += Get-Item -LiteralPath $sevenZipLibrary
    $sourceHashes = @($sourceFiles | ForEach-Object {
        [pscustomobject]@{ Path = $_.FullName; Sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
    })
    $sourceHashes | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
    $executable = Join-Path $artifacts 'bin\InstallerRuntimeTests.exe'
    @'
This is the production-linked installer runtime fixture, invoked only with
--installer-runtime-headless. Its twenty existing scenarios remain unchanged.
Headless boundaries reject null/invalid/foreign paint HWNDs before Windows,
reject presentation and window creation, and require zero actual GUI events.
The one native launch-validation child is this same test executable, started
suspended with CREATE_NO_WINDOW, resumed to its immediate-return probe entry,
and waited to completion. The foreground installed-child handoff is excluded.
All non-headless/handoff API forwarding remains unchanged and is not exercised.
Source hashes are checked after building and after the native run. Native/build
logs and executable hashes are retained even when the safety assertion fails.
'@ | Set-Content -LiteralPath (Join-Path $artifacts 'README.txt')
}

function Assert-HeadlessSourceFreeze {
    foreach ($item in $sourceHashes) {
        if ((Get-FileHash -LiteralPath $item.Path).Hash -ne $item.Sha256) {
            throw ('Source changed while verifying headless installer: ' + $item.Path)
        }
    }
}

function Invoke-HeadlessRuntimeTool([string]$Exe, [string[]]$Arguments, [string]$Log) {
    $launch = [Diagnostics.ProcessStartInfo]::new($Exe)
    $launch.WorkingDirectory = $root
    $launch.UseShellExecute = $false
    $launch.CreateNoWindow = $true
    $launch.RedirectStandardOutput = $true
    $launch.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $launch.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($launch)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(600000)) {
            $process.Kill($true)
            throw 'Headless installer tool exceeded its bounded timeout.'
        }
        $text = $stdout.Result + $stderr.Result
        [IO.File]::WriteAllText($Log, $text)
        $record = [pscustomobject]@{
            Exe = $Exe; Sha256 = (Get-FileHash -LiteralPath $Exe).Hash
            Arguments = $Arguments; CreateNoWindow = $true
            ExitCode = $process.ExitCode; Log = $Log
            LogSha256 = (Get-FileHash -LiteralPath $Log).Hash
        }
        $record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath ($Log + '.json')
        return [pscustomobject]@{ Record = $record; Text = $text }
    } finally { $process.Dispose() }
}

function Test-RegularToolFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    $item = Get-Item -LiteralPath $Path -Force
    return (-not $item.PSIsContainer -and
            ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0)
}

$msbuildPath = $null
$msbuildCommand = Get-Command msbuild.exe -CommandType Application `
    -ErrorAction SilentlyContinue | Select-Object -First 1
if ($msbuildCommand -and (Test-RegularToolFile $msbuildCommand.Source)) {
    $msbuildPath = $msbuildCommand.Source
}

if (-not $msbuildPath) {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-RegularToolFile $vswherePath) {
        $installationPath = @(
            & $vswherePath -latest -products * `
                -requires Microsoft.Component.MSBuild `
                -property installationPath
        ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath `
                'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-RegularToolFile $candidate) {
                $msbuildPath = [IO.Path]::GetFullPath($candidate)
            }
        }
    }
}

if (-not $msbuildPath) {
    throw 'MSBuild was not found. Install Visual Studio C++ Build Tools.'
}
if (-not (Test-RegularToolFile $sevenZipLibrary)) {
    throw "Build Release|x64 before running this test. Missing: $sevenZipLibrary"
}

if ($HeadlessOnly) {
    $built = Invoke-HeadlessRuntimeTool $msbuildPath @($project, '/m:1', '/t:Rebuild', '/nodeReuse:false',
        ('/p:Configuration=' + $Configuration), ('/p:Platform=' + $Platform), '/v:minimal',
        ('/p:OutDir=' + (Join-Path $artifacts 'bin') + '\'),
        ('/p:IntDir=' + (Join-Path $artifacts 'obj') + '\')) (Join-Path $artifacts 'build.log')
    if ($built.Record.ExitCode -ne 0) { throw 'Headless installer runtime build failed; inspect build.log.' }
    Assert-HeadlessSourceFreeze
    $native = Invoke-HeadlessRuntimeTool $executable @('--installer-runtime-headless') (Join-Path $artifacts 'native.log')
    Assert-HeadlessSourceFreeze
    $safe = $native.Record.ExitCode -eq 0 -and
        $native.Text -match 'Installer headless safety: invalid_paint=0 presentation=0 window_requests=0 windows=0 native_show=0 native_focus=0 native_foreground=0 device_input=0' -and
        $native.Text -match 'Installer runtime tests passed \(20 scenarios\)' -and
        $native.Text -notmatch 'Installed handoff readiness passed'
    [pscustomobject]@{ Passed = $safe; SourceFreeze = $true; Native = $native.Record; Build = $built.Record } |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
    Write-Output $native.Text
    if (-not $safe) { throw 'Headless installer runtime/safety check failed; inspect retained native.log.' }
} else {
    & $msbuildPath $project /m:1 /t:Rebuild `
        /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Installer runtime test build failed with exit code $LASTEXITCODE."
    }
    & $executable
    if ($LASTEXITCODE -ne 0) {
        throw "Installer runtime tests failed with exit code $LASTEXITCODE."
    }
}
