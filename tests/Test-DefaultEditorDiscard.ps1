param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [int]$Cycles = 2,
    [switch]$DiagnosticCloseBeforeLauncherReady
)

$ErrorActionPreference = 'Stop'
if ($Cycles -lt 1 -or $Cycles -gt 2) { throw 'Cycles must be 1 or 2.' }
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$archive = Join-Path $projectRoot 'Default.7z'
if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) { throw "Missing executable: $resolvedExe" }
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw "Missing starter archive: $archive" }
$edge = Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe'
if (-not (Test-Path -LiteralPath $edge -PathType Leaf)) {
    $edge = Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe'
}
if (-not (Test-Path -LiteralPath $edge -PathType Leaf)) { throw 'Microsoft Edge was not found.' }
$liveConfig = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'
$liveHash = if (Test-Path -LiteralPath $liveConfig -PathType Leaf) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $liveConfig).Hash
} else { '<missing>' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 10)
$qaParent = Join-Path $projectRoot 'build\default-discard-qa'
$qaRoot = Join-Path $qaParent ('run-' + $runId)
$qaExeDir = Join-Path $qaRoot 'a'
$qaExe = Join-Path $qaExeDir 'q.exe'
$data = Join-Path $qaExeDir 'd'
$sites = Join-Path $data 'Sites'
$launcher = $null
$ownedBrowserPids = [System.Collections.Generic.List[int]]::new()
$timeline = [System.Collections.Generic.List[string]]::new()
$completed = $false

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class DefaultDiscardQa {
    delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr window, uint message, UIntPtr wp, IntPtr lp);
    public static string Text(IntPtr window) {
        var text = new StringBuilder(8192); GetWindowTextW(window, text, text.Capacity); return text.ToString();
    }
    public static IntPtr Find(uint pid, string cls, string title) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((window, unused) => {
            uint id; GetWindowThreadProcessId(window, out id);
            var name = new StringBuilder(256); GetClassNameW(window, name, name.Capacity);
            if (id == pid && name.ToString() == cls &&
                (String.IsNullOrEmpty(title) || Text(window) == title)) { result = window; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static int Pid(IntPtr window) {
        uint pid; GetWindowThreadProcessId(window, out pid); return (int)pid;
    }
    public static string[] DialogSnapshots(uint pid) {
        var values = new System.Collections.Generic.List<string>();
        EnumWindows((window, unused) => {
            uint id; GetWindowThreadProcessId(window, out id);
            var name = new StringBuilder(256); GetClassNameW(window, name, name.Capacity);
            if (id != pid || name.ToString() != "#32770") return true;
            var staticText = new System.Collections.Generic.List<string>();
            EnumChildWindows(window, (child, childUnused) => {
                var childClass = new StringBuilder(256);
                GetClassNameW(child, childClass, childClass.Capacity);
                if (childClass.ToString().Equals("Static", StringComparison.OrdinalIgnoreCase)) {
                    string text = Text(child);
                    if (!String.IsNullOrWhiteSpace(text))
                        staticText.Add(text.Replace("\r", " ").Replace("\n", " "));
                }
                return true;
            }, IntPtr.Zero);
            values.Add("title=" + Text(window) + " static=" + String.Join(" || ", staticText));
            return true;
        }, IntPtr.Zero);
        return values.ToArray();
    }
}
'@

function Wait-Until([scriptblock]$Check, [string]$Description, [int]$Seconds = 20) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        $value = & $Check
        if ($value -is [IntPtr]) {
            if ($value -ne [IntPtr]::Zero) { return $value }
        } elseif ($value) { return $value }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Description
}
function Get-ProfileProcesses {
    $needle = [IO.Path]::GetFullPath($data).TrimEnd('\')
    @(Get-CimInstance Win32_Process -Filter "Name = 'msedge.exe'" -ErrorAction SilentlyContinue |
        Where-Object { [string]$_.CommandLine -match [regex]::Escape($needle) })
}
function Get-ProfileProcessSnapshot {
    $values = @(Get-ProfileProcesses | Sort-Object ProcessId | ForEach-Object {
        "pid=$($_.ProcessId) cmd=$($_.CommandLine)"
    })
    if ($values.Count -eq 0) { return '<none>' }
    return ($values -join ' || ')
}
function Get-LauncherDialogSnapshot {
    if (-not $launcher -or $launcher.HasExited) { return '<launcher-exited>' }
    $values = @([DefaultDiscardQa]::DialogSnapshots([uint32]$launcher.Id))
    if ($values.Count -eq 0) { return '<none>' }
    return ($values -join ' | ')
}
function Add-Timeline([int]$Cycle, [string]$Stage) {
    $timeline.Add(('{0:o} cycle={1} stage={2} dialogs=[{3}] processes=[{4}]' -f
        [DateTime]::UtcNow, $Cycle, $Stage,
        (Get-LauncherDialogSnapshot), (Get-ProfileProcessSnapshot)))
}
function Get-ProfileWindow {
    param([int]$BrowserProcessId)
    $window = [DefaultDiscardQa]::Find([uint32]$BrowserProcessId, 'Chrome_WidgetWin_1', $null)
    if ($window -ne [IntPtr]::Zero -and [DefaultDiscardQa]::IsWindowVisible($window)) { $window }
}
function Wait-Launcher {
    Wait-Until {
        if ($launcher.HasExited) { throw "QA launcher exited with code $($launcher.ExitCode)." }
        [DefaultDiscardQa]::Find([uint32]$launcher.Id, 'ctSpacesLauncherClass', $null)
    } 'QA launcher window was not ready.'
}
function Write-FailureLog([string]$Message) {
    if (-not (Test-Path -LiteralPath $qaRoot)) { return }
    New-Item -ItemType Directory -Path $qaRoot -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $qaRoot 'failure.log'), $Message)
}

