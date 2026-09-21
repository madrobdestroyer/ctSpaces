param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$ScreenshotOnPath = '',
    [string]$ScreenshotOffPath = '',
    [string]$PinnedMenuScreenshotPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 10)
$qaDir = Join-Path $projectRoot ('.qa-restore-tabs-' + $runId)
$qaExe = Join-Path $qaDir 'ctSpaces-restore-tabs-qa.exe'
$qaDataDir = Join-Path $qaDir 'data'
$qaConfig = Join-Path $qaDataDir 'config.ini'
$clientName = 'Contoso'
$browserId = 'edge'
$restorePreferenceKey = "$clientName|$browserId"
$launcherProcess = $null
$launcherWindow = [IntPtr]::Zero
$liveConfigPath = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'
$liveConfigHashBefore = if (Test-Path -LiteralPath $liveConfigPath -PathType Leaf) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $liveConfigPath).Hash
} else {
    '<missing>'
}

if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    throw "Release executable not found: $resolvedExe"
}

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class CtRestoreTabsQaNative
{
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback,
                                           IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hWnd,
                                                        out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassNameW(IntPtr hWnd, char[] className,
                                            int maxCount);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int controlId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindowEnabled(IntPtr hWnd);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtrW(IntPtr hWnd, int index);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessageW(IntPtr hWnd, uint message,
                                           UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SendMessageTimeoutW(
        IntPtr hWnd,
        uint message,
        UIntPtr wParam,
        IntPtr lParam,
        uint flags,
        uint timeoutMilliseconds,
        out UIntPtr result
    );

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int command);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(
        IntPtr dpiContext
    );

    public static IntPtr FindProcessWindowWithChild(uint processId, int childId)
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

    public static IntPtr FindVisibleWindowByClass(string className)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            if (!IsWindowVisible(hWnd))
                return true;

            char[] actualClass = new char[128];
            int length = GetClassNameW(hWnd, actualClass, actualClass.Length);
            if (length > 0 &&
                new string(actualClass, 0, length).Equals(
                    className, StringComparison.Ordinal)) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

function Invoke-WindowMessage {
    param(
        [IntPtr]$Window,
        [uint32]$Message,
        [UIntPtr]$WParam = [UIntPtr]::Zero,
        [IntPtr]$LParam = [IntPtr]::Zero,
        [uint32]$TimeoutMilliseconds = 2000
    )

    $result = [UIntPtr]::Zero
    $ok = [CtRestoreTabsQaNative]::SendMessageTimeoutW(
        $Window, $Message, $WParam, $LParam, 0x0003,
        $TimeoutMilliseconds, [ref]$result
    )
    if (-not $ok) {
        throw "Window message 0x$($Message.ToString('X')) timed out."
    }
    $result.ToUInt64()
}

function Set-WindowTextValue {
    param([IntPtr]$Window, [string]$Text)

    $textPointer = [Runtime.InteropServices.Marshal]::StringToHGlobalUni($Text)
    try {
        [void](Invoke-WindowMessage -Window $Window -Message 0x000C `
            -LParam $textPointer)
    } finally {
        [Runtime.InteropServices.Marshal]::FreeHGlobal($textPointer)
    }
}

function Wait-ForLauncherWindow {
    param([Diagnostics.Process]$Process, [int]$TimeoutSeconds = 15)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 100
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "The QA launcher exited with code $($Process.ExitCode)."
        }
        $window = [CtRestoreTabsQaNative]::FindProcessWindowWithChild(
            [uint32]$Process.Id, 207
        )
        if ($window -ne [IntPtr]::Zero) {
            return $window
        }
    } while ([DateTime]::UtcNow -lt $deadline)

    throw 'Timed out waiting for the Restore tabs control.'
}

function Wait-ForPopupMenu {
    param([int]$TimeoutSeconds = 4)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $menu = [CtRestoreTabsQaNative]::FindVisibleWindowByClass('#32768')
        if ($menu -ne [IntPtr]::Zero) {
            return $menu
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)

    throw 'The pinned-client options menu did not appear.'
}

function Wait-ForEnabledState {
    param(
        [IntPtr]$Window,
        [bool]$Enabled,
        [int]$TimeoutSeconds = 4
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ([CtRestoreTabsQaNative]::IsWindowEnabled($Window) -eq $Enabled) {
            return
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "Restore tabs enabled state did not become $Enabled."
}

function Wait-ForConfigPattern {
    param([string]$Pattern, [int]$TimeoutSeconds = 4)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (Test-Path -LiteralPath $qaConfig -PathType Leaf) {
            $configText = Get-Content -LiteralPath $qaConfig -Raw
            if ($configText -match $Pattern) {
                return $configText
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "config.ini did not match: $Pattern"
}

function Save-WindowScreenshot {
    param(
        [IntPtr]$Window,
        [string]$Path,
        [bool]$Activate = $true
    )

    if (-not $Path) {
        return ''
    }
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $directory = Split-Path -Parent $resolvedPath
    if ($directory) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    if ($Activate) {
        [void][CtRestoreTabsQaNative]::ShowWindow($Window, 9)
        [void][CtRestoreTabsQaNative]::SetForegroundWindow($Window)
        Start-Sleep -Milliseconds 200
    }

    $previousDpiContext = [CtRestoreTabsQaNative]::SetThreadDpiAwarenessContext(
        [IntPtr]::new(-4)
    )
    try {
        $rect = [CtRestoreTabsQaNative+RECT]::new()
        if (-not [CtRestoreTabsQaNative]::GetWindowRect($Window, [ref]$rect)) {
            throw 'Could not read the launcher rectangle for a screenshot.'
        }
        $width = $rect.Right - $rect.Left
        $height = $rect.Bottom - $rect.Top
        $bitmap = [Drawing.Bitmap]::new($width, $height)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen(
                $rect.Left, $rect.Top, 0, 0,
                [Drawing.Size]::new($width, $height),
                [Drawing.CopyPixelOperation]::SourceCopy
            )
            $bitmap.Save($resolvedPath, [Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    } finally {
        if ($previousDpiContext -ne [IntPtr]::Zero) {
            [void][CtRestoreTabsQaNative]::SetThreadDpiAwarenessContext(
                $previousDpiContext
            )
        }
    }
    $resolvedPath
}

function Stop-QaLauncher {
    if ($launcherWindow -ne [IntPtr]::Zero -and
        [CtRestoreTabsQaNative]::IsWindow($launcherWindow)) {
        [void][CtRestoreTabsQaNative]::PostMessageW(
            $launcherWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero
        )
    }
    if ($launcherProcess) {
        try {
            [void]$launcherProcess.WaitForExit(4000)
            if (-not $launcherProcess.HasExited) {
                $launcherProcess.Kill()
                [void]$launcherProcess.WaitForExit(4000)
            }
        } finally {
            $launcherProcess.Dispose()
        }
    }
    $script:launcherProcess = $null
    $script:launcherWindow = [IntPtr]::Zero
}

function Start-QaLauncher {
    $arguments = '--qa-instance=' + $runId +
        ' --qa-data-dir="' + $qaDataDir + '"'
    $script:launcherProcess = Start-Process -FilePath $qaExe `
        -ArgumentList $arguments -PassThru
    [void]$script:launcherProcess.WaitForInputIdle(10000)
    $script:launcherWindow = Wait-ForLauncherWindow $script:launcherProcess
}

try {
    New-Item -ItemType Directory -Path $qaDir -Force | Out-Null
    Copy-Item -LiteralPath $resolvedExe -Destination $qaExe -Force
    New-Item -ItemType File -Path (Join-Path $qaDir 'ctSpaces.portable') `
        -Force | Out-Null
    New-Item -ItemType Directory `
        -Path (Join-Path $qaDataDir "Sites\$clientName\Default") `
        -Force | Out-Null
    [IO.File]::WriteAllText(
        $qaConfig,
        "[user]`r`nbrowser=$browserId`r`n`r`n" +
            "[pinned]`r`ncount=1`r`nclient0=$clientName`r`n",
        [Text.UTF8Encoding]::new($false)
    )

    Start-QaLauncher
    $toggle = [CtRestoreTabsQaNative]::GetDlgItem($launcherWindow, 207)
    $clientEdit = [CtRestoreTabsQaNative]::GetDlgItem($launcherWindow, 206)
    $clientCombo = [CtRestoreTabsQaNative]::GetDlgItem($launcherWindow, 102)
    $tempButton = [CtRestoreTabsQaNative]::GetDlgItem($launcherWindow, 200)
    if ($toggle -eq [IntPtr]::Zero -or $clientEdit -eq [IntPtr]::Zero -or
        $clientCombo -eq [IntPtr]::Zero -or $tempButton -eq [IntPtr]::Zero) {
        throw 'One or more launcher controls were not found.'
    }

    $buttonStyle = [CtRestoreTabsQaNative]::GetWindowLongPtrW(
        $toggle, -16
    ).ToInt64()
    if (($buttonStyle -band 0xF) -ne 0xB) {
        throw 'Restore tabs is not an owner-drawn themed button.'
    }

    Wait-ForEnabledState $toggle $false
    Set-WindowTextValue $clientEdit $clientName
    Wait-ForEnabledState $toggle $true
    [void](Invoke-WindowMessage -Window $clientCombo -Message 0x014F)
    Start-Sleep -Milliseconds 150

    $toggleRect = [CtRestoreTabsQaNative+RECT]::new()
    $tempRect = [CtRestoreTabsQaNative+RECT]::new()
    [void][CtRestoreTabsQaNative]::GetWindowRect($toggle, [ref]$toggleRect)
    [void][CtRestoreTabsQaNative]::GetWindowRect($tempButton, [ref]$tempRect)
    if ($toggleRect.Right -ge $tempRect.Left -or
        $toggleRect.Bottom -le $toggleRect.Top) {
        throw 'Restore tabs overlaps the footer utility buttons.'
    }

    $onScreenshot = Save-WindowScreenshot $launcherWindow $ScreenshotOnPath

    $launcherClient = [CtRestoreTabsQaNative+RECT]::new()
    [void][CtRestoreTabsQaNative]::GetClientRect(
        $launcherWindow, [ref]$launcherClient
    )
    $pinnedX = [Math]::Max(1, [Math]::Round($launcherClient.Right * 0.10))
    $pinnedY = [Math]::Max(1, [Math]::Round($launcherClient.Bottom * 0.34))
    $mousePosition = [IntPtr]::new(
        (($pinnedY -band 0xFFFF) -shl 16) -bor ($pinnedX -band 0xFFFF)
    )
    if (-not [CtRestoreTabsQaNative]::PostMessageW(
            $launcherWindow, 0x0205, [UIntPtr]::Zero, $mousePosition
        )) {
        throw 'Could not open the pinned-client options menu.'
    }
    $pinnedMenu = Wait-ForPopupMenu
    $pinnedMenuScreenshot = Save-WindowScreenshot `
        $pinnedMenu $PinnedMenuScreenshotPath $false
    [void][CtRestoreTabsQaNative]::PostMessageW(
        $pinnedMenu, 0x0100, [UIntPtr]::new(0x1B), [IntPtr]::Zero
    )
    [void][CtRestoreTabsQaNative]::PostMessageW(
        $pinnedMenu, 0x0101, [UIntPtr]::new(0x1B), [IntPtr]::Zero
    )
    Start-Sleep -Milliseconds 150

    [void](Invoke-WindowMessage -Window $toggle -Message 0x00F5)
    $offConfig = Wait-ForConfigPattern `
        '(?m)^disabled_count=1\r?$'
    $expectedPreferencePattern = '(?m)^disabled_client0=' +
        [regex]::Escape($restorePreferenceKey) + '\r?$'
    if ($offConfig -notmatch $expectedPreferencePattern) {
        throw 'The selected client/browser pair was not saved as the restore exception.'
    }
    $offScreenshot = Save-WindowScreenshot $launcherWindow $ScreenshotOffPath

    Set-WindowTextValue $clientEdit 'Future Client'
    Wait-ForEnabledState $toggle $false

    Stop-QaLauncher
    Start-QaLauncher
    $toggle = [CtRestoreTabsQaNative]::GetDlgItem($launcherWindow, 207)
    $clientEdit = [CtRestoreTabsQaNative]::GetDlgItem($launcherWindow, 206)
    Set-WindowTextValue $clientEdit $clientName
    Wait-ForEnabledState $toggle $true
    [void](Invoke-WindowMessage -Window $toggle -Message 0x00F5)
    [void](Wait-ForConfigPattern '(?m)^disabled_count=0\r?$')

    $liveConfigHashAfter = if (
        Test-Path -LiteralPath $liveConfigPath -PathType Leaf
    ) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $liveConfigPath).Hash
    } else {
        '<missing>'
    }
    if ($liveConfigHashAfter -ne $liveConfigHashBefore) {
        throw 'Restore-tabs QA modified the live ctSpaces preference file.'
    }

    [pscustomobject]@{
        Client = $clientName
        Browser = $browserId
        RestorePreferenceKey = $restorePreferenceKey
        ExistingClientDefaultsOn = $true
        OffPreferencePersists = $true
        NewClientIsDisabled = $true
        OwnerDrawn = $true
        FooterDoesNotOverlap = $true
        PinnedOptionsMenu = $true
        LiveConfigUnchanged = $true
        OnScreenshot = $onScreenshot
        OffScreenshot = $offScreenshot
        PinnedMenuScreenshot = $pinnedMenuScreenshot
    } | ConvertTo-Json
} finally {
    Stop-QaLauncher

    $resolvedQaDir = [IO.Path]::GetFullPath($qaDir)
    $resolvedProject = $projectRoot.TrimEnd('\') + '\'
    if ($resolvedQaDir.StartsWith(
            $resolvedProject, [StringComparison]::OrdinalIgnoreCase
        ) -and [IO.Path]::GetFileName($resolvedQaDir).StartsWith(
            '.qa-restore-tabs-', [StringComparison]::Ordinal
        ) -and (Test-Path -LiteralPath $resolvedQaDir)) {
        Remove-Item -LiteralPath $resolvedQaDir -Recurse -Force
    }
}
