param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [ValidateRange(0, 25)]
    [int]$ManualIterations = 0,
    [string]$AutoFetchDomain = '',
    [string]$TaskbarScreenshotPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$iconBeforePath = Join-Path $projectRoot 'icons\color_b.ico'
$iconAfterPath = Join-Path $projectRoot 'icons\delete_b.ico'
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 10)
$qaDir = Join-Path $projectRoot ('.qa-icon-refresh-' + $runId)
$testName = '__ctSpacesIconQA_' + $runId
$dataRoot = Join-Path $qaDir 'data'
$sitesRoot = Join-Path $dataRoot 'Sites'
$profilePath = Join-Path $sitesRoot $testName
$launcherProcess = $null
$launcherWindow = [IntPtr]::Zero
$browserWindow = [IntPtr]::Zero
$fileDialog = [IntPtr]::Zero
$inputDialog = [IntPtr]::Zero
$successDialog = [IntPtr]::Zero
$oldDpiContext = [IntPtr]::Zero
$versionHeader = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'version.h')
$displayVersionMatch = [regex]::Match(
    $versionHeader,
    '#define\s+CTSPACES_DISPLAY_VERSION_TEXT\s+"(?<version>\d+\.\d+)"'
)
if (-not $displayVersionMatch.Success) {
    throw 'version.h does not define the ctSpaces display version.'
}
$expectedLauncherTitle = 'ctSpaces v' + $displayVersionMatch.Groups['version'].Value

