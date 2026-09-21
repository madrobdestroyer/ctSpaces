param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$ArtifactDirectory = '',
    [switch]$CompileOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 10)
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
    $ArtifactDirectory = Join-Path $projectRoot ('build\cleanup-scan-ui-' + $runId)
}
$artifactRoot = [IO.Path]::GetFullPath($ArtifactDirectory)
$fixtureRoot = Join-Path $artifactRoot 'fixtures'
$utf8 = [Text.UTF8Encoding]::new($false)
$ownedProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()
$results = [Collections.Generic.List[object]]::new()
$testPassed = $false
$liveConfig = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'

function Get-FileFingerprint([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
    return '<missing>'
}
$liveConfigBefore = Get-FileFingerprint $liveConfig

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public static class CleanupScanUiQa {
    delegate bool EnumProc(IntPtr window, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct MonitorInfo {
        public uint Size; public Rect Monitor; public Rect Work; public uint Flags;
    }
    [StructLayout(LayoutKind.Sequential)] struct CopyDataStruct {
        public UIntPtr Tag; public uint Bytes; public IntPtr Text;
    }

    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dialog, int id);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern bool GetMonitorInfoW(IntPtr monitor, ref MonitorInfo info);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern bool SendMessageTimeoutW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageTimeoutW")] static extern bool SendMessageTimeoutTextW(IntPtr window, uint message, UIntPtr wParam, StringBuilder text, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr SendMessageText(IntPtr window, uint message, IntPtr wParam, StringBuilder text);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool WritePrivateProfileStringW(string section, string key, string value, string path);
    [DllImport("user32.dll")] static extern IntPtr GetWindowDC(IntPtr window);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr window, IntPtr dc);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

    public static string Text(IntPtr window) {
        var text = new StringBuilder(32768);
        UIntPtr result;
        if (!SendMessageTimeoutTextW(window, 0x000D,
            new UIntPtr((uint)text.Capacity), text, 0x0003, 1000, out result)) {
            return String.Empty;
        }
        return text.ToString();
    }

    public static string DescendantText(IntPtr window) {
        var values = new List<string>();
        EnumChildWindows(window, (child, unused) => {
            string value = Text(child);
            if (!String.IsNullOrWhiteSpace(value)) values.Add(value);
            return true;
        }, IntPtr.Zero);
        return String.Join("\n", values);
    }

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

    public static int Count(uint processId, string className, string title) {
        int count = 0;
        EnumWindows((window, unused) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != processId) return true;
            var cls = new StringBuilder(128); GetClassNameW(window, cls, cls.Capacity);
            if (cls.ToString() == className &&
                (String.IsNullOrEmpty(title) || Text(window) == title)) ++count;
            return true;
        }, IntPtr.Zero);
        return count;
    }

    public static string Windows(uint processId) {
        var values = new List<string>();
        EnumWindows((window, unused) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != processId) return true;
            var cls = new StringBuilder(128); GetClassNameW(window, cls, cls.Capacity);
            values.Add(String.Format("0x{0:X} class='{1}' title='{2}' visible={3} enabled={4}",
                window.ToInt64(), cls, Text(window), IsWindowVisible(window), IsWindowEnabled(window)));
            return true;
        }, IntPtr.Zero);
        return String.Join("; ", values);
    }

    public static bool Ping(IntPtr window, uint timeoutMs) {
        UIntPtr result;
        return SendMessageTimeoutW(window, 0, UIntPtr.Zero, IntPtr.Zero,
            0x0003, timeoutMs, out result);
    }

    // -1 means the UI call timed out, 0 means the request was explicitly
    // rejected, and 1 means it was accepted.
    public static int CopyDataResult(IntPtr window, string arguments) {
        IntPtr text = Marshal.StringToHGlobalUni(arguments);
        IntPtr data = IntPtr.Zero;
        try {
            var copy = new CopyDataStruct {
                Tag = new UIntPtr(0x43545350u),
                Bytes = checked((uint)((arguments.Length + 1) * 2)),
                Text = text
            };
            data = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(CopyDataStruct)));
            Marshal.StructureToPtr(copy, data, false);
            UIntPtr result;
            bool completed = SendMessageTimeoutW(window, 0x004A, UIntPtr.Zero,
                data, 0x0003, 2000, out result);
            return !completed ? -1 : (result == UIntPtr.Zero ? 0 : 1);
        } finally {
            if (data != IntPtr.Zero) Marshal.FreeHGlobal(data);
            Marshal.FreeHGlobal(text);
        }
    }

    public static string[] ListRows(IntPtr list) {
        UIntPtr ignored;
        if (!SendMessageTimeoutW(list, 0x018B, UIntPtr.Zero, IntPtr.Zero,
            0x0003, 1000, out ignored)) throw new Exception("List count timed out.");
        int count = unchecked((int)ignored.ToUInt64());
        var rows = new List<string>();
        for (int index = 0; index < count; ++index) {
            IntPtr lengthValue = SendMessageText(list, 0x018A, (IntPtr)index, null);
            int length = lengthValue.ToInt32();
            if (length < 0) throw new Exception("List row length was unavailable.");
            var text = new StringBuilder(length + 1);
            if (SendMessageText(list, 0x0189, (IntPtr)index, text).ToInt32() < 0)
                throw new Exception("List row read failed.");
            rows.Add(text.ToString());
        }
        return rows.ToArray();
    }

    public static bool InsideWorkArea(IntPtr window) {
        IntPtr previous = SetThreadDpiAwarenessContext((IntPtr)(-4));
        try {
            Rect rect;
            if (!GetWindowRect(window, out rect)) return false;
            IntPtr monitor = MonitorFromWindow(window, 2);
            var info = new MonitorInfo();
            info.Size = (uint)Marshal.SizeOf(typeof(MonitorInfo));
            if (monitor == IntPtr.Zero || !GetMonitorInfoW(monitor, ref info)) return false;
            return rect.Left >= info.Work.Left && rect.Top >= info.Work.Top &&
                   rect.Right <= info.Work.Right && rect.Bottom <= info.Work.Bottom;
        } finally {
            if (previous != IntPtr.Zero) SetThreadDpiAwarenessContext(previous);
        }
    }

    public static void Capture(IntPtr window, string path) {
        IntPtr previous = SetThreadDpiAwarenessContext((IntPtr)(-4));
        try {
            Rect rect;
            if (!GetWindowRect(window, out rect)) throw new Exception("Could not read capture bounds.");
            int width = rect.Right - rect.Left, height = rect.Bottom - rect.Top;
            if (width <= 0 || height <= 0) throw new Exception("Invalid capture bounds.");
            using (var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb))
            using (var graphics = Graphics.FromImage(bitmap)) {
                IntPtr dc = graphics.GetHdc();
                try {
                    if (!PrintWindow(window, dc, 2) && !PrintWindow(window, dc, 0))
                        throw new Exception("PrintWindow failed.");
                } finally { graphics.ReleaseHdc(dc); }
                bitmap.Save(path, ImageFormat.Png);
            }
        } finally {
            if (previous != IntPtr.Zero) SetThreadDpiAwarenessContext(previous);
        }
    }
}
'@

