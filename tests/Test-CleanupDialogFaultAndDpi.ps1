param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64')]
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$runId = [Guid]::NewGuid().ToString('N')
$qaRoot = Join-Path $projectRoot ('build\cleanup-dialog-fault-' + $runId.Substring(0, 10))
$qaExeDir = Join-Path $qaRoot 'app'
$qaExe = Join-Path $qaExeDir 'ctSpaces-qa.exe'
$qaData = Join-Path $qaExeDir 'data'
$sites = Join-Path $qaData 'Sites'
$config = Join-Path $qaData 'config.ini'
$launcher = $null
$testPassed = $false
$utf8 = [Text.UTF8Encoding]::new($false)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class CleanupDialogFaultQa {
    delegate bool EnumProc(IntPtr window, IntPtr data);
    delegate bool MonitorEnumProc(IntPtr monitor, IntPtr dc, ref Rect rect, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct MonitorInfo {
        public uint Size;
        public Rect Monitor;
        public Rect Work;
        public uint Flags;
    }
    [StructLayout(LayoutKind.Sequential)] struct Size { public int X, Y; }
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorEnumProc fn, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern bool GetMonitorInfoW(IntPtr monitor, ref MonitorInfo info);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dialog, int id);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr window, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr window, IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr item);
    [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] static extern bool GetTextExtentPoint32W(IntPtr dc, string text, int length, out Size size);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr SendMessageText(IntPtr window, uint message, IntPtr wParam, StringBuilder text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendText(IntPtr window, uint message, IntPtr wParam, string text);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool WritePrivateProfileStringW(string section, string key, string value, string path);

    public static IntPtr Find(uint processId, string className, string title) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, unused) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != processId) return true;
            var cls = new StringBuilder(128); GetClassNameW(window, cls, cls.Capacity);
            if (cls.ToString() != className) return true;
            if (!String.IsNullOrEmpty(title) && Text(window) != title) return true;
            found = window; return false;
        }, IntPtr.Zero);
        return found;
    }

    public static string Text(IntPtr window) {
        var text = new StringBuilder(32768);
        GetWindowTextW(window, text, text.Capacity);
        return text.ToString();
    }

    public static string DescendantText(IntPtr window) {
        var values = new List<string>();
        EnumChildWindows(window, (child, unused) => {
            string text = Text(child);
            if (!String.IsNullOrEmpty(text)) values.Add(text);
            return true;
        }, IntPtr.Zero);
        return String.Join("\n", values);
    }

    static bool MeasureWindowText(IntPtr window, string text, out Size size) {
        size = new Size();
        IntPtr font = SendMessageW(window, 0x0031, IntPtr.Zero, IntPtr.Zero);
        IntPtr dc = GetDC(window);
        if (dc == IntPtr.Zero) return false;
        IntPtr old = font != IntPtr.Zero ? SelectObject(dc, font) : IntPtr.Zero;
        bool measured = GetTextExtentPoint32W(dc, text, text.Length, out size);
        if (old != IntPtr.Zero) SelectObject(dc, old);
        ReleaseDC(window, dc);
        return measured;
    }

    public static bool TextFits(IntPtr window, int horizontalPaddingDip) {
        Rect rect; Size textSize;
        string text = Text(window);
        if (!GetClientRect(window, out rect) ||
            !MeasureWindowText(window, text, out textSize)) return false;
        int padding = (int)((long)horizontalPaddingDip * GetDpiForWindow(window) / 96);
        return textSize.X + padding <= rect.Right - rect.Left &&
               textSize.Y <= rect.Bottom - rect.Top;
    }

    public static bool ListExtentCoversFirstRow(IntPtr list) {
        int length = SendMessageW(list, 0x018A, IntPtr.Zero, IntPtr.Zero).ToInt32();
        if (length <= 0) return false;
        var row = new StringBuilder(length + 1);
        if (SendMessageText(list, 0x0189, IntPtr.Zero, row).ToInt32() < 0)
            return false;
        Size textSize;
        if (!MeasureWindowText(list, row.ToString(), out textSize)) return false;
        int extent = SendMessageW(list, 0x0193, IntPtr.Zero, IntPtr.Zero).ToInt32();
        int padding = (int)((long)16 * GetDpiForWindow(list) / 96);
        return extent >= textSize.X + padding;
    }

    public static Rect[] MonitorWorkAreas() {
        var values = new List<Rect>();
        EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero,
            (IntPtr monitor, IntPtr unusedDc, ref Rect unusedRect, IntPtr unused) => {
                var info = new MonitorInfo();
                info.Size = (uint)Marshal.SizeOf(typeof(MonitorInfo));
                if (GetMonitorInfoW(monitor, ref info)) values.Add(info.Work);
                return true;
            }, IntPtr.Zero);
        return values.ToArray();
    }
}
'@

