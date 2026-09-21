param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [int]$SelectionChanges = 240
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 10)
$qaDir = Join-Path $projectRoot ('.qa-theme-' + $runId)
$qaExe = Join-Path $qaDir 'ctSpaces-theme-qa.exe'
$qaDataDir = Join-Path $qaDir 'data'
$liveConfigPath = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'
$liveConfigHashBefore = if (Test-Path -LiteralPath $liveConfigPath -PathType Leaf) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $liveConfigPath).Hash
} else {
    '<missing>'
}
$launcherProcess = $null
$launcherWindow = [IntPtr]::Zero
$themeDialog = [IntPtr]::Zero

if ($SelectionChanges -lt 20) {
    throw 'SelectionChanges must be at least 20.'
}
if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    throw "Release executable not found: $resolvedExe"
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class CtThemeQaNative
{
    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int controlId);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessageW(
        IntPtr hWnd,
        uint message,
        UIntPtr wParam,
        IntPtr lParam
    );

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
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern uint GetGuiResources(IntPtr process, uint flags);

    public static IntPtr FindProcessWindowWithChild(uint processId, int childId)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint ownerProcessId;
            GetWindowThreadProcessId(hWnd, out ownerProcessId);
            if (ownerProcessId == processId && GetDlgItem(hWnd, childId) != IntPtr.Zero) {
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
    $ok = [CtThemeQaNative]::SendMessageTimeoutW(
        $Window,
        $Message,
        $WParam,
        $LParam,
        0x0003,
        $TimeoutMilliseconds,
        [ref]$result
    )
    if (-not $ok) {
        throw "Window 0x$($Window.ToInt64().ToString('X')) stopped responding to message 0x$($Message.ToString('X'))."
    }
    return $result.ToUInt64()
}

function Wait-ForProcessWindowWithChild {
    param(
        [Diagnostics.Process]$Process,
        [int]$ChildId,
        [int]$TimeoutSeconds = 15
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 100
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "The QA launcher exited with code $($Process.ExitCode)."
        }
        $window = [CtThemeQaNative]::FindProcessWindowWithChild(
            [uint32]$Process.Id,
            $ChildId
        )
        if ($window -ne [IntPtr]::Zero) {
            return $window
        }
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "Timed out waiting for child control $ChildId."
}