if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    throw "Release executable is missing: $resolvedExe"
}
$edgeCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe'),
    (Join-Path $env:LOCALAPPDATA 'Microsoft\Edge\Application\msedge.exe')
)
$resolvedBrowser = $edgeCandidates | Where-Object {
    $_ -and (Test-Path -LiteralPath $_ -PathType Leaf)
} | ForEach-Object {
    [IO.Path]::GetFullPath($_)
} | Select-Object -First 1
if (-not $resolvedBrowser) {
    throw 'Microsoft Edge is required for the live icon QA.'
}
if ($ManualIterations -gt 0 -and
    (-not (Test-Path -LiteralPath $iconBeforePath -PathType Leaf) -or
     -not (Test-Path -LiteralPath $iconAfterPath -PathType Leaf))) {
    throw 'The two QA icon files are missing.'
}
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class CtIconQaNative {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct WindowRect {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    private struct PropertyKey {
        public Guid FormatId;
        public uint PropertyId;

        public PropertyKey(Guid formatId, uint propertyId) {
            FormatId = formatId;
            PropertyId = propertyId;
        }
    }

    [StructLayout(LayoutKind.Explicit, Size = 24)]
    private struct PropVariant {
        [FieldOffset(0)] public ushort VarType;
        [FieldOffset(8)] public IntPtr PointerValue;
    }

    [ComImport]
    [Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IPropertyStore {
        [PreserveSig] int GetCount(out uint propertyCount);
        [PreserveSig] int GetAt(uint propertyIndex, out PropertyKey key);
        [PreserveSig] int GetValue(ref PropertyKey key, out PropVariant value);
        [PreserveSig] int SetValue(ref PropertyKey key, ref PropVariant value);
        [PreserveSig] int Commit();
    }

    [DllImport("shell32.dll", PreserveSig = true)]
    private static extern int SHGetPropertyStoreForWindow(
        IntPtr hWnd, ref Guid interfaceId,
        [MarshalAs(UnmanagedType.Interface)] out IPropertyStore propertyStore);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode,
               PreserveSig = true)]
    private static extern int SHGetPropertyStoreFromParsingName(
        string path, IntPtr bindContext, uint flags, ref Guid interfaceId,
        [MarshalAs(UnmanagedType.Interface)] out IPropertyStore propertyStore);

    [DllImport("propsys.dll", CharSet = CharSet.Unicode,
               PreserveSig = true)]
    private static extern int PropVariantToStringAlloc(
        ref PropVariant value, out IntPtr result);

    [DllImport("ole32.dll", PreserveSig = true)]
    private static extern int PropVariantClear(ref PropVariant value);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hWnd, int id);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageW(IntPtr hWnd, uint msg,
                                             IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode,
               EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessageStringW(IntPtr hWnd, uint msg,
                                                   IntPtr wParam,
                                                   string lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode,
               EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessageTextW(IntPtr hWnd, uint msg,
                                                 IntPtr wParam,
                                                 StringBuilder lParam);

    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr hWnd, uint msg,
                                             IntPtr wParam, IntPtr lParam);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CommandLineToArgvW(string commandLine,
                                                     out int argumentCount);

    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback,
                                           IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr parent,
                                                EnumWindowsProc callback,
                                                IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsWindowEnabled(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool SetWindowTextW(IntPtr hWnd, string text);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowTextLengthW(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowTextW(IntPtr hWnd, StringBuilder text,
                                            int maxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hWnd, StringBuilder text,
                                           int maxCount);

    [DllImport("user32.dll")]
    public static extern int GetDlgCtrlID(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd,
                                                       out uint processId);

    [DllImport("user32.dll")]
    public static extern IntPtr CopyIcon(IntPtr hIcon);

    [DllImport("user32.dll")]
    public static extern bool DestroyIcon(IntPtr hIcon);

    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowW(string className,
                                             string windowName);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr hWnd,
                                             out WindowRect rect);

    public static WindowRect GetTaskbarRect() {
        var taskbar = FindWindowW("Shell_TrayWnd", null);
        WindowRect rect;
        if (taskbar == IntPtr.Zero || !GetWindowRect(taskbar, out rect)) {
            throw new InvalidOperationException(
                "The Windows taskbar rectangle could not be read."
            );
        }
        return rect;
    }

    public static string GetWindowPropertyString(IntPtr hWnd,
                                                  uint propertyId) {
        var interfaceId = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
        IPropertyStore propertyStore = null;
        var result = SHGetPropertyStoreForWindow(
            hWnd, ref interfaceId, out propertyStore
        );
        if (result < 0 || propertyStore == null) return String.Empty;

        var key = new PropertyKey(
            new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"), propertyId
        );
        var value = new PropVariant();
        try {
            result = propertyStore.GetValue(ref key, out value);
            if (result < 0) return String.Empty;

            IntPtr textPointer;
            result = PropVariantToStringAlloc(ref value, out textPointer);
            if (result < 0 || textPointer == IntPtr.Zero) return String.Empty;
            try {
                return Marshal.PtrToStringUni(textPointer) ?? String.Empty;
            } finally {
                Marshal.FreeCoTaskMem(textPointer);
            }
        } finally {
            PropVariantClear(ref value);
            Marshal.FinalReleaseComObject(propertyStore);
        }
    }

    public static void SetShortcutAppId(string shortcutPath, string appId) {
        var interfaceId = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
        IPropertyStore propertyStore = null;
        var result = SHGetPropertyStoreFromParsingName(
            shortcutPath, IntPtr.Zero, 0x2, ref interfaceId,
            out propertyStore
        );
        if (result < 0 || propertyStore == null)
            Marshal.ThrowExceptionForHR(result);

        var key = new PropertyKey(
            new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"), 5
        );
        var value = new PropVariant();
        value.VarType = 31;
        value.PointerValue = Marshal.StringToCoTaskMemUni(appId);
        try {
            result = propertyStore.SetValue(ref key, ref value);
            if (result >= 0)
                result = propertyStore.Commit();
            if (result < 0)
                Marshal.ThrowExceptionForHR(result);
        } finally {
            PropVariantClear(ref value);
            Marshal.FinalReleaseComObject(propertyStore);
        }
    }

    public static string GetShortcutAppId(string shortcutPath) {
        var interfaceId = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
        IPropertyStore propertyStore = null;
        var result = SHGetPropertyStoreFromParsingName(
            shortcutPath, IntPtr.Zero, 0, ref interfaceId,
            out propertyStore
        );
        if (result < 0 || propertyStore == null) return String.Empty;

        var key = new PropertyKey(
            new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"), 5
        );
        var value = new PropVariant();
        try {
            result = propertyStore.GetValue(ref key, out value);
            if (result < 0) return String.Empty;
            IntPtr textPointer;
            result = PropVariantToStringAlloc(ref value, out textPointer);
            if (result < 0 || textPointer == IntPtr.Zero) return String.Empty;
            try {
                return Marshal.PtrToStringUni(textPointer) ?? String.Empty;
            } finally {
                Marshal.FreeCoTaskMem(textPointer);
            }
        } finally {
            PropVariantClear(ref value);
            Marshal.FinalReleaseComObject(propertyStore);
        }
    }

    public static IntPtr FindBrowserWindowForProcess(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            if (!IsWindowVisible(hWnd) || GetWindowTextLengthW(hWnd) <= 0)
                return true;

            uint ownerId;
            GetWindowThreadProcessId(hWnd, out ownerId);
            if (ownerId != processId) return true;

            var className = new StringBuilder(256);
            GetClassNameW(hWnd, className, className.Capacity);
            if (!className.ToString().Equals("Chrome_WidgetWin_1",
                    StringComparison.Ordinal)) return true;

            found = hWnd;
            return false;
        }, IntPtr.Zero);
        return found;
    }

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
            if (!IsWindowVisible(hWnd)) return true;

            uint ownerId;
            GetWindowThreadProcessId(hWnd, out ownerId);
            if (ownerId != processId) return true;

            var className = new StringBuilder(256);
            GetClassNameW(hWnd, className, className.Capacity);
            if (!className.ToString().Equals(expectedClass,
                    StringComparison.Ordinal)) return true;

            found = hWnd;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindProcessFileDialog(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            uint ownerId;
            GetWindowThreadProcessId(hWnd, out ownerId);
            if (ownerId != processId || !IsWindowVisible(hWnd)) return true;

            var className = new StringBuilder(256);
            GetClassNameW(hWnd, className, className.Capacity);
            if (!className.ToString().Equals("#32770",
                    StringComparison.Ordinal)) return true;
            if (GetDlgItem(hWnd, 0x047C) == IntPtr.Zero &&
                GetDlgItem(hWnd, 0x0480) == IntPtr.Zero) return true;

            found = hWnd;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindDescendantById(IntPtr parent, int controlId) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (hWnd, lParam) => {
            if (GetDlgCtrlID(hWnd) != controlId) return true;
            found = hWnd;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public static string GetDescendantTextByClass(IntPtr parent,
                                                   string expectedClass) {
        var result = new StringBuilder();
        EnumChildWindows(parent, (hWnd, lParam) => {
            var className = new StringBuilder(256);
            GetClassNameW(hWnd, className, className.Capacity);
            if (!className.ToString().Equals(expectedClass,
                    StringComparison.Ordinal)) return true;

            var value = new StringBuilder(2048);
            GetWindowTextW(hWnd, value, value.Capacity);
            if (value.Length == 0) return true;
            if (result.Length > 0) result.Append(" | ");
            result.Append(value);
            return true;
        }, IntPtr.Zero);
        return result.ToString();
    }

    public static string DescribeProcessWindows(uint processId) {
        var descriptions = new StringBuilder();
        EnumWindows((hWnd, lParam) => {
            uint ownerId;
            GetWindowThreadProcessId(hWnd, out ownerId);
            if (ownerId != processId) return true;

            var title = new StringBuilder(512);
            var className = new StringBuilder(256);
            GetWindowTextW(hWnd, title, title.Capacity);
            GetClassNameW(hWnd, className, className.Capacity);
            if (descriptions.Length > 0) descriptions.Append("; ");
            descriptions.Append("class='").Append(className)
                        .Append("', title='").Append(title).Append("'");
            return true;
        }, IntPtr.Zero);
        return descriptions.Length == 0 ? "none" : descriptions.ToString();
    }

    public static string DescribeDescendants(IntPtr parent) {
        var descriptions = new StringBuilder();
        EnumChildWindows(parent, (hWnd, lParam) => {
            var title = new StringBuilder(512);
            var className = new StringBuilder(256);
            GetWindowTextW(hWnd, title, title.Capacity);
            GetClassNameW(hWnd, className, className.Capacity);
            if (descriptions.Length > 0) descriptions.Append("; ");
            descriptions.Append("id=").Append(GetDlgCtrlID(hWnd))
                        .Append(", class='").Append(className)
                        .Append("', text='").Append(title).Append("'");
            return descriptions.Length < 12000;
        }, IntPtr.Zero);
        return descriptions.ToString();
    }

    public static string[] ParseCommandLine(string commandLine) {
        if (String.IsNullOrWhiteSpace(commandLine))
            return new string[0];
        int argumentCount;
        IntPtr arguments = CommandLineToArgvW(commandLine, out argumentCount);
        if (arguments == IntPtr.Zero)
            throw new InvalidOperationException("CommandLineToArgvW failed.");
        try {
            string[] result = new string[argumentCount];
            for (int index = 0; index < argumentCount; ++index) {
                IntPtr argument = Marshal.ReadIntPtr(arguments,
                                                     index * IntPtr.Size);
                result[index] = Marshal.PtrToStringUni(argument) ?? "";
            }
            return result;
        }
        finally {
            LocalFree(arguments);
        }
    }

    public static string GetExpectedAppUserModelId(string clientName,
                                                    string browserId) {
        const int maximumSlugLength = 48;
        const ulong fnvOffset = 14695981039346656037UL;
        const ulong fnvPrime = 1099511628211UL;
        ulong hash = fnvOffset;
        StringBuilder slug = new StringBuilder(maximumSlugLength);
        bool previousWasSeparator = false;

        foreach (char character in clientName) {
            char lower = char.ToLowerInvariant(character);
            unchecked {
                hash ^= (ushort)lower;
                hash *= fnvPrime;
            }

            char safeCharacter = '\0';
            if ((lower >= 'a' && lower <= 'z') ||
                (lower >= '0' && lower <= '9')) {
                safeCharacter = lower;
            } else if (!previousWasSeparator) {
                safeCharacter = '-';
            }
            if (safeCharacter != '\0' && slug.Length < maximumSlugLength) {
                slug.Append(safeCharacter);
                previousWasSeparator = safeCharacter == '-';
            }
        }
        while (slug.Length > 0 && slug[slug.Length - 1] == '-')
            slug.Remove(slug.Length - 1, 1);
        if (slug.Length == 0)
            slug.Append("client");

        return "ctSpaces.client." + slug + "." + hash.ToString("X16") +
               "." + browserId;
    }
}
'@

function Get-WindowIconInfo {
    param([IntPtr]$Window)

    $wmGetIcon = 0x007F
    $icon = [CtIconQaNative]::SendMessageW(
        $Window, $wmGetIcon, [IntPtr]::new(1), [IntPtr]::Zero
    )
    if ($icon -eq [IntPtr]::Zero) {
        $icon = [CtIconQaNative]::SendMessageW(
            $Window, $wmGetIcon, [IntPtr]::Zero, [IntPtr]::Zero
        )
    }
    if ($icon -eq [IntPtr]::Zero) {
        return $null
    }

    $copy = [CtIconQaNative]::CopyIcon($icon)
    if ($copy -eq [IntPtr]::Zero) {
        return $null
    }

    $managedIcon = $null
    $bitmap = $null
    $stream = $null
    $sha = $null
    try {
        $managedIcon = [Drawing.Icon]::FromHandle($copy)
        $bitmap = $managedIcon.ToBitmap()
        $stream = [IO.MemoryStream]::new()
        $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
        $sha = [Security.Cryptography.SHA256]::Create()
        $hashBytes = $sha.ComputeHash($stream.ToArray())
        [pscustomobject]@{
            Handle = $icon.ToInt64()
            SHA256 = [Convert]::ToHexString($hashBytes)
        }
    } finally {
        if ($sha) { $sha.Dispose() }
        if ($stream) { $stream.Dispose() }
        if ($bitmap) { $bitmap.Dispose() }
        if ($managedIcon) { $managedIcon.Dispose() }
        [void][CtIconQaNative]::DestroyIcon($copy)
    }
}

function Get-TaskbarIdentityInfo {
    param([IntPtr]$Window)

    $appId = [CtIconQaNative]::GetWindowPropertyString($Window, 5)
    $iconResource = [CtIconQaNative]::GetWindowPropertyString($Window, 3)
    $resourcePath = ''
    if ($iconResource.EndsWith(',0', [StringComparison]::Ordinal)) {
        $resourcePath = $iconResource.Substring(0, $iconResource.Length - 2)
    }

    [pscustomobject]@{
        AppId = $appId
        IconResource = $iconResource
        IconResourcePath = $resourcePath
        IconResourceExists = $resourcePath -and
            (Test-Path -LiteralPath $resourcePath -PathType Leaf)
    }
}

function Get-QaTaskbarLog {
    $logPath = Join-Path $qaDir 'taskbar-identity.log'
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        return 'QA taskbar log was not created.'
    }
    (Get-Content -LiteralPath $logPath) -join '; '
}