if ($CompileOnly) {
    Write-Host 'Cleanup scan UI PowerShell and native helper compilation passed.'
    return
}

function Assert-True([bool]$Value, [string]$Message) {
    if (-not $Value) { throw $Message }
}

function Wait-Until([scriptblock]$Check, [string]$Description, [int]$Seconds = 10) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        $value = & $Check
        if ($value -is [IntPtr]) {
            if ($value -ne [IntPtr]::Zero) { return $value }
        } elseif ($value) { return $value }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Description
}

function Wait-DialogText([IntPtr]$Dialog, [string]$Needle, [string]$Description) {
    Wait-Until {
        [CleanupScanUiQa]::IsWindow($Dialog) -and
        [CleanupScanUiQa]::IsWindowVisible($Dialog) -and
        [CleanupScanUiQa]::DescendantText($Dialog).Contains($Needle)
    } $Description 5 | Out-Null
    return [CleanupScanUiQa]::DescendantText($Dialog)
}

function Set-Ini([string]$Path, [string]$Section, [string]$Key, [string]$Value) {
    if (-not [CleanupScanUiQa]::WritePrivateProfileStringW($Section, $Key, $Value, $Path)) {
        throw "Could not set [$Section] $Key in the isolated configuration."
    }
}

function Send-Command([IntPtr]$Window, [int]$Command) {
    if (-not [CleanupScanUiQa]::PostMessageW(
            $Window, 0x0111, [UIntPtr]::new([uint32]$Command), [IntPtr]::Zero)) {
        throw "Could not post command $Command."
    }
}

