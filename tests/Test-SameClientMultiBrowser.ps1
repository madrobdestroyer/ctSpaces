param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [int]$TimeoutSeconds = 25
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$qaParent = [IO.Path]::GetFullPath((
    Join-Path $projectRoot 'build\qa-same-client-multi-browser'
))
$runId = [Guid]::NewGuid().ToString('N')
$qaRoot = Join-Path $qaParent ('run-' + $runId)
$qaExeDir = Join-Path $qaRoot 'app'
$qaExe = Join-Path $qaExeDir 'ctSpaces-multi-browser-qa.exe'
$qaDataDir = Join-Path $qaExeDir 'data'
$qaConfig = Join-Path $qaDataDir 'config.ini'
$clientName = '__ctSpacesMultiBrowserQA_' + $runId.Substring(0, 12)
$clientRoot = Join-Path $qaDataDir ('Sites\' + $clientName)
$edgeSlot = Join-Path $clientRoot 'Browsers\edge'
$chromeSlot = Join-Path $clientRoot 'Browsers\chrome'
$edgeProfile = Join-Path $edgeSlot 'Profile'
$chromeProfile = Join-Path $chromeSlot 'Profile'
$edgeSentinel = Join-Path $edgeProfile 'edge-slot-sentinel.txt'
$chromeSentinel = Join-Path $chromeProfile 'chrome-slot-sentinel.txt'
$edgeCookieLike = Join-Path $edgeProfile 'Default\Cookies.edge-qa-sentinel'
$chromeCookieLike = Join-Path $chromeProfile 'Default\Cookies.chrome-qa-sentinel'
$liveDataDir = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces'
$liveConfigPath = Join-Path $liveDataDir 'config.ini'
$liveClientPath = Join-Path $liveDataDir ('Sites\' + $clientName)
$sourcePath = Join-Path $projectRoot 'ctSpaces.cpp'
$launcher = $null
$launcherWindow = [IntPtr]::Zero
$serverJob = $null
$edgeWindowInfo = $null
$chromeWindowInfo = $null
$result = $null
$normalBrowserShutdown = $false
$normalLauncherShutdown = $false
$testPassed = $false
$failureRecord = $null
$taskbarLog = Join-Path $qaExeDir 'taskbar-identity.log'

function Get-FileFingerprint {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return 'file:' + (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
    if (Test-Path -LiteralPath $Path) {
        return 'non-file'
    }
    'missing'
}

$liveConfigBefore = Get-FileFingerprint $liveConfigPath

if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    throw "Release executable not found: $resolvedExe"
}
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Production source not found: $sourcePath"
}
if (Test-Path -LiteralPath $liveClientPath) {
    throw "Refusing to run because the unique live QA client already exists: $liveClientPath"
}

function Resolve-InstalledBrowser {
    param([ValidateSet('edge', 'chrome')][string]$Browser)

    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $programFiles = [Environment]::GetFolderPath('ProgramFiles')
    if ($Browser -eq 'edge') {
        $candidates = @(
            (Join-Path $programFilesX86 'Microsoft\Edge\Application\msedge.exe'),
            (Join-Path $programFiles 'Microsoft\Edge\Application\msedge.exe'),
            (Join-Path $env:LOCALAPPDATA 'Microsoft\Edge\Application\msedge.exe')
        )
    } else {
        $candidates = @(
            (Join-Path $programFiles 'Google\Chrome\Application\chrome.exe'),
            (Join-Path $programFilesX86 'Google\Chrome\Application\chrome.exe'),
            (Join-Path $env:LOCALAPPDATA 'Google\Chrome\Application\chrome.exe')
        )
    }
    $candidates | Where-Object {
        $_ -and (Test-Path -LiteralPath $_ -PathType Leaf)
    } | Select-Object -First 1
}

$installedEdge = Resolve-InstalledBrowser edge
$installedChrome = Resolve-InstalledBrowser chrome
if (-not $installedEdge) {
    throw 'Microsoft Edge is required for the same-client multi-browser QA.'
}
if (-not $installedChrome) {
    throw 'Google Chrome is required for the same-client multi-browser QA.'
}

if (-not ('CtSameClientMultiBrowserQaNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class CtSameClientMultiBrowserQaNative
{
    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct CopyDataStruct
    {
        public UIntPtr dwData;
        public uint cbData;
        public IntPtr lpData;
    }

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback,
                                           IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumChildWindows(IntPtr parent,
                                                EnumWindowsProc callback,
                                                IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hWnd,
                                                        out uint processId);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextLengthW(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(IntPtr hWnd,
                                             StringBuilder text,
                                             int maxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassNameW(IntPtr hWnd,
                                            StringBuilder text,
                                            int maxCount);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(IntPtr hWnd, uint command);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindowLongPtrW(IntPtr hWnd, int index);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int controlId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindowEnabled(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessageW(IntPtr hWnd, uint message,
                                           UIntPtr wParam, IntPtr lParam);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CommandLineToArgvW(string commandLine,
                                                     out int argumentCount);

    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SendMessageTimeoutW(
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

    public static IntPtr FindPrimaryWindow(uint processId)
    {
        const uint GW_OWNER = 4;
        const int GWL_EXSTYLE = -20;
        const long WS_EX_TOOLWINDOW = 0x00000080L;
        const long WS_EX_NOACTIVATE = 0x08000000L;
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint ownerProcessId;
            GetWindowThreadProcessId(hWnd, out ownerProcessId);
            long exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE).ToInt64();
            if (ownerProcessId == processId && IsWindowVisible(hWnd) &&
                GetWindowTextLengthW(hWnd) > 0 &&
                GetWindow(hWnd, GW_OWNER) == IntPtr.Zero &&
                (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) == 0) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static string GetWindowTitle(IntPtr hWnd)
    {
        int length = GetWindowTextLengthW(hWnd);
        StringBuilder text = new StringBuilder(Math.Max(1, length + 1));
        GetWindowTextW(hWnd, text, text.Capacity);
        return text.ToString();
    }

    public static string[] GetProcessDialogSnapshots(uint processId)
    {
        var snapshots = new System.Collections.Generic.List<string>();
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint ownerProcessId;
            GetWindowThreadProcessId(hWnd, out ownerProcessId);
            if (ownerProcessId != processId)
                return true;
            var className = new StringBuilder(128);
            GetClassNameW(hWnd, className, className.Capacity);
            if (!className.ToString().Equals("#32770",
                    StringComparison.Ordinal))
                return true;
            var values = new System.Collections.Generic.List<string>();
            EnumChildWindows(hWnd, delegate(IntPtr child, IntPtr unused) {
                var childClass = new StringBuilder(128);
                GetClassNameW(child, childClass, childClass.Capacity);
                if (childClass.ToString().Equals("Static",
                        StringComparison.OrdinalIgnoreCase)) {
                    string value = GetWindowTitle(child);
                    if (!String.IsNullOrWhiteSpace(value))
                        values.Add(value.Replace("\r", " ").Replace("\n", " "));
                }
                return true;
            }, IntPtr.Zero);
            snapshots.Add("title=" + GetWindowTitle(hWnd) +
                          " | static=" + String.Join(" || ", values));
            return true;
        }, IntPtr.Zero);
        return snapshots.ToArray();
    }

    public static bool SendLaunchRequest(IntPtr hWnd, string arguments)
    {
        const uint WM_COPYDATA = 0x004A;
        IntPtr text = Marshal.StringToHGlobalUni(arguments);
        IntPtr data = IntPtr.Zero;
        try {
            CopyDataStruct value = new CopyDataStruct();
            value.dwData = new UIntPtr(0x43545350u);
            value.cbData = checked((uint)((arguments.Length + 1) * 2));
            value.lpData = text;
            data = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(CopyDataStruct)));
            Marshal.StructureToPtr(value, data, false);
            UIntPtr result;
            return SendMessageTimeoutW(hWnd, WM_COPYDATA, UIntPtr.Zero, data,
                0x0003, 5000, out result) && result != UIntPtr.Zero;
        }
        finally {
            if (data != IntPtr.Zero)
                Marshal.FreeHGlobal(data);
            Marshal.FreeHGlobal(text);
        }
    }

    public static bool SendCommand(IntPtr hWnd, uint command)
    {
        const uint WM_COMMAND = 0x0111;
        UIntPtr ignored;
        return SendMessageTimeoutW(hWnd, WM_COMMAND, new UIntPtr(command),
            IntPtr.Zero, 0x0003, 5000, out ignored);
    }

    public static string[] ParseCommandLine(string commandLine)
    {
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

    public static bool RequestSessionShutdown(IntPtr hWnd)
    {
        const uint WM_QUERYENDSESSION = 0x0011;
        const uint WM_ENDSESSION = 0x0016;
        const int ENDSESSION_CLOSEAPP = 0x00000001;
        UIntPtr ignored;
        SendMessageTimeoutW(hWnd, WM_QUERYENDSESSION, UIntPtr.Zero,
            new IntPtr(ENDSESSION_CLOSEAPP), 0x0003, 1000, out ignored);
        return PostMessageW(hWnd, WM_ENDSESSION, new UIntPtr(1),
                            new IntPtr(ENDSESSION_CLOSEAPP));
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
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Failure
}

function Test-ExactProfileCommandLine {
    param([string]$CommandLine, [string]$ProfilePath)

    if ([string]::IsNullOrWhiteSpace($CommandLine)) {
        return $false
    }
    $expectedProfile = [IO.Path]::GetFullPath($ProfilePath).TrimEnd(
        [char[]]"\/"
    )
    $arguments = [CtSameClientMultiBrowserQaNative]::ParseCommandLine(
        $CommandLine
    )
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

function Get-UserDataDirArgument {
    param([string]$CommandLine)

    if ([string]::IsNullOrWhiteSpace($CommandLine)) { return $null }
    try {
        $arguments = [CtSameClientMultiBrowserQaNative]::ParseCommandLine(
            $CommandLine
        )
    } catch { return $null }
    for ($index = 0; $index -lt $arguments.Count; $index++) {
        if ($arguments[$index].Equals(
                '--user-data-dir', [StringComparison]::OrdinalIgnoreCase
            ) -and $index + 1 -lt $arguments.Count) {
            return $arguments[$index + 1]
        }
        if ($arguments[$index].StartsWith(
                '--user-data-dir=', [StringComparison]::OrdinalIgnoreCase
            )) {
            return $arguments[$index].Substring('--user-data-dir='.Length)
        }
    }
    $null
}

function Get-QaOwnedBrowserDiagnostics {
    $qaPrefix = [IO.Path]::GetFullPath($qaDataDir).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    $diagnostics = [Collections.Generic.List[string]]::new()
    foreach ($pair in @(
        @{ Image = 'msedge.exe'; Path = $installedEdge; Expected = $edgeProfile },
        @{ Image = 'chrome.exe'; Path = $installedChrome; Expected = $chromeProfile }
    )) {
        foreach ($process in @(Get-CimInstance Win32_Process -Filter (
                    "Name = '$($pair.Image)'"
                ) -ErrorAction SilentlyContinue)) {
            $profileArgument = Get-UserDataDirArgument $process.CommandLine
            if ([string]::IsNullOrWhiteSpace($profileArgument) -or
                -not [IO.Path]::IsPathRooted($profileArgument)) { continue }
            try {
                $resolvedProfile = [IO.Path]::GetFullPath(
                    $profileArgument
                ).TrimEnd([char[]]'\/')
                $resolvedImage = if ($process.ExecutablePath) {
                    [IO.Path]::GetFullPath([string]$process.ExecutablePath)
                } else { '' }
            } catch { continue }
            if (-not ($resolvedProfile + [IO.Path]::DirectorySeparatorChar).StartsWith(
                    $qaPrefix, [StringComparison]::OrdinalIgnoreCase
                ) -or -not $resolvedImage.Equals(
                    [IO.Path]::GetFullPath($pair.Path),
                    [StringComparison]::OrdinalIgnoreCase
                )) { continue }
            $exactExpected = $resolvedProfile.Equals(
                [IO.Path]::GetFullPath($pair.Expected).TrimEnd([char[]]'\/'),
                [StringComparison]::OrdinalIgnoreCase
            )
            $diagnostics.Add(
                "image=$($pair.Image) pid=$($process.ProcessId) " +
                "exactExpected=$exactExpected userDataDir=$resolvedProfile " +
                "commandLine=$($process.CommandLine)"
            )
        }
    }
    @($diagnostics)
}

function Write-FailureDiagnostics {
    param($Failure)

    try {
        $lines = [Collections.Generic.List[string]]::new()
        $lines.Add('failure=' + $(if ($Failure) {
            $Failure.Exception.Message
        } else { 'unknown failure' }))
        $lines.Add('launcherPid=' + $(if ($launcher) { $launcher.Id } else { 0 }))
        $lines.Add('launcherDialogs:')
        if ($launcher -and -not $launcher.HasExited) {
            foreach ($dialog in [CtSameClientMultiBrowserQaNative]::GetProcessDialogSnapshots(
                    [uint32]$launcher.Id
                )) {
                $lines.Add('  ' + $dialog)
            }
        }
        $lines.Add('qaOwnedBrowserProcesses:')
        foreach ($browser in @(Get-QaOwnedBrowserDiagnostics)) {
            $lines.Add('  ' + $browser)
        }
        $lines.Add('taskbarIdentityLog:')
        if (Test-Path -LiteralPath $taskbarLog -PathType Leaf) {
            foreach ($line in @(Get-Content -LiteralPath $taskbarLog)) {
                $lines.Add('  ' + $line)
            }
        } else {
            $lines.Add('  (missing)')
        }
        [IO.File]::WriteAllLines(
            (Join-Path $qaRoot 'failure-diagnostics.txt'), $lines,
            [Text.UTF8Encoding]::new($false)
        )
    } catch {
        Write-Warning "Could not write QA failure diagnostics: $_"
    }
}

function Get-ProfileProcesses {
    param(
        [ValidateSet('msedge.exe', 'chrome.exe')][string]$ImageName,
        [string]$ProfilePath
    )

    @(
        Get-CimInstance Win32_Process -Filter "Name = '$ImageName'" |
            Where-Object {
                Test-ExactProfileCommandLine $_.CommandLine $ProfilePath
            }
    )
}

function Open-VerifiedProfileProcess {
    param(
        [Parameter(Mandatory = $true)]$ProfileProcess,
        [Parameter(Mandatory = $true)][string]$ImageName,
        [Parameter(Mandatory = $true)][string]$ImagePath,
        [Parameter(Mandatory = $true)][string]$ProfilePath
    )

    $ownedProcess = $null
    try {
        try {
            $ownedProcess = [Diagnostics.Process]::GetProcessById(
                [int]$ProfileProcess.ProcessId
            )
        } catch [ArgumentException] {
            return $null
        }
        # Retain the process handle before re-reading identity. Subsequent Kill
        # therefore cannot target a different process that reuses this PID.
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
        $expectedImage = [IO.Path]::GetFullPath($ImagePath)
        $handleImage = [IO.Path]::GetFullPath($ownedProcess.MainModule.FileName)
        $currentImage = if ($current.ExecutablePath) {
            [IO.Path]::GetFullPath([string]$current.ExecutablePath)
        } else {
            ''
        }
        $sameImage =
            ([string]$current.Name).Equals(
                $ImageName, [StringComparison]::OrdinalIgnoreCase
            ) -and
            $handleImage.Equals(
                $expectedImage, [StringComparison]::OrdinalIgnoreCase
            ) -and
            $currentImage.Equals(
                $expectedImage, [StringComparison]::OrdinalIgnoreCase
            )
        $sameProfile =
            (Test-ExactProfileCommandLine $ProfileProcess.CommandLine $ProfilePath) -and
            (Test-ExactProfileCommandLine $current.CommandLine $ProfilePath)
        if (-not $sameCreation -or -not $sameImage -or -not $sameProfile) {
            throw "Refusing to act on reused or mismatched browser PID $($ProfileProcess.ProcessId)."
        }
        return $ownedProcess
    } catch {
        if ($ownedProcess) {
            $ownedProcess.Dispose()
        }
        throw
    }
}

function Get-ProfileWindowInfo {
    param(
        [ValidateSet('msedge.exe', 'chrome.exe')][string]$ImageName,
        [string]$ProfilePath
    )

    foreach ($process in (Get-ProfileProcesses $ImageName $ProfilePath)) {
        $window = [CtSameClientMultiBrowserQaNative]::FindPrimaryWindow(
            [uint32]$process.ProcessId
        )
        if ($window -ne [IntPtr]::Zero) {
            return [pscustomobject]@{
                ProcessId = [int]$process.ProcessId
                Window = $window
                CommandLine = [string]$process.CommandLine
                Title = [CtSameClientMultiBrowserQaNative]::GetWindowTitle(
                    $window
                )
            }
        }
    }
    $null
}

function Assert-IsolationSentinels {
    $expected = @{
        $edgeSentinel = 'edge-slot-only'
        $edgeCookieLike = 'edge-cookie-like-only'
        $chromeSentinel = 'chrome-slot-only'
        $chromeCookieLike = 'chrome-cookie-like-only'
    }
    foreach ($entry in $expected.GetEnumerator()) {
        if (-not (Test-Path -LiteralPath $entry.Key -PathType Leaf) -or
            [IO.File]::ReadAllText($entry.Key) -ne $entry.Value) {
            throw "A browser-slot sentinel changed or disappeared: $($entry.Key)"
        }
    }

    $crossed = @(
        (Join-Path $chromeProfile 'edge-slot-sentinel.txt'),
        (Join-Path $chromeProfile 'Default\Cookies.edge-qa-sentinel'),
        (Join-Path $edgeProfile 'chrome-slot-sentinel.txt'),
        (Join-Path $edgeProfile 'Default\Cookies.chrome-qa-sentinel')
    ) | Where-Object { Test-Path -LiteralPath $_ }
    if ($crossed.Count -gt 0) {
        throw "Browser-slot data crossed isolation boundaries: $($crossed -join ', ')"
    }
}

function Assert-SessionIdentitySource {
    $source = [IO.File]::ReadAllText($sourcePath)
    $checks = @(
        @('browser-qualified active key',
          'struct\s+ClientBrowserKey\s*\{[\s\S]*?std::wstring\s+clientName[\s\S]*?BrowserKind\s+browser'),
        @('active registration uses client and browser',
          'g_activeProfiles\s*\[\s*ClientBrowserKey\s*\{\s*name\s*,\s*browser\s*\}\s*\]'),
        @('session captures the resolved browser',
          'g_sessions\.push_back\s*\(\s*\{\s*name\s*,\s*browser\s*,\s*pid\s*\}\s*\)'),
        @('session tab uses only the client name',
          'GetSessionTabText\([^;{}]*\)\s*\{[\s\S]*?const\s+Session\s*&session\s*=\s*g_sessions\[iSession\][\s\S]*?return\s+session\.clientName\s*;')
    )
    foreach ($check in $checks) {
        if ($source -notmatch $check[1]) {
            throw "Production source lost its $($check[0]) invariant."
        }
    }
}

function Start-LocalTitleServer {
    $portProbe = [Net.Sockets.TcpListener]::new(
        [Net.IPAddress]::Loopback, 0
    )
    $portProbe.Start()
    $port = ([Net.IPEndPoint]$portProbe.LocalEndpoint).Port
    $portProbe.Stop()

    $job = Start-Job -ArgumentList $port -ScriptBlock {
        param($Port)
        $listener = [Net.Sockets.TcpListener]::new(
            [Net.IPAddress]::Loopback, $Port
        )
        try {
            $listener.Start()
            Write-Output 'READY'
            while ($true) {
                $client = $listener.AcceptTcpClient()
                try {
                    $stream = $client.GetStream()
                    $reader = [IO.StreamReader]::new(
                        $stream, [Text.Encoding]::ASCII, $false, 1024, $true
                    )
                    $request = $reader.ReadLine()
                    while ($reader.ReadLine()) { }
                    $title = if ($request -match '\s/chrome(?:\s|\?)') {
                        'ctSpaces Chrome QA'
                    } elseif ($request -match '\s/edge(?:\s|\?)') {
                        'ctSpaces Edge QA'
                    } else {
                        'ctSpaces Multi Browser QA'
                    }
                    $body = [Text.Encoding]::UTF8.GetBytes(
                        '<!doctype html><title>' + $title +
                        '</title><h1>' + $title + '</h1>'
                    )
                    $headers = [Text.Encoding]::ASCII.GetBytes(
                        "HTTP/1.1 200 OK`r`nContent-Type: text/html; " +
                        "charset=utf-8`r`nContent-Length: $($body.Length)`r`n" +
                        "Connection: close`r`n`r`n"
                    )
                    $stream.Write($headers, 0, $headers.Length)
                    $stream.Write($body, 0, $body.Length)
                    $stream.Flush()
                } finally {
                    $client.Dispose()
                }
            }
        } finally {
            $listener.Stop()
        }
    }

    [void](Wait-Until {
        (Receive-Job -Job $job -Keep -ErrorAction SilentlyContinue) -contains
            'READY'
    } 'The local title server did not start.' 10)
    [pscustomobject]@{ Job = $job; Port = $port }
}

function Stop-LocalTitleServer {
    if (-not $serverJob) { return }
    Stop-Job -Job $serverJob -ErrorAction SilentlyContinue
    Remove-Job -Job $serverJob -Force -ErrorAction SilentlyContinue
    $script:serverJob = $null
}

try {
    New-Item -ItemType Directory -Path $qaExeDir -Force | Out-Null
    foreach ($profile in @($edgeProfile, $chromeProfile)) {
        New-Item -ItemType Directory -Path (Join-Path $profile 'Default') `
            -Force | Out-Null
    }
    Copy-Item -LiteralPath $resolvedExe -Destination $qaExe -Force
    New-Item -ItemType File -Path (Join-Path $qaExeDir 'ctSpaces.portable') `
        -Force | Out-Null

    $utf8NoBom = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText(
        (Join-Path $clientRoot 'ctSpaces-client-v2'),
        "ctSpaces-client-schema=2`r`n", $utf8NoBom
    )
    foreach ($slot in @(
        @{ Root = $edgeSlot; Id = 'edge' },
        @{ Root = $chromeSlot; Id = 'chrome' }
    )) {
        [IO.File]::WriteAllText(
            (Join-Path $slot.Root 'ctSpaces-browser-v2'),
            "ctSpaces-browser-schema=2`r`nbrowser=$($slot.Id)`r`n",
            $utf8NoBom
        )
        [IO.File]::WriteAllText(
            (Join-Path $slot.Root 'Profile\ctSpaces'),
            "ctSpaces-profile=2`r`n", $utf8NoBom
        )
        [IO.File]::WriteAllText(
            (Join-Path $slot.Root 'Profile\Default\Preferences'),
            '{}', $utf8NoBom
        )
    }
    [IO.File]::WriteAllText($edgeSentinel, 'edge-slot-only', $utf8NoBom)
    [IO.File]::WriteAllText(
        $edgeCookieLike, 'edge-cookie-like-only', $utf8NoBom
    )
    [IO.File]::WriteAllText($chromeSentinel, 'chrome-slot-only', $utf8NoBom)
    [IO.File]::WriteAllText(
        $chromeCookieLike, 'chrome-cookie-like-only', $utf8NoBom
    )
    [IO.File]::WriteAllText(
        $qaConfig,
        "[user]`r`ntheme_name=Dark - Gothic`r`n" +
        "client_title_first=0`r`nbrowser=edge`r`n`r`n" +
        "[archived]`r`ncount=0`r`n",
        $utf8NoBom
    )

    Assert-IsolationSentinels
    Assert-SessionIdentitySource

    $server = Start-LocalTitleServer
    $serverJob = $server.Job
    $edgeUrl = "http://127.0.0.1:$($server.Port)/edge"
    $chromeUrl = "http://127.0.0.1:$($server.Port)/chrome"

    $arguments = '--qa-instance=' + $runId.Substring(0, 10) +
        ' --qa-data-dir="' + $qaDataDir + '"'
    $launcher = Start-Process -FilePath $qaExe -ArgumentList $arguments `
        -PassThru
    [void]$launcher.WaitForInputIdle(10000)
    $launcherWindow = Wait-Until {
        $launcher.Refresh()
        if ($launcher.HasExited) {
            throw "The QA launcher exited with code $($launcher.ExitCode)."
        }
        $window = [CtSameClientMultiBrowserQaNative]::FindProcessWindowWithChild(
            [uint32]$launcher.Id, 206
        )
        if ($window -ne [IntPtr]::Zero) { $window }
    } 'Timed out waiting for the isolated QA launcher.'

    $openButton = [CtSameClientMultiBrowserQaNative]::GetDlgItem(
        $launcherWindow, 1
    )
    if ($openButton -eq [IntPtr]::Zero) {
        throw 'The isolated launcher did not create its Open button.'
    }

    $edgeRequest = '--client "' + $clientName +
        '" --browser edge --url "' + $edgeUrl + '"'
    if (-not [CtSameClientMultiBrowserQaNative]::SendLaunchRequest(
            $launcherWindow, $edgeRequest
        )) {
        throw 'ctSpaces rejected the Edge client launch request.'
    }
    [void](Wait-Until {
        [CtSameClientMultiBrowserQaNative]::IsWindowEnabled($openButton)
    } 'The launcher did not finish the Edge launch.' 10)
    $edgeWindowInfo = Wait-Until {
        Get-ProfileWindowInfo 'msedge.exe' $edgeProfile
    } 'Edge did not open the exact ctSpaces Edge profile.'

    $chromeRequest = '--client "' + $clientName +
        '" --browser chrome --url "' + $chromeUrl + '"'
    if (-not [CtSameClientMultiBrowserQaNative]::SendLaunchRequest(
            $launcherWindow, $chromeRequest
        )) {
        throw 'ctSpaces rejected the Chrome client launch request.'
    }
    [void](Wait-Until {
        [CtSameClientMultiBrowserQaNative]::IsWindowEnabled($openButton)
    } 'The launcher did not finish the Chrome launch.' 10)
    $chromeWindowInfo = Wait-Until {
        Get-ProfileWindowInfo 'chrome.exe' $chromeProfile
    } 'Chrome did not open the exact ctSpaces Chrome profile.'

    $edgeProcesses = Get-ProfileProcesses 'msedge.exe' $edgeProfile
    $chromeProcesses = Get-ProfileProcesses 'chrome.exe' $chromeProfile
    if ($edgeProcesses.Count -eq 0 -or $chromeProcesses.Count -eq 0) {
        throw 'Edge and Chrome were not simultaneously alive in both slots.'
    }
    $overlap = @($edgeProcesses.ProcessId | Where-Object {
        $chromeProcesses.ProcessId -contains $_
    })
    if ($overlap.Count -gt 0) {
        throw "The Edge and Chrome process sets unexpectedly overlap: $overlap"
    }
    if ($edgeWindowInfo.CommandLine.IndexOf(
            $edgeProfile, [StringComparison]::OrdinalIgnoreCase
        ) -lt 0 -or
        $chromeWindowInfo.CommandLine.IndexOf(
            $chromeProfile, [StringComparison]::OrdinalIgnoreCase
        ) -lt 0 -or
        [IO.Path]::GetFullPath($edgeProfile) -eq
            [IO.Path]::GetFullPath($chromeProfile)) {
        throw 'The browsers did not use two distinct exact ctSpaces profiles.'
    }

    [void](Wait-Until {
        $edgeWindowInfo.Title =
            [CtSameClientMultiBrowserQaNative]::GetWindowTitle(
                $edgeWindowInfo.Window
            )
        $edgeWindowInfo.Title.EndsWith(
            " - $clientName [Microsoft Edge] - ctSpaces",
            [StringComparison]::OrdinalIgnoreCase
        )
    } 'The live Edge title did not start in page-first mode.')
    [void](Wait-Until {
        $chromeWindowInfo.Title =
            [CtSameClientMultiBrowserQaNative]::GetWindowTitle(
                $chromeWindowInfo.Window
            )
        $chromeWindowInfo.Title.EndsWith(
            " - $clientName [Google Chrome] - ctSpaces",
            [StringComparison]::OrdinalIgnoreCase
        )
    } 'The live Chrome title did not start in page-first mode.')

    if (-not [CtSameClientMultiBrowserQaNative]::SendCommand(
            $launcherWindow, 41016
        )) {
        throw 'The launcher rejected the live client-title preference toggle.'
    }
    [void](Wait-Until {
        (Get-Content -Raw -LiteralPath $qaConfig) -match
            '(?m)^client_title_first=1\r?$'
    } 'The live client-title preference toggle was not persisted.')

    [void](Wait-Until {
        $edgeWindowInfo.Title =
            [CtSameClientMultiBrowserQaNative]::GetWindowTitle(
                $edgeWindowInfo.Window
            )
        $edgeWindowInfo.Title.StartsWith(
            "$clientName [Microsoft Edge] - ",
            [StringComparison]::OrdinalIgnoreCase
        ) -and $edgeWindowInfo.Title.EndsWith(
            ' - ctSpaces', [StringComparison]::Ordinal
        )
    } 'The live Edge identity was not client-and-browser qualified.')
    [void](Wait-Until {
        $chromeWindowInfo.Title =
            [CtSameClientMultiBrowserQaNative]::GetWindowTitle(
                $chromeWindowInfo.Window
            )
        $chromeWindowInfo.Title.StartsWith(
            "$clientName [Google Chrome] - ",
            [StringComparison]::OrdinalIgnoreCase
        ) -and $chromeWindowInfo.Title.EndsWith(
            ' - ctSpaces', [StringComparison]::Ordinal
        )
    } 'The live Chrome identity was not client-and-browser qualified.')

    Assert-IsolationSentinels

    if (-not [CtSameClientMultiBrowserQaNative]::RequestSessionShutdown(
            $edgeWindowInfo.Window
        )) {
        throw 'Could not send Edge a coordinated shutdown request.'
    }
    if (-not [CtSameClientMultiBrowserQaNative]::RequestSessionShutdown(
            $chromeWindowInfo.Window
        )) {
        throw 'Could not send Chrome a coordinated shutdown request.'
    }
    [void](Wait-Until {
        (Get-ProfileProcesses 'msedge.exe' $edgeProfile).Count -eq 0 -and
        (Get-ProfileProcesses 'chrome.exe' $chromeProfile).Count -eq 0
    } 'One or both browser profile processes stayed alive after shutdown.')
    $normalBrowserShutdown = $true
    Assert-IsolationSentinels

    Start-Sleep -Milliseconds 750
    if (-not [CtSameClientMultiBrowserQaNative]::PostMessageW(
            $launcherWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero
        )) {
        throw 'Could not close the isolated ctSpaces launcher.'
    }
    if (-not $launcher.WaitForExit(10000)) {
        throw 'The isolated ctSpaces launcher did not close after both sessions ended.'
    }
    $normalLauncherShutdown = $true

    $result = [pscustomobject]@{
        Client = $clientName
        EdgeExecutable = $installedEdge
        ChromeExecutable = $installedChrome
        EdgeProfile = $edgeProfile
        ChromeProfile = $chromeProfile
        EdgeProcessIds = @($edgeProcesses.ProcessId)
        ChromeProcessIds = @($chromeProcesses.ProcessId)
        EdgeWindowTitle = $edgeWindowInfo.Title
        ChromeWindowTitle = $chromeWindowInfo.Title
        DistinctProfiles = $true
        BrowsersCoexisted = $true
        ActiveIdentityBrowserQualified = $true
        SessionIdentityBrowserQualified = $true
        TitlePreferenceFlippedLive = $true
        SlotSentinelsStayedIsolated = $true
        CoordinatedBrowserShutdown = $true
    }
    $testPassed = $true
}
catch {
    $failureRecord = $_
    throw
}
finally {
    $finalIsolationError = $null
    if ((Get-FileFingerprint $liveConfigPath) -ne $liveConfigBefore) {
        $finalIsolationError =
            'Same-client multi-browser QA changed the live ctSpaces configuration.'
    } elseif (Test-Path -LiteralPath $liveClientPath) {
        $finalIsolationError =
            'Same-client multi-browser QA created data in the live Sites folder.'
    }
    if ($finalIsolationError) {
        $testPassed = $false
        if (-not $failureRecord) {
            $failureRecord = [Management.Automation.ErrorRecord]::new(
                [InvalidOperationException]::new($finalIsolationError),
                'MultiBrowserIsolation',
                [Management.Automation.ErrorCategory]::SecurityError,
                $liveDataDir
            )
        }
    }
    if (-not $testPassed) {
        Write-FailureDiagnostics $failureRecord
    }
    try {
        foreach ($pair in @(
            @{ Image = 'msedge.exe'; Path = $installedEdge; Profile = $edgeProfile },
            @{ Image = 'chrome.exe'; Path = $installedChrome; Profile = $chromeProfile }
        )) {
            foreach ($process in (Get-ProfileProcesses $pair.Image $pair.Profile)) {
                $ownedProcess = Open-VerifiedProfileProcess $process `
                    $pair.Image $pair.Path $pair.Profile
                if ($ownedProcess) {
                    try {
                        $window = [CtSameClientMultiBrowserQaNative]::FindPrimaryWindow(
                            [uint32]$ownedProcess.Id
                        )
                        if ($window -ne [IntPtr]::Zero) {
                            [void][CtSameClientMultiBrowserQaNative]::RequestSessionShutdown(
                                $window
                            )
                        }
                    } finally {
                        $ownedProcess.Dispose()
                    }
                }
            }
        }
        Start-Sleep -Milliseconds 600
        foreach ($pair in @(
            @{ Image = 'msedge.exe'; Path = $installedEdge; Profile = $edgeProfile },
            @{ Image = 'chrome.exe'; Path = $installedChrome; Profile = $chromeProfile }
        )) {
            foreach ($process in (Get-ProfileProcesses $pair.Image $pair.Profile)) {
                $ownedProcess = Open-VerifiedProfileProcess $process `
                    $pair.Image $pair.Path $pair.Profile
                if ($ownedProcess) {
                    try {
                        if (-not $ownedProcess.HasExited) {
                            $ownedProcess.Kill()
                            [void]$ownedProcess.WaitForExit(5000)
                        }
                    } finally {
                        $ownedProcess.Dispose()
                    }
                }
            }
        }
    } catch {
        Write-Warning "Disposable browser cleanup encountered an error: $_"
    }

    if ($launcherWindow -ne [IntPtr]::Zero -and
        [CtSameClientMultiBrowserQaNative]::IsWindow($launcherWindow)) {
        [void][CtSameClientMultiBrowserQaNative]::PostMessageW(
            $launcherWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero
        )
    }
    if ($launcher) {
        try {
            [void]$launcher.WaitForExit(4000)
            if (-not $launcher.HasExited) {
                $launcher.Kill()
                [void]$launcher.WaitForExit(4000)
            }
        } catch {
        } finally {
            $launcher.Dispose()
        }
    }

    Stop-LocalTitleServer

    $resolvedQaRoot = [IO.Path]::GetFullPath($qaRoot)
    $allowedPrefix = $qaParent.TrimEnd('\') + '\'
    if ($testPassed -and $resolvedQaRoot.StartsWith(
            $allowedPrefix, [StringComparison]::OrdinalIgnoreCase
        ) -and [IO.Path]::GetDirectoryName($resolvedQaRoot).Equals(
            $qaParent, [StringComparison]::OrdinalIgnoreCase
        ) -and [IO.Path]::GetFileName($resolvedQaRoot).StartsWith(
            'run-', [StringComparison]::Ordinal
        ) -and (Test-Path -LiteralPath $resolvedQaRoot)) {
        $cleanupDeadline = [DateTime]::UtcNow.AddSeconds(12)
        do {
            try {
                Remove-Item -LiteralPath $resolvedQaRoot -Recurse -Force `
                    -ErrorAction Stop
                break
            } catch {
                if ([DateTime]::UtcNow -ge $cleanupDeadline) { throw }
                Start-Sleep -Milliseconds 300
            }
        } while (Test-Path -LiteralPath $resolvedQaRoot)
    }

    if ($testPassed -and (Test-Path -LiteralPath $resolvedQaRoot)) {
        throw "The run-scoped QA root was not removed: $resolvedQaRoot"
    }
    if (-not $testPassed -and (Test-Path -LiteralPath $resolvedQaRoot)) {
        Write-Warning "Preserved failed multi-browser QA fixture: $resolvedQaRoot"
    }
    if ($finalIsolationError) { throw $finalIsolationError }
}

$result | Add-Member -NotePropertyName NormalBrowserShutdown `
    -NotePropertyValue $normalBrowserShutdown
$result | Add-Member -NotePropertyName NormalLauncherShutdown `
    -NotePropertyValue $normalLauncherShutdown
$result | Add-Member -NotePropertyName LiveConfigUnchanged `
    -NotePropertyValue $true
$result | Add-Member -NotePropertyName LiveSitesUnchanged `
    -NotePropertyValue $true
$result | Add-Member -NotePropertyName RunArtifactsRemoved `
    -NotePropertyValue $true
$result | ConvertTo-Json -Depth 4