try {
    New-Item -ItemType Directory -Path $qaDir -Force | Out-Null
    Copy-Item -LiteralPath $resolvedExe -Destination $qaExe -Force
    New-Item -ItemType File -Path (Join-Path $qaDir 'ctSpaces.portable') `
        -Force | Out-Null

    $qaArguments = '--qa-instance=' + $runId +
        ' --qa-data-dir="' + $qaDataDir + '"'
    $launcherProcess = Start-Process -FilePath $qaExe `
        -ArgumentList $qaArguments -PassThru
    [void]$launcherProcess.WaitForInputIdle(10000)

    $launcherWindow = Wait-ForProcessWindowWithChild `
        -Process $launcherProcess -ChildId 102

    # Open the modal Themes dialog through the same command the options menu uses.
    if (-not [CtThemeQaNative]::PostMessageW(
            $launcherWindow,
            0x0111,
            [UIntPtr]::new(41006),
            [IntPtr]::Zero
        )) {
        throw 'Could not post the Themes command.'
    }

    $themeDialog = Wait-ForProcessWindowWithChild `
        -Process $launcherProcess -ChildId 5201
    $themeCombo = [CtThemeQaNative]::GetDlgItem($themeDialog, 5201)
    if ($themeCombo -eq [IntPtr]::Zero) {
        throw 'The Themes combo box was not found.'
    }

    $comboGetCount = 0x0146
    $comboGetSelection = 0x0147
    $comboSetSelection = 0x014E
    $themeCount = [int](Invoke-WindowMessage `
        -Window $themeCombo -Message $comboGetCount)
    $initialSelection = [int](Invoke-WindowMessage `
        -Window $themeCombo -Message $comboGetSelection)
    $selectableIndexes = @(
        0..($themeCount - 1) | Where-Object { $_ -ne 3 }
    )
    if ($themeCount -lt 5 -or $selectableIndexes.Count -lt 4) {
        throw "The theme catalog is unexpectedly small ($themeCount entries)."
    }

    $gdiBefore = [CtThemeQaNative]::GetGuiResources($launcherProcess.Handle, 0)
    $selectionNotification = [UIntPtr]::new(([uint64]1 -shl 16) -bor 5201)

    for ($i = 0; $i -lt $SelectionChanges; $i++) {
        $selection = $selectableIndexes[$i % $selectableIndexes.Count]
        if (-not [CtThemeQaNative]::PostMessageW(
                $themeCombo,
                $comboSetSelection,
                [UIntPtr]::new([uint64]$selection),
                [IntPtr]::Zero
            )) {
            throw "Could not queue theme selection $selection."
        }
        if (-not [CtThemeQaNative]::PostMessageW(
                $themeDialog,
                0x0111,
                $selectionNotification,
                $themeCombo
            )) {
            throw 'Could not queue the theme selection notification.'
        }
    }

    Start-Sleep -Milliseconds 450
    [void](Invoke-WindowMessage -Window $themeDialog -Message 0x0000 `
        -TimeoutMilliseconds 3000)
    [void](Invoke-WindowMessage -Window $launcherWindow -Message 0x0000 `
        -TimeoutMilliseconds 3000)

    # Return to the original selection, then exercise the full Apply path.
    [void](Invoke-WindowMessage -Window $themeCombo -Message $comboSetSelection `
        -WParam ([UIntPtr]::new([uint64]$initialSelection)))
    [void](Invoke-WindowMessage -Window $themeDialog -Message 0x0111 `
        -WParam $selectionNotification -LParam $themeCombo)
    Start-Sleep -Milliseconds 250
    [void](Invoke-WindowMessage -Window $themeDialog -Message 0x0111 `
        -WParam ([UIntPtr]::new(5202)) -TimeoutMilliseconds 5000)

    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([CtThemeQaNative]::IsWindow($themeDialog) -and
        [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 50
    }
    if ([CtThemeQaNative]::IsWindow($themeDialog)) {
        throw 'The Themes dialog did not close after Apply.'
    }

    [void](Invoke-WindowMessage -Window $launcherWindow -Message 0x0000 `
        -TimeoutMilliseconds 3000)
    $gdiAfter = [CtThemeQaNative]::GetGuiResources($launcherProcess.Handle, 0)
    $gdiGrowth = [int]$gdiAfter - [int]$gdiBefore
    if ($gdiGrowth -gt 32) {
        throw "Theme switching leaked too many GDI objects ($gdiGrowth)."
    }

    $liveConfigHashAfter = if (Test-Path -LiteralPath $liveConfigPath -PathType Leaf) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $liveConfigPath).Hash
    } else {
        '<missing>'
    }
    if ($liveConfigHashAfter -ne $liveConfigHashBefore) {
        throw 'Theme QA modified the live ctSpaces preference file.'
    }

    Write-Host "Theme responsiveness passed: $SelectionChanges rapid changes, GDI delta $gdiGrowth."
}
finally {
    if ($themeDialog -ne [IntPtr]::Zero -and
        [CtThemeQaNative]::IsWindow($themeDialog)) {
        [void][CtThemeQaNative]::PostMessageW(
            $themeDialog, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero
        )
    }
    if ($launcherWindow -ne [IntPtr]::Zero -and
        [CtThemeQaNative]::IsWindow($launcherWindow)) {
        [void][CtThemeQaNative]::PostMessageW(
            $launcherWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero
        )
    }
    if ($launcherProcess) {
        try {
            [void]$launcherProcess.WaitForExit(5000)
            if (-not $launcherProcess.HasExited) {
                $launcherProcess.Kill()
                [void]$launcherProcess.WaitForExit(5000)
            }
        } catch {
        } finally {
            $launcherProcess.Dispose()
        }
    }

    $resolvedQaDir = [IO.Path]::GetFullPath($qaDir)
    $resolvedProject = $projectRoot.TrimEnd('\') + '\'
    if ($resolvedQaDir.StartsWith(
            $resolvedProject, [StringComparison]::OrdinalIgnoreCase
        ) -and [IO.Path]::GetFileName($resolvedQaDir).StartsWith(
            '.qa-theme-', [StringComparison]::Ordinal
        ) -and (Test-Path -LiteralPath $resolvedQaDir)) {
        Remove-Item -LiteralPath $resolvedQaDir -Recurse -Force
    }
}