function Close-Window([IntPtr]$Window, [string]$Description) {
    if (-not [CleanupScanUiQa]::PostMessageW(
            $Window, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)) {
        throw "Could not close $Description."
    }
    Wait-Until { -not [CleanupScanUiQa]::IsWindow($Window) } `
        "$Description did not close." 5 | Out-Null
}

function Get-SitesSnapshot([string]$Sites) {
    if (-not (Test-Path -LiteralPath $Sites)) { return '<missing>' }
    $root = [IO.Path]::GetFullPath($Sites).TrimEnd('\') + '\'
    $rows = foreach ($item in Get-ChildItem -LiteralPath $Sites -Force -Recurse | Sort-Object FullName) {
        $relative = $item.FullName.Substring($root.Length)
        if ($item.PSIsContainer) {
            "D|$relative"
        } else {
            "F|$relative|$($item.Length)|$((Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash)"
        }
    }
    return ($rows -join "`n")
}

function Assert-FixturePath([string]$Path) {
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot).TrimEnd(
        [IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $prefix = $resolvedFixtureRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing a destructive fixture operation outside '$resolvedFixtureRoot': $resolvedPath"
    }
    return $resolvedPath
}

function New-Client([string]$Sites, [int]$Index) {
    $name = 'Cleanup Scan Client {0:D2}' -f $Index
    $root = Join-Path $Sites $name
    $slot = Join-Path $root 'Browsers\edge'
    $profile = Join-Path $slot 'Profile'
    New-Item -ItemType Directory -Path (Join-Path $profile 'Default\Cache\nested') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $root 'ctSpaces-client-v2'), "ctSpaces-client-schema=2`r`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $slot 'ctSpaces-browser-v2'), "ctSpaces-browser-schema=2`r`nbrowser=edge`r`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'ctSpaces'), "ctSpaces-profile=2`r`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'Default\Preferences'), '{}', $utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'Default\Cache\nested\sentinel.txt'), $name, $utf8)
    $old = [DateTime]::UtcNow.AddMonths(-4).ToFileTimeUtc()
    [IO.File]::WriteAllText((Join-Path $root 'ctSpaces-client-activity'),
        "ctSpaces-activity=1`r`nopened=$old`r`n", $utf8)
    return $name
}

function New-Fixture([string]$Name, [string]$Theme, [int]$ClientCount, [bool]$CreateSites = $true) {
    $root = Join-Path $fixtureRoot $Name
    if (Test-Path -LiteralPath $root) {
        throw "Refusing to reuse an existing per-run cleanup fixture directory: $root"
    }
    $app = Join-Path $root 'app'
    $data = Join-Path $app 'data'
    $sites = Join-Path $data 'Sites'
    New-Item -ItemType Directory -Path $app, $data -Force | Out-Null
    if ($CreateSites) { New-Item -ItemType Directory -Path $sites -Force | Out-Null }
    $exe = Join-Path $app 'ctSpaces-cleanup-scan-qa.exe'
    Copy-Item -LiteralPath $resolvedExe -Destination $exe
    [IO.File]::WriteAllText((Join-Path $app 'ctSpaces.portable'), '', $utf8)
    $config = Join-Path $data 'config.ini'
    [IO.File]::WriteAllText($config,
        "[user]`r`nbrowser=edge`r`ntheme_name=$Theme`r`n" +
        "[guide]`r`nwelcome_handled=1`r`n" +
        "[qa]`r`ncleanup_scan_entry_delay_ms=250`r`n", $utf8)
    $clients = @()
    if ($CreateSites) {
        for ($index = 1; $index -le $ClientCount; ++$index) {
            $clients += New-Client $sites $index
        }
        $unsafe = Join-Path $sites 'Unsafe Unmarked Folder'
        New-Item -ItemType Directory -Path $unsafe -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $unsafe 'must-remain.txt'), 'unsafe', $utf8)
    }
    return [pscustomobject]@{
        Name = $Name; Root = $root; App = $app; Data = $data; Sites = $sites
        Exe = $exe; Config = $config; Clients = $clients; Process = $null
        Main = [IntPtr]::Zero
    }
}

