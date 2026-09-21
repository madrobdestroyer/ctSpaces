param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [ValidateSet('Dark - Gothic', 'Dark - Crimson', 'Marine')]
    [string]$ThemeName = 'Dark - Gothic'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$qaParent = Join-Path $projectRoot 'build\cleanup-qa'
$runId = [Guid]::NewGuid().ToString('N')
$qaRoot = Join-Path $qaParent ('run-' + $runId.Substring(0, 10))
$qaExeDir = Join-Path $qaRoot 'a'
$qaExe = Join-Path $qaExeDir 'q.exe'
$data = Join-Path $qaExeDir 'd'
$shortcuts = Join-Path $qaExeDir 's'
$sites = Join-Path $data 'Sites'
$launcher = $null
$completed = $false
$utf8 = [Text.UTF8Encoding]::new($false)
$liveConfig = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'
$liveHash = if (Test-Path -LiteralPath $liveConfig -PathType Leaf) {
    (Get-FileHash -LiteralPath $liveConfig).Hash
} else { 'missing' }

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class CleanupQa {
    public static string ScreenshotPath;
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] struct Point { public int x, y; }
    delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc fn, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dialog, int id);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowScrollBar(IntPtr window, int bar, bool show);
    [DllImport("user32.dll")] static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] static extern uint GetMenuItemID(IntPtr menu, int index);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetMenuStringW(IntPtr menu, uint item, StringBuilder text, int size, uint flags);
    [DllImport("user32.dll")] static extern bool GetMenuItemRect(IntPtr owner, IntPtr menu, uint item, out Rect rect);
    [DllImport("user32.dll")] static extern uint GetMenuState(IntPtr menu, uint item, uint flags);
    [DllImport("user32.dll")] static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [StructLayout(LayoutKind.Sequential)] struct MouseInput { public int x, y; public uint data, flags, time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Explicit)] struct InputUnion { [FieldOffset(0)] public MouseInput mouse; }
    [StructLayout(LayoutKind.Sequential)] struct Input { public uint type; public InputUnion value; }
    [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint count, Input[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wp, IntPtr lp);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr window, uint message, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendText(IntPtr window, uint message, IntPtr wp, string text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr window, uint message, IntPtr wp, StringBuilder text);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileIntW(string section, string key, int def, string path);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr window, IntPtr dc);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr window, uint flags);
    [DllImport("user32.dll")] static extern int MapWindowPoints(IntPtr from, IntPtr to, ref Point point, uint count);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleBitmap(IntPtr dc, int width, int height);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr item);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr item);
    [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern uint GetPixel(IntPtr dc, int x, int y);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
    public static uint PaintCheckboxCenter(IntPtr window) {
        IntPtr previous = SetThreadDpiAwarenessContext((IntPtr)(-4));
        try {
            Rect rect; GetClientRect(window, out rect);
            return PaintPixelAware(window, (int)(13 * GetDpiForWindow(window) / 192), rect.bottom / 2);
        } finally { if (previous != IntPtr.Zero) SetThreadDpiAwarenessContext(previous); }
    }
    public static uint PaintScrollArrow(IntPtr window) {
        IntPtr previous = SetThreadDpiAwarenessContext((IntPtr)(-4));
        try { Rect rect; GetClientRect(window, out rect); return PaintPixelAware(window, rect.right + 3, 3); }
        finally { if (previous != IntPtr.Zero) SetThreadDpiAwarenessContext(previous); }
    }
    public static uint PaintPixel(IntPtr window, int x, int y) {
        IntPtr previous = SetThreadDpiAwarenessContext((IntPtr)(-4));
        try { return PaintPixelAware(window, x, y); }
        finally { if (previous != IntPtr.Zero) SetThreadDpiAwarenessContext(previous); }
    }
    static uint PaintPixelAware(IntPtr window, int x, int y) {
        Rect rect; if (window == IntPtr.Zero || !GetClientRect(window, out rect)) throw new Exception("Invalid theme control");
        x = x < 0 ? rect.right + x : x; y = y < 0 ? rect.bottom + y : y;
        IntPtr root = GetAncestor(window, 2);
        if (root != window && root != IntPtr.Zero) {
            Point point = new Point { x = x, y = y };
            MapWindowPoints(window, root, ref point, 1);
            x = point.x; y = point.y; window = root; GetClientRect(window, out rect);
        }
        int width = rect.right, height = rect.bottom;
        IntPtr screen = GetDC(IntPtr.Zero), dc = CreateCompatibleDC(screen);
        IntPtr bitmap = CreateCompatibleBitmap(screen, width, height);
        ReleaseDC(IntPtr.Zero, screen);
        IntPtr old = SelectObject(dc, bitmap);
        try {
            if (!PrintWindow(window, dc, 1))
                SendMessageW(window, 0x318, dc, (IntPtr)12); // Native child WM_PRINTCLIENT.
            if (!String.IsNullOrEmpty(ScreenshotPath))
                using (var image = System.Drawing.Image.FromHbitmap(bitmap))
                    image.Save(ScreenshotPath, System.Drawing.Imaging.ImageFormat.Png);
            return GetPixel(dc, x, y);
        } finally { SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); }
    }
    public static string Text(IntPtr window) {
        var text = new StringBuilder(8192); ReadText(window, 0xD, (IntPtr)text.Capacity, text); return text.ToString();
    }
    public static int HeightDip(IntPtr window) {
        IntPtr previous = SetThreadDpiAwarenessContext((IntPtr)(-4));
        try { Rect rect; GetClientRect(window, out rect); return rect.bottom * 96 / (int)GetDpiForWindow(window); }
        finally { if (previous != IntPtr.Zero) SetThreadDpiAwarenessContext(previous); }
    }
    public static string ListText(IntPtr list, int index) {
        int length = SendMessageW(list, 0x18A, (IntPtr)index, IntPtr.Zero).ToInt32();
        if (length < 0) throw new Exception("List row unavailable");
        IntPtr text = Marshal.AllocHGlobal((length + 1) * 2);
        try {
            if (SendMessageW(list, 0x189, (IntPtr)index, text).ToInt64() < 0)
                throw new Exception("List row read failed");
            return Marshal.PtrToStringUni(text);
        } finally { Marshal.FreeHGlobal(text); }
    }
    public static string MenuLabels(IntPtr popup) {
        IntPtr menu = SendMessageW(popup, 0x1E1, IntPtr.Zero, IntPtr.Zero);
        var labels = new System.Collections.Generic.List<string>();
        for (uint i = 0; i < GetMenuItemCount(menu); ++i) {
            var text = new StringBuilder(256); GetMenuStringW(menu, i, text, text.Capacity, 0x400);
            if (text.Length > 0) labels.Add(text.ToString());
        }
        return String.Join("|", labels);
    }
    public static void ChooseMenu(IntPtr popup, uint command) {
        IntPtr previous = SetThreadDpiAwarenessContext((IntPtr)(-4));
        try {
            IntPtr menu = SendMessageW(popup, 0x1E1, IntPtr.Zero, IntPtr.Zero);
            for (uint i = 0; i < GetMenuItemCount(menu); ++i) {
                if (GetMenuItemID(menu, (int)i) != command) continue;
                if ((GetMenuState(menu, i, 0x400) & 3) != 0) throw new Exception("Menu action disabled");
                Rect rect; if (!GetMenuItemRect(IntPtr.Zero, menu, i, out rect)) throw new Exception("Missing menu rectangle");
                Point old; GetCursorPos(out old);
                try {
                    Point point = new Point { x=(rect.left+rect.right)/2, y=(rect.top+rect.bottom)/2 };
                    SetCursorPos(point.x, point.y);
                    var inputs = new Input[2];
                    inputs[0].value.mouse.flags = 2;
                    inputs[1].value.mouse.flags = 4;
                    if (SendInput(2, inputs, Marshal.SizeOf(typeof(Input))) != 2)
                        throw new Exception("QA menu click injection failed");
                    System.Threading.Thread.Sleep(100);
                } finally { SetCursorPos(old.x, old.y); }
                return;
            }
            throw new Exception("Menu action missing");
        } finally { if (previous != IntPtr.Zero) SetThreadDpiAwarenessContext(previous); }
    }
    public static IntPtr Find(uint pid, string cls, string title) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, unused) => {
            uint id; GetWindowThreadProcessId(window, out id);
            var text = new StringBuilder(256); GetClassNameW(window, text, text.Capacity);
            if (id == pid && text.ToString() == cls && (String.IsNullOrEmpty(title) || Text(window) == title)) {
                found = window; return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@ -ReferencedAssemblies System.Drawing
[CleanupQa]::ScreenshotPath = Join-Path $projectRoot ('build\cleanup-theme-' + $runId.Substring(0, 10) + '.png')
function Wait-Until([scriptblock]$Check, [string]$Description) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $result = & $Check
        if ($result -is [IntPtr]) {
            if ($result -ne [IntPtr]::Zero) { return $result }
        } elseif ($result) { return $result }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Description
}
function Get-CleanupDialogDiagnostics([IntPtr]$Dialog, [string]$Label) {
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("[$Label]")
    if ($Dialog -eq [IntPtr]::Zero) {
        $lines.Add('dialog=zero')
    } else {
        $lines.Add("dialog=$Dialog visible=$([CleanupQa]::IsWindowVisible($Dialog)) title='$([CleanupQa]::Text($Dialog))'")
        foreach ($id in @(1200, 1201, 1202, 1203, 1204, 1205, 1206, 1, 2)) {
            $control = [CleanupQa]::GetDlgItem($Dialog, $id)
            if ($control -eq [IntPtr]::Zero) { continue }
            $text = ''
            try { $text = [CleanupQa]::Text($control) } catch { $text = '<text-read-failed>' }
            $enabled = [CleanupQa]::IsWindowEnabled($control)
            $visible = [CleanupQa]::IsWindowVisible($control)
            $lines.Add("control[$id]=$control visible=$visible enabled=$enabled text='$text'")
            if ($id -eq 1201) {
                try {
                    $count = [CleanupQa]::SendMessageW($control, 0x18B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
                    $rows = @()
                    for ($index = 0; $index -lt [Math]::Max(0, $count); ++$index) {
                        $rows += [CleanupQa]::ListText($control, $index)
                    }
                    $lines.Add("list.count=$count rows='$($rows -join ' || ')'")
                } catch { $lines.Add("list.read-error=$($_.Exception.Message)") }
            }
        }
    }
    $browserProcesses = @(Get-Process -Name msedge,chrome,brave,firefox -ErrorAction SilentlyContinue |
        ForEach-Object { "$($_.ProcessName):pid=$($_.Id)" })
    $lines.Add("browser-processes=$($browserProcesses -join ' | ')")
    return ($lines -join [Environment]::NewLine)
}
function Wait-CleanupList([IntPtr]$Dialog, [int]$ExpectedCount, [string]$Description) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    $last = ''
    do {
        if ([CleanupQa]::IsWindowVisible($Dialog)) {
            $list = [CleanupQa]::GetDlgItem($Dialog, 1201)
            $heading = [CleanupQa]::GetDlgItem($Dialog, 1202)
            if ($list -ne [IntPtr]::Zero -and $heading -ne [IntPtr]::Zero -and
                [CleanupQa]::IsWindowVisible($list) -and
                [CleanupQa]::Text($heading).Length -gt 0) {
                $count = [CleanupQa]::SendMessageW($list, 0x18B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
                if ($count -eq $ExpectedCount) { return $list }
            }
            $last = Get-CleanupDialogDiagnostics $Dialog $Description
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    $diagnosticPath = Join-Path $qaRoot ($Description.Replace(' ', '-') + '.log')
    [IO.File]::WriteAllText($diagnosticPath, $last, $utf8)
    throw "$Description; expected rows=$ExpectedCount. Diagnostics: $diagnosticPath`n$last"
}
function Write-Activity([string]$Name, [DateTime]$Date) {
    [IO.File]::WriteAllText((Join-Path $sites "$Name\ctSpaces-client-activity"),
        "ctSpaces-activity=1`r`nopened=$($Date.ToFileTimeUtc())`r`n", $utf8)
}
function Make-Client([string]$Name) {
    $root = Join-Path $sites $Name
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $root 'ctSpaces-client-v2'),
        "ctSpaces-client-schema=2`r`n", $utf8)
    foreach ($browser in @('edge', 'chrome', 'firefox')) {
        $slot = Join-Path $root "Browsers\$browser"
        $profile = Join-Path $slot 'Profile'
        New-Item -ItemType Directory -Path (Join-Path $profile 'Default') -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $slot 'ctSpaces-browser-v2'),
            "ctSpaces-browser-schema=2`r`nbrowser=$browser`r`n", $utf8)
        if ($browser -ne 'firefox') {
            [IO.File]::WriteAllText((Join-Path $profile 'ctSpaces'),
                "ctSpaces-profile=2`r`n", $utf8)
            [IO.File]::WriteAllText((Join-Path $profile 'Default\Preferences'), '{}', $utf8)
        }
        [IO.File]::WriteAllText((Join-Path $profile 'browser-data-sentinel'), $browser, $utf8)
    }
    [IO.File]::WriteAllText((Join-Path $root 'client-icon-sentinel'), 'icon', $utf8)
}
function Command([IntPtr]$Window, [int]$Id) {
    [void][CleanupQa]::PostMessageW($Window, 0x111, [UIntPtr]::new([uint32]$Id), [CleanupQa]::GetDlgItem($Window, $Id))
}
function Close-Summary {
    Wait-Until {
        $dialog = [CleanupQa]::Find([uint32]$launcher.Id, '#32770', 'Cleanup Complete')
        if ($dialog -eq [IntPtr]::Zero) { return $true }
        # A one-button result dialog can be dismissed with its close action;
        # BM_CLICK is unreliable when the hidden QA owner is not foreground.
        [void][CleanupQa]::PostMessageW($dialog, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
        return $false
    } 'Summary did not close' | Out-Null
}
function Assert-Themed([IntPtr]$Dialog, [bool]$HasList, [bool]$HasDetails = $true) {
    [CleanupQa]::ScreenshotPath = Join-Path $projectRoot (
        'build\cleanup-theme-' + $runId.Substring(0, 10) +
        $(if ($HasList) { '-preview.png' } else { '-result.png' }))
    $palette = switch ($ThemeName) {
        'Dark - Gothic' { @( @(26,25,27), @(32,30,33), @(135,50,72) ) }
        'Dark - Crimson' { @( @(20,20,21), @(27,26,27), @(174,35,52) ) }
        'Marine' { @( @(136,192,184), @(200,224,216), @(0,0,128) ) }
    }
    $colors = @($palette | ForEach-Object { [uint32]($_[0] + ($_[1] -shl 8) + ($_[2] -shl 16)) })
    Wait-Until {
        [CleanupQa]::IsWindowVisible($Dialog) -and
            [CleanupQa]::PaintPixel($Dialog, 3, 3) -eq $colors[0]
    } 'Dialog background does not match selected theme after initialization' | Out-Null
    if ($HasList) {
        $list = [CleanupQa]::GetDlgItem($Dialog, 1201)
        $listColor = [CleanupQa]::PaintPixel($list, -25, -25)
        if ($listColor -ne $colors[1]) { throw "Client-list background does not match selected theme: $listColor instead of $($colors[1])" }
        $selectedColor = [CleanupQa]::PaintPixel($list, -25, 10)
        if ($selectedColor -ne $colors[2]) { throw "Selected client row does not match theme accent: $selectedColor instead of $($colors[2])" }
        $checkboxColor = [CleanupQa]::PaintCheckboxCenter([CleanupQa]::GetDlgItem($Dialog, 1203))
        if ($checkboxColor -ne $colors[1]) {
            throw "Confirmation checkbox does not match selected theme: $checkboxColor instead of $($colors[1])"
        }
        foreach ($buttonId in @(1, 2, 1204, 1205)) {
            # Owner-drawn buttons are painted by the shared app theme renderer.
            $button = [CleanupQa]::GetDlgItem($Dialog, $buttonId)
            if ([CleanupQa]::PaintPixel($button, 3, 3) -eq 0xFFFFFF) { throw 'Deletion button still paints white' }
        }
    } elseif ($HasDetails) {
        if ([CleanupQa]::PaintPixel([CleanupQa]::GetDlgItem($Dialog, 1206), -25, -25) -ne $colors[1]) {
            throw 'Cleanup result text background does not match selected theme'
        }
    }
}
function Open-EditMenu([IntPtr]$Edit, [string]$Expected) {
    [void][CleanupQa]::PostMessageW($Edit, 0x7B, [UIntPtr]::Zero, [IntPtr](-1))
    $popup = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32768', $null) } 'Text context menu not shown'
    Wait-Until { [CleanupQa]::MenuLabels($popup) -eq $Expected } 'Unexpected text menu entries' | Out-Null
    $color = [CleanupQa]::PaintPixel($popup, -8, -8)
    if ($ThemeName.StartsWith('Dark') -and ($color -band 255) -gt 180 -and
        (($color -shr 8) -band 255) -gt 180 -and (($color -shr 16) -band 255) -gt 180) {
        throw "Text context menu still paints bright in dark mode: $color"
    }
    return $popup
}
function Wait-EditMenuClosed {
    Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32768', $null) -eq [IntPtr]::Zero } 'Text menu did not close' | Out-Null
}
try {
    New-Item -ItemType Directory -Path $qaExeDir, $shortcuts, $sites -Force | Out-Null
    Copy-Item -LiteralPath ([IO.Path]::GetFullPath($ExePath)) -Destination $qaExe
    [IO.File]::WriteAllText((Join-Path $qaExeDir 'ctSpaces.portable'), '', $utf8)
    foreach ($name in @('OldClient', 'OldArchived', 'ChangedAfterPreview', 'Recent',
                        'Unknown', 'Corrupt', 'Future', 'ActiveClient')) { Make-Client $name }
    $old = [DateTime]::UtcNow.AddMonths(-4)
    foreach ($name in @('OldClient', 'OldArchived', 'ChangedAfterPreview')) { Write-Activity $name $old }
    Write-Activity 'Recent' ([DateTime]::UtcNow.AddMonths(-1))
    Write-Activity 'Future' ([DateTime]::UtcNow.AddMonths(1))
    [IO.File]::WriteAllText((Join-Path $sites 'Corrupt\ctSpaces-client-activity'), 'corrupt', $utf8)
    New-Item -ItemType Directory -Path (Join-Path $sites 'UnsafeLayout'),
        (Join-Path $sites 'Default'), (Join-Path $data 'Default'), (Join-Path $data 'Temp') -Force | Out-Null
    Write-Activity 'UnsafeLayout' $old
    Write-Activity 'Default' $old
    [IO.File]::WriteAllText((Join-Path $data 'Default\keep'), 'default-data', $utf8)
    [IO.File]::WriteAllText((Join-Path $data 'Temp\keep'), 'temp-data', $utf8)
    $config = Join-Path $data 'config.ini'
    [IO.File]::WriteAllText($config,
        "[user]`r`nbrowser=edge`r`ntheme_name=$ThemeName`r`n" +
        "[archived]`r`ncount=1`r`nclient0=OldArchived`r`n" +
        "[pinned]`r`ncount=1`r`nclient0=OldClient`r`n" +
        "[restore_tabs]`r`ndisabled_count=1`r`ndisabled_client0=OldClient|edge`r`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $shortcuts 'unrelated.txt'), 'keep', $utf8)
    $launcher = Start-Process -FilePath $qaExe -WindowStyle Hidden -PassThru -ArgumentList (
        '--qa-instance=' + $runId.Substring(0, 10) +
        ' --qa-data-dir="' + $data + '" --qa-shortcut-dir="' + $shortcuts + '"')
    $main = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, 'ctSpacesLauncherClass', $null) } 'Launcher not ready'
    Wait-Until { Test-Path -LiteralPath (Join-Path $sites 'Unknown\ctSpaces-client-activity') } 'Baseline not initialized' | Out-Null
    $clientEdit = [CleanupQa]::GetDlgItem($main, 206)
    [void][CleanupQa]::SendText($clientEdit, 0xC, [IntPtr]::Zero, 'ContextMenuTest')
    [void][CleanupQa]::SendMessageW($clientEdit, 0xB1, [IntPtr]::Zero, [IntPtr]::Zero)
    $popup = Open-EditMenu $clientEdit 'Undo|Cut|Copy|Paste|Delete|Select All'
    [CleanupQa]::ChooseMenu($popup, 6)
    Wait-EditMenuClosed
    Wait-Until { [CleanupQa]::SendMessageW($clientEdit, 0xB0, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq (15 -shl 16) } 'Text menu Select All did not select client name' | Out-Null
    $popup = Open-EditMenu $clientEdit 'Undo|Cut|Copy|Paste|Delete|Select All'
    [CleanupQa]::ChooseMenu($popup, 5)
    Wait-EditMenuClosed
    Wait-Until { [CleanupQa]::Text($clientEdit) -eq '' } 'Text menu Delete failed' | Out-Null
    $popup = Open-EditMenu $clientEdit 'Undo|Cut|Copy|Paste|Delete|Select All'
    [CleanupQa]::ChooseMenu($popup, 1)
    Wait-EditMenuClosed
    Wait-Until { [CleanupQa]::Text($clientEdit) -eq 'ContextMenuTest' } 'Text menu Undo failed' | Out-Null
    Command $main 104
    $about = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', 'About') } 'About not shown'
    Wait-Until { [CleanupQa]::Text([CleanupQa]::GetDlgItem($about, 1005)).Contains(
        'Cameron Kincer (Client Cleanup Idea)') } 'Cleanup idea credit missing' | Out-Null
    Assert-Themed $about $false $false
    if ([CleanupQa]::PaintPixel([CleanupQa]::GetDlgItem($about, 1005), -3, -3) -ne
        [CleanupQa]::PaintPixel($about, 3, 3)) { throw 'About credit background is not themed' }
    if ([CleanupQa]::PaintPixel([CleanupQa]::GetDlgItem($about, 1004), -3, -3) -ne
        [CleanupQa]::PaintPixel($about, 3, 3)) { throw 'About link background is not themed' }
    Command $about 1
    Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', 'About') -eq [IntPtr]::Zero } 'About did not close' | Out-Null
    [void][CleanupQa]::SendText([CleanupQa]::GetDlgItem($main, 206), 0xC, [IntPtr]::Zero, 'OldClient')
    $shortcutCreated = [CleanupQa]::SendMessageW($main, (0x8000 + 12), [IntPtr]::Zero, [IntPtr]::Zero)
    if ($shortcutCreated -eq [IntPtr]::Zero) { throw 'Managed shortcut command failed' }
    if (-not (Test-Path -LiteralPath (Join-Path $shortcuts 'OldClient.lnk'))) {
        throw 'Application did not create managed shortcut'
    }
    # A genuine browser launch must durably record opened=, not baseline=.
    [void][CleanupQa]::SendText([CleanupQa]::GetDlgItem($main, 206), 0xC, [IntPtr]::Zero, 'ActiveClient')
    Command $main 1
    Wait-Until {
        $errorDialog = [CleanupQa]::Find([uint32]$launcher.Id, '#32770', $null)
        if ($errorDialog -ne [IntPtr]::Zero) {
            throw ([CleanupQa]::Text($errorDialog) + ': ' +
                [CleanupQa]::Text([CleanupQa]::GetDlgItem($errorDialog, 65535)))
        }
        $path = Join-Path $sites 'ActiveClient\ctSpaces-client-activity'
        try { (Test-Path -LiteralPath $path) -and ([IO.File]::ReadAllText($path).Contains('opened=')) }
        catch [IO.IOException] { $false }
    } 'Browser open did not record client activity' | Out-Null
    # Deliberately age the marker while this browser is still open: process-use
    # protection must override an apparently stale activity record.
    Wait-Until { [CleanupQa]::IsWindowEnabled([CleanupQa]::GetDlgItem($main, 1)) } 'Launch not finished' | Out-Null
    # Avoid racing the safety probe against this disposable browser's startup.
    Start-Sleep -Seconds 5
    Write-Activity 'ActiveClient' $old
    $previewTitle = 'Clean Up Inactive Clients'
    Command $main 41120
    $preview = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', $previewTitle) } 'Preview not shown'
    $list = Wait-CleanupList $preview 3 'Preview rows not ready'
    $count = [CleanupQa]::SendMessageW($list, 0x18B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($count -ne 3) { throw "Expected exactly three stale eligible clients, found $count" }
    Assert-Themed $preview $true
    if ([CleanupQa]::IsWindowEnabled([CleanupQa]::GetDlgItem($preview, 1))) { throw 'Delete initially enabled' }
    Command $preview 1
    Start-Sleep -Milliseconds 150
    if ([CleanupQa]::Find([uint32]$launcher.Id, '#32770', $previewTitle) -eq [IntPtr]::Zero) {
        throw 'Deletion proceeded without acknowledgement'
    }
    Command $preview 2
    Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', $previewTitle) -eq [IntPtr]::Zero } 'Cancel failed' | Out-Null
    foreach ($name in @('OldClient', 'OldArchived', 'ChangedAfterPreview')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sites $name))) { throw 'Cancel deleted a client' }
    }
    Command $main 41120
    $preview = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', $previewTitle) } 'Second preview not shown'
    $list = Wait-CleanupList $preview 3 'Second preview rows not ready'
    Write-Activity 'ChangedAfterPreview' ([DateTime]::UtcNow)
    [void][CleanupQa]::SendMessageW([CleanupQa]::GetDlgItem($preview, 1203), 0xF1, [IntPtr]1, [IntPtr]::Zero)
    Command $preview 1203
    Wait-Until {
        [CleanupQa]::IsWindowEnabled([CleanupQa]::GetDlgItem($preview, 1))
    } 'Delete not enabled after acknowledgement' | Out-Null
    Command $preview 1
    $summary = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', 'Cleanup Complete') } 'Cleanup did not finish'
    Wait-Until { [CleanupQa]::GetDlgItem($summary, 1206) -ne [IntPtr]::Zero } 'Result text not ready' | Out-Null
    Assert-Themed $summary $false
    $popup = Open-EditMenu ([CleanupQa]::GetDlgItem($summary, 1206)) 'Copy|Select All'
    [CleanupQa]::ChooseMenu($popup, 6)
    Wait-EditMenuClosed
    [void][CleanupQa]::SendMessageW([CleanupQa]::GetDlgItem($summary, 1206), 0xB1, [IntPtr]::Zero, [IntPtr]::Zero)
    if ($ThemeName.StartsWith('Dark')) {
        $details = [CleanupQa]::GetDlgItem($summary, 1206)
        [void][CleanupQa]::ShowScrollBar($details, 1, $true)
        $scrollColor = [CleanupQa]::PaintScrollArrow($details)
        if (($scrollColor -band 255) -gt 180 -and (($scrollColor -shr 8) -band 255) -gt 180 -and
            (($scrollColor -shr 16) -band 255) -gt 180) { throw "Required scrollbar still paints bright in dark mode: $scrollColor" }
        [void][CleanupQa]::ShowScrollBar($details, 1, $false)
    }
    if ([CleanupQa]::SendMessageW([CleanupQa]::GetDlgItem($summary, 1206), 0xB0,
        [IntPtr]::Zero, [IntPtr]::Zero) -ne [IntPtr]::Zero) { throw 'Result details were automatically selected' }
    foreach ($name in @('OldClient', 'OldArchived')) {
        if (Test-Path -LiteralPath (Join-Path $sites $name)) { throw "Eligible client not entirely deleted: $name" }
    }
    foreach ($name in @('ChangedAfterPreview', 'Recent', 'Unknown', 'Corrupt', 'Future', 'ActiveClient', 'UnsafeLayout', 'Default')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sites $name))) { throw "Protected client was deleted: $name" }
    }
    if (Test-Path -LiteralPath (Join-Path $shortcuts 'OldClient.lnk')) { throw 'Managed shortcut retained' }
    if (-not (Test-Path -LiteralPath (Join-Path $shortcuts 'unrelated.txt'))) { throw 'Unrelated shortcut-directory data deleted' }
    foreach ($relative in @('Default\keep', 'Temp\keep')) {
        if (-not (Test-Path -LiteralPath (Join-Path $data $relative))) { throw 'Default/Temp data changed' }
    }
    foreach ($setting in @(@('archived','count'), @('pinned','count'), @('restore_tabs','disabled_count'))) {
        if ([CleanupQa]::GetPrivateProfileIntW($setting[0], $setting[1], -1, $config) -ne 0) {
            throw "Deleted-client metadata retained: $($setting[0])"
        }
    }
    Close-Summary
    # Manual deletion has no inactivity gate, but still excludes active/unsafe
    # clients. Recent, baseline, corrupt-history and future-history are allowed.
    Command $main 41121
    $manualTitle = 'Delete Multiple Clients'
    $manual = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', $manualTitle) } 'Manual selector not shown'
    $manualList = Wait-CleanupList $manual 5 'Manual selection rows not ready'
    $manualCount = [CleanupQa]::SendMessageW($manualList, 0x18B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($manualCount -ne 5) { throw "Expected five safe closed clients for manual deletion; got $manualCount" }
    if ([CleanupQa]::SendMessageW($manualList, 0x190, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne 0) {
        throw 'Manual deletion should start with no selection'
    }
    Command $manual 2
    Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', $manualTitle) -eq [IntPtr]::Zero } 'Manual cancel failed' | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $sites 'Recent'))) { throw 'Manual cancel deleted data' }
    Command $main 41121
    $manual = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', $manualTitle) } 'Second manual selector not shown'
    $manualList = Wait-CleanupList $manual 5 'Second manual selection rows not ready'
    [void][CleanupQa]::SendMessageW([CleanupQa]::GetDlgItem($manual, 1203), 0xF1, [IntPtr]1, [IntPtr]::Zero)
    [void][CleanupQa]::SendMessageW($manual, 0x111, [IntPtr]1203, [IntPtr]::Zero)
    if ([CleanupQa]::IsWindowEnabled([CleanupQa]::GetDlgItem($manual, 1))) { throw 'Acknowledgement without selection enabled deletion' }
    [void][CleanupQa]::SendMessageW($manual, 0x111, [IntPtr]1204, [IntPtr]::Zero)
    if ([CleanupQa]::SendMessageW($manualList, 0x190, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32() -ne 5) {
        throw 'Select All did not select every listed client'
    }
    [void][CleanupQa]::SendMessageW($manual, 0x111, [IntPtr]1205, [IntPtr]::Zero)
    if ([CleanupQa]::IsWindowEnabled([CleanupQa]::GetDlgItem($manual, 1))) { throw 'Clear Selection did not disable deletion' }
    for ($index = 0; $index -lt $manualCount; ++$index) {
        $row = [CleanupQa]::ListText($manualList, $index)
        if ($row.StartsWith('Recent    |') -or $row.StartsWith('Unknown    |')) {
            [void][CleanupQa]::SendMessageW($manualList, 0x185, [IntPtr]1, [IntPtr]$index)
        }
    }
    [void][CleanupQa]::SendMessageW($manual, 0x111, [IntPtr](0x10000 + 1201), [IntPtr]::Zero)
    if ([CleanupQa]::Text([CleanupQa]::GetDlgItem($manual, 1)) -ne 'Delete Selected (2)') {
        throw 'Selected deletion count incorrect'
    }
    Command $manual 1
    $summary = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', 'Cleanup Complete') } 'Manual cleanup did not finish'
    Wait-Until { [CleanupQa]::Text([CleanupQa]::GetDlgItem($summary, 1206)).Length -gt 0 } 'Manual summary not initialized' | Out-Null
    foreach ($name in @('Recent', 'Unknown')) {
        if (Test-Path -LiteralPath (Join-Path $sites $name)) { throw "Selected recent client was not deleted: $name. $([CleanupQa]::Text([CleanupQa]::GetDlgItem($summary, 1206)))" }
    }
    foreach ($name in @('ChangedAfterPreview', 'Corrupt', 'Future', 'ActiveClient', 'UnsafeLayout', 'Default')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sites $name))) { throw "Unselected/protected client deleted: $name" }
    }
    Close-Summary
    Command $main 41120
    $empty = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', 'Inactive Clients') } 'Nothing-to-clean message not shown'
    Assert-Themed $empty $false $false
    if ([CleanupQa]::HeightDip($empty) -gt 200) { throw 'Nothing-to-clean message is unnecessarily tall' }
    if ([CleanupQa]::GetDlgItem($empty, 1206) -ne [IntPtr]::Zero) { throw 'Nothing-to-clean message still uses a selectable details panel' }
    if ([CleanupQa]::PaintPixel($empty, -3, -3) -ne [CleanupQa]::PaintPixel($empty, 3, 3)) {
        throw 'Message-box bottom band is not themed'
    }
    [void][CleanupQa]::PostMessageW($empty, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', 'Inactive Clients') -eq [IntPtr]::Zero } 'Nothing-to-clean message did not close' | Out-Null
    [void][CleanupQa]::PostMessageW($main, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    $exit = Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', 'Exit ctSpaces') } 'Exit confirmation not shown'
    Assert-Themed $exit $false $false
    Command $exit 7
    Wait-Until { [CleanupQa]::Find([uint32]$launcher.Id, '#32770', 'Exit ctSpaces') -eq [IntPtr]::Zero } 'Themed No button did not cancel exit' | Out-Null
    if ($launcher.HasExited) { throw 'No incorrectly accepted exit' }
    [void][CleanupQa]::PostMessageW($main, 0x10, [UIntPtr]::Zero, [IntPtr]::Zero)
    Wait-Until {
        $confirm = [CleanupQa]::Find([uint32]$launcher.Id, '#32770', $null)
        if ($confirm -ne [IntPtr]::Zero) {
            $yes = [CleanupQa]::GetDlgItem($confirm, 6)
            if ($yes -ne [IntPtr]::Zero) {
                [void][CleanupQa]::PostMessageW($confirm, 0x111,
                    [UIntPtr]::new([uint32]6), $yes)
            }
        }
        $launcher.Refresh()
        $launcher.HasExited
    } 'QA launcher did not close' | Out-Null
    $after = if (Test-Path -LiteralPath $liveConfig -PathType Leaf) {
        (Get-FileHash -LiteralPath $liveConfig).Hash
    } else { 'missing' }
    if ($after -ne $liveHash) { throw 'Live configuration changed' }
    $completed = $true
    [pscustomobject]@{ WholeClientDeletion=$true; ArchivedIncluded=$true;
        ActiveProtected=$true; ActivityRecordedOnOpen=$true; PreviewCancel=$true;
        ReopenedAfterPreviewProtected=$true; UnknownBaseline=$true;
        RecentFutureCorruptUnsafeProtected=$true; DefaultTempProtected=$true;
        ManagedShortcutRemoved=$true; MetadataPruned=$true; LiveConfigUnchanged=$true;
        CameronKincerCredit=$true; ManualNoTimerDeletion=$true;
        MultiSelectionControls=$true; ManualCancel=$true; UnselectedClientsPreserved=$true;
        SelectedTheme=$ThemeName; ThemePaletteRendering=$true; AboutThemed=$true;
        CompactEmptyMessage=$true; MessageBoxesThemed=$true; ThemedNoCancelsExit=$true;
        TextMenuThemedAndSimplified=$true; TextMenuSelectDeleteUndo=$true; ReadOnlyMenu=$true }
} finally {
    if ($launcher -and -not $launcher.HasExited) {
        # Restrict emergency process cleanup to this exact QA executable tree.
        & taskkill.exe /PID $launcher.Id /T /F | Out-Null
    }
    if (-not $completed -and (Test-Path -LiteralPath $qaRoot)) {
        $failureRoot = Join-Path $projectRoot ('build\cleanup-failures\' + $runId.Substring(0, 10))
        New-Item -ItemType Directory -Path (Split-Path -Parent $failureRoot) -Force | Out-Null
        Copy-Item -LiteralPath $qaRoot -Destination $failureRoot -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Preserved cleanup QA failure fixture: $failureRoot"
    }
    $target = [IO.Path]::GetFullPath($qaRoot)
    $prefix = [IO.Path]::GetFullPath($qaParent).TrimEnd('\') + '\'
    if ($target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($target).StartsWith('run-') -and
        (Test-Path -LiteralPath $target)) {
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            try {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction Stop
                break
            } catch {
                if ([DateTime]::UtcNow -ge $deadline) { throw }
                Start-Sleep -Milliseconds 300
            }
        } while (Test-Path -LiteralPath $target)
    }
}
