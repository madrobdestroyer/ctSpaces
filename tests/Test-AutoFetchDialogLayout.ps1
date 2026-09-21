param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$ScreenshotPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 10)
$qaDir = Join-Path $projectRoot ('.qa-auto-fetch-' + $runId)
$testName = '__ctSpacesDialogQA_' + $runId
$dataRoot = Join-Path $qaDir 'data'
$sitesRoot = Join-Path $dataRoot 'Sites'
$profilePath = Join-Path $sitesRoot $testName
$launcherProcess = $null
$launcherWindow = [IntPtr]::Zero
$dialogWindow = [IntPtr]::Zero
$oldDpiContext = [IntPtr]::Zero

if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    throw "Release executable is missing: $resolvedExe"
}
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class CtDialogQaNative {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hWnd, int id);

    [DllImport("user32.dll", CharSet = CharSet.Unicode,
               EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessageStringW(IntPtr hWnd, uint msg,
                                                    IntPtr wParam,
                                                    string lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageW(IntPtr hWnd, uint msg,
                                             IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr hWnd, uint msg,
                                            IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback,
                                           IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsWindowEnabled(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hWnd, StringBuilder text,
                                           int maxCount);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd,
                                                       out uint processId);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern int MapWindowPoints(IntPtr from, IntPtr to,
                                             ref RECT rect, uint count);

    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hWnd);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtrW(IntPtr hWnd, int index);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

    public static IntPtr FindProcessWindowWithChild(uint processId,
                                                     int childId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            uint ownerId;
            GetWindowThreadProcessId(hWnd, out ownerId);
            if (ownerId == processId && GetDlgItem(hWnd, childId) != IntPtr.Zero) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindProcessWindowByClass(uint processId,
                                                   string expectedClass) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            uint ownerId;
            GetWindowThreadProcessId(hWnd, out ownerId);
            if (ownerId != processId || !IsWindowVisible(hWnd)) return true;

            var className = new StringBuilder(256);
            GetClassNameW(hWnd, className, className.Capacity);
            if (!className.ToString().Equals(expectedClass,
                    StringComparison.Ordinal)) return true;

            found = hWnd;
            return false;
        }, IntPtr.Zero);
        return found;
    }
}
'@

function Get-ChildRect {
    param(
        [IntPtr]$Parent,
        [IntPtr]$Child
    )

    $rect = [CtDialogQaNative+RECT]::new()
    if (-not [CtDialogQaNative]::GetWindowRect($Child, [ref]$rect)) {
        throw 'Could not read a dialog control rectangle.'
    }
    [void][CtDialogQaNative]::MapWindowPoints(
        [IntPtr]::Zero, $Parent, [ref]$rect, 2
    )
    $rect
}

function Convert-ToDip {
    param(
        [int]$Pixels,
        [uint32]$Dpi
    )
    [Math]::Round($Pixels * 96.0 / $Dpi, 1)
}