function Start-Fixture($Fixture) {
    $arguments = '--qa-instance=' + $runId + $Fixture.Name +
        ' --qa-data-dir="' + $Fixture.Data + '"'
    $Fixture.Process = Start-Process -FilePath $Fixture.Exe -ArgumentList $arguments -PassThru
    $ownedProcesses.Add($Fixture.Process)
    [void]$Fixture.Process.WaitForInputIdle(10000)
    $Fixture.Main = Wait-Until {
        $Fixture.Process.Refresh()
        if ($Fixture.Process.HasExited) { throw "Fixture $($Fixture.Name) exited during startup." }
        [CleanupScanUiQa]::Find([uint32]$Fixture.Process.Id, 'ctSpacesLauncherClass', $null)
    } "Fixture $($Fixture.Name) did not open its main window." 20
    try {
        Wait-Until {
            $currentGo = [CleanupScanUiQa]::GetDlgItem($Fixture.Main, 1)
            $currentGo -ne [IntPtr]::Zero -and
            [CleanupScanUiQa]::IsWindowEnabled($currentGo) -and
            [CleanupScanUiQa]::Ping($Fixture.Main, 500)
        } "Fixture $($Fixture.Name) was not responsive before cleanup." 10 | Out-Null
    } catch {
        $currentGo = [CleanupScanUiQa]::GetDlgItem($Fixture.Main, 1)
        $diagnostic = "main=$($Fixture.Main) mainValid=$([CleanupScanUiQa]::IsWindow($Fixture.Main)) " +
            "mainEnabled=$([CleanupScanUiQa]::IsWindowEnabled($Fixture.Main)) " +
            "mainPing=$([CleanupScanUiQa]::Ping($Fixture.Main, 500)) go=$currentGo " +
            "goEnabled=$(if ($currentGo -ne [IntPtr]::Zero) { [CleanupScanUiQa]::IsWindowEnabled($currentGo) } else { $false }); " +
            "windows=$([CleanupScanUiQa]::Windows([uint32]$Fixture.Process.Id))"
        throw "$($_.Exception.Message) $diagnostic"
    }
    return $Fixture
}

function Assert-NoCleanupFollowup($Fixture) {
    foreach ($title in @('Delete Multiple Clients', 'Clean Up Inactive Clients',
            'Cleanup Cancelled', 'Cleanup Complete')) {
        if ([CleanupScanUiQa]::Find([uint32]$Fixture.Process.Id, '#32770', $title) -ne [IntPtr]::Zero) {
            throw "Unexpected cleanup follow-up '$title' appeared after scan cancellation."
        }
    }
}

function Wait-MainRecovered($Fixture, [string]$Context) {
    Wait-Until {
        [CleanupScanUiQa]::IsWindow($Fixture.Main) -and
        [CleanupScanUiQa]::IsWindowEnabled($Fixture.Main) -and
        [CleanupScanUiQa]::Ping($Fixture.Main, 500)
    } "The launcher did not recover after $Context." 5 | Out-Null
}

