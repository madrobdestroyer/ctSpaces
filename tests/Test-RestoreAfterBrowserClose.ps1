param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$BrowserPath = '',
    [ValidateRange(1, 2)][int]$TabCount = 1,
    [ValidateSet('NormalWindowClose', 'SessionShutdown')]
    [string]$CloseMode = 'NormalWindowClose',
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\restore-after-browser-close'),
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'QaProcess.ps1')
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$evidenceRoot = [IO.Path]::GetFullPath($ArtifactDirectory)
if (-not $evidenceRoot.StartsWith((Join-Path $projectRoot 'build\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Restoration evidence must stay below workspace build.'
}
$qaParent = Join-Path $projectRoot 'build\qa-browser-close-restore'
$runId = [Guid]::NewGuid().ToString('N')
$qaTitlePrefix = 'ctSpaces Restore ' + $runId.Substring(0, 12)
$qaRoot = Join-Path $qaParent ('run-' + $runId)
$qaExeDir = Join-Path $qaRoot 'app'
$qaExe = Join-Path $qaExeDir 'ctSpaces-browser-close-qa.exe'
$qaDataDir = Join-Path $qaExeDir 'data'
$clientName = 'RestoreCloseQA'
$profilePath = Join-Path $qaDataDir "Sites\$clientName"
$preferencesPath = Join-Path $profilePath 'Default\Preferences'
$configPath = Join-Path $qaDataDir 'config.ini'
$launcherProcess = $null
$completed = $false
$launcherWindow = [IntPtr]::Zero
$ownedProcesses = [Collections.Generic.Dictionary[int, Diagnostics.Process]]::new()
$liveConfigPath = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'
$liveConfigHashBefore = if (Test-Path -LiteralPath $liveConfigPath -PathType Leaf) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $liveConfigPath).Hash
} else {
    '<missing>'
}

if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    throw "Release executable not found: $resolvedExe"
}

if ([string]::IsNullOrWhiteSpace($BrowserPath)) {
    $browserCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe'),
        (Join-Path $env:ProgramFiles 'BraveSoftware\Brave-Browser\Application\brave.exe')
    )
    $BrowserPath = $browserCandidates | Where-Object {
        $_ -and (Test-Path -LiteralPath $_ -PathType Leaf)
    } | Select-Object -First 1
}
if (-not $BrowserPath -or
    -not (Test-Path -LiteralPath $BrowserPath -PathType Leaf)) {
    throw 'No supported Chromium browser was found.'
}
$resolvedBrowser = [IO.Path]::GetFullPath($BrowserPath)
$browserImageName = [IO.Path]::GetFileName($resolvedBrowser)
$browserId = ''
$browserDisplayName = ''
switch ([IO.Path]::GetFileName($resolvedBrowser).ToLowerInvariant()) {
    'msedge.exe' {
        $browserId = 'edge'
        $browserDisplayName = 'Microsoft Edge'
    }
    'chrome.exe' {
        $browserId = 'chrome'
        $browserDisplayName = 'Google Chrome'
    }
    'brave.exe' {
        $browserId = 'brave'
        $browserDisplayName = 'Brave Browser'
    }
    default {
        throw "The selected browser executable is not supported: $resolvedBrowser"
    }
}