function Wait-Until([scriptblock]$Check, [string]$Description, [int]$Seconds = 20) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        $result = & $Check
        if ($result -is [IntPtr]) {
            if ($result -ne [IntPtr]::Zero) { return $result }
        } elseif ($result) {
            return $result
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Description
}

function Send-Command([IntPtr]$Window, [int]$Id) {
    if (-not [CleanupDialogFaultQa]::PostMessageW(
            $Window, 0x0111, [UIntPtr]::new([uint32]$Id),
            [CleanupDialogFaultQa]::GetDlgItem($Window, $Id))) {
        throw "Could not post command $Id."
    }
}

function Close-Dialog([IntPtr]$Dialog) {
    $title = [CleanupDialogFaultQa]::Text($Dialog)
    if (-not [CleanupDialogFaultQa]::PostMessageW(
            $Dialog, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)) {
        throw "Could not post WM_CLOSE to '$title'."
    }
    Wait-Until { -not [CleanupDialogFaultQa]::IsWindow($Dialog) } `
        "Dialog '$title' did not close after WM_CLOSE." | Out-Null
}

function Assert-PreviewGeometry([IntPtr]$Preview) {
    $ack = [CleanupDialogFaultQa]::GetDlgItem($Preview, 1203)
    if ([CleanupDialogFaultQa]::Text($ack) -ne
        'I understand that all selected clients will be permanently deleted.') {
        throw 'The cleanup acknowledgement text is missing.'
    }
    if (-not [CleanupDialogFaultQa]::TextFits($ack, 18)) {
        throw "The cleanup acknowledgement clips at $([CleanupDialogFaultQa]::GetDpiForWindow($Preview)) DPI."
    }
    foreach ($id in @(1204, 1205, 1, 2)) {
        $button = [CleanupDialogFaultQa]::GetDlgItem($Preview, $id)
        if ($button -eq [IntPtr]::Zero -or
            -not [CleanupDialogFaultQa]::TextFits($button, 12)) {
            throw "Cleanup button $id clips at $([CleanupDialogFaultQa]::GetDpiForWindow($Preview)) DPI."
        }
    }
    $list = [CleanupDialogFaultQa]::GetDlgItem($Preview, 1201)
    if (-not [CleanupDialogFaultQa]::ListExtentCoversFirstRow($list)) {
        throw "The cleanup list horizontal extent does not cover its measured first row at $([CleanupDialogFaultQa]::GetDpiForWindow($Preview)) DPI."
    }
}

function Test-AvailableMonitorGeometry([IntPtr]$Preview) {
    $windowRect = [CleanupDialogFaultQa+Rect]::new()
    if (-not [CleanupDialogFaultQa]::GetWindowRect($Preview, [ref]$windowRect)) {
        throw 'Could not read the cleanup preview rectangle.'
    }
    $width = $windowRect.Right - $windowRect.Left
    $height = $windowRect.Bottom - $windowRect.Top
    $observed = [Collections.Generic.HashSet[uint32]]::new()
    foreach ($work in [CleanupDialogFaultQa]::MonitorWorkAreas()) {
        $x = $work.Left + [Math]::Max(0, (($work.Right - $work.Left) - $width) / 2)
        $y = $work.Top + [Math]::Max(0, (($work.Bottom - $work.Top) - $height) / 2)
        if (-not [CleanupDialogFaultQa]::MoveWindow(
                $Preview, [int]$x, [int]$y, $width, $height, $true)) {
            throw 'Could not move the cleanup preview to an available monitor.'
        }
        Start-Sleep -Milliseconds 250
        [void]$observed.Add([CleanupDialogFaultQa]::GetDpiForWindow($Preview))
        Assert-PreviewGeometry $Preview
        if ([CleanupDialogFaultQa]::GetWindowRect($Preview, [ref]$windowRect)) {
            $width = $windowRect.Right - $windowRect.Left
            $height = $windowRect.Bottom - $windowRect.Top
        }
    }
    return @($observed)
}

function Write-Activity([string]$Name, [DateTime]$When) {
    [IO.File]::WriteAllText(
        (Join-Path $sites "$Name\ctSpaces-client-activity"),
        "ctSpaces-activity=1`r`nopened=$($When.ToFileTimeUtc())`r`n", $utf8)
}

function Make-Client([string]$Name, [DateTime]$When) {
    $root = Join-Path $sites $Name
    $slot = Join-Path $root 'Browsers\edge'
    $profile = Join-Path $slot 'Profile'
    New-Item -ItemType Directory -Path (Join-Path $profile 'Default') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $root 'ctSpaces-client-v2'),
        "ctSpaces-client-schema=2`r`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $slot 'ctSpaces-browser-v2'),
        "ctSpaces-browser-schema=2`r`nbrowser=edge`r`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'ctSpaces'),
        "ctSpaces-profile=2`r`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'Default\Preferences'), '{}', $utf8)
    Write-Activity $Name $When
}

function Build-And-RunMetricsHarness {
    $project = Join-Path $PSScriptRoot 'OwnerDrawUiTests.vcxproj'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "vswhere was not found: $vswhere"
    }
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
        -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (-not $msbuild) { throw 'MSBuild was not found.' }
    & $msbuild $project /m:1 /nodeReuse:false /t:Rebuild `
        /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "OwnerDrawUiTests build failed: $LASTEXITCODE" }
    $testExe = Join-Path $projectRoot "build\tests\$Platform\$Configuration\OwnerDrawUiTests.exe"
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "OwnerDrawUiTests failed: $LASTEXITCODE" }
}