function Open-Scan($Fixture, [bool]$Manual, [string]$Screenshot = '') {
    $command = if ($Manual) { 41121 } else { 41120 }
    $title = if ($Manual) { 'Preparing Delete Multiple Clients' } else { 'Preparing Inactive Cleanup' }
    $statusText = if ($Manual) {
        'Inspecting closed clients before the deletion preview...'
    } else {
        'Inspecting inactive clients before the cleanup preview...'
    }
    Send-Command $Fixture.Main $command
    $dialog = Wait-Until {
        [CleanupScanUiQa]::Find([uint32]$Fixture.Process.Id, '#32770', $title)
    } "The '$title' progress dialog did not open." 5
    Wait-Until {
        if (-not [CleanupScanUiQa]::IsWindow($dialog) -or
            -not [CleanupScanUiQa]::IsWindowVisible($dialog)) { return $false }
        $readyStatus = [CleanupScanUiQa]::GetDlgItem($dialog, 1315)
        $readyProgress = [CleanupScanUiQa]::GetDlgItem($dialog, 1316)
        $readyCancel = [CleanupScanUiQa]::GetDlgItem($dialog, 2)
        return $readyStatus -ne [IntPtr]::Zero -and
            [CleanupScanUiQa]::Text($readyStatus) -eq $statusText -and
            $readyProgress -ne [IntPtr]::Zero -and $readyCancel -ne [IntPtr]::Zero
    } "$title did not finish visible initialization." 3 | Out-Null
    Assert-True ([CleanupScanUiQa]::GetWindow($dialog, 4) -eq $Fixture.Main) "$title was not owned by the launcher."
    Assert-True (-not [CleanupScanUiQa]::IsWindowEnabled($Fixture.Main)) "$title did not disable its owner."
    Assert-True ([CleanupScanUiQa]::InsideWorkArea($dialog)) "$title was outside its monitor work area."
    $status = [CleanupScanUiQa]::GetDlgItem($dialog, 1315)
    $progress = [CleanupScanUiQa]::GetDlgItem($dialog, 1316)
    $cancel = [CleanupScanUiQa]::GetDlgItem($dialog, 2)
    Assert-True ($status -ne [IntPtr]::Zero -and [CleanupScanUiQa]::IsWindowVisible($status)) 'The cleanup scan status is missing.'
    Assert-True ([CleanupScanUiQa]::Text($status) -eq $statusText) "Unexpected cleanup scan status: '$([CleanupScanUiQa]::Text($status))'."
    Assert-True ($progress -ne [IntPtr]::Zero -and [CleanupScanUiQa]::IsWindowVisible($progress)) 'The cleanup scan progress indicator is missing.'
    Assert-True ($cancel -ne [IntPtr]::Zero -and [CleanupScanUiQa]::IsWindowVisible($cancel) -and
        [CleanupScanUiQa]::IsWindowEnabled($cancel)) 'The cleanup scan Cancel button is unavailable.'
    1..4 | ForEach-Object {
        Assert-True ([CleanupScanUiQa]::Ping($Fixture.Main, 200)) 'The launcher message loop stopped responding while cleanup inspection ran.'
    }
    if (-not [string]::IsNullOrWhiteSpace($Screenshot)) {
        # Let the themed owner-drawn button and the first marquee frame paint;
        # finding the initialized HWND is intentionally not treated as a paint
        # completion signal.
        Start-Sleep -Milliseconds 250
        Assert-True ([CleanupScanUiQa]::Ping($dialog, 500)) 'The cleanup scan dialog did not finish its initial paint.'
        [CleanupScanUiQa]::Capture($dialog, $Screenshot)
        Assert-True ((Test-Path -LiteralPath $Screenshot -PathType Leaf) -and
            (Get-Item -LiteralPath $Screenshot).Length -gt 1000) 'The themed progress screenshot was not created.'
    }
    return $dialog
}