function Register-OwnedProcess {
    param([Parameter(Mandatory = $true)][Diagnostics.Process]$Process)

    $processId = $Process.Id
    if ($ownedProcesses.ContainsKey($processId)) {
        if (-not [Object]::ReferenceEquals($ownedProcesses[$processId], $Process)) {
            $Process.Dispose()
        }
        return $ownedProcesses[$processId]
    }

    # Opening SafeHandle now binds this object to the process that exists now;
    # cleanup never acts on a bare PID that Windows could later reuse.
    $null = $Process.SafeHandle
    $ownedProcesses.Add($processId, $Process)
    return $Process
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class CtRestoreCloseQaNative
{
    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback,
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
    public static extern bool SetForegroundWindow(IntPtr hWnd);

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
    public static extern bool SendMessageTimeoutW(
        IntPtr hWnd,
        uint message,
        UIntPtr wParam,
        IntPtr lParam,
        uint flags,
        uint timeoutMilliseconds,
        out UIntPtr result
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

    public static IntPtr FindVisibleWindowByTitle(string fragment)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            if (!IsWindowVisible(hWnd))
                return true;
            int length = GetWindowTextLengthW(hWnd);
            if (length <= 0)
                return true;
            StringBuilder title = new StringBuilder(length + 1);
            GetWindowTextW(hWnd, title, title.Capacity);
            if (title.ToString().IndexOf(
                    fragment, StringComparison.OrdinalIgnoreCase) >= 0) {
                found = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static uint GetWindowProcessId(IntPtr hWnd)
    {
        uint processId;
        GetWindowThreadProcessId(hWnd, out processId);
        return processId;
    }

    public static string GetWindowTitle(IntPtr hWnd)
    {
        int length = GetWindowTextLengthW(hWnd);
        StringBuilder title = new StringBuilder(Math.Max(1, length + 1));
        GetWindowTextW(hWnd, title, title.Capacity);
        return title.ToString();
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
Add-Type -Path (Join-Path $PSScriptRoot 'ChromiumSessionFixture.cs')

function Start-QaBrowser {
    param([string[]]$AdditionalArguments = @())

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resolvedBrowser
    $startInfo.UseShellExecute = $false
    $startInfo.ArgumentList.Add("--user-data-dir=$profilePath")
    $startInfo.ArgumentList.Add('--no-first-run')
    $startInfo.ArgumentList.Add('--no-default-browser-check')
    $startInfo.ArgumentList.Add('--disable-sync')
    $startInfo.ArgumentList.Add('--disable-features=SyncPromo')
    $startInfo.ArgumentList.Add('--edge-skip-compat-layer-relaunch')
    $startInfo.ArgumentList.Add('--no-service-autorun')
    $startInfo.ArgumentList.Add('--disable-background-mode')
    foreach ($argument in $AdditionalArguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($startInfo)
    Register-OwnedProcess $process
}

function Test-ExactProfileCommandLine {
    param([string]$CommandLine)

    if ([string]::IsNullOrWhiteSpace($CommandLine)) {
        return $false
    }
    $expectedProfile = [IO.Path]::GetFullPath($profilePath).TrimEnd(
        [char[]]"\/"
    )
    try {
        $arguments = [CtRestoreCloseQaNative]::ParseCommandLine($CommandLine)
    } catch {
        return $false
    }
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

function Get-DisposableProfileProcesses {
    @(
        Get-QaProcess Win32_Process -Filter "Name = '$browserImageName'" |
            Where-Object {
            Test-ExactProfileCommandLine $_.CommandLine
        }
    )
}

function Wait-ForDisposableProfileExit {
    param([Parameter(Mandatory = $true)][string]$Phase)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $observations = [Collections.Generic.List[object]]::new()
    do {
        $pending = @(Get-DisposableProfileProcesses)
        $observations.Add([pscustomobject]@{
            ElapsedMilliseconds = $watch.ElapsedMilliseconds
            ProcessIds = @($pending | ForEach-Object { [int]$_.ProcessId })
        })
        if ($pending.Count -eq 0) { break }
        Start-Sleep -Milliseconds 100
    } while ($watch.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
    [pscustomobject]@{
        Phase = $Phase
        TimeoutSeconds = $TimeoutSeconds
        Observations = @($observations.ToArray())
        Passed = ($pending.Count -eq 0)
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (
        Join-Path $evidenceRoot ($runId + '-' + $Phase + '-process-exit.json')
    )
    if ($pending.Count) {
        throw "$Phase left disposable profile processes after $TimeoutSeconds seconds: $($pending.ProcessId -join ', ')"
    }
}

function Stop-DisposableProfileProcess {
    param([Parameter(Mandatory = $true)]$ProfileProcess)

    $process = $null
    try {
        $process = [Diagnostics.Process]::GetProcessById(
            [int]$ProfileProcess.ProcessId
        )
        # Retain a handle first, then prove that it still names the same image
        # and creation time observed by the profile-scoped CIM query.
        $null = $process.SafeHandle
        $expectedStart = ([DateTime]$ProfileProcess.CreationDate).ToUniversalTime()
        $actualStart = $process.StartTime.ToUniversalTime()
        $expectedImage = [IO.Path]::GetFullPath($resolvedBrowser)
        $handleImage = [IO.Path]::GetFullPath($process.MainModule.FileName)
        $current = Get-QaProcess Win32_Process -Filter (
            'ProcessId = ' + [int]$ProfileProcess.ProcessId
        ) -ErrorAction Stop | Select-Object -First 1
        $currentStart = if ($current) {
            ([DateTime]$current.CreationDate).ToUniversalTime()
        } else {
            [DateTime]::MinValue
        }
        $sameStart = [Math]::Abs(
            ($actualStart - $expectedStart).TotalSeconds
        ) -lt 1 -and [Math]::Abs(
            ($actualStart - $currentStart).TotalSeconds
        ) -lt 1
        $currentImage = if ($current -and $current.ExecutablePath) {
            [IO.Path]::GetFullPath([string]$current.ExecutablePath)
        } else {
            ''
        }
        $sameImage = $current -and
            ($process.ProcessName + '.exe').Equals(
                $browserImageName, [StringComparison]::OrdinalIgnoreCase
            ) -and ([string]$current.Name).Equals(
                $browserImageName, [StringComparison]::OrdinalIgnoreCase
            ) -and
            $handleImage.Equals(
                $expectedImage, [StringComparison]::OrdinalIgnoreCase
            ) -and
            $currentImage.Equals(
                $expectedImage, [StringComparison]::OrdinalIgnoreCase
            )
        $sameProfile = $current -and
            (Test-ExactProfileCommandLine $ProfileProcess.CommandLine) -and
            (Test-ExactProfileCommandLine $current.CommandLine)
        if (-not $sameStart -or -not $sameImage -or -not $sameProfile) {
            throw "Refusing to terminate reused or mismatched PID $($ProfileProcess.ProcessId)."
        }
        if (-not $process.HasExited) {
            $process.Kill()
            [void]$process.WaitForExit(3000)
        }
    } finally {
        if ($process) {
            $process.Dispose()
        }
    }
}

function Wait-ForTitledBrowserWindow {
    param([int]$Timeout = $TimeoutSeconds)

    $deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
    do {
        $window = [CtRestoreCloseQaNative]::FindVisibleWindowByTitle(
            $qaTitlePrefix
        )
        if ($window -ne [IntPtr]::Zero) {
            $windowProcessId =
                [int][CtRestoreCloseQaNative]::GetWindowProcessId($window)
            $observed = Get-DisposableProfileProcesses | Where-Object {
                [int]$_.ProcessId -eq $windowProcessId -and $_.ExecutablePath -and
                [IO.Path]::GetFullPath($_.ExecutablePath).Equals($resolvedBrowser, [StringComparison]::OrdinalIgnoreCase)
            } | Select-Object -First 1
            if (-not $observed) { throw 'Refusing an unverified browser window outside the exact disposable profile.' }
            if (-not $ownedProcesses.ContainsKey($windowProcessId)) {
                $windowProcess = [Diagnostics.Process]::GetProcessById($windowProcessId)
                try {
                    $null = $windowProcess.SafeHandle
                    if ($windowProcess.HasExited -or
                        [Math]::Abs(($windowProcess.StartTime.ToUniversalTime() - ([DateTime]$observed.CreationDate).ToUniversalTime()).TotalSeconds) -ge 1 -or
                        -not [IO.Path]::GetFullPath($windowProcess.MainModule.FileName).Equals($resolvedBrowser, [StringComparison]::OrdinalIgnoreCase) -or
                        [int][CtRestoreCloseQaNative]::GetWindowProcessId($window) -ne $windowProcessId) {
                        throw 'Refusing a reused or mismatched browser window process.'
                    }
                    [void](Register-OwnedProcess $windowProcess)
                } catch { $windowProcess.Dispose(); throw }
            }
            return $window
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)

    throw 'The browser did not restore a ctSpaces QA tab.'
}

function Get-SessionPaths {
    @(Get-ChildItem -LiteralPath (Join-Path $profilePath 'Default\Sessions') -Filter 'Session_*' -File -ErrorAction SilentlyContinue | ForEach-Object FullName)
}

function Wait-NewSessionSnapshot {
    param([string[]]$PreviousSessionPaths)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $fresh = Get-SessionPaths | Where-Object { $_ -notin $PreviousSessionPaths } | Sort-Object -Descending | Select-Object -First 1
        if ($fresh -and (Get-Item -LiteralPath $fresh).Length -gt 8) { return $fresh }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Browser session storage did not finish initializing before the close request.'
}

function Wait-ForRestoredTabs {
    param([string[]]$PreviousSessionPaths, [string[]]$ExpectedUrls)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $lastError = 'The browser has not written its new current-session snapshot.'
    do {
        $fresh = Get-SessionPaths | Where-Object { $_ -notin $PreviousSessionPaths } | Sort-Object -Descending | Select-Object -First 1
        if ($fresh) {
            try {
                $urls = @([ChromiumSessionFixture]::CurrentUrls($fresh))
                if (@($ExpectedUrls | Where-Object { $_ -notin $urls }).Count -eq 0) {
                    return [pscustomobject]@{Session=$fresh; RestoredUrls=$urls; ExpectedUrls=$ExpectedUrls}
                }
                $lastError = 'Current-session tabs: ' + ($urls -join ', ')
            } catch { $lastError = [string]$_ }
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "The new browser session did not retain every expected fixture tab. $lastError"
}

function Save-SessionEvidence {
    param($Receipt, [string]$Phase)
    New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
    $destination = Join-Path $evidenceRoot ($runId + '-' + $Phase + '.session')
    Copy-Item -LiteralPath $Receipt.Session -Destination $destination
    $sourceHash = (Get-FileHash -LiteralPath $Receipt.Session -Algorithm SHA256).Hash
    $copyHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if ($sourceHash -ne $copyHash) { throw 'Restoration session changed while retaining its evidence.' }
    $retainedUrls = @([ChromiumSessionFixture]::CurrentUrls($destination))
    if (@($Receipt.ExpectedUrls | Where-Object { $_ -notin $retainedUrls }).Count) { throw 'Retained session no longer proves all expected tabs.' }
    [pscustomobject]@{Session=$destination;Sha256=$copyHash;RestoredUrls=$retainedUrls;ExpectedUrls=$Receipt.ExpectedUrls}
}

function Wait-ForWindowToClose {
    param([IntPtr]$Window, [int]$Timeout = $TimeoutSeconds)

    $deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
    do {
        if (-not [CtRestoreCloseQaNative]::IsWindow($Window)) {
            return $true
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    $false
}

function Close-BrowserNormally {
    param([IntPtr]$BrowserWindow)

    if ($CloseMode -eq 'SessionShutdown') {
        if (-not [CtRestoreCloseQaNative]::RequestSessionShutdown($BrowserWindow)) {
            throw 'Could not send the browser session shutdown request.'
        }
        if (-not (Wait-ForWindowToClose $BrowserWindow)) {
            throw 'The browser window did not close after session shutdown.'
        }
        return $false
    }
    if (-not [CtRestoreCloseQaNative]::PostMessageW(
            $BrowserWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero
        )) {
        throw 'Could not send the normal browser close request.'
    }

    if (-not (Wait-ForWindowToClose $BrowserWindow)) {
        throw 'The normal browser close request did not close the window. Use a one-tab fixture for unattended WM_CLOSE coverage; the separate two-tab SessionShutdown case covers multi-tab restoration without approving browser UI prompts.'
    }
    $false
}

function Wait-ForLauncherWindow {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 100
        $launcherProcess.Refresh()
        if ($launcherProcess.HasExited) {
            throw "The QA launcher exited with code $($launcherProcess.ExitCode)."
        }
        $window = [CtRestoreCloseQaNative]::FindProcessWindowWithChild(
            [uint32]$launcherProcess.Id, 207
        )
        if ($window -ne [IntPtr]::Zero) {
            return $window
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Timed out waiting for the QA launcher.'
}

function Set-WindowTextValue {
    param([IntPtr]$Window, [string]$Text)

    $pointer = [Runtime.InteropServices.Marshal]::StringToHGlobalUni($Text)
    try {
        $result = [UIntPtr]::Zero
        if (-not [CtRestoreCloseQaNative]::SendMessageTimeoutW(
                $Window, 0x000C, [UIntPtr]::Zero, $pointer,
                0x0003, 2000, [ref]$result
            )) {
            throw 'Could not set the QA client name.'
        }
    } finally {
        [Runtime.InteropServices.Marshal]::FreeHGlobal($pointer)
    }
}

function Set-DisposableRestorePreference {
    param([int]$Value)

    $text = [IO.File]::ReadAllText($preferencesPath)
    $pattern = '"restore_on_startup"\s*:\s*\d+'
    if ($text -match $pattern) {
        $text = [regex]::Replace(
            $text, $pattern, '"restore_on_startup":' + $Value
        )
    } else {
        $sessionMatch = [regex]::Match($text, '"session"\s*:\s*\{')
        if ($sessionMatch.Success) {
            $contentStart = $sessionMatch.Index + $sessionMatch.Length
            $nextContent = $contentStart
            while ($nextContent -lt $text.Length -and
                [char]::IsWhiteSpace($text[$nextContent])) {
                $nextContent++
            }
            $separator = if (
                $nextContent -lt $text.Length -and $text[$nextContent] -eq '}'
            ) { '' } else { ',' }
            $text = $text.Insert(
                $contentStart,
                '"restore_on_startup":' + $Value + $separator
            )
        } else {
            $rootObject = $text.IndexOf('{')
            if ($rootObject -lt 0) {
                throw 'The disposable browser Preferences file is not JSON.'
            }
            $nextContent = $rootObject + 1
            while ($nextContent -lt $text.Length -and
                [char]::IsWhiteSpace($text[$nextContent])) {
                $nextContent++
            }
            $separator = if (
                $nextContent -lt $text.Length -and $text[$nextContent] -eq '}'
            ) { '' } else { ',' }
            $text = $text.Insert(
                $rootObject + 1,
                '"session":{"restore_on_startup":' + $Value + '}' +
                    $separator
            )
        }
    }
    [IO.File]::WriteAllText(
        $preferencesPath, $text, [Text.UTF8Encoding]::new($false)
    )
}

try {
    New-Item -ItemType Directory -Path $qaExeDir -Force | Out-Null
    New-Item -ItemType Directory -Path $profilePath -Force | Out-Null
    Copy-Item -LiteralPath $resolvedExe -Destination $qaExe -Force
    New-Item -ItemType File -Path (Join-Path $qaExeDir 'ctSpaces.portable') `
        -Force | Out-Null

    $alphaPath = Join-Path $qaRoot 'alpha.html'
    $betaPath = Join-Path $qaRoot 'beta.html'
    [IO.File]::WriteAllText(
        $alphaPath, "<title>$qaTitlePrefix Alpha</title><h1>Alpha</h1>"
    )
    [IO.File]::WriteAllText(
        $betaPath, "<title>$qaTitlePrefix Beta</title><h1>Beta</h1>"
    )
    $alphaUrl = ([Uri]::new($alphaPath)).AbsoluteUri
    $betaUrl = ([Uri]::new($betaPath)).AbsoluteUri

    $seedArguments = @('--new-window', $alphaUrl)
    if ($TabCount -eq 2) { $seedArguments += $betaUrl }
    $expectedUrls = @($alphaUrl)
    if ($TabCount -eq 2) { $expectedUrls += $betaUrl }
    $seedProcess = Start-QaBrowser $seedArguments
    $seedWindow = Wait-ForTitledBrowserWindow
    $null = Wait-NewSessionSnapshot @()
    $seedPromptApproved = Close-BrowserNormally $seedWindow
    if (-not $seedProcess.WaitForExit(10000)) {
        throw 'The disposable browser root process stayed open after Close all.'
    }
    Wait-ForDisposableProfileExit 'seed-browser-close'
    New-Item -ItemType File -Path (Join-Path $profilePath 'ctSpaces') `
        -Force | Out-Null
    [IO.File]::WriteAllText(
        (Join-Path $profilePath 'ctSpaces-legacy-browser-v2'),
        "ctSpaces-legacy-browser-schema=2`r`nbrowser=$browserId`r`n",
        [Text.UTF8Encoding]::new($false)
    )
    Set-DisposableRestorePreference 5
    [IO.File]::WriteAllText(
        $configPath,
        "[user]`r`nclient_title_first=1`r`nbrowser=$browserId`r`n",
        [Text.UTF8Encoding]::new($false)
    )

    $launcherArguments = '--qa-instance=' + $runId.Substring(0, 10) +
        ' --qa-data-dir="' + $qaDataDir + '"'
    $launcherProcess = Start-Process -FilePath $qaExe `
        -ArgumentList $launcherArguments -PassThru
    [void](Register-OwnedProcess $launcherProcess)
    [void]$launcherProcess.WaitForInputIdle(10000)
    $launcherWindow = Wait-ForLauncherWindow

    $clientEdit = [CtRestoreCloseQaNative]::GetDlgItem($launcherWindow, 206)
    $restoreToggle = [CtRestoreCloseQaNative]::GetDlgItem($launcherWindow, 207)
    $openButton = [CtRestoreCloseQaNative]::GetDlgItem($launcherWindow, 1)
    Set-WindowTextValue $clientEdit $clientName
    $enabledDeadline = [DateTime]::UtcNow.AddSeconds(4)
    while (-not [CtRestoreCloseQaNative]::IsWindowEnabled($restoreToggle) -and
        [DateTime]::UtcNow -lt $enabledDeadline) {
        Start-Sleep -Milliseconds 50
    }
    if (-not [CtRestoreCloseQaNative]::IsWindowEnabled($restoreToggle)) {
        throw 'Restore tabs did not enable for the disposable existing client.'
    }
    $firstSessionBefore = Get-SessionPaths
    $result = [UIntPtr]::Zero
    if (-not [CtRestoreCloseQaNative]::SendMessageTimeoutW(
            $openButton, 0x00F5, [UIntPtr]::Zero, [IntPtr]::Zero,
            0x0003, 2000, [ref]$result
        )) {
        throw 'Could not open the disposable client through ctSpaces.'
    }

    $ctSpacesWindow = Wait-ForTitledBrowserWindow
    $null = Wait-NewSessionSnapshot $firstSessionBefore
    $ctSpacesPromptApproved = Close-BrowserNormally $ctSpacesWindow
    Wait-ForDisposableProfileExit 'first-reopen-close'
    # Chromium keeps its writable current-session file locked while running.
    # Read the fresh snapshot only after this reopened browser closes normally.
    $firstTabs = Save-SessionEvidence (Wait-ForRestoredTabs $firstSessionBefore $expectedUrls) 'first-reopen'

    $secondSessionBefore = Get-SessionPaths
    Set-WindowTextValue $clientEdit $clientName
    $result = [UIntPtr]::Zero
    if (-not [CtRestoreCloseQaNative]::SendMessageTimeoutW(
            $openButton, 0x00F5, [UIntPtr]::Zero, [IntPtr]::Zero,
            0x0003, 2000, [ref]$result
        )) {
        throw 'Could not reopen the disposable client through ctSpaces.'
    }
    $restoredWindow = Wait-ForTitledBrowserWindow
    $null = Wait-NewSessionSnapshot $secondSessionBefore
    $titleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $restoredTitle =
            [CtRestoreCloseQaNative]::GetWindowTitle($restoredWindow)
        $titleUpdated =
            $restoredTitle.StartsWith(
                "$clientName [$browserDisplayName] - ",
                [StringComparison]::OrdinalIgnoreCase
            ) -and $restoredTitle.EndsWith(
                ' - ctSpaces', [StringComparison]::Ordinal
            )
        if ($titleUpdated) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $titleDeadline)
    if (-not $titleUpdated) {
        throw "Client-first browser title did not apply: $restoredTitle"
    }
    $restoredProcessId =
        [int][CtRestoreCloseQaNative]::GetWindowProcessId($restoredWindow)
    if ($ownedProcesses.ContainsKey($restoredProcessId)) {
        $restoredProcess = $ownedProcesses[$restoredProcessId]
    } else {
        $restoredProcess = Register-OwnedProcess `
            ([Diagnostics.Process]::GetProcessById($restoredProcessId))
    }
    [void][CtRestoreCloseQaNative]::RequestSessionShutdown($restoredWindow)
    try {
        [void]$restoredProcess.WaitForExit(10000)
    } catch {
    }
    $secondTabs = Save-SessionEvidence (Wait-ForRestoredTabs $secondSessionBefore $expectedUrls) 'second-reopen'

    $liveConfigHashAfter = if (
        Test-Path -LiteralPath $liveConfigPath -PathType Leaf
    ) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $liveConfigPath).Hash
    } else {
        '<missing>'
    }
    if ($liveConfigHashAfter -ne $liveConfigHashBefore) {
        throw 'Browser-close QA modified the live ctSpaces preference file.'
    }

    $completed = $true
    [pscustomobject]@{
        Browser = [IO.Path]::GetFileName($resolvedBrowser)
        BrowserId = $browserId
        BrowserDisplayName = $browserDisplayName
        TabCount = $TabCount
        CloseMode = $CloseMode
        FirstRestoredTabs = $firstTabs
        SecondRestoredTabs = $secondTabs
        SeedCloseAllPromptApproved = $seedPromptApproved
        CtSpacesCloseAllPromptApproved = $ctSpacesPromptApproved
        BackgroundProfileReleased = $true
        RestoredAfterFirstBrowserClose = $true
        RestoredAfterSecondBrowserClose = $true
        RestoredWindowTitle = $restoredTitle
        LiveConfigUnchanged = $true
    } | ConvertTo-Json -Depth 5
} finally {
    if ($launcherWindow -ne [IntPtr]::Zero -and
        [CtRestoreCloseQaNative]::IsWindow($launcherWindow)) {
        [void][CtRestoreCloseQaNative]::PostMessageW(
            $launcherWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero
        )
    }
    foreach ($ownedProcess in @($ownedProcesses.Values)) {
        try {
            if (-not $ownedProcess.HasExited) {
                $ownedProcess.Kill()
                [void]$ownedProcess.WaitForExit(3000)
            }
        } catch {
        } finally {
            $ownedProcess.Dispose()
        }
    }

    try {
        $profileProcesses = Get-DisposableProfileProcesses
        foreach ($profileProcess in $profileProcesses) {
            Stop-DisposableProfileProcess $profileProcess
        }
        if ($profileProcesses) {
            Start-Sleep -Milliseconds 1200
        }
    } catch {
        Write-Warning "Could not query disposable browser processes: $_"
    }

    $resolvedQaRoot = [IO.Path]::GetFullPath($qaRoot)
    $allowedPrefix = [IO.Path]::GetFullPath($qaParent).TrimEnd('\') + '\'
    if ($resolvedQaRoot.StartsWith(
            $allowedPrefix, [StringComparison]::OrdinalIgnoreCase
        ) -and [IO.Path]::GetFileName($resolvedQaRoot).StartsWith(
            'run-', [StringComparison]::Ordinal
        ) -and (Test-Path -LiteralPath $resolvedQaRoot) -and $completed) {
        $cleanupDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            try {
                Remove-Item -LiteralPath $resolvedQaRoot -Recurse -Force `
                    -ErrorAction Stop
                break
            } catch {
                if ([DateTime]::UtcNow -ge $cleanupDeadline) {
                    throw
                }
                Start-Sleep -Milliseconds 300
            }
        } while (Test-Path -LiteralPath $resolvedQaRoot)
    } elseif (Test-Path -LiteralPath $resolvedQaRoot) {
        Write-Warning "Preserved browser restoration failure fixture: $resolvedQaRoot"
    }
}
