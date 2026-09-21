param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [int]$TimeoutSeconds = 12
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$qaParent = Join-Path $projectRoot 'build\qwf'
$runId = [Guid]::NewGuid().ToString('N')
$qaRoot = Join-Path $qaParent ('run-' + $runId.Substring(0, 10))
$qaExeDir = Join-Path $qaRoot 'a'
$qaExe = Join-Path $qaExeDir 'q.exe'
$qaDataDir = Join-Path $qaExeDir 'd'
$qaShortcutDir = Join-Path $qaExeDir 's'
$configPath = Join-Path $qaDataDir 'config.ini'
$clientSuffix = $runId.Substring(0, 10)
$alphaName = 'WFAlpha_' + $clientSuffix
$betaName = 'WFBeta_' + $clientSuffix
$gammaName = 'WFGamma_' + $clientSuffix
$caseOnlyName = $alphaName.ToLowerInvariant()
$renamedName = $alphaName + ' Renamed'
$alphaRoot = Join-Path $qaDataDir ('Sites\' + $alphaName)
$caseOnlyRoot = Join-Path $qaDataDir ('Sites\' + $caseOnlyName)
$edgeProfile = Join-Path $alphaRoot 'Browsers\edge\Profile'
$chromeProfile = Join-Path $alphaRoot 'Browsers\chrome\Profile'
$edgeSentinel = Join-Path $edgeProfile 'edge-slot-sentinel.txt'
$chromeSentinel = Join-Path $chromeProfile 'chrome-slot-sentinel.txt'
$edgeCookieLike = Join-Path $edgeProfile 'Default\Cookies.edge-qa-sentinel'
$chromeCookieLike = Join-Path $chromeProfile 'Default\Cookies.chrome-qa-sentinel'
$deepTailDirectory = 'Browsers\edge\Profile\Default'
$deepTailFileName = 'f' * (148 - $deepTailDirectory.Length)
$deepTailRelativePath = Join-Path $deepTailDirectory $deepTailFileName
if (1 + $deepTailRelativePath.Length -ne 150) {
    throw 'The case-only rename deep-tail fixture is not exactly 150 UTF-16 characters from the client root.'
}
$alphaDeepTailPath = Join-Path $alphaRoot $deepTailRelativePath
$liveDataDir = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces'
$liveConfigPath = Join-Path $liveDataDir 'config.ini'
$liveClientPaths = @(
    $alphaName, $caseOnlyName, $betaName, $gammaName, $renamedName
) |
    ForEach-Object { Join-Path $liveDataDir ('Sites\' + $_) }
$launcher = $null
$launcherWindow = [IntPtr]::Zero

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
foreach ($liveClientPath in $liveClientPaths) {
    if (Test-Path -LiteralPath $liveClientPath) {
        throw "Refusing to run because the unique live QA client exists: $liveClientPath"
    }
}

if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) {
    throw "Release executable not found: $resolvedExe"
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class CtWorkflowQaNative
{
    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

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

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback,
                                           IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hWnd,
                                                        out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassNameW(IntPtr hWnd, StringBuilder text,
                                            int maxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(IntPtr hWnd, StringBuilder text,
                                             int maxCount);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int controlId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessageW(IntPtr hWnd, uint message,
                                           UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SendMessageTimeoutW(
        IntPtr hWnd, uint message, UIntPtr wParam, IntPtr lParam,
        uint flags, uint timeoutMilliseconds, out UIntPtr result
    );

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode,
               SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool WritePrivateProfileStringW(
        string section, string key, string value, string filePath
    );

    [DllImport("shell32.dll", CharSet = CharSet.Unicode,
               PreserveSig = true)]
    private static extern int SHGetPropertyStoreFromParsingName(
        string path, IntPtr bindContext, uint flags, ref Guid interfaceId,
        [MarshalAs(UnmanagedType.Interface)] out IPropertyStore propertyStore
    );

    [DllImport("propsys.dll", CharSet = CharSet.Unicode,
               PreserveSig = true)]
    private static extern int PropVariantToStringAlloc(
        ref PropVariant value, out IntPtr result
    );

    [DllImport("ole32.dll", PreserveSig = true)]
    private static extern int PropVariantClear(ref PropVariant value);

    public static IntPtr FindWindow(uint processId, string className,
                                    string title)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint ownerProcessId;
            GetWindowThreadProcessId(hWnd, out ownerProcessId);
            if (ownerProcessId != processId)
                return true;

            StringBuilder classText = new StringBuilder(128);
            GetClassNameW(hWnd, classText, classText.Capacity);
            if (!String.IsNullOrEmpty(className) &&
                !String.Equals(classText.ToString(), className,
                               StringComparison.Ordinal))
                return true;

            StringBuilder titleText = new StringBuilder(256);
            GetWindowTextW(hWnd, titleText, titleText.Capacity);
            if (!String.IsNullOrEmpty(title) &&
                !String.Equals(titleText.ToString(), title,
                               StringComparison.Ordinal))
                return true;
            found = hWnd;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    public static string GetShortcutAppId(string shortcutPath) {
        var interfaceId = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
        IPropertyStore propertyStore = null;
        var result = SHGetPropertyStoreFromParsingName(
            shortcutPath, IntPtr.Zero, 0, ref interfaceId, out propertyStore
        );
        if (result < 0 || propertyStore == null)
            return String.Empty;

        var key = new PropertyKey(
            new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"), 5
        );
        var value = new PropVariant();
        try {
            result = propertyStore.GetValue(ref key, out value);
            if (result < 0)
                return String.Empty;
            IntPtr textPointer;
            result = PropVariantToStringAlloc(ref value, out textPointer);
            if (result < 0 || textPointer == IntPtr.Zero)
                return String.Empty;
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

function Wait-Until {
    param([scriptblock]$Condition, [string]$Failure)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $value = & $Condition
        if ($value) {
            return $value
        }
        Start-Sleep -Milliseconds 75
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Failure
}

function Send-WindowMessage {
    param(
        [IntPtr]$Window,
        [uint32]$Message,
        [UIntPtr]$WParam = [UIntPtr]::Zero,
        [IntPtr]$LParam = [IntPtr]::Zero
    )

    $result = [UIntPtr]::Zero
    if (-not [CtWorkflowQaNative]::SendMessageTimeoutW(
            $Window, $Message, $WParam, $LParam, 0x0003, 2500,
            [ref]$result
        )) {
        throw "Window message 0x$($Message.ToString('X')) timed out."
    }
    $result
}

function Set-WindowTextValue {
    param([IntPtr]$Window, [string]$Text)

    $pointer = [Runtime.InteropServices.Marshal]::StringToHGlobalUni($Text)
    try {
        [void](Send-WindowMessage -Window $Window -Message 0x000C `
            -LParam $pointer)
    } finally {
        [Runtime.InteropServices.Marshal]::FreeHGlobal($pointer)
    }
}

function Invoke-AppCommand {
    param([int]$Command)

    if (-not [CtWorkflowQaNative]::PostMessageW(
            $launcherWindow, 0x0111, [UIntPtr]::new([uint32]$Command),
            [IntPtr]::Zero
        )) {
        throw "Could not post app command $Command."
    }
}

function Wait-ForConfigPattern {
    param([string]$Pattern, [switch]$CaseSensitive)

    try {
        [void](Wait-Until {
            if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
                return $false
            }
            $configText = [IO.File]::ReadAllText($configPath)
            if ($CaseSensitive) {
                return $configText -cmatch $Pattern
            }
            $configText -match $Pattern
        } "Configuration did not match: $Pattern")
    } catch {
        $actualConfig = if (Test-Path -LiteralPath $configPath -PathType Leaf) {
            [IO.File]::ReadAllText($configPath)
        } else {
            '<missing>'
        }
        throw "$($_.Exception.Message)`nActual configuration:`n$actualConfig"
    }
}

function Set-QaRenameTarget {
    param([Parameter(Mandatory = $true)][string]$ClientName)

    if (-not [CtWorkflowQaNative]::WritePrivateProfileStringW(
            'qa', 'rename_to', $ClientName, $configPath
        )) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Could not update the isolated QA rename target (Windows error $errorCode)."
    }
    $targetRegex = [regex]::Escape($ClientName)
    Wait-ForConfigPattern `
        -Pattern "(?ms)^\[qa\].*?^rename_to=${targetRegex}\r?$" `
        -CaseSensitive
}

function Get-ExactChildDirectoryNames {
    param([Parameter(Mandatory = $true)][string]$Parent)

    if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
        return @()
    }
    @([IO.Directory]::EnumerateDirectories($Parent) | ForEach-Object {
        [IO.Path]::GetFileName($_)
    })
}

function Get-ExactChildFileNames {
    param([Parameter(Mandatory = $true)][string]$Parent)

    if (-not (Test-Path -LiteralPath $Parent -PathType Container)) {
        return @()
    }
    @([IO.Directory]::EnumerateFiles($Parent) | ForEach-Object {
        [IO.Path]::GetFileName($_)
    })
}

function Assert-V2SlotSentinels {
    param([Parameter(Mandatory = $true)][string]$ClientRoot)

    $expected = @{
        (Join-Path $ClientRoot 'Browsers\edge\Profile\edge-slot-sentinel.txt') =
            'edge-slot-only'
        (Join-Path $ClientRoot 'Browsers\edge\Profile\Default\Cookies.edge-qa-sentinel') =
            'edge-cookie-like-only'
        (Join-Path $ClientRoot 'Browsers\chrome\Profile\chrome-slot-sentinel.txt') =
            'chrome-slot-only'
        (Join-Path $ClientRoot 'Browsers\chrome\Profile\Default\Cookies.chrome-qa-sentinel') =
            'chrome-cookie-like-only'
    }
    foreach ($entry in $expected.GetEnumerator()) {
        if (-not (Test-Path -LiteralPath $entry.Key -PathType Leaf) -or
            [IO.File]::ReadAllText($entry.Key) -ne $entry.Value) {
            throw "A v2 browser-slot sentinel changed or disappeared: $($entry.Key)"
        }
    }
    $crossed = @(
        (Join-Path $ClientRoot 'Browsers\chrome\Profile\edge-slot-sentinel.txt'),
        (Join-Path $ClientRoot 'Browsers\chrome\Profile\Default\Cookies.edge-qa-sentinel'),
        (Join-Path $ClientRoot 'Browsers\edge\Profile\chrome-slot-sentinel.txt'),
        (Join-Path $ClientRoot 'Browsers\edge\Profile\Default\Cookies.chrome-qa-sentinel')
    ) | Where-Object { Test-Path -LiteralPath $_ }
    if ($crossed.Count -gt 0) {
        throw "Browser-slot data crossed isolation boundaries: $($crossed -join ', ')"
    }
}

function Assert-LiveDataUnchanged {
    if ((Get-FileFingerprint $liveConfigPath) -ne $liveConfigBefore) {
        throw 'Workflow QA changed the live ctSpaces configuration.'
    }
    foreach ($liveClientPath in $liveClientPaths) {
        if (Test-Path -LiteralPath $liveClientPath) {
            throw "Workflow QA created a client below live Sites: $liveClientPath"
        }
    }
}

try {
    New-Item -ItemType Directory -Path $qaExeDir -Force | Out-Null
    New-Item -ItemType Directory -Path $qaDataDir -Force | Out-Null
    New-Item -ItemType Directory -Path $qaShortcutDir -Force | Out-Null
    Copy-Item -LiteralPath $resolvedExe -Destination $qaExe -Force
    New-Item -ItemType File -Path (Join-Path $qaExeDir 'ctSpaces.portable') `
        -Force | Out-Null

    $utf8NoBom = [Text.UTF8Encoding]::new($false)
    foreach ($client in @($betaName, $gammaName)) {
        $clientPath = Join-Path $qaDataDir "Sites\$client"
        New-Item -ItemType Directory -Path $clientPath -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $clientPath 'Default') `
            -Force | Out-Null
        New-Item -ItemType File -Path (Join-Path $clientPath 'ctSpaces') `
            -Force | Out-Null
        [IO.File]::WriteAllText(
            (Join-Path $clientPath 'ctSpaces-legacy-browser-v2'),
            "ctSpaces-legacy-browser-schema=2`r`nbrowser=edge`r`n",
            [Text.ASCIIEncoding]::new()
        )
    }
    foreach ($profile in @($edgeProfile, $chromeProfile)) {
        New-Item -ItemType Directory -Path (Join-Path $profile 'Default') `
            -Force | Out-Null
    }
    [IO.File]::WriteAllText(
        (Join-Path $alphaRoot 'ctSpaces-client-v2'),
        "ctSpaces-client-schema=2`r`n", $utf8NoBom
    )
    foreach ($slot in @(
        @{ Root = (Join-Path $alphaRoot 'Browsers\edge'); Id = 'edge' },
        @{ Root = (Join-Path $alphaRoot 'Browsers\chrome'); Id = 'chrome' }
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
        $alphaDeepTailPath, 'case-only-deep-tail', $utf8NoBom
    )
    Copy-Item -LiteralPath (Join-Path $projectRoot 'icons\ctdkgrsq.ico') `
        -Destination (Join-Path $alphaRoot 'client.ico') -Force
    [IO.File]::WriteAllText(
        $configPath,
        "[user]`r`ntheme_name=Dark - Gothic`r`nbrowser=edge`r`n" +
        "client_title_first=0`r`n`r`n" +
        "[pinned]`r`ncount=2`r`nclient0=$alphaName`r`nclient1=$betaName`r`n`r`n" +
        "[restore_tabs]`r`ndisabled_count=2`r`n" +
        "disabled_client0=$alphaName|edge`r`n" +
        "disabled_client1=$alphaName|chrome`r`n`r`n" +
        "[archived]`r`ncount=1`r`nclient0=$gammaName`r`n`r`n" +
        "[qa]`r`nrename_to=$caseOnlyName`r`n",
        $utf8NoBom
    )
    Assert-V2SlotSentinels $alphaRoot

    $arguments = '--qa-instance=' + $runId.Substring(0, 10) +
        ' --qa-data-dir="' + $qaDataDir + '"' +
        ' --qa-shortcut-dir="' + $qaShortcutDir + '"'
    $launcher = Start-Process -FilePath $qaExe -ArgumentList $arguments `
        -PassThru
    [void]$launcher.WaitForInputIdle(10000)
    $launcherWindow = Wait-Until {
        $window = [CtWorkflowQaNative]::FindWindow(
            [uint32]$launcher.Id, 'ctSpacesLauncherClass', ''
        )
        if ($window -ne [IntPtr]::Zero) { $window }
    } 'Timed out waiting for the workflow QA launcher.'

    try {
        $clientEdit = Wait-Until {
            $control = [CtWorkflowQaNative]::GetDlgItem($launcherWindow, 206)
            if ($control -ne [IntPtr]::Zero) { $control }
        } 'The client editor was not created.'
    } catch {
        if ($launcher.HasExited) {
            throw "The workflow QA launcher exited before creating the client editor (exit code $($launcher.ExitCode))."
        }
        $startupDiagnostic = Join-Path $qaDataDir 'qa-startup-error.txt'
        if (Test-Path -LiteralPath $startupDiagnostic -PathType Leaf) {
            $details = (Get-Content -Raw -LiteralPath $startupDiagnostic).Trim()
            throw "The client editor was not created. ctSpaces reported: $details"
        }
        $currentLauncherWindow = [CtWorkflowQaNative]::FindWindow(
            [uint32]$launcher.Id, 'ctSpacesLauncherClass', ''
        )
        $controlState = @(
            102, 200, 201, 205, 206, 207
        ) | ForEach-Object {
            $handle = [CtWorkflowQaNative]::GetDlgItem($launcherWindow, $_)
            "$_=$($handle.ToInt64())"
        }
        $freshControlState = @(
            102, 200, 201, 205, 206, 207
        ) | ForEach-Object {
            $handle = [CtWorkflowQaNative]::GetDlgItem($currentLauncherWindow, $_)
            "$_=$($handle.ToInt64())"
        }
        throw "The client editor was not created. Captured window=$($launcherWindow.ToInt64()) (valid=$([CtWorkflowQaNative]::IsWindow($launcherWindow))); current window=$($currentLauncherWindow.ToInt64()). Captured controls: $($controlState -join ', '). Current controls: $($freshControlState -join ', ')."
    }
    $clientCombo = Wait-Until {
        $control = [CtWorkflowQaNative]::GetDlgItem($launcherWindow, 102)
        if ($control -ne [IntPtr]::Zero) { $control }
    } 'The client list was not created.'

    $alphaRegex = [regex]::Escape($alphaName)
    $betaRegex = [regex]::Escape($betaName)
    $gammaRegex = [regex]::Escape($gammaName)
    $caseOnlyRegex = [regex]::Escape($caseOnlyName)
    $renamedRegex = [regex]::Escape($renamedName)
    $gammaPointer = [Runtime.InteropServices.Marshal]::StringToHGlobalUni(
        $gammaName
    )
    try {
        $gammaIndex = Send-WindowMessage -Window $clientCombo `
            -Message 0x0158 -WParam ([UIntPtr]::new([uint32]::MaxValue)) `
            -LParam $gammaPointer
    } finally {
        [Runtime.InteropServices.Marshal]::FreeHGlobal($gammaPointer)
    }
    if ($gammaIndex.ToUInt64() -ne [uint64]::MaxValue) {
        throw 'The archived client remained in the normal client list.'
    }
    Wait-ForConfigPattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${alphaRegex}\|edge\r?$"
    Wait-ForConfigPattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${alphaRegex}\|chrome\r?$"

    $reorderResult = Send-WindowMessage -Window $launcherWindow `
        -Message (0x8000 + 10) -LParam ([IntPtr]::new(1))
    if ($reorderResult -eq [UIntPtr]::Zero) {
        throw 'The QA pin reorder command was rejected.'
    }
    Wait-ForConfigPattern "(?ms)^\[pinned\].*?^client0=${betaRegex}\r?$.*?^client1=${alphaRegex}\r?$"

    $shell = New-Object -ComObject WScript.Shell
    $historicalEdgeShortcutPath = Join-Path $qaShortcutDir `
        ($alphaName + ' - Microsoft Edge - ctSpaces.lnk')
    $historicalEdgeShortcut = $shell.CreateShortcut(
        $historicalEdgeShortcutPath
    )
    $historicalEdgeShortcut.TargetPath = $qaExe
    $historicalEdgeShortcut.Arguments =
        "--client $alphaName --browser edge"
    $historicalEdgeShortcut.WorkingDirectory = $qaExeDir
    $historicalEdgeShortcut.IconLocation = Join-Path $alphaRoot 'client.ico'
    $historicalEdgeShortcut.Save()
    $historicalNeutralShortcutPath = Join-Path $qaShortcutDir `
        ($alphaName + ' - ctSpaces.lnk')
    $historicalNeutralShortcut = $shell.CreateShortcut(
        $historicalNeutralShortcutPath
    )
    $historicalNeutralShortcut.TargetPath = $qaExe
    $historicalNeutralShortcut.Arguments = "--client $alphaName"
    $historicalNeutralShortcut.WorkingDirectory = $qaExeDir
    $historicalNeutralShortcut.IconLocation = Join-Path $alphaRoot 'client.ico'
    $historicalNeutralShortcut.Save()

    Set-WindowTextValue $clientEdit $alphaName
    $shortcutResult = Send-WindowMessage -Window $launcherWindow `
        -Message (0x8000 + 12)
    if ($shortcutResult -eq [UIntPtr]::Zero) {
        throw 'The QA desktop-shortcut command was rejected.'
    }
    $alphaShortcutPath = Join-Path $qaShortcutDir ($alphaName + '.lnk')
    [void](Wait-Until {
        (Test-Path -LiteralPath $alphaShortcutPath -PathType Leaf) -and
        -not (Test-Path -LiteralPath $historicalEdgeShortcutPath) -and
        -not (Test-Path -LiteralPath $historicalNeutralShortcutPath)
    } 'The client-only shortcut was not created or verified legacy shortcuts were not retired.')
    $alphaShortcut = $shell.CreateShortcut($alphaShortcutPath)
    if ($alphaShortcut.TargetPath -ne $qaExe -or
        $alphaShortcut.Arguments -cne (
            "--client $alphaName --browser edge"
        ) -or
        $alphaShortcut.Description -cne $alphaName) {
        throw 'The client-only shortcut target, title, or selected Edge argument is incorrect.'
    }
    $expectedEdgeShortcutAppId =
        [CtWorkflowQaNative]::GetExpectedAppUserModelId($alphaName, 'edge')
    $observedEdgeShortcutAppId =
        [CtWorkflowQaNative]::GetShortcutAppId($alphaShortcutPath)
    if ($observedEdgeShortcutAppId -cne $expectedEdgeShortcutAppId) {
        throw "The client-only shortcut AppID was '$observedEdgeShortcutAppId' instead of the exact selected-Edge identity '$expectedEdgeShortcutAppId'."
    }
    $shortcutUsedClientIconPath =
        $alphaShortcut.IconLocation -match 'client\.ico'

    $qaShortcutDrive = [IO.DriveInfo]::new(
        [IO.Path]::GetPathRoot($qaShortcutDir)
    )
    if ($qaShortcutDrive.DriveFormat -ne 'NTFS') {
        throw 'The replace-existing shortcut tunneling test requires NTFS.'
    }
    $tunneledCreationSentinel = [DateTime]::SpecifyKind(
        [DateTime]'2001-02-03T04:05:06', [DateTimeKind]::Utc
    )
    [IO.File]::SetCreationTimeUtc(
        $alphaShortcutPath, $tunneledCreationSentinel
    )
    $expectedTunneledCreationTime =
        [IO.File]::GetCreationTimeUtc($alphaShortcutPath)
    Invoke-AppCommand 42002
    Wait-ForConfigPattern '(?m)^browser=chrome\r?$'
    $replacementShortcutResult = Send-WindowMessage `
        -Window $launcherWindow -Message (0x8000 + 12)
    if ($replacementShortcutResult -eq [UIntPtr]::Zero) {
        throw 'Replacing the existing owned shortcut was rejected.'
    }
    $replacementShortcut = $shell.CreateShortcut($alphaShortcutPath)
    if ($replacementShortcut.TargetPath -ne $qaExe -or
        $replacementShortcut.Arguments -cne (
            "--client $alphaName --browser chrome"
        )) {
        throw 'The same client-only shortcut was not retargeted to the selected Chrome browser.'
    }
    $expectedChromeShortcutAppId =
        [CtWorkflowQaNative]::GetExpectedAppUserModelId($alphaName, 'chrome')
    $observedChromeShortcutAppId =
        [CtWorkflowQaNative]::GetShortcutAppId($alphaShortcutPath)
    if ($observedChromeShortcutAppId -cne $expectedChromeShortcutAppId -or
        $observedChromeShortcutAppId -ceq $observedEdgeShortcutAppId) {
        throw "The recreated shortcut AppID was '$observedChromeShortcutAppId' instead of the distinct selected-Chrome identity '$expectedChromeShortcutAppId'."
    }
    if ([IO.File]::GetCreationTimeUtc($alphaShortcutPath) -ne
        $expectedTunneledCreationTime) {
        throw 'NTFS did not tunnel the vacated shortcut creation time into the replacement fixture.'
    }
    $shortcutTransactionResidue = @(
        Get-ChildItem -LiteralPath $qaShortcutDir -File |
            Where-Object { $_.Name -like '~*.lnk' }
    )
    if ($shortcutTransactionResidue.Count -ne 0) {
        throw "Replacing the owned shortcut left quarantine/staging residue: $($shortcutTransactionResidue.Name -join ', ')"
    }

    $betaShortcutPath = Join-Path $qaShortcutDir ($betaName + '.lnk')
    $unrelatedShortcut = $shell.CreateShortcut($betaShortcutPath)
    $unrelatedShortcut.TargetPath = $qaExe
    $unrelatedShortcut.Arguments =
        "--client $gammaName --browser edge"
    $unrelatedShortcut.WorkingDirectory = $qaExeDir
    $unrelatedShortcut.Save()
    $unrelatedShortcutHash =
        (Get-FileHash -LiteralPath $betaShortcutPath -Algorithm SHA256).Hash
    Set-WindowTextValue $clientEdit $betaName
    $unrelatedReplacementResult = Send-WindowMessage `
        -Window $launcherWindow -Message (0x8000 + 12)
    if ($unrelatedReplacementResult -ne [UIntPtr]::Zero -or
        (Get-FileHash -LiteralPath $betaShortcutPath -Algorithm SHA256).Hash `
            -cne $unrelatedShortcutHash) {
        throw 'Shortcut creation replaced an unrelated same-named Desktop link.'
    }
    Set-WindowTextValue $clientEdit $alphaName

    $caseOnlyRenameResult = Send-WindowMessage -Window $launcherWindow `
        -Message (0x8000 + 13)
    if ($caseOnlyRenameResult -eq [UIntPtr]::Zero) {
        throw 'The QA case-only safe-rename command was rejected.'
    }
    $sitesDirectory = Join-Path $qaDataDir 'Sites'
    [void](Wait-Until {
        $exactNames = @(Get-ExactChildDirectoryNames $sitesDirectory)
        ($exactNames -ccontains $caseOnlyName) -and
        -not ($exactNames -ccontains $alphaName)
    } 'The case-only rename did not commit the exact requested directory casing.')
    Assert-V2SlotSentinels $caseOnlyRoot
    $caseOnlyDeepTailPath = Join-Path $caseOnlyRoot $deepTailRelativePath
    if (-not (Test-Path -LiteralPath $caseOnlyDeepTailPath -PathType Leaf)) {
        throw 'The case-only rename lost the valid 150-character managed file tail.'
    }
    Wait-ForConfigPattern `
        -Pattern "(?ms)^\[pinned\].*?^client0=${betaRegex}\r?$.*?^client1=${caseOnlyRegex}\r?$" `
        -CaseSensitive
    Wait-ForConfigPattern `
        -Pattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${caseOnlyRegex}\|edge\r?$" `
        -CaseSensitive
    Wait-ForConfigPattern `
        -Pattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${caseOnlyRegex}\|chrome\r?$" `
        -CaseSensitive

    $caseOnlyShortcutFileName = $caseOnlyName + '.lnk'
    $caseOnlyShortcutPath =
        Join-Path $qaShortcutDir $caseOnlyShortcutFileName
    [void](Wait-Until {
        $exactShortcutNames = @(Get-ExactChildFileNames $qaShortcutDir)
        ($exactShortcutNames -ccontains $caseOnlyShortcutFileName) -and
        -not ($exactShortcutNames -ccontains ($alphaName + '.lnk'))
    } 'The one client shortcut did not migrate to the exact case-only name.')
    $caseOnlyShortcut = $shell.CreateShortcut($caseOnlyShortcutPath)
    if ($caseOnlyShortcut.TargetPath -ne $qaExe -or
        $caseOnlyShortcut.Arguments -cne (
            "--client $caseOnlyName --browser chrome"
        )) {
        throw 'The case-only client shortcut lost its selected-browser identity.'
    }

    Set-QaRenameTarget $renamedName
    Set-WindowTextValue $clientEdit $caseOnlyName
    $alphaRoot = $caseOnlyRoot
    $alphaShortcutPath = $caseOnlyShortcutPath

    $renameResult = Send-WindowMessage -Window $launcherWindow `
        -Message (0x8000 + 13)
    if ($renameResult -eq [UIntPtr]::Zero) {
        throw 'The QA safe-rename command was rejected.'
    }
    $renamedProfilePath = Join-Path $qaDataDir ('Sites\' + $renamedName)
    [void](Wait-Until {
        (Test-Path -LiteralPath $renamedProfilePath -PathType Container) -and
        -not (Test-Path -LiteralPath $alphaRoot)
    } 'The client profile folder was not renamed safely.')
    Assert-V2SlotSentinels $renamedProfilePath
    if (-not (Test-Path -LiteralPath `
            (Join-Path $renamedProfilePath $deepTailRelativePath) `
            -PathType Leaf)) {
        throw 'The normal rename lost the deep-tail file after the case-only transaction.'
    }
    Wait-ForConfigPattern "(?ms)^\[pinned\].*?^client0=${betaRegex}\r?$.*?^client1=${renamedRegex}\r?$"
    Wait-ForConfigPattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${renamedRegex}\|edge\r?$"
    Wait-ForConfigPattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${renamedRegex}\|chrome\r?$"
    $renamedShortcutPath = Join-Path $qaShortcutDir ($renamedName + '.lnk')
    [void](Wait-Until {
        (Test-Path -LiteralPath $renamedShortcutPath -PathType Leaf) -and
        -not (Test-Path -LiteralPath $alphaShortcutPath)
    } 'The one client shortcut did not migrate with the rename.')
    $renamedShortcut = $shell.CreateShortcut($renamedShortcutPath)
    if ($renamedShortcut.TargetPath -ne $qaExe -or
        $renamedShortcut.Arguments -notmatch (
            '--client\s+"' + $renamedRegex + '"'
        ) -or
        $renamedShortcut.Arguments -notmatch '--browser\s+chrome') {
        throw 'The renamed client shortcut lost its client or selected browser.'
    }

    $archiveResult = Send-WindowMessage -Window $launcherWindow `
        -Message (0x8000 + 14)
    if ($archiveResult -eq [UIntPtr]::Zero) {
        throw 'The QA archive command was rejected.'
    }
    Wait-ForConfigPattern "(?ms)^\[archived\].*?^count=2\r?$.*?^client1=${renamedRegex}\r?$"
    Wait-ForConfigPattern "(?ms)^\[pinned\].*?^count=1\r?$.*?^client0=${betaRegex}\r?$"
    if (-not (Test-Path -LiteralPath $renamedProfilePath -PathType Container)) {
        throw 'Archiving moved or deleted the browser profile.'
    }
    Assert-V2SlotSentinels $renamedProfilePath
    Wait-ForConfigPattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${renamedRegex}\|edge\r?$"
    Wait-ForConfigPattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${renamedRegex}\|chrome\r?$"
    if (-not (Test-Path -LiteralPath $renamedShortcutPath -PathType Leaf)) {
        throw 'Archiving removed the client shortcut.'
    }

    $restoreResult = Send-WindowMessage -Window $launcherWindow `
        -Message (0x8000 + 11) -WParam ([UIntPtr]::new(1))
    if ($restoreResult -eq [UIntPtr]::Zero) {
        throw 'The QA archived-client restore command was rejected.'
    }
    Wait-ForConfigPattern "(?ms)^\[archived\].*?^count=1\r?$.*?^client0=${gammaRegex}\r?$"
    Wait-ForConfigPattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${renamedRegex}\|edge\r?$"
    Wait-ForConfigPattern "(?ms)^\[restore_tabs\].*?^disabled_client\d+=${renamedRegex}\|chrome\r?$"
    Assert-V2SlotSentinels $renamedProfilePath
    if (-not (Test-Path -LiteralPath $renamedShortcutPath -PathType Leaf)) {
        throw 'Restoring the archived client lost its shortcut.'
    }
    if ((Get-FileHash -LiteralPath $betaShortcutPath -Algorithm SHA256).Hash `
            -cne $unrelatedShortcutHash) {
        throw 'Rename or archive changed the unrelated same-named shortcut fixture.'
    }

    Invoke-AppCommand 41016
    Wait-ForConfigPattern '(?m)^client_title_first=1\r?$'
    Assert-LiveDataUnchanged

    [pscustomobject]@{
        PinnedOrderPersisted = $true
        ArchivedClientHidden = $true
        ExactClientOnlyShortcutCreated = $true
        HistoricalBrowserAndNeutralShortcutsRetired = $true
        SamePathShortcutRetargetedFromEdgeToChrome = $true
        SelectedBrowserShortcutAppIdRetargeted = $true
        ExistingShortcutReplacementSurvivedCreationTimeTunneling = $true
        UnrelatedSameNamedShortcutPreserved = $true
        ShortcutUsedClientIconPath = $shortcutUsedClientIconPath
        V2EdgeAndChromeSlotsPreserved = $true
        CaseOnlyRenameExactDirectoryCasing = $true
        CaseOnlyRenameMigratedPin = $true
        CaseOnlyRenameMigratedBothRestoreSettings = $true
        CaseOnlyRenameMigratedSingleShortcut = $true
        CaseOnlyRenamePreserved150CharacterTail = $true
        RenameMovedProfile = $true
        RenameMigratedPin = $true
        RenameMigratedBothRestoreSettings = $true
        RenameMigratedSingleShortcut = $true
        RenamedShortcutUsesNewClient = $true
        ArchivePreservedProfile = $true
        ArchiveRetainedBothRestoreSettings = $true
        ArchiveRetainedSingleShortcut = $true
        ArchivedClientRestored = $true
        ClientTitlePreferencePersisted = $true
        LiveConfigUnchanged = $true
        LiveSitesUnchanged = $true
    } | ConvertTo-Json
} finally {
    if ($launcherWindow -ne [IntPtr]::Zero -and
        [CtWorkflowQaNative]::IsWindow($launcherWindow)) {
        [void][CtWorkflowQaNative]::PostMessageW(
            $launcherWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero
        )
    }
    if ($launcher) {
        try {
            [void]$launcher.WaitForExit(5000)
            if (-not $launcher.HasExited) {
                $launcher.Kill()
                [void]$launcher.WaitForExit(5000)
            }
        } catch {
        } finally {
            $launcher.Dispose()
        }
    }

    $resolvedQaRoot = [IO.Path]::GetFullPath($qaRoot)
    $allowedPrefix = [IO.Path]::GetFullPath($qaParent).TrimEnd('\') + '\'
    if ($resolvedQaRoot.StartsWith(
            $allowedPrefix, [StringComparison]::OrdinalIgnoreCase
        ) -and [IO.Path]::GetFileName($resolvedQaRoot).StartsWith('run-') -and
        (Test-Path -LiteralPath $resolvedQaRoot)) {
        Remove-Item -LiteralPath $resolvedQaRoot -Recurse -Force
    }
    Assert-LiveDataUnchanged
}