function Cancel-Scan($Fixture, [IntPtr]$Dialog, [ValidateSet('button', 'escape', 'close', 'owner-close')] [string]$Route) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    switch ($Route) {
        'button' { Send-Command $Dialog 2 }
        'escape' {
            Assert-True ([CleanupScanUiQa]::PostMessageW($Dialog, 0x0100, [UIntPtr]::new(0x1B), [IntPtr]::Zero)) 'Could not post Escape to the scan dialog.'
            [void][CleanupScanUiQa]::PostMessageW($Dialog, 0x0101, [UIntPtr]::new(0x1B), [IntPtr]::Zero)
        }
        'close' {
            Assert-True ([CleanupScanUiQa]::PostMessageW($Dialog, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)) 'Could not close the scan dialog.'
        }
        'owner-close' {
            Assert-True ([CleanupScanUiQa]::PostMessageW($Fixture.Main, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)) 'Could not post WM_CLOSE to the launcher during a scan.'
        }
    }
    Wait-Until { -not [CleanupScanUiQa]::IsWindow($Dialog) } `
        "Cleanup scan did not finish cancellation through $Route." 2 | Out-Null
    if ($clock.Elapsed.TotalSeconds -ge 2) {
        throw "Cleanup scan cancellation through $Route took $([Math]::Round($clock.Elapsed.TotalMilliseconds)) ms."
    }
    Assert-NoCleanupFollowup $Fixture
    Wait-MainRecovered $Fixture $Route
}

function Open-And-CancelPreview($Fixture, [bool]$Manual, [int]$ExpectedClients) {
    Set-Ini $Fixture.Config 'qa' 'cleanup_scan_entry_delay_ms' '0'
    $command = if ($Manual) { 41121 } else { 41120 }
    Send-Command $Fixture.Main $command
    $title = if ($Manual) { 'Delete Multiple Clients' } else { 'Clean Up Inactive Clients' }
    $preview = Wait-Until {
        [CleanupScanUiQa]::Find([uint32]$Fixture.Process.Id, '#32770', $title)
    } "Retry did not reach the '$title' preview." 15
    $list = [CleanupScanUiQa]::GetDlgItem($preview, 1201)
    $rows = [CleanupScanUiQa]::ListRows($list)
    if ($rows.Count -ne $ExpectedClients) {
        throw "Retry preview contained $($rows.Count) rows instead of ${ExpectedClients}: $($rows -join ' | ')"
    }
    foreach ($client in $Fixture.Clients) {
        if (-not ($rows | Where-Object { $_.StartsWith($client + '    |') })) {
            throw "Retry preview omitted validated client '$client'."
        }
    }
    if ($rows -match 'Unsafe Unmarked Folder') {
        throw 'The preview included an unvalidated client directory.'
    }
    Close-Window $preview $title
    Wait-MainRecovered $Fixture 'retry preview Cancel'
    Set-Ini $Fixture.Config 'qa' 'cleanup_scan_entry_delay_ms' '250'
}

function Assert-SitesUnchanged($Fixture, [string]$Before, [string]$Context) {
    $after = Get-SitesSnapshot $Fixture.Sites
    if ($after -cne $Before) { throw "$Context changed the isolated client collection." }
}

function Invoke-ManualScenario {
    $fixture = Start-Fixture (New-Fixture 'manual-marine' 'Marine' 6)
    $before = Get-SitesSnapshot $fixture.Sites
    $screen = Join-Path $artifactRoot 'manual-progress-Marine.png'
    $scan = Open-Scan $fixture $true $screen

    # Commands posted to a disabled owner are still dispatched by the modal
    # loop. They must not create a nested cleanup or launch a browser.
    Send-Command $fixture.Main 41121
    Send-Command $fixture.Main 41120
    Start-Sleep -Milliseconds 150
    if ([CleanupScanUiQa]::Count([uint32]$fixture.Process.Id, '#32770', 'Preparing Delete Multiple Clients') -ne 1 -or
        [CleanupScanUiQa]::Find([uint32]$fixture.Process.Id, '#32770', 'Preparing Inactive Cleanup') -ne [IntPtr]::Zero) {
        throw 'A nested cleanup command bypassed the cleanup busy state.'
    }
    $request = '--client "' + $fixture.Clients[0] + '" --browser edge'
    $copyResult = [CleanupScanUiQa]::CopyDataResult($fixture.Main, $request)
    if ($copyResult -ne 0) { throw "Cleanup-busy WM_COPYDATA returned $copyResult instead of explicit rejection." }
    $exactProfile = [regex]::Escape((Join-Path $fixture.Sites ($fixture.Clients[0] + '\Browsers\edge\Profile')))
    $launched = @(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine -match $exactProfile })
    if ($launched.Count) { throw 'Cleanup-busy WM_COPYDATA launched the isolated browser profile.' }

    Cancel-Scan $fixture $scan 'button'
    Assert-SitesUnchanged $fixture $before 'Manual Cancel'
    Open-And-CancelPreview $fixture $true 6
    Assert-SitesUnchanged $fixture $before 'Manual retry'

    $scan = Open-Scan $fixture $true
    Cancel-Scan $fixture $scan 'owner-close'
    Assert-True ([CleanupScanUiQa]::IsWindow($fixture.Main)) 'Main WM_CLOSE destroyed the launcher during cleanup scan.'
    Assert-SitesUnchanged $fixture $before 'Owner-close cancellation'
    Open-And-CancelPreview $fixture $true 6
    Assert-SitesUnchanged $fixture $before 'Owner-close retry'
    $results.Add([pscustomobject]@{ Scenario = 'manual'; Theme = 'Marine'; Screenshot = $screen; Passed = $true })
}

function Invoke-InactiveScenario {
    $fixture = Start-Fixture (New-Fixture 'inactive-gothic' 'Dark - Gothic' 6)
    $before = Get-SitesSnapshot $fixture.Sites
    $screen = Join-Path $artifactRoot 'inactive-progress-Gothic.png'
    $scan = Open-Scan $fixture $false $screen
    Cancel-Scan $fixture $scan 'escape'
    Assert-SitesUnchanged $fixture $before 'Inactive Escape'
    Open-And-CancelPreview $fixture $false 6
    Assert-SitesUnchanged $fixture $before 'Inactive Escape retry'

    $scan = Open-Scan $fixture $false
    Cancel-Scan $fixture $scan 'close'
    Assert-SitesUnchanged $fixture $before 'Inactive title-bar Close'
    Open-And-CancelPreview $fixture $false 6
    Assert-SitesUnchanged $fixture $before 'Inactive title-bar Close retry'
    $results.Add([pscustomobject]@{ Scenario = 'inactive'; Theme = 'Dark - Gothic'; Screenshot = $screen; Passed = $true })
}

function Invoke-FaultScenario {
    $fixture = Start-Fixture (New-Fixture 'faults' 'Dark - Gothic' 2)
    $before = Get-SitesSnapshot $fixture.Sites
    foreach ($fault in @(
            [pscustomobject]@{ Dialog = 'scan'; Scan = ''; Needle = 'Windows error' },
            [pscustomobject]@{ Dialog = ''; Scan = 'timer'; Needle = 'progress timer could not be started' },
            [pscustomobject]@{ Dialog = ''; Scan = 'worker'; Needle = 'inspection worker could not be started' })) {
        Set-Ini $fixture.Config 'qa' 'cleanup_dialog_fault' $fault.Dialog
        Set-Ini $fixture.Config 'qa' 'cleanup_scan_fault' $fault.Scan
        Send-Command $fixture.Main 41121
        $failure = Wait-Until {
            [CleanupScanUiQa]::Find([uint32]$fixture.Process.Id, '#32770', 'Cleanup Cancelled')
        } "Cleanup scan fault '$($fault.Dialog)$($fault.Scan)' was not reported." 10
        $text = Wait-DialogText $failure 'The collection could not be inspected completely.' `
            "Cleanup scan fault '$($fault.Dialog)$($fault.Scan)' did not finish visible initialization."
        foreach ($needle in @('The collection could not be inspected completely.',
                'Nothing was deleted.', $fault.Needle)) {
            if (-not $text.Contains($needle)) {
                throw "Cleanup scan fault '$($fault.Dialog)$($fault.Scan)' omitted '$needle': $text"
            }
        }
        Close-Window $failure 'Cleanup Cancelled'
        Wait-MainRecovered $fixture "fault '$($fault.Dialog)$($fault.Scan)'"
        Assert-SitesUnchanged $fixture $before "Fault '$($fault.Dialog)$($fault.Scan)'"
    }
    Set-Ini $fixture.Config 'qa' 'cleanup_dialog_fault' ''
    Set-Ini $fixture.Config 'qa' 'cleanup_scan_fault' ''
    Open-And-CancelPreview $fixture $true 2
    Assert-SitesUnchanged $fixture $before 'Fault recovery retry'
    $results.Add([pscustomobject]@{ Scenario = 'fault recovery'; Passed = $true })
}