try {
    $oldDpiContext = [CtDialogQaNative]::SetThreadDpiAwarenessContext(
        [IntPtr]::new(-4)
    )

    New-Item -ItemType Directory -Path $qaDir -Force | Out-Null
    Copy-Item -LiteralPath $resolvedExe `
        -Destination (Join-Path $qaDir 'ctSpaces-dialog-qa.exe') -Force
    New-Item -ItemType File -Path (Join-Path $qaDir 'ctSpaces.portable') `
        -Force | Out-Null

    if (Test-Path -LiteralPath $profilePath) {
        throw "Refusing to use an existing QA profile: $profilePath"
    }
    New-Item -ItemType Directory -Path $profilePath -Force | Out-Null
    New-Item -ItemType File -Path (Join-Path $profilePath 'ctSpaces') `
        -Force | Out-Null

    $qaArguments = '--qa-instance=' + $runId +
        ' --qa-data-dir="' + $dataRoot + '"'
    $launcherProcess = Start-Process `
        -FilePath (Join-Path $qaDir 'ctSpaces-dialog-qa.exe') `
        -ArgumentList $qaArguments -PassThru
    [void]$launcherProcess.WaitForInputIdle(10000)

    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 150
        $launcherProcess.Refresh()
        if ($launcherProcess.HasExited) {
            throw "The QA launcher exited with code $($launcherProcess.ExitCode)."
        }
        $launcherWindow = [CtDialogQaNative]::FindProcessWindowWithChild(
            [uint32]$launcherProcess.Id, 102
        )
    } while ($launcherWindow -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $deadline)
    if ($launcherWindow -eq [IntPtr]::Zero) {
        throw 'The QA launcher window did not appear.'
    }

    $combo = [CtDialogQaNative]::GetDlgItem($launcherWindow, 102)
    $itemIndex = [CtDialogQaNative]::SendMessageStringW(
        $combo, 0x0158, [IntPtr]::new(-1), $testName
    )
    if ($itemIndex.ToInt64() -lt 0) {
        throw 'Could not find the disposable client in the client list.'
    }
    [void][CtDialogQaNative]::SendMessageW(
        $combo, 0x014E, $itemIndex, [IntPtr]::Zero
    )

    if (-not [CtDialogQaNative]::PostMessageW(
            $launcherWindow, 0x0111, [IntPtr]::new(41008), [IntPtr]::Zero
        )) {
        throw 'Could not open the Auto-fetch Icon dialog.'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $dialogWindow = [CtDialogQaNative]::FindProcessWindowByClass(
            [uint32]$launcherProcess.Id, 'InputBoxWndClass'
        )
    } while ($dialogWindow -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $deadline)
    if ($dialogWindow -eq [IntPtr]::Zero) {
        throw 'The Auto-fetch Icon dialog did not appear.'
    }

    [void][CtDialogQaNative]::SetForegroundWindow($dialogWindow)
    Start-Sleep -Milliseconds 300

    $dpi = [CtDialogQaNative]::GetDpiForWindow($dialogWindow)
    $client = [CtDialogQaNative+RECT]::new()
    $window = [CtDialogQaNative+RECT]::new()
    [void][CtDialogQaNative]::GetClientRect($dialogWindow, [ref]$client)
    [void][CtDialogQaNative]::GetWindowRect($dialogWindow, [ref]$window)

    $prompt = Get-ChildRect $dialogWindow `
        ([CtDialogQaNative]::GetDlgItem($dialogWindow, 101))
    $edit = Get-ChildRect $dialogWindow `
        ([CtDialogQaNative]::GetDlgItem($dialogWindow, 102))
    $ok = Get-ChildRect $dialogWindow `
        ([CtDialogQaNative]::GetDlgItem($dialogWindow, 1))
    $cancel = Get-ChildRect $dialogWindow `
        ([CtDialogQaNative]::GetDlgItem($dialogWindow, 2))

    $clientWidthDip = Convert-ToDip ($client.Right - $client.Left) $dpi
    $clientHeightDip = Convert-ToDip ($client.Bottom - $client.Top) $dpi
    $editWidthDip = Convert-ToDip ($edit.Right - $edit.Left) $dpi
    if ($clientWidthDip -lt 385 -or $clientWidthDip -gt 399) {
        throw "Unexpected dialog client width: $clientWidthDip DIP"
    }
    if ($clientHeightDip -lt 132 -or $clientHeightDip -gt 145) {
        throw "Unexpected dialog client height: $clientHeightDip DIP"
    }
    if ($editWidthDip -lt 350) {
        throw "The domain field is too narrow: $editWidthDip DIP"
    }
    if ($prompt.Bottom -gt $edit.Top -or $edit.Bottom -gt $ok.Top) {
        throw 'Dialog controls overlap vertically.'
    }
    if ($ok.Right -gt $cancel.Left -or $cancel.Right -gt $client.Right) {
        throw 'Dialog buttons overlap or extend outside the client area.'
    }

    $ownerDraw = 0x0000000B
    $okStyle = [CtDialogQaNative]::GetWindowLongPtrW(
        [CtDialogQaNative]::GetDlgItem($dialogWindow, 1), -16
    ).ToInt64()
    $cancelStyle = [CtDialogQaNative]::GetWindowLongPtrW(
        [CtDialogQaNative]::GetDlgItem($dialogWindow, 2), -16
    ).ToInt64()
    if (($okStyle -band 0xF) -ne $ownerDraw -or
        ($cancelStyle -band 0xF) -ne $ownerDraw) {
        throw 'The dialog buttons are not using the ctSpaces owner-draw style.'
    }

    if ($ScreenshotPath) {
        $resolvedScreenshot = [IO.Path]::GetFullPath($ScreenshotPath)
        $screenshotDir = Split-Path -Parent $resolvedScreenshot
        if ($screenshotDir) {
            New-Item -ItemType Directory -Path $screenshotDir -Force | Out-Null
        }
        $width = $window.Right - $window.Left
        $height = $window.Bottom - $window.Top
        $bitmap = [Drawing.Bitmap]::new($width, $height)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen(
                $window.Left, $window.Top, 0, 0,
                [Drawing.Size]::new($width, $height),
                [Drawing.CopyPixelOperation]::SourceCopy
            )
            $bitmap.Save($resolvedScreenshot, [Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }

    if (-not [CtDialogQaNative]::PostMessageW(
            $dialogWindow, 0x0100, [IntPtr]::new(0x1B), [IntPtr]::Zero
        )) {
        throw 'Could not send Escape to the Auto-fetch Icon dialog.'
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    while ([CtDialogQaNative]::IsWindow($dialogWindow) -and
           [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 50
    }
    if ([CtDialogQaNative]::IsWindow($dialogWindow)) {
        throw 'Escape did not close the Auto-fetch Icon dialog.'
    }
    if (-not [CtDialogQaNative]::IsWindowEnabled($launcherWindow)) {
        throw 'The launcher remained disabled after the dialog closed.'
    }
    $dialogWindow = [IntPtr]::Zero

    [pscustomobject]@{
        ClientWidthDIP = $clientWidthDip
        ClientHeightDIP = $clientHeightDip
        DomainFieldWidthDIP = $editWidthDip
        DPI = $dpi
        ThemedOwnerDrawButtons = $true
        EscapeClosesAndReenablesLauncher = $true
        Screenshot = $resolvedScreenshot
    } | ConvertTo-Json
} finally {
    if ($dialogWindow -ne [IntPtr]::Zero) {
        [void][CtDialogQaNative]::PostMessageW(
            $dialogWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero
        )
        Start-Sleep -Milliseconds 250
    }
    if ($launcherWindow -ne [IntPtr]::Zero) {
        [void][CtDialogQaNative]::PostMessageW(
            $launcherWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero
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

    $resolvedProfile = [IO.Path]::GetFullPath($profilePath)
    $resolvedSites = [IO.Path]::GetFullPath($sitesRoot).TrimEnd('\') + '\'
    if ($resolvedProfile.StartsWith(
            $resolvedSites, [StringComparison]::OrdinalIgnoreCase
        ) -and [IO.Path]::GetFileName($resolvedProfile) -eq $testName -and
        (Test-Path -LiteralPath $resolvedProfile)) {
        Remove-Item -LiteralPath $resolvedProfile -Recurse -Force
    }

    $resolvedQaDir = [IO.Path]::GetFullPath($qaDir)
    $resolvedProject = $projectRoot.TrimEnd('\') + '\'
    if ($resolvedQaDir.StartsWith(
            $resolvedProject, [StringComparison]::OrdinalIgnoreCase
        ) -and [IO.Path]::GetFileName($resolvedQaDir).StartsWith(
            '.qa-auto-fetch-', [StringComparison]::Ordinal
        ) -and (Test-Path -LiteralPath $resolvedQaDir)) {
        Remove-Item -LiteralPath $resolvedQaDir -Recurse -Force
    }

    if ($oldDpiContext -ne [IntPtr]::Zero) {
        [void][CtDialogQaNative]::SetThreadDpiAwarenessContext($oldDpiContext)
    }
}