try {
    New-Item -ItemType Directory -Path $qaExeDir, $sites -Force | Out-Null
    Copy-Item -LiteralPath $resolvedExe -Destination $qaExe -Force
    [IO.File]::WriteAllText((Join-Path $qaExeDir 'ctSpaces.portable'), '')
    Copy-Item -LiteralPath $archive -Destination (Join-Path $data 'Default.7z') -Force
    $keepClient = Join-Path $sites 'KeepClient'
    $keepSlot = Join-Path $keepClient 'Browsers\edge'
    $keepProfile = Join-Path $keepSlot 'Profile'
    $marker = Join-Path $keepProfile 'marker.txt'
    New-Item -ItemType Directory -Path (Join-Path $keepProfile 'Default') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $keepClient 'ctSpaces-client-v2'),
        "ctSpaces-client-schema=2`r`n")
    [IO.File]::WriteAllText((Join-Path $keepSlot 'ctSpaces-browser-v2'),
        "ctSpaces-browser-schema=2`r`nbrowser=edge`r`n")
    [IO.File]::WriteAllText((Join-Path $keepProfile 'ctSpaces'),
        "ctSpaces-profile=2`r`n")
    [IO.File]::WriteAllText((Join-Path $keepProfile 'Default\Preferences'), '{}')
    [IO.File]::WriteAllText($marker, 'must remain unchanged')
    $config = Join-Path $data 'config.ini'
    [IO.File]::WriteAllText($config, "[user]`r`nbrowser=edge`r`n")
    [IO.File]::WriteAllText((Join-Path $data 'default-template-revision.txt'), '4')
    $qaArchive = Join-Path $data 'Default.7z'
    $archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaArchive).Hash
    $launcher = Start-Process -FilePath $qaExe -WindowStyle Hidden -PassThru -ArgumentList (
        '--qa-instance=' + $runId + ' --qa-data-dir="' + $data + '"')
    $main = Wait-Launcher
    $mainGo = [DefaultDiscardQa]::GetDlgItem($main, 1)
    if ($mainGo -eq [IntPtr]::Zero) { throw 'Launcher Open control was not created.' }
    Start-Sleep -Milliseconds 500
    for ($cycle = 1; $cycle -le $Cycles; ++$cycle) {
        Wait-Until {
            [DefaultDiscardQa]::IsWindowEnabled($mainGo)
        } "Cycle ${cycle}: launcher was not ready before opening Default."
        if (Test-Path -LiteralPath (Join-Path $data 'Default')) {
            throw "Disposable Default directory existed before cycle $cycle."
        }
        $beforeMarker = (Get-FileHash -Algorithm SHA256 -LiteralPath $marker).Hash
        [void][DefaultDiscardQa]::SendMessageW($main, 0x111,
            [UIntPtr]::new([uint32]42001), [IntPtr]::Zero)
        [void][DefaultDiscardQa]::SendMessageW($main, 0x111,
            [UIntPtr]::new([uint32]41005), [IntPtr]::Zero)
        $profileProcess = Wait-Until {
            $candidate = @(Get-ProfileProcesses | Where-Object { $_.ProcessId -notin $ownedBrowserPids })
            if ($candidate.Count -gt 0) { $candidate[0] }
        } "Cycle ${cycle}: Edge Default editor process was not observed."
        $ownedBrowserPids.Add([int]$profileProcess.ProcessId)
        $profileWindow = Wait-Until {
            Get-ProfileWindow ([int]$profileProcess.ProcessId)
        } "Cycle ${cycle}: Edge Default editor window was not observed."
        if (-not (Test-Path -LiteralPath (Join-Path $data 'Default'))) {
            throw "Cycle ${cycle}: Default directory was not created."
        }
        if (-not $DiagnosticCloseBeforeLauncherReady) {
            Wait-Until {
                [DefaultDiscardQa]::IsWindowEnabled($mainGo)
            } "Cycle ${cycle}: launcher did not finish the Default launch before browser close."
        }
        Add-Timeline $cycle 'before-browser-close'
        $beforeCloseDialogs = @([DefaultDiscardQa]::DialogSnapshots(
            [uint32]$launcher.Id))
        if ($beforeCloseDialogs.Count -gt 0) {
            throw "Cycle ${cycle}: launcher dialog appeared before the visible Default browser was closed: $($beforeCloseDialogs -join ' | '); processes: $(Get-ProfileProcessSnapshot)"
        }
        [void][DefaultDiscardQa]::PostMessageW($profileWindow, 0x10,
            [UIntPtr]::Zero, [IntPtr]::Zero)
        Wait-Until {
            $p = Get-Process -Id ([int]$profileProcess.ProcessId) -ErrorAction SilentlyContinue
            $null -eq $p -or $p.HasExited
        } "Cycle ${cycle}: Edge Default editor did not close normally."
        Add-Timeline $cycle 'after-observed-process-exit'
        $promptWaitStarted = [DateTime]::UtcNow
        $savePrompt = Wait-Until {
            $save = [DefaultDiscardQa]::Find(
                [uint32]$launcher.Id, '#32770', 'Save Default Profile')
            if ($save -ne [IntPtr]::Zero) { return $save }
            $other = [DefaultDiscardQa]::Find(
                [uint32]$launcher.Id, '#32770', $null)
            if ($other -ne [IntPtr]::Zero) {
                $elapsed = [int]([DateTime]::UtcNow -
                    $promptWaitStarted).TotalMilliseconds
                $snapshot = @([DefaultDiscardQa]::DialogSnapshots(
                    [uint32]$launcher.Id)) -join ' | '
                throw "Cycle ${cycle}: alternate dialog appeared after ${elapsed}ms: $snapshot; processes: $(Get-ProfileProcessSnapshot)"
            }
        } "Cycle ${cycle}: Save Default Profile prompt was not shown."
        [void][DefaultDiscardQa]::PostMessageW($savePrompt, 0x111,
            [UIntPtr]::new([uint32]7), [DefaultDiscardQa]::GetDlgItem($savePrompt, 7))
        Wait-Until {
            -not [DefaultDiscardQa]::IsWindow($savePrompt)
        } "Cycle ${cycle}: Save Default Profile prompt did not close after No."
        Wait-Until {
            -not (Test-Path -LiteralPath (Join-Path $data 'Default'))
        } "Cycle ${cycle}: disposable Default directory remained after discard."
        $afterArchiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $qaArchive).Hash
        if ($afterArchiveHash -ne $archiveHash) { throw "Cycle ${cycle}: Default.7z changed after discard." }
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $marker).Hash -ne $beforeMarker) {
            throw "Cycle ${cycle}: existing client/browser marker changed."
        }
    }
    $afterLiveHash = if (Test-Path -LiteralPath $liveConfig -PathType Leaf) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $liveConfig).Hash
    } else { '<missing>' }
    if ($afterLiveHash -ne $liveHash) { throw 'Live ctSpaces configuration changed.' }
    if (-not [DefaultDiscardQa]::IsWindowEnabled([DefaultDiscardQa]::GetDlgItem($main, 1))) {
        throw 'Launcher Open control was not re-enabled after Default discard.'
    }
    $completed = $true
    [pscustomobject]@{ Cycles=$Cycles; DefaultArchiveUnchanged=$true;
        DisposableDefaultRemoved=$true; ExistingClientMarkerUnchanged=$true;
        LiveConfigUnchanged=$true; LauncherReenabled=$true } | ConvertTo-Json
} catch {
    $processSnapshot = @(Get-CimInstance Win32_Process -Filter "Name = 'msedge.exe'" -ErrorAction SilentlyContinue |
        Where-Object { [string]$_.CommandLine -match [regex]::Escape([IO.Path]::GetFullPath($data)) } |
        ForEach-Object { "pid=$($_.ProcessId) cmd=$($_.CommandLine)" }) -join [Environment]::NewLine
    $dialogSnapshot = ''
    if ($launcher) {
        $dialogs = @([DefaultDiscardQa]::DialogSnapshots([uint32]$launcher.Id))
        if ($dialogs.Count -gt 0) {
            $dialogSnapshot = "`nLauncher dialog snapshot:`n" +
                ($dialogs -join [Environment]::NewLine)
        }
    }
    $timelineSnapshot = if ($timeline.Count -gt 0) {
        "`nLifecycle timeline:`n" + ($timeline -join [Environment]::NewLine)
    } else { '' }
    Write-FailureLog ($_.Exception.ToString() + "`nEdge process snapshot:`n" +
        $processSnapshot + $dialogSnapshot + $timelineSnapshot)
    throw
} finally {
    if ($launcher -and -not $launcher.HasExited) {
        [void][DefaultDiscardQa]::PostMessageW(
            [DefaultDiscardQa]::Find([uint32]$launcher.Id, 'ctSpacesLauncherClass', $null),
            0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 500
    }
    foreach ($qaBrowserPid in @($ownedBrowserPids)) {
        $process = Get-Process -Id $qaBrowserPid -ErrorAction SilentlyContinue
        if ($process) {
            $owned = @(Get-CimInstance Win32_Process -Filter "ProcessId = $qaBrowserPid" -ErrorAction SilentlyContinue |
                Where-Object { [string]$_.CommandLine -match [regex]::Escape([IO.Path]::GetFullPath($data)) })
            if ($owned) { Stop-Process -Id $qaBrowserPid -Force -ErrorAction SilentlyContinue }
        }
    }
    if ($launcher -and -not $launcher.HasExited) { Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue }
    if (-not $completed -and (Test-Path -LiteralPath $qaRoot)) {
        $failureRoot = Join-Path $projectRoot ('build\default-discard-failures\' + $runId)
        New-Item -ItemType Directory -Path (Split-Path -Parent $failureRoot) -Force | Out-Null
        Copy-Item -LiteralPath $qaRoot -Destination $failureRoot -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Preserved Default-discard failure fixture: $failureRoot"
    }
    $target = [IO.Path]::GetFullPath($qaRoot)
    $prefix = [IO.Path]::GetFullPath($qaParent).TrimEnd('\') + '\'
    if ($target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($target).StartsWith('run-', [StringComparison]::Ordinal) -and
        (Test-Path -LiteralPath $target)) {
        Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
    }
}
