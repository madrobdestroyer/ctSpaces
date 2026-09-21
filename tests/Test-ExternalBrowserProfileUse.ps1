param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$qaParent = [IO.Path]::GetFullPath((
    Join-Path $projectRoot 'build\qa-external-browser-profile-use'
))
$runId = [Guid]::NewGuid().ToString('N')
$qaRoot = Join-Path $qaParent ('run-' + $runId)
$qaExeDir = Join-Path $qaRoot 'app'
$qaExe = Join-Path $qaExeDir 'ctSpaces-external-browser-qa.exe'
$qaDataDir = Join-Path $qaExeDir 'data'
$qaConfig = Join-Path $qaDataDir 'config.ini'
$clientName = 'External Browser QA'
$clientRoot = Join-Path $qaDataDir ('Sites\' + $clientName)
$edgeSlot = Join-Path $clientRoot 'Browsers\edge'
$edgeProfile = Join-Path $edgeSlot 'Profile'
$sentinelPath = Join-Path $edgeProfile 'profile-sentinel.txt'
$fakeBrowserPath = Join-Path $qaExeDir 'msedge.exe'
$launcher = $null
$launcherWindow = [IntPtr]::Zero
$fakeBrowser = $null
$result = $null
$liveConfigPath = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'

function Get-FileFingerprint {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return 'file:' + (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
    if (Test-Path -LiteralPath $Path) {
        return 'non-file'
    }
    return 'missing'
}

$liveConfigBefore = Get-FileFingerprint $liveConfigPath

if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    throw "Release executable not found: $resolvedExe"
}

if (-not ('CtExternalBrowserProfileUseQaNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class CtExternalBrowserProfileUseQaNative
{
    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback,
                                           IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hWnd,
                                                        out uint processId);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int controlId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessageW(IntPtr hWnd, uint message,
                                           UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SendMessageTimeoutW(
        IntPtr hWnd, uint message, UIntPtr wParam, IntPtr lParam,
        uint flags, uint timeoutMilliseconds, out UIntPtr result
    );

    public static IntPtr FindProcessWindowWithChild(uint processId,
                                                     int childId)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint ownerProcessId;
            GetWindowThreadProcessId(hWnd, out ownerProcessId);
            if (ownerProcessId == processId &&
                GetDlgItem(hWnd, childId) != IntPtr.Zero) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
}

function Wait-Until {
    param(
        [scriptblock]$Condition,
        [string]$Failure,
        [int]$Seconds = $TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        $value = & $Condition
        if ($value) {
            return $value
        }
        Start-Sleep -Milliseconds 75
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Failure
}

function Send-WindowMessage {
    param(
        [IntPtr]$Window,
        [uint32]$Message,
        [UIntPtr]$WParam = [UIntPtr]::Zero,
        [IntPtr]$LParam = [IntPtr]::Zero
    )

    $messageResult = [UIntPtr]::Zero
    if (-not [CtExternalBrowserProfileUseQaNative]::SendMessageTimeoutW(
            $Window, $Message, $WParam, $LParam, 0x0003, 3000,
            [ref]$messageResult
        )) {
        throw "Window message 0x$($Message.ToString('X')) timed out."
    }
    return $messageResult
}

function Set-WindowTextValue {
    param([IntPtr]$Window, [string]$Text)

    $textPointer = [Runtime.InteropServices.Marshal]::StringToHGlobalUni($Text)
    try {
        [void](Send-WindowMessage -Window $Window -Message 0x000C `
            -LParam $textPointer)
    }
    finally {
        [Runtime.InteropServices.Marshal]::FreeHGlobal($textPointer)
    }
}

function Get-ConfigText {
    if (Test-Path -LiteralPath $qaConfig -PathType Leaf) {
        return [IO.File]::ReadAllText($qaConfig)
    }
    return ''
}

try {
    New-Item -ItemType Directory -Path $qaExeDir -Force | Out-Null
    New-Item -ItemType Directory -Path $qaDataDir -Force | Out-Null
    New-Item -ItemType Directory -Path $edgeProfile -Force | Out-Null
    Copy-Item -LiteralPath $resolvedExe -Destination $qaExe -Force
    New-Item -ItemType File -Path (Join-Path $qaExeDir 'ctSpaces.portable') `
        -Force | Out-Null

    $utf8NoBom = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText(
        (Join-Path $clientRoot 'ctSpaces-client-v2'),
        "ctSpaces-client-schema=2`r`n",
        $utf8NoBom
    )
    [IO.File]::WriteAllText(
        (Join-Path $edgeSlot 'ctSpaces-browser-v2'),
        "ctSpaces-browser-schema=2`r`nbrowser=edge`r`n",
        $utf8NoBom
    )
    [IO.File]::WriteAllText(
        (Join-Path $edgeProfile 'ctSpaces'),
        "ctSpaces-profile=2`r`n",
        $utf8NoBom
    )
    [IO.File]::WriteAllText($sentinelPath, 'preserve-me', $utf8NoBom)
    [IO.File]::WriteAllText(
        $qaConfig,
        "[user]`r`ntheme_name=Dark - Gothic`r`n`r`n" +
        "[archived]`r`ncount=0`r`n",
        $utf8NoBom
    )

    $systemPowerShell = Join-Path $env:WINDIR `
        'System32\WindowsPowerShell\v1.0\powershell.exe'
    if (-not (Test-Path -LiteralPath $systemPowerShell -PathType Leaf)) {
        throw "Windows PowerShell sleeper source was not found: $systemPowerShell"
    }
    Copy-Item -LiteralPath $systemPowerShell -Destination $fakeBrowserPath

    $arguments = '--qa-instance=' + $runId.Substring(0, 10) +
        ' --qa-data-dir="' + $qaDataDir + '"'
    $launcher = Start-Process -FilePath $qaExe -ArgumentList $arguments `
        -WindowStyle Hidden -PassThru
    [void]$launcher.WaitForInputIdle(10000)
    $launcherWindow = Wait-Until {
        $launcher.Refresh()
        if ($launcher.HasExited) {
            throw "The QA launcher exited with code $($launcher.ExitCode)."
        }
        $window = [CtExternalBrowserProfileUseQaNative]::FindProcessWindowWithChild(
            [uint32]$launcher.Id, 206
        )
        if ($window -ne [IntPtr]::Zero) { $window }
    } 'Timed out waiting for the isolated QA launcher.'

    $clientEdit = [CtExternalBrowserProfileUseQaNative]::GetDlgItem(
        $launcherWindow, 206
    )
    if ($clientEdit -eq [IntPtr]::Zero) {
        throw 'The isolated QA launcher did not create its client editor.'
    }
    Set-WindowTextValue -Window $clientEdit -Text $clientName

    # A copied Windows component is a harmless sleeper. Its image name is
    # deliberately msedge.exe, and its exact absolute --user-data-dir value is
    # visible to the same OS process inspection used by ctSpaces.
    $fakeStart = [Diagnostics.ProcessStartInfo]::new()
    $fakeStart.FileName = $fakeBrowserPath
    $fakeStart.UseShellExecute = $false
    $fakeStart.CreateNoWindow = $true
    $fakeStart.Arguments =
        '-NoLogo -NoProfile -NonInteractive ' +
        '-Command "& { param([string]$probe) Start-Sleep -Seconds 120 }" ' +
        '"--user-data-dir=' + $edgeProfile + '"'
    $fakeBrowser = [Diagnostics.Process]::Start($fakeStart)
    if (-not $fakeBrowser) {
        throw 'The harmless fake Edge process could not be started.'
    }
    [void](Wait-Until {
        $fakeBrowser.Refresh()
        if ($fakeBrowser.HasExited) {
            throw "The fake Edge process exited early with code $($fakeBrowser.ExitCode)."
        }
        $process = Get-Process -Id $fakeBrowser.Id -ErrorAction SilentlyContinue
        if ($process -and $process.ProcessName -eq 'msedge') { $process }
    } 'The fake Edge process was not visible to Windows process enumeration.')

    # WM_APP_QA_ARCHIVE_CLIENT calls the same ArchiveClientProfile path as the
    # UI, synchronously and without a confirmation dialog.
    $archiveMessage = [uint32](0x8000 + 14)
    $blockedResult = Send-WindowMessage -Window $launcherWindow `
        -Message $archiveMessage
    if ($blockedResult -ne [UIntPtr]::Zero) {
        throw 'Archiving was not blocked while an external Edge process used the profile.'
    }
    if (-not (Test-Path -LiteralPath $clientRoot -PathType Container) -or
        -not (Test-Path -LiteralPath $sentinelPath -PathType Leaf)) {
        throw 'The blocked archive attempt changed the isolated client data.'
    }
    if ((Get-ConfigText) -match '(?im)^client\d+=External Browser QA\r?$') {
        throw 'The blocked archive attempt still recorded the client as archived.'
    }

    # WM_APP_QA_CAN_EXPORT runs the same whole-Sites profile-use gate used by
    # Export, without opening a Save dialog in the isolated QA process.
    $canExportMessage = [uint32](0x8000 + 18)
    $blockedExportResult = Send-WindowMessage -Window $launcherWindow `
        -Message $canExportMessage
    if ($blockedExportResult -ne [UIntPtr]::Zero) {
        throw 'Backup was not blocked while an external Edge process used a Sites profile.'
    }

    $fakeBrowser.Kill()
    if (-not $fakeBrowser.WaitForExit(5000)) {
        throw 'The fake Edge process did not exit within five seconds.'
    }
    [void](Wait-Until {
        -not (Get-Process -Id $fakeBrowser.Id -ErrorAction SilentlyContinue)
    } 'Windows still reported the fake Edge process after it exited.' 5)

    $allowedExportResult = Send-WindowMessage -Window $launcherWindow `
        -Message $canExportMessage
    if ($allowedExportResult -eq [UIntPtr]::Zero) {
        throw 'Backup remained blocked after the external Edge process exited.'
    }

    $allowedResult = Send-WindowMessage -Window $launcherWindow `
        -Message $archiveMessage
    if ($allowedResult -eq [UIntPtr]::Zero) {
        throw 'Archiving remained blocked after the external Edge process exited.'
    }
    [void](Wait-Until {
        (Get-ConfigText) -match
            '(?ms)^\[archived\].*?^count=1\r?$.*?^client0=External Browser QA\r?$'
    } 'The successful archive was not persisted in the isolated configuration.')
    if (-not (Test-Path -LiteralPath $clientRoot -PathType Container) -or
        -not (Test-Path -LiteralPath $sentinelPath -PathType Leaf) -or
        [IO.File]::ReadAllText($sentinelPath) -ne 'preserve-me') {
        throw 'Archiving did not preserve the isolated client profile exactly.'
    }

    $result = [pscustomobject]@{
        ExternalEdgeProcessDetected = $true
        BackupBlockedWhileProfileActive = $true
        BackupAllowedAfterProcessExit = $true
        ArchiveBlockedWhileProfileActive = $true
        ArchiveSucceededAfterProcessExit = $true
        ClientProfilePreserved = $true
        QaDataDirectory = $qaDataDir
    }
}
finally {
    if ($fakeBrowser) {
        $fakeBrowser.Refresh()
        if (-not $fakeBrowser.HasExited) {
            $fakeBrowser.Kill()
            [void]$fakeBrowser.WaitForExit(5000)
        }
        $fakeBrowser.Dispose()
    }

    if ($launcherWindow -ne [IntPtr]::Zero -and
        [CtExternalBrowserProfileUseQaNative]::IsWindow($launcherWindow)) {
        [void][CtExternalBrowserProfileUseQaNative]::PostMessageW(
            $launcherWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero
        )
    }
    if ($launcher) {
        try {
            [void]$launcher.WaitForExit(5000)
            if (-not $launcher.HasExited) {
                $launcher.Kill()
                [void]$launcher.WaitForExit(5000)
            }
        } catch {
        } finally {
            $launcher.Dispose()
        }
    }

    $resolvedQaRoot = [IO.Path]::GetFullPath($qaRoot)
    $allowedPrefix = $qaParent.TrimEnd('\') + '\'
    if ($resolvedQaRoot.StartsWith(
            $allowedPrefix, [StringComparison]::OrdinalIgnoreCase
        ) -and [IO.Path]::GetFileName($resolvedQaRoot).StartsWith('run-') -and
        (Test-Path -LiteralPath $resolvedQaRoot)) {
        $cleanupDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            try {
                Remove-Item -LiteralPath $resolvedQaRoot -Recurse -Force `
                    -ErrorAction Stop
                break
            }
            catch {
                if ([DateTime]::UtcNow -ge $cleanupDeadline) { throw }
                Start-Sleep -Milliseconds 250
            }
        } while (Test-Path -LiteralPath $resolvedQaRoot)
    }

    $liveConfigAfter = Get-FileFingerprint $liveConfigPath
    if ($liveConfigAfter -ne $liveConfigBefore) {
        throw 'The external-browser QA run changed the live ctSpaces configuration.'
    }
}

$result | ConvertTo-Json