function Invoke-EmptyCollectionScenario {
    $fixture = Start-Fixture (New-Fixture 'empty-root' 'Marine' 0 $false)
    if (Test-Path -LiteralPath $fixture.Sites) {
        $safeSites = Assert-FixturePath $fixture.Sites
        Remove-Item -LiteralPath $safeSites -Recurse -Force
    }
    Set-Ini $fixture.Config 'qa' 'cleanup_scan_entry_delay_ms' '0'
    foreach ($state in @('missing', 'empty')) {
        if ($state -eq 'empty') { New-Item -ItemType Directory -Path $fixture.Sites -Force | Out-Null }
        Send-Command $fixture.Main 41121
        $notice = Wait-Until {
            [CleanupScanUiQa]::Find([uint32]$fixture.Process.Id, '#32770', 'Delete Multiple Clients')
        } "The $state Sites collection did not report an empty manual preview." 10
        $text = Wait-DialogText $notice 'No closed, safe clients are available to delete.' `
            "The $state Sites result did not finish visible initialization."
        if (-not $text.Contains('No closed, safe clients are available to delete.')) {
            throw "Unexpected $state Sites result: $text"
        }
        Close-Window $notice "empty cleanup result ($state)"
        Wait-MainRecovered $fixture "$state collection result"
    }
    $safeSites = Assert-FixturePath $fixture.Sites
    Remove-Item -LiteralPath $safeSites -Force
    [IO.File]::WriteAllText($safeSites, 'not a directory', $utf8)
    Send-Command $fixture.Main 41121
    $failure = Wait-Until {
        [CleanupScanUiQa]::Find([uint32]$fixture.Process.Id, '#32770', 'Cleanup Cancelled')
    } 'An invalid Sites file was not rejected safely.' 10
    $failureText = Wait-DialogText $failure 'The collection could not be inspected completely.' `
        'The invalid Sites result did not finish visible initialization.'
    foreach ($needle in @('The collection could not be inspected completely.', 'Nothing was deleted.')) {
        if (-not $failureText.Contains($needle)) {
            throw "Invalid Sites result omitted '$needle': $failureText"
        }
    }
    Close-Window $failure 'invalid Sites cleanup result'
    Wait-MainRecovered $fixture 'invalid Sites result'
    if (-not (Test-Path -LiteralPath $safeSites -PathType Leaf) -or
        [IO.File]::ReadAllText($safeSites) -cne 'not a directory') {
        throw 'The invalid Sites fixture file was changed.'
    }
    $results.Add([pscustomobject]@{ Scenario = 'missing, empty, and invalid Sites'; Passed = $true })
}

if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    throw "Missing executable: $resolvedExe"
}
if (Test-Path -LiteralPath $fixtureRoot) {
    throw "Refusing to reuse an existing cleanup fixture root: $fixtureRoot"
}
New-Item -ItemType Directory -Path $artifactRoot, $fixtureRoot -Force | Out-Null