function Save-TaskbarScreenshot {
    param([string]$Path)

    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $parent = Split-Path -Parent $resolvedPath
    if ($parent) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $rect = [CtIconQaNative]::GetTaskbarRect()
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        throw "The Windows taskbar returned an invalid size: ${width}x${height}."
    }

    $bitmap = [Drawing.Bitmap]::new($width, $height)
    $graphics = $null
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen(
            $rect.Left, $rect.Top, 0, 0,
            [Drawing.Size]::new($width, $height)
        )
        $bitmap.Save($resolvedPath, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        if ($graphics) { $graphics.Dispose() }
        $bitmap.Dispose()
    }
    $resolvedPath
}

function Open-LauncherClient {
    param(
        [IntPtr]$LauncherWindow,
        [IntPtr]$ComboWindow,
        [string]$ClientName
    )

    $itemIndex = [CtIconQaNative]::SendMessageStringW(
        $ComboWindow, 0x0158, [IntPtr]::new(-1), $ClientName
    )
    if ($itemIndex.ToInt64() -lt 0) {
        throw 'Could not find the disposable client in the client list.'
    }
    $selectedIndex = [CtIconQaNative]::SendMessageW(
        $ComboWindow, 0x014E, $itemIndex, [IntPtr]::Zero
    )
    if ($selectedIndex.ToInt64() -ne $itemIndex.ToInt64()) {
        throw 'Could not select the disposable client.'
    }

    $actualName = [Text.StringBuilder]::new(256)
    [void][CtIconQaNative]::SendMessageTextW(
        $ComboWindow, 0x000D, [IntPtr]::new($actualName.Capacity), $actualName
    )
    if ($actualName.ToString() -ne $ClientName) {
        throw "The client field did not retain the QA name: '$actualName'"
    }

    $openWindow = [CtIconQaNative]::GetDlgItem($LauncherWindow, 1)
    if ($openWindow -eq [IntPtr]::Zero) {
        throw 'Could not find the Open button.'
    }
    if (-not [CtIconQaNative]::PostMessageW(
            $LauncherWindow, 0x0111, [IntPtr]::new(1), $openWindow
        )) {
        throw 'Could not post the Open command.'
    }
}