$liveConfig = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'
$liveHashBefore = if (Test-Path -LiteralPath $liveConfig -PathType Leaf) {
    (Get-FileHash -LiteralPath $liveConfig).Hash
} else { 'missing' }

try {
    Build-And-RunMetricsHarness
    New-Item -ItemType Directory -Path $qaExeDir, $sites -Force | Out-Null
    Copy-Item -LiteralPath $resolvedExe -Destination $qaExe
    [IO.File]::WriteAllText((Join-Path $qaExeDir 'ctSpaces.portable'), '', $utf8)
    $old = [DateTime]::UtcNow.AddMonths(-4)
    $deletedName = 'Old Client With A Deliberately Long Cleanup Preview Name'
    $changedName = 'Old Client Whose Activity Changes After Preview'
    Make-Client $deletedName $old
    Make-Client $changedName $old
    [IO.File]::WriteAllText($config,
        "[user]`r`nbrowser=edge`r`ntheme_name=Dark - Gothic`r`n" +
        "[qa]`r`ncleanup_dialog_fault=preview`r`n", $utf8)

    $arguments = '--qa-instance=' + $runId.Substring(0, 10) +
        ' --qa-data-dir="' + $qaData + '"'
    $launcher = Start-Process -FilePath $qaExe -ArgumentList $arguments -PassThru
    [void]$launcher.WaitForInputIdle(10000)
    $main = Wait-Until {
        [CleanupDialogFaultQa]::Find([uint32]$launcher.Id, 'ctSpacesLauncherClass', $null)
    } 'The isolated launcher did not open.'

    Send-Command $main 41120
    $failure = Wait-Until {
        [CleanupDialogFaultQa]::Find([uint32]$launcher.Id, '#32770', 'Cleanup Unavailable')
    } 'The injected preview-creation failure was not reported.'
    $failureText = [CleanupDialogFaultQa]::DescendantText($failure)
    if (-not $failureText.Contains('Nothing was deleted') -or
        -not $failureText.Contains('Windows error: 1814')) {
        throw "Unexpected preview-failure text: $failureText"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $sites $deletedName)) -or
        -not (Test-Path -LiteralPath (Join-Path $sites $changedName))) {
        throw 'The preview-creation failure deleted a client.'
    }
    Close-Dialog $failure
    Wait-Until { [CleanupDialogFaultQa]::IsWindowEnabled($main) } `
        'The launcher stayed disabled after preview-creation failure.' | Out-Null

    if (-not [CleanupDialogFaultQa]::WritePrivateProfileStringW(
            'qa', 'cleanup_dialog_fault', 'result', $config)) {
        throw 'Could not change the isolated cleanup fault mode.'
    }

    # Verify that keyboard Cancel remains native after the font/theme changes.
    Send-Command $main 41120
    $preview = Wait-Until {
        [CleanupDialogFaultQa]::Find([uint32]$launcher.Id, '#32770', 'Clean Up Inactive Clients')
    } 'The cleanup preview did not open.'
    $actualDpi = [CleanupDialogFaultQa]::GetDpiForWindow($preview)
    if ($actualDpi -lt 96) { throw "Invalid actual dialog DPI: $actualDpi" }
    $actualMonitorDpis = Test-AvailableMonitorGeometry $preview
    if (-not [CleanupDialogFaultQa]::PostMessageW(
            $preview, 0x0100, [UIntPtr]::new(0x1B), [IntPtr]::Zero)) {
        throw 'Could not post keyboard Escape to the cleanup preview.'
    }
    Wait-Until { -not [CleanupDialogFaultQa]::IsWindow($preview) } `
        'Keyboard Cancel did not close the cleanup preview.' | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $sites $deletedName))) {
        throw 'Keyboard Cancel deleted a client.'
    }

    Send-Command $main 41120
    $preview = Wait-Until {
        [CleanupDialogFaultQa]::Find([uint32]$launcher.Id, '#32770', 'Clean Up Inactive Clients')
    } 'The second cleanup preview did not open.'
    $list = [CleanupDialogFaultQa]::GetDlgItem($preview, 1201)
    Wait-Until {
        [CleanupDialogFaultQa]::SendMessageW(
            $list, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -eq 2
    } 'The cleanup preview did not contain both clients.' | Out-Null
    Assert-PreviewGeometry $preview
    $ack = [CleanupDialogFaultQa]::GetDlgItem($preview, 1203)
    Write-Activity $changedName ([DateTime]::UtcNow)
    [void][CleanupDialogFaultQa]::SendMessageW($ack, 0x00F1, [IntPtr]1, [IntPtr]::Zero)
    Send-Command $preview 1203
    Wait-Until { [CleanupDialogFaultQa]::IsWindowEnabled(
            [CleanupDialogFaultQa]::GetDlgItem($preview, 1)) } `
        'The acknowledged cleanup action did not enable.' | Out-Null
    Send-Command $preview 1

    $fallback = Wait-Until {
        [CleanupDialogFaultQa]::Find([uint32]$launcher.Id, '#32770', 'Cleanup Complete')
    } 'The injected result-dialog failure did not preserve the cleanup summary.'
    $fallbackText = [CleanupDialogFaultQa]::DescendantText($fallback)
    foreach ($required in @('Deleted 1 client(s)', 'Skipped 1 item(s)',
            $changedName, 'activity date changed',
            'scrollable details window could not be opened', 'Windows error: 1814')) {
        if (-not $fallbackText.Contains($required)) {
            throw "The fallback summary omitted '$required': $fallbackText"
        }
    }
    if (Test-Path -LiteralPath (Join-Path $sites $deletedName)) {
        throw 'The eligible client was not deleted before the result fallback.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $sites $changedName))) {
        throw 'The changed client was deleted despite the final activity recheck.'
    }
    Close-Dialog $fallback
    $mainGo = [CleanupDialogFaultQa]::GetDlgItem($main, 1)
    Wait-Until { [CleanupDialogFaultQa]::IsWindowEnabled($mainGo) } `
        'The launcher controls stayed disabled after the result fallback.' | Out-Null

    # Opening About proves that normal launcher commands remain usable.
    Send-Command $main 104
    $about = Wait-Until {
        [CleanupDialogFaultQa]::Find([uint32]$launcher.Id, '#32770', 'About')
    } 'The launcher was not usable after cleanup fault recovery.'
    Close-Dialog $about

    Write-Host "Cleanup dialog fault recovery passed; initial actual DPI was $actualDpi; available-monitor actual DPIs: $($actualMonitorDpis -join ', ')."
    $testPassed = $true
} finally {
    if ($launcher -and -not $launcher.HasExited) {
        $main = [CleanupDialogFaultQa]::Find([uint32]$launcher.Id, 'ctSpacesLauncherClass', $null)
        if ($main -ne [IntPtr]::Zero) {
            [void][CleanupDialogFaultQa]::PostMessageW(
                $main, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        }
        if (-not $launcher.WaitForExit(5000)) { $launcher.Kill() }
    }
    $liveHashAfter = if (Test-Path -LiteralPath $liveConfig -PathType Leaf) {
        (Get-FileHash -LiteralPath $liveConfig).Hash
    } else { 'missing' }
    $liveHashChanged = $liveHashAfter -ne $liveHashBefore
    if ($liveHashChanged) { $testPassed = $false }
    if ($testPassed -and (Test-Path -LiteralPath $qaRoot)) {
        $resolvedQaRoot = [IO.Path]::GetFullPath($qaRoot)
        $resolvedBuildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
        $buildPrefix = $resolvedBuildRoot.TrimEnd(
            [IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) +
            [IO.Path]::DirectorySeparatorChar
        if (-not $resolvedQaRoot.StartsWith(
                $buildPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not [IO.Path]::GetDirectoryName($resolvedQaRoot).Equals(
                $resolvedBuildRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not ([IO.Path]::GetFileName($resolvedQaRoot)).StartsWith(
                'cleanup-dialog-fault-', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected QA path: $resolvedQaRoot"
        }
        Remove-Item -LiteralPath $resolvedQaRoot -Recurse -Force
    } elseif (-not $testPassed -and (Test-Path -LiteralPath $qaRoot)) {
        Write-Warning "Preserved failed cleanup-dialog QA fixture: $qaRoot"
    }
    if ($liveHashChanged) {
        throw 'The isolated cleanup fault test changed the live configuration.'
    }
}