try {
    Invoke-ManualScenario
    Invoke-InactiveScenario
    Invoke-FaultScenario
    Invoke-EmptyCollectionScenario
    $testPassed = $true
} finally {
    foreach ($process in $ownedProcesses) {
        try {
            $process.Refresh()
            if (-not $process.HasExited) {
                $main = [CleanupScanUiQa]::Find([uint32]$process.Id, 'ctSpacesLauncherClass', $null)
                if ($main -ne [IntPtr]::Zero) {
                    [void][CleanupScanUiQa]::PostMessageW($main, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
                }
                if (-not $process.WaitForExit(3000)) {
                    $process.Kill()
                    [void]$process.WaitForExit(3000)
                }
            }
        } catch { Write-Warning "Could not clean an owned cleanup QA process: $($_.Exception.Message)" }
        finally { $process.Dispose() }
    }
    $liveConfigAfter = Get-FileFingerprint $liveConfig
    if ($liveConfigAfter -cne $liveConfigBefore) {
        $testPassed = $false
        throw 'The isolated cleanup scan test changed the live configuration.'
    }
    $summary = [pscustomobject]@{
        Executable = $resolvedExe
        Sha256 = (Get-FileHash -LiteralPath $resolvedExe -Algorithm SHA256).Hash
        Passed = $testPassed
        Scenarios = $results
    }
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $artifactRoot 'summary.json') -Encoding utf8
    if (-not $testPassed) { Write-Warning "Preserved failed cleanup scan fixture: $artifactRoot" }
}

Write-Host "Cleanup scan responsiveness, cancellation, isolation, fault recovery, and retry checks passed."
Write-Host "Artifacts: $artifactRoot"