function Test-ExactProfileCommandLine {
    param([string]$CommandLine)

    if ([string]::IsNullOrWhiteSpace($CommandLine)) {
        return $false
    }
    $expectedProfile = [IO.Path]::GetFullPath($profilePath).TrimEnd(
        [char[]]"\/"
    )
    $arguments = [CtIconQaNative]::ParseCommandLine($CommandLine)
    for ($index = 0; $index -lt $arguments.Count; $index++) {
        $candidate = $null
        if ($arguments[$index].Equals(
                '--user-data-dir', [StringComparison]::OrdinalIgnoreCase
            ) -and $index + 1 -lt $arguments.Count) {
            $candidate = $arguments[$index + 1]
        } elseif ($arguments[$index].StartsWith(
                '--user-data-dir=', [StringComparison]::OrdinalIgnoreCase
            )) {
            $candidate = $arguments[$index].Substring('--user-data-dir='.Length)
        }
        if ([string]::IsNullOrWhiteSpace($candidate) -or
            -not [IO.Path]::IsPathRooted($candidate)) {
            continue
        }
        try {
            $resolvedCandidate = [IO.Path]::GetFullPath($candidate).TrimEnd(
                [char[]]"\/"
            )
            if ($resolvedCandidate.Equals(
                    $expectedProfile, [StringComparison]::OrdinalIgnoreCase
                )) {
                return $true
            }
        } catch {
        }
    }
    $false
}

