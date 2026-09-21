param(
    [string]$BrowserPath = '',
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$qaParent = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build\qa-close-all'))
$qaRoot = Join-Path $qaParent ("profile-{0}" -f [guid]::NewGuid().ToString('N'))

if ([string]::IsNullOrWhiteSpace($BrowserPath)) {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $env:LocalAppData 'Microsoft\Edge\Application\msedge.exe'),
        (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe'),
        (Join-Path $env:ProgramFiles 'BraveSoftware\Brave-Browser\Application\brave.exe')
    )
    $BrowserPath = $candidates | Where-Object {
        $_ -and (Test-Path -LiteralPath $_ -PathType Leaf)
    } | Select-Object -First 1
}

if (-not $BrowserPath -or -not (Test-Path -LiteralPath $BrowserPath -PathType Leaf)) {
    throw 'No supported Chromium browser was found for the coordinated-shutdown QA test.'
}

if (-not ('CtSpacesShutdownQa' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class CtSpacesShutdownQa
{
    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    private const uint WM_QUERYENDSESSION = 0x0011;
    private const uint WM_ENDSESSION = 0x0016;
    private const uint ENDSESSION_CLOSEAPP = 0x00000001;
    private const uint SMTO_BLOCK = 0x0001;
    private const uint SMTO_ABORTIFHUNG = 0x0002;
    private const int GW_OWNER = 4;
    private const int GWL_EXSTYLE = -20;
    private const long WS_EX_TOOLWINDOW = 0x00000080L;
    private const long WS_EX_NOACTIVATE = 0x08000000L;

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextLength(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(IntPtr hWnd, uint command);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    private static extern IntPtr GetWindowLongPtr64(IntPtr hWnd, int index);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongW")]
    private static extern IntPtr GetWindowLong32(IntPtr hWnd, int index);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr SendMessageTimeout(
        IntPtr hWnd,
        uint message,
        UIntPtr wParam,
        IntPtr lParam,
        uint flags,
        uint timeout,
        out UIntPtr result);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool PostMessage(
        IntPtr hWnd,
        uint message,
        UIntPtr wParam,
        IntPtr lParam);

    private static long GetExtendedStyle(IntPtr hWnd)
    {
        return IntPtr.Size == 8
            ? GetWindowLongPtr64(hWnd, GWL_EXSTYLE).ToInt64()
            : GetWindowLong32(hWnd, GWL_EXSTYLE).ToInt64();
    }

    public static bool RequestShutdown(uint processId)
    {
        IntPtr browserWindow = IntPtr.Zero;
        EnumWindows((hWnd, _) =>
        {
            uint windowProcessId;
            GetWindowThreadProcessId(hWnd, out windowProcessId);
            long exStyle = GetExtendedStyle(hWnd);
            if (windowProcessId == processId && IsWindowVisible(hWnd) &&
                GetWindowTextLength(hWnd) > 0 &&
                GetWindow(hWnd, GW_OWNER) == IntPtr.Zero &&
                (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) == 0)
            {
                browserWindow = hWnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);

        if (browserWindow == IntPtr.Zero)
            return false;

        UIntPtr ignored;
        SendMessageTimeout(
            browserWindow,
            WM_QUERYENDSESSION,
            UIntPtr.Zero,
            new IntPtr(ENDSESSION_CLOSEAPP),
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            1000,
            out ignored);

        return PostMessage(
            browserWindow,
            WM_ENDSESSION,
            new UIntPtr(1),
            new IntPtr(ENDSESSION_CLOSEAPP));
    }
}
'@
}

$process = $null
try {
    New-Item -ItemType Directory -Path $qaRoot -Force | Out-Null

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = [IO.Path]::GetFullPath($BrowserPath)
    $startInfo.UseShellExecute = $false
    $startInfo.ArgumentList.Add("--user-data-dir=$qaRoot")
    $startInfo.ArgumentList.Add('--no-first-run')
    $startInfo.ArgumentList.Add('--no-default-browser-check')
    $startInfo.ArgumentList.Add('--disable-sync')
    $startInfo.ArgumentList.Add('--disable-features=SyncPromo')
    $startInfo.ArgumentList.Add('--edge-skip-compat-layer-relaunch')
    $startInfo.ArgumentList.Add('--no-service-autorun')
    $startInfo.ArgumentList.Add('--new-window')
    $startInfo.ArgumentList.Add('data:text/html,<title>ctSpaces QA One</title>One')
    $startInfo.ArgumentList.Add('data:text/html,<title>ctSpaces QA Two</title>Two')

    $process = [Diagnostics.Process]::Start($startInfo)
    $windowDeadline = [DateTime]::UtcNow.AddSeconds(10)
    $shutdownSent = $false
    while (-not $shutdownSent -and [DateTime]::UtcNow -lt $windowDeadline) {
        if ($process.HasExited) {
            throw "The QA browser exited before a window was available (exit code $($process.ExitCode))."
        }
        $shutdownSent = [CtSpacesShutdownQa]::RequestShutdown([uint32]$process.Id)
        if (-not $shutdownSent) {
            Start-Sleep -Milliseconds 200
        }
    }

    if (-not $shutdownSent) {
        throw 'The QA browser did not create a primary window within 10 seconds.'
    }
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        throw "The QA browser did not exit within $TimeoutSeconds seconds."
    }

    Write-Host "Coordinated browser shutdown passed with $([IO.Path]::GetFileName($BrowserPath))."
}
finally {
    if ($process) {
        try {
            if (-not $process.HasExited) {
                $process.Kill()
                $null = $process.WaitForExit(5000)
            }
        } catch {
        } finally {
            $process.Dispose()
        }
    }

    $resolvedQaRoot = [IO.Path]::GetFullPath($qaRoot)
    $allowedPrefix = $qaParent.TrimEnd('\') + '\'
    if ($resolvedQaRoot.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedQaRoot)) {
        $cleanupDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            try {
                Remove-Item -LiteralPath $resolvedQaRoot -Recurse -Force -ErrorAction Stop
                break
            }
            catch {
                if ([DateTime]::UtcNow -ge $cleanupDeadline) {
                    throw
                }
                Start-Sleep -Milliseconds 300
            }
        } while (Test-Path -LiteralPath $resolvedQaRoot)
    }
}