function Get-TestBrowserProcesses {
    Get-CimInstance Win32_Process -Filter "Name = 'msedge.exe'" `
        -ErrorAction SilentlyContinue | Where-Object {
            Test-ExactProfileCommandLine $_.CommandLine
        }
}

function Open-VerifiedTestBrowserProcess {
    param([Parameter(Mandatory = $true)]$ProfileProcess)

    $ownedProcess = $null
    try {
        try {
            $ownedProcess = [Diagnostics.Process]::GetProcessById(
                [int]$ProfileProcess.ProcessId
            )
        } catch [ArgumentException] {
            return $null
        }
        $null = $ownedProcess.SafeHandle
        $current = Get-CimInstance Win32_Process -Filter (
            'ProcessId = ' + [int]$ProfileProcess.ProcessId
        ) -ErrorAction Stop | Select-Object -First 1
        if (-not $current) {
            $ownedProcess.Dispose()
            return $null
        }

        $snapshotStart = ([DateTime]$ProfileProcess.CreationDate).ToUniversalTime()
        $currentStart = ([DateTime]$current.CreationDate).ToUniversalTime()
        $handleStart = $ownedProcess.StartTime.ToUniversalTime()
        $sameCreation =
            [Math]::Abs(($snapshotStart - $handleStart).TotalSeconds) -lt 1 -and
            [Math]::Abs(($currentStart - $handleStart).TotalSeconds) -lt 1
        $expectedImage = [IO.Path]::GetFullPath($resolvedBrowser)
        $handleImage = [IO.Path]::GetFullPath($ownedProcess.MainModule.FileName)
        $currentImage = if ($current.ExecutablePath) {
            [IO.Path]::GetFullPath([string]$current.ExecutablePath)
        } else {
            ''
        }
        $sameImage =
            ([string]$current.Name).Equals(
                'msedge.exe', [StringComparison]::OrdinalIgnoreCase
            ) -and
            $handleImage.Equals(
                $expectedImage, [StringComparison]::OrdinalIgnoreCase
            ) -and
            $currentImage.Equals(
                $expectedImage, [StringComparison]::OrdinalIgnoreCase
            )
        $sameProfile =
            (Test-ExactProfileCommandLine $ProfileProcess.CommandLine) -and
            (Test-ExactProfileCommandLine $current.CommandLine)
        if (-not $sameCreation -or -not $sameImage -or -not $sameProfile) {
            throw "Refusing to terminate reused or mismatched browser PID $($ProfileProcess.ProcessId)."
        }
        return $ownedProcess
    } catch {
        if ($ownedProcess) {
            $ownedProcess.Dispose()
        }
        throw
    }
}

function Open-SetIconFile {
    param(
        [IntPtr]$LauncherWindow,
        [uint32]$LauncherProcessId,
        [string]$IconPath
    )

    if (-not [CtIconQaNative]::PostMessageW(
            $LauncherWindow, 0x0111, [IntPtr]::new(41001), [IntPtr]::Zero
        )) {
        throw 'Could not open Set Profile Icon.'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $dialog = [IntPtr]::Zero
    do {
        Start-Sleep -Milliseconds 100
        $dialog = [CtIconQaNative]::FindProcessFileDialog($LauncherProcessId)
    } while ($dialog -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $deadline)
    if ($dialog -eq [IntPtr]::Zero) {
        throw 'The Set Profile Icon file picker did not appear.'
    }

    [void][CtIconQaNative]::SetForegroundWindow($dialog)
    $pathApplied = $false
    $expectedLeaf = [IO.Path]::GetFileName($IconPath)
    for ($attempt = 0; $attempt -lt 10 -and -not $pathApplied; $attempt++) {
        Start-Sleep -Milliseconds 100
        # Explorer-style OPENFILENAME control IDs: cmb13 and edt1. The edit
        # control is nested on current Windows builds, so search descendants.
        foreach ($controlId in @(0x047C, 0x0480)) {
            $control = [CtIconQaNative]::FindDescendantById(
                $dialog, $controlId
            )
            if ($control -ne [IntPtr]::Zero) {
                [void][CtIconQaNative]::SetWindowTextW($control, $IconPath)
                [void][CtIconQaNative]::SendMessageStringW(
                    $control, 0x000C, [IntPtr]::Zero, $IconPath
                )
            }
            [void][CtIconQaNative]::SendMessageStringW(
                $dialog, 0x0468, [IntPtr]::new($controlId), $IconPath
            )
        }

        $selectedName = [Text.StringBuilder]::new(1024)
        $editControl = [CtIconQaNative]::FindDescendantById($dialog, 0x0480)
        if ($editControl -eq [IntPtr]::Zero) {
            $editControl = [CtIconQaNative]::FindDescendantById(
                $dialog, 0x047C
            )
        }
        if ($editControl -ne [IntPtr]::Zero) {
            [void][CtIconQaNative]::SendMessageTextW(
                $editControl, 0x000D,
                [IntPtr]::new($selectedName.Capacity), $selectedName
            )
        }
        if ($selectedName.Length -eq 0) {
            [void][CtIconQaNative]::SendMessageTextW(
                $dialog, 0x0464, [IntPtr]::new($selectedName.Capacity),
                $selectedName
            )
        }
        $pathApplied = $selectedName.ToString() -eq $IconPath -or
            $selectedName.ToString() -eq $expectedLeaf
    }
    if (-not $pathApplied) {
        $dialogTitle = [Text.StringBuilder]::new(256)
        [void][CtIconQaNative]::GetWindowTextW(
            $dialog, $dialogTitle, $dialogTitle.Capacity
        )
        $controls = [CtIconQaNative]::DescribeDescendants($dialog)
        throw "The '$dialogTitle' icon picker retained '$selectedName' instead of the QA path. Controls: $controls"
    }

    $openButton = [CtIconQaNative]::GetDlgItem($dialog, 1)
    if ($openButton -eq [IntPtr]::Zero) {
        throw 'Could not find the Open button in the icon file picker.'
    }
    [void][CtIconQaNative]::SendMessageW(
        $openButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero
    )
    $dialog
}

function Close-LauncherMessageBox {
    param(
        [IntPtr]$Dialog,
        [IntPtr]$LauncherWindow
    )

    $button = [CtIconQaNative]::GetDlgItem($Dialog, 1)
    if ($button -eq [IntPtr]::Zero) {
        $button = [CtIconQaNative]::FindDescendantById($Dialog, 1)
    }
    $title = [Text.StringBuilder]::new(256)
    [void][CtIconQaNative]::GetWindowTextW($Dialog, $title, $title.Capacity)
    if ($button -ne [IntPtr]::Zero) {
        [void][CtIconQaNative]::SendMessageW(
            $button, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero
        )
    } else {
        [void][CtIconQaNative]::PostMessageW(
            $Dialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero
        )
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([CtIconQaNative]::IsWindow($Dialog) -and
           [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 50
    }
    while (-not [CtIconQaNative]::IsWindowEnabled($LauncherWindow) -and
           [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 50
    }
    if ([CtIconQaNative]::IsWindow($Dialog) -or
        -not [CtIconQaNative]::IsWindowEnabled($LauncherWindow)) {
        $dialogOpen = [CtIconQaNative]::IsWindow($Dialog)
        $launcherEnabled = [CtIconQaNative]::IsWindowEnabled($LauncherWindow)
        throw "The '$title' result message did not close cleanly (dialog open: $dialogOpen, launcher enabled: $launcherEnabled)."
    }
}

try {
    $oldDpiContext = [CtIconQaNative]::SetThreadDpiAwarenessContext(
        [IntPtr]::new(-4)
    )

    New-Item -ItemType Directory -Path $qaDir -Force | Out-Null
    Copy-Item -LiteralPath $resolvedExe -Destination (Join-Path $qaDir 'ctSpaces-icon-qa.exe') -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'Default.7z') -Destination (Join-Path $qaDir 'Default.7z') -Force
    New-Item -ItemType File -Path (Join-Path $qaDir 'ctSpaces.portable') -Force | Out-Null

    if (Test-Path -LiteralPath $profilePath) {
        throw "Refusing to use an existing QA profile: $profilePath"
    }
    New-Item -ItemType Directory -Path $profilePath -Force | Out-Null
    $defaultProfilePath = Join-Path $profilePath 'Default'
    New-Item -ItemType Directory -Path $defaultProfilePath -Force | Out-Null
    [IO.File]::WriteAllText(
        (Join-Path $defaultProfilePath 'Preferences'), '{}',
        [Text.UTF8Encoding]::new($false)
    )
    New-Item -ItemType File -Path (Join-Path $profilePath 'ctSpaces') -Force | Out-Null
    [IO.File]::WriteAllText(
        (Join-Path $profilePath 'ctSpaces-legacy-browser-v2'),
        "ctSpaces-legacy-browser-schema=2`r`nbrowser=edge`r`n",
        [Text.UTF8Encoding]::new($false)
    )
    if ($ManualIterations -gt 0) {
        Copy-Item -LiteralPath $iconBeforePath `
            -Destination (Join-Path $profilePath 'client.ico') -Force
    }

    $qaArguments = '--qa-instance=' + $runId +
        ' --qa-data-dir="' + $dataRoot + '"'
    $qaExePath = Join-Path $qaDir 'ctSpaces-icon-qa.exe'
    $shortcutPath = Join-Path $qaDir 'client-appid-launch.lnk'
    $shortcutAppId = [CtIconQaNative]::GetExpectedAppUserModelId(
        $testName, 'edge'
    )
    $shortcutShell = New-Object -ComObject WScript.Shell
    $shortcut = $null
    try {
        $shortcut = $shortcutShell.CreateShortcut($shortcutPath)
        $shortcut.TargetPath = $qaExePath
        $shortcut.Arguments = $qaArguments
        $shortcut.WorkingDirectory = $qaDir
        $shortcut.Description = 'Isolated ctSpaces AppID inheritance QA'
        $shortcut.Save()
    } finally {
        if ($shortcut) {
            [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject(
                $shortcut
            )
        }
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject(
            $shortcutShell
        )
    }
    [CtIconQaNative]::SetShortcutAppId($shortcutPath, $shortcutAppId)
    $observedShortcutAppId =
        [CtIconQaNative]::GetShortcutAppId($shortcutPath)
    if ($observedShortcutAppId -ne $shortcutAppId) {
        throw "The isolated launch shortcut AppID was '$observedShortcutAppId' instead of '$shortcutAppId'."
    }
    Start-Process -FilePath $shortcutPath | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $launcherCim = Get-CimInstance Win32_Process `
            -Filter "Name = 'ctSpaces-icon-qa.exe'" `
            -ErrorAction SilentlyContinue | Where-Object {
                $_.ExecutablePath -and
                [IO.Path]::GetFullPath($_.ExecutablePath) -eq $qaExePath -and
                $_.CommandLine -and
                $_.CommandLine.Contains(
                    "--qa-instance=$runId",
                    [StringComparison]::OrdinalIgnoreCase
                )
            } | Select-Object -First 1
        if ($launcherCim) {
            $launcherProcess = Get-Process -Id $launcherCim.ProcessId `
                -ErrorAction SilentlyContinue
        }
    } while (-not $launcherProcess -and
             [DateTime]::UtcNow -lt $deadline)
    if (-not $launcherProcess) {
        throw 'The isolated client-AppID shortcut did not start ctSpaces.'
    }
    [void]$launcherProcess.WaitForInputIdle(10000)

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 200
        $launcherProcess.Refresh()
        if ($launcherProcess.HasExited) {
            throw "The QA launcher exited before showing its window (exit code $($launcherProcess.ExitCode))."
        }
        $launcherWindow = [CtIconQaNative]::FindProcessWindowWithChild(
            [uint32]$launcherProcess.Id, 102
        )
    } while ($launcherWindow -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $deadline)
    if ($launcherWindow -eq [IntPtr]::Zero) {
        $windows = [CtIconQaNative]::DescribeProcessWindows(
            [uint32]$launcherProcess.Id
        )
        throw "The QA launcher window did not appear. Process windows: $windows"
    }
    $launcherTitle = [Text.StringBuilder]::new(256)
    [void][CtIconQaNative]::GetWindowTextW(
        $launcherWindow, $launcherTitle, $launcherTitle.Capacity
    )
    if ($launcherTitle.ToString() -ne $expectedLauncherTitle) {
        throw "The launcher title was '$launcherTitle' instead of '$expectedLauncherTitle'."
    }
    $expectedLauncherAppId = 'ctSpaces.launcher'
    $launcherTaskbarIdentity =
        Get-TaskbarIdentityInfo -Window $launcherWindow
    $launcherTaskbarAppId = $launcherTaskbarIdentity.AppId
    if ($launcherTaskbarAppId -ne $expectedLauncherAppId) {
        $qaLog = Get-QaTaskbarLog
        throw "The launcher window AppID was '$launcherTaskbarAppId' instead of '$expectedLauncherAppId'. Diagnostics: $qaLog"
    }

    $combo = [CtIconQaNative]::GetDlgItem($launcherWindow, 102)
    if ($combo -eq [IntPtr]::Zero) {
        throw 'Could not find the disposable client field.'
    }
    Open-LauncherClient -LauncherWindow $launcherWindow `
        -ComboWindow $combo -ClientName $testName

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 250
        foreach ($process in @(Get-TestBrowserProcesses)) {
            $browserWindow = [CtIconQaNative]::FindBrowserWindowForProcess(
                [uint32]$process.ProcessId
            )
            if ($browserWindow -ne [IntPtr]::Zero) {
                break
            }
        }
    } while ($browserWindow -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $deadline)
    if ($browserWindow -eq [IntPtr]::Zero) {
        $browserDetails = @(
            foreach ($process in @(Get-TestBrowserProcesses)) {
                $windows = [CtIconQaNative]::DescribeProcessWindows(
                    [uint32]$process.ProcessId
                )
                "pid=$($process.ProcessId), name=$($process.Name), windows=$windows, command=$($process.CommandLine)"
            }
        ) -join ' | '
        if (-not $browserDetails) {
            $browserDetails = 'no process used the disposable profile path'
        }
        throw "The disposable client browser window did not appear. $browserDetails"
    }

    $expectedAppId = [CtIconQaNative]::GetExpectedAppUserModelId(
        $testName, 'edge'
    )
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    $taskbarIdentity = $null
    do {
        Start-Sleep -Milliseconds 200
        $taskbarIdentity = Get-TaskbarIdentityInfo -Window $browserWindow
    } while ($taskbarIdentity.AppId -ne $expectedAppId -and
             [DateTime]::UtcNow -lt $deadline)
    $initialTaskbarAppId = $taskbarIdentity.AppId
    if ($initialTaskbarAppId -ne $expectedAppId) {
        $qaLog = Get-QaTaskbarLog
        throw "The initial browser window AppID was '$initialTaskbarAppId' instead of '$expectedAppId'. Diagnostics: $qaLog"
    }
    if ($initialTaskbarAppId -eq $launcherTaskbarAppId) {
        throw "The launcher and browser windows shared AppID '$initialTaskbarAppId'."
    }
    $taskbarScreenshotBefore = ''
    if (-not [string]::IsNullOrWhiteSpace($TaskbarScreenshotPath)) {
        $resolvedTaskbarPath = [IO.Path]::GetFullPath($TaskbarScreenshotPath)
        $beforeTaskbarPath = Join-Path (Split-Path -Parent $resolvedTaskbarPath) `
            (([IO.Path]::GetFileNameWithoutExtension($resolvedTaskbarPath)) +
             '-before' + [IO.Path]::GetExtension($resolvedTaskbarPath))
        $taskbarScreenshotBefore = Save-TaskbarScreenshot `
            -Path $beforeTaskbarPath
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $beforeIcon = $null
    do {
        Start-Sleep -Milliseconds 200
        $beforeIcon = Get-WindowIconInfo -Window $browserWindow
    } while (-not $beforeIcon -and [DateTime]::UtcNow -lt $deadline)
    if (-not $beforeIcon) {
        if ($ManualIterations -eq 0) {
            $beforeIcon = [pscustomobject]@{
                Handle = 0
                SHA256 = 'NO_CUSTOM_ICON'
            }
        } else {
            throw 'The browser window never received the first client icon.'
        }
    }

    $afterIcon = $beforeIcon
    $changedThroughSetProfileIcon = $false
    if ($ManualIterations -gt 0) {
    $fileDialog = Open-SetIconFile -LauncherWindow $launcherWindow `
        -LauncherProcessId ([uint32]$launcherProcess.Id) `
        -IconPath $iconAfterPath

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([CtIconQaNative]::IsWindow($fileDialog) -and
           [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    if ([CtIconQaNative]::IsWindow($fileDialog)) {
        throw 'The Set Profile Icon file picker did not accept the QA icon.'
    }
    $fileDialog = [IntPtr]::Zero

    $expectedIconHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $iconAfterPath).Hash
    $actualIconHash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath (Join-Path $profilePath 'client.ico')).Hash
    if ($actualIconHash -ne $expectedIconHash) {
        throw 'The Set Profile Icon picker closed without applying the QA icon.'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $successDialog = [CtIconQaNative]::FindProcessWindowByClass(
            [uint32]$launcherProcess.Id, '#32770'
        )
    } while ($successDialog -eq [IntPtr]::Zero -and
             [DateTime]::UtcNow -lt $deadline)
    if ($successDialog -eq [IntPtr]::Zero) {
        throw 'Set Profile Icon did not show its success confirmation.'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    $afterIcon = $null
    do {
        Start-Sleep -Milliseconds 250
        $afterIcon = Get-WindowIconInfo -Window $browserWindow
    } while ((-not $afterIcon -or $afterIcon.SHA256 -eq $beforeIcon.SHA256) -and
             [DateTime]::UtcNow -lt $deadline)

    if (-not $afterIcon -or $afterIcon.SHA256 -eq $beforeIcon.SHA256) {
        throw 'Set Profile Icon did not update the live browser window.'
    }

    Close-LauncherMessageBox -Dialog $successDialog `
        -LauncherWindow $launcherWindow
    $successDialog = [IntPtr]::Zero

    for ($iteration = 2; $iteration -le $ManualIterations; $iteration++) {
        $nextIconPath = if (($iteration % 2) -eq 0) {
            $iconBeforePath
        } else {
            $iconAfterPath
        }
        $previousIcon = $afterIcon
        $fileDialog = Open-SetIconFile -LauncherWindow $launcherWindow `
            -LauncherProcessId ([uint32]$launcherProcess.Id) `
            -IconPath $nextIconPath

        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while ([CtIconQaNative]::IsWindow($fileDialog) -and
               [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 100
        }
        if ([CtIconQaNative]::IsWindow($fileDialog)) {
            throw "Manual icon iteration $iteration did not close its file picker."
        }
        $fileDialog = [IntPtr]::Zero

        $expectedIconHash = (Get-FileHash -Algorithm SHA256 `
            -LiteralPath $nextIconPath).Hash
        $actualIconHash = (Get-FileHash -Algorithm SHA256 `
            -LiteralPath (Join-Path $profilePath 'client.ico')).Hash
        if ($actualIconHash -ne $expectedIconHash) {
            throw "Manual icon iteration $iteration did not commit its icon file."
        }

        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 100
            $successDialog = [CtIconQaNative]::FindProcessWindowByClass(
                [uint32]$launcherProcess.Id, '#32770'
            )
        } while ($successDialog -eq [IntPtr]::Zero -and
                 [DateTime]::UtcNow -lt $deadline)
        if ($successDialog -eq [IntPtr]::Zero) {
            throw "Manual icon iteration $iteration did not show success."
        }

        $deadline = [DateTime]::UtcNow.AddSeconds(12)
        $currentIcon = $null
        do {
            Start-Sleep -Milliseconds 150
            $currentIcon = Get-WindowIconInfo -Window $browserWindow
        } while ((-not $currentIcon -or
                  $currentIcon.SHA256 -eq $previousIcon.SHA256) -and
                 [DateTime]::UtcNow -lt $deadline)
        if (-not $currentIcon -or
            $currentIcon.SHA256 -eq $previousIcon.SHA256) {
            throw "Manual icon iteration $iteration did not update the live window."
        }

        Close-LauncherMessageBox -Dialog $successDialog `
            -LauncherWindow $launcherWindow
        $successDialog = [IntPtr]::Zero
        $afterIcon = $currentIcon
    }
    $changedThroughSetProfileIcon = $true
    }

    $autoFetchChangedLive = $false
    $taskbarScreenshot = ''
    if (-not [string]::IsNullOrWhiteSpace($AutoFetchDomain)) {
        $previousIcon = $afterIcon
        $iconFile = Join-Path $profilePath 'client.ico'
        $previousFileHash = if (Test-Path -LiteralPath $iconFile) {
            (Get-FileHash -Algorithm SHA256 -LiteralPath $iconFile).Hash
        } else {
            ''
        }

        if (-not [CtIconQaNative]::PostMessageW(
                $launcherWindow, 0x0111, [IntPtr]::new(41008), [IntPtr]::Zero
            )) {
            throw 'Could not open Auto-fetch Icon.'
        }

        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 100
            $inputDialog = [CtIconQaNative]::FindProcessWindowByClass(
                [uint32]$launcherProcess.Id, 'InputBoxWndClass'
            )
        } while ($inputDialog -eq [IntPtr]::Zero -and
                 [DateTime]::UtcNow -lt $deadline)
        if ($inputDialog -eq [IntPtr]::Zero) {
            throw 'The Auto-fetch Icon input window did not appear.'
        }

        $domainField = [CtIconQaNative]::GetDlgItem($inputDialog, 102)
        $okButton = [CtIconQaNative]::GetDlgItem($inputDialog, 1)
        if ($domainField -eq [IntPtr]::Zero -or $okButton -eq [IntPtr]::Zero) {
            throw 'The Auto-fetch Icon controls were not available.'
        }
        [void][CtIconQaNative]::SendMessageStringW(
            $domainField, 0x000C, [IntPtr]::Zero, $AutoFetchDomain
        )
        $actualDomain = [Text.StringBuilder]::new(512)
        [void][CtIconQaNative]::SendMessageTextW(
            $domainField, 0x000D, [IntPtr]::new($actualDomain.Capacity),
            $actualDomain
        )
        if ($actualDomain.ToString() -ne $AutoFetchDomain) {
            throw "Auto-fetch retained '$actualDomain' instead of '$AutoFetchDomain'."
        }
        [void][CtIconQaNative]::SendMessageW(
            $okButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero
        )

        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while ([CtIconQaNative]::IsWindow($inputDialog) -and
               [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 100
        }
        if ([CtIconQaNative]::IsWindow($inputDialog)) {
            throw 'The Auto-fetch Icon input window did not submit.'
        }
        $inputDialog = [IntPtr]::Zero

        $deadline = [DateTime]::UtcNow.AddSeconds(45)
        do {
            Start-Sleep -Milliseconds 200
            $launcherProcess.Refresh()
            if ($launcherProcess.HasExited) {
                throw "ctSpaces crashed during Auto-fetch Icon with exit code $($launcherProcess.ExitCode)."
            }
            $successDialog = [CtIconQaNative]::FindProcessWindowByClass(
                [uint32]$launcherProcess.Id, '#32770'
            )
        } while ($successDialog -eq [IntPtr]::Zero -and
                 [DateTime]::UtcNow -lt $deadline)
        if ($successDialog -eq [IntPtr]::Zero) {
            $iconWasWritten = Test-Path -LiteralPath $iconFile -PathType Leaf
            $currentHash = if ($iconWasWritten) {
                (Get-FileHash -Algorithm SHA256 -LiteralPath $iconFile).Hash
            } else {
                ''
            }
            $iconChanged = $iconWasWritten -and $currentHash -ne $previousFileHash
            $clientField = [CtIconQaNative]::GetDlgItem($launcherWindow, 102)
            $clientFieldEnabled = $clientField -ne [IntPtr]::Zero -and
                [CtIconQaNative]::IsWindowEnabled($clientField)
            throw "Auto-fetch Icon did not show a result (icon written: $iconWasWritten; icon changed: $iconChanged; client field enabled: $clientFieldEnabled)."
        }

        $resultTitle = [Text.StringBuilder]::new(256)
        [void][CtIconQaNative]::GetWindowTextW(
            $successDialog, $resultTitle, $resultTitle.Capacity
        )
        if ($resultTitle.ToString() -ne 'Success') {
            $messageText = [CtIconQaNative]::GetDescendantTextByClass(
                $successDialog, 'Static'
            )
            throw "Auto-fetch Icon returned '$resultTitle': $messageText"
        }

        $currentFileHash = (Get-FileHash -Algorithm SHA256 `
            -LiteralPath $iconFile).Hash
        if ($currentFileHash -eq $previousFileHash) {
            throw 'Auto-fetch Icon reported success without replacing the icon file.'
        }

        $deadline = [DateTime]::UtcNow.AddSeconds(12)
        $currentIcon = $null
        do {
            Start-Sleep -Milliseconds 150
            $currentIcon = Get-WindowIconInfo -Window $browserWindow
        } while ((-not $currentIcon -or
                  $currentIcon.SHA256 -eq $previousIcon.SHA256) -and
                 [DateTime]::UtcNow -lt $deadline)
        if (-not $currentIcon -or
            $currentIcon.SHA256 -eq $previousIcon.SHA256) {
            throw 'Auto-fetch Icon did not update the live browser window.'
        }

        $deadline = [DateTime]::UtcNow.AddSeconds(12)
        do {
            Start-Sleep -Milliseconds 150
            $taskbarIdentity = Get-TaskbarIdentityInfo -Window $browserWindow
        } while (($taskbarIdentity.AppId -ne $expectedAppId -or
                  -not $taskbarIdentity.IconResourceExists -or
                  $taskbarIdentity.IconResource -notmatch
                      'ctSpaces-taskbar-[0-9A-F]{16}\.ico,0$') -and
                 [DateTime]::UtcNow -lt $deadline)
        if ($taskbarIdentity.AppId -ne $expectedAppId) {
            $browserTitle = [Text.StringBuilder]::new(512)
            [void][CtIconQaNative]::GetWindowTextW(
                $browserWindow, $browserTitle, $browserTitle.Capacity
            )
            $qaLog = Get-QaTaskbarLog
            throw "The browser window AppID was '$($taskbarIdentity.AppId)' instead of '$expectedAppId' after Auto-fetch. Window title: '$browserTitle'. Diagnostics: $qaLog"
        }
        if (-not $taskbarIdentity.IconResourceExists) {
            throw "The taskbar icon resource '$($taskbarIdentity.IconResource)' does not exist."
        }
        if ($taskbarIdentity.IconResource -notmatch
            'ctSpaces-taskbar-[0-9A-F]{16}\.ico,0$') {
            throw "The taskbar icon resource was not versioned: '$($taskbarIdentity.IconResource)'."
        }
        if (-not [string]::IsNullOrWhiteSpace($TaskbarScreenshotPath)) {
            $taskbarScreenshot = Save-TaskbarScreenshot `
                -Path $TaskbarScreenshotPath
        }

        Close-LauncherMessageBox -Dialog $successDialog `
            -LauncherWindow $launcherWindow
        $successDialog = [IntPtr]::Zero
        $afterIcon = $currentIcon
        $autoFetchChangedLive = $true
    }

    [pscustomobject]@{
        Client = $testName
        ShortcutTaskbarAppId = $observedShortcutAppId
        LauncherStartedThroughClientAppIdShortcut = $true
        LauncherTitle = $launcherTitle.ToString()
        LauncherTaskbarAppId = $launcherTaskbarAppId
        WindowIconBefore = $beforeIcon.SHA256
        WindowIconAfter = $afterIcon.SHA256
        WindowIconChangedLive = $afterIcon.SHA256 -ne $beforeIcon.SHA256
        ChangedThroughSetProfileIcon = $changedThroughSetProfileIcon
        ManualIconChanges = $ManualIterations
        AutoFetchDomain = $AutoFetchDomain
        AutoFetchChangedLive = $autoFetchChangedLive
        InitialTaskbarAppId = $initialTaskbarAppId
        LauncherAndBrowserTaskbarAppIdsAreDistinct = $true
        TaskbarAppId = $taskbarIdentity.AppId
        TaskbarIconResource = $taskbarIdentity.IconResource
        TaskbarIconResourceExists = $taskbarIdentity.IconResourceExists
        TaskbarScreenshotBefore = $taskbarScreenshotBefore
        TaskbarScreenshot = $taskbarScreenshot
        ChangedBeforeSuccessDismissed = $true
    } | ConvertTo-Json
} finally {
    if ($successDialog -ne [IntPtr]::Zero) {
        $successButton = [CtIconQaNative]::GetDlgItem($successDialog, 1)
        [void][CtIconQaNative]::PostMessageW(
            $successDialog, 0x0111, [IntPtr]::new(1), $successButton
        )
    }
    if ($fileDialog -ne [IntPtr]::Zero) {
        [void][CtIconQaNative]::PostMessageW(
            $fileDialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero
        )
    }
    if ($inputDialog -ne [IntPtr]::Zero) {
        [void][CtIconQaNative]::PostMessageW(
            $inputDialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero
        )
    }
    if ($browserWindow -ne [IntPtr]::Zero) {
        [void][CtIconQaNative]::PostMessageW(
            $browserWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero
        )
        Start-Sleep -Seconds 2
    }

    foreach ($process in @(Get-TestBrowserProcesses)) {
        $ownedProcess = $null
        try {
            $ownedProcess = Open-VerifiedTestBrowserProcess $process
            if ($ownedProcess -and -not $ownedProcess.HasExited) {
                $ownedProcess.Kill()
                [void]$ownedProcess.WaitForExit(5000)
            }
        } catch {
        } finally {
            if ($ownedProcess) {
                $ownedProcess.Dispose()
            }
        }
    }

    Start-Sleep -Milliseconds 750
    if ($launcherProcess) {
        try {
            if (-not $launcherProcess.HasExited) {
                if ($launcherWindow -ne [IntPtr]::Zero) {
                    [void][CtIconQaNative]::PostMessageW(
                        $launcherWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero
                    )
                }
                if (-not $launcherProcess.WaitForExit(3000)) {
                    $launcherProcess.Kill()
                    [void]$launcherProcess.WaitForExit(3000)
                }
            }
        } catch {
        } finally {
            $launcherProcess.Dispose()
        }
    }

    $expectedSitesRoot = [IO.Path]::GetFullPath($sitesRoot)
    $resolvedProfile = [IO.Path]::GetFullPath($profilePath)
    if ($testName.StartsWith('__ctSpacesIconQA_', [StringComparison]::Ordinal) -and
        [IO.Path]::GetFullPath((Split-Path -Parent $resolvedProfile)) -eq $expectedSitesRoot -and
        (Test-Path -LiteralPath $resolvedProfile)) {
        Remove-Item -LiteralPath $resolvedProfile -Recurse -Force
    }

    $resolvedQaDir = [IO.Path]::GetFullPath($qaDir)
    if ((Split-Path -Leaf $resolvedQaDir).StartsWith(
            '.qa-icon-refresh-',
            [StringComparison]::Ordinal
        ) -and $resolvedQaDir.StartsWith(
            $projectRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase
        ) -and (Test-Path -LiteralPath $resolvedQaDir)) {
        for ($cleanupAttempt = 0; $cleanupAttempt -lt 20; ++$cleanupAttempt) {
            try {
                Remove-Item -LiteralPath $resolvedQaDir -Recurse -Force
                break
            } catch {
                if ($cleanupAttempt -eq 19) { throw }
                Start-Sleep -Milliseconds 100
            }
        }
    }

    if ($oldDpiContext -ne [IntPtr]::Zero) {
        [void][CtIconQaNative]::SetThreadDpiAwarenessContext($oldDpiContext)
    }
}
