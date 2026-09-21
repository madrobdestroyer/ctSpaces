param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\quick-tour-ui'),
    [uint32[]]$ExpectedDpis = @()
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe = [IO.Path]::GetFullPath($ExePath)
$artifactRoot = [IO.Path]::GetFullPath($ArtifactDirectory)
$run = [Guid]::NewGuid().ToString('N').Substring(0,10)
$qa = Join-Path $root ('build\quick-tour-' + $run)
$app = Join-Path $qa 'app'
$data = Join-Path $app 'data'
$config = Join-Path $data 'config.ini'
$copiedExe = Join-Path $app 'quick-tour-qa.exe'
$utf8 = [Text.UTF8Encoding]::new($false)
$process = $null
$main = [IntPtr]::Zero
$owned = @()
$completed = $false
$oldDpiContext = [IntPtr]::Zero
$liveConfig = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'
function Get-Fingerprint([string]$Path) { if (Test-Path -LiteralPath $Path -PathType Leaf) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { '<missing>' } }
function Get-StableFingerprint([string]$Path) {
    $previous='';$same=0
    for($attempt=0;$attempt-lt 20;$attempt++){Start-Sleep -Milliseconds 100;$current=Get-Fingerprint $Path;if($current-eq$previous){$same++}else{$previous=$current;$same=0};if($same-ge 3){return $current}}
    throw "Configuration did not settle: $Path"
}
$liveConfigBefore = Get-Fingerprint $liveConfig
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing executable: $exe" }

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class QuickTourQa {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
  [StructLayout(LayoutKind.Sequential)] struct CopyDataStruct { public UIntPtr Tag; public uint Bytes; public IntPtr Text; }
  delegate bool EnumProc(IntPtr w, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w, out uint p);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr w,StringBuilder b,int n);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr w,StringBuilder b,int n);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr w,uint m,IntPtr p,StringBuilder b);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr w,int id);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr w);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr w);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w,out Rect r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr w,out Rect r);
  [DllImport("user32.dll")] public static extern int MapWindowPoints(IntPtr f,IntPtr t,ref Rect r,uint n);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr w);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr w,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr w,int command);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr w,uint m,UIntPtr p,IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process,uint kind);
  [DllImport("user32.dll")] public static extern int GetWindowRgnBox(IntPtr w,out Rect r);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool SendMessageTimeoutW(IntPtr w,uint m,UIntPtr p,IntPtr l,uint f,uint ms,out UIntPtr result);
  public static string Text(IntPtr w){var b=new StringBuilder(32768);GetWindowTextW(w,b,b.Capacity);return b.ToString();}
  public static string ControlText(IntPtr w){var b=new StringBuilder(32768);ReadText(w,0xD,(IntPtr)b.Capacity,b);return b.ToString();}
  public static IntPtr Find(uint pid,string cls,string title){IntPtr hit=IntPtr.Zero;EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner!=pid)return true;var b=new StringBuilder(256);GetClassNameW(w,b,b.Capacity);if((String.IsNullOrEmpty(cls)||b.ToString()==cls)&&(String.IsNullOrEmpty(title)||Text(w)==title)){hit=w;return false;}return true;},IntPtr.Zero);return hit;}
  public static Rect ChildRect(IntPtr parent,int id){Rect r;if(!GetWindowRect(GetDlgItem(parent,id),out r))throw new Exception("missing control "+id);MapWindowPoints(IntPtr.Zero,parent,ref r,2);return r;}
  public static bool CopyData(IntPtr w,string value){IntPtr text=Marshal.StringToHGlobalUni(value),data=IntPtr.Zero;try{var c=new CopyDataStruct{Tag=new UIntPtr(0x43545350u),Bytes=checked((uint)((value.Length+1)*2)),Text=text};data=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(CopyDataStruct)));Marshal.StructureToPtr(c,data,false);UIntPtr result;return SendMessageTimeoutW(w,0x4A,UIntPtr.Zero,data,3,5000,out result)&&result!=UIntPtr.Zero;}finally{if(data!=IntPtr.Zero)Marshal.FreeHGlobal(data);Marshal.FreeHGlobal(text);}}
  public static void ClosePidWindows(uint pid){EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==pid)PostMessageW(w,0x10,UIntPtr.Zero,IntPtr.Zero);return true;},IntPtr.Zero);}
}
'@
$oldDpiContext = [QuickTourQa]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))

function Wait-Until([scriptblock]$Check,[string]$Failure,[int]$Seconds=12){
    $end=[DateTime]::UtcNow.AddSeconds($Seconds)
    do{$value=&$Check;if($value-is[IntPtr]){if($value-ne[IntPtr]::Zero){return $value}}elseif($value){return $value};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end)
    throw $Failure
}
function Assert($ok,[string]$message){if(-not $ok){throw $message}}
function Start-QaApp {
    $script:process=Start-Process -FilePath $copiedExe -ArgumentList ('--qa-instance='+$run+' --qa-data-dir="'+$data+'"') -PassThru
    [void]$script:process.WaitForInputIdle(10000)
    $script:main=Wait-Until{$script:process.Refresh();if($script:process.HasExited){throw "Quick Tour fixture exited: $($script:process.ExitCode)"};[QuickTourQa]::Find([uint32]$script:process.Id,'ctSpacesLauncherClass','')} 'Launcher did not open.'
}
function Stop-QaApp {
    if($script:main-ne[IntPtr]::Zero-and[QuickTourQa]::IsWindow($script:main)){[void][QuickTourQa]::PostMessageW($script:main,0x10,[UIntPtr]::Zero,[IntPtr]::Zero)}
    if($script:process){[void]$script:process.WaitForExit(5000);if(-not$script:process.HasExited){$script:process.Kill();[void]$script:process.WaitForExit(5000)};$script:process.Dispose()}
    $script:process=$null;$script:main=[IntPtr]::Zero
}
function Find-Tour([int]$Seconds=10){Wait-Until{[QuickTourQa]::Find([uint32]$process.Id,'#32770','ctSpaces Quick Tour')} 'Quick Tour did not open.' $Seconds}
function Find-Frame([int]$Seconds=10){Wait-Until{[QuickTourQa]::Find([uint32]$process.Id,'ctSpacesQuickTourHighlight','')} 'Quick Tour highlight did not open.' $Seconds}
function Open-Tour {[void][QuickTourQa]::PostMessageW($main,0x111,[UIntPtr]41019,[IntPtr]::Zero);Find-Tour}
function Assert-InBounds([IntPtr]$tour,[int]$id){$c=[QuickTourQa+Rect]::new();[void][QuickTourQa]::GetClientRect($tour,[ref]$c);$r=[QuickTourQa]::ChildRect($tour,$id);Assert($r.Left-ge 0-and$r.Top-ge 0-and$r.Right-le$c.Right-and$r.Bottom-le$c.Bottom) "Quick Tour control $id is outside the dialog."}
function Assert-TourLayout([IntPtr]$tour){
    Assert(-not[QuickTourQa]::IsWindowEnabled($main)) 'Main launcher remained enabled while Quick Tour was open.'
    foreach($id in @(1310,1311,1312,1313,1,2)){Assert-InBounds $tour $id}
    $frame=Find-Frame;$region=[QuickTourQa+Rect]::new();Assert(([QuickTourQa]::GetWindowRgnBox($frame,[ref]$region))-gt 1) 'Quick Tour highlight has no hollow shaped frame region.'
    $tr=[QuickTourQa+Rect]::new();[void][QuickTourQa]::GetWindowRect($tour,[ref]$tr);$work=[Windows.Forms.Screen]::FromHandle($tour).WorkingArea
    Assert($tr.Left-ge$work.Left-and$tr.Top-ge$work.Top-and$tr.Right-le$work.Right-and$tr.Bottom-le$work.Bottom) 'Quick Tour callout is outside the monitor work area.'
    $frame
}
function Assert-FrameContainsControl([IntPtr]$frame,[int]$controlId,[string]$label){
    $control=[QuickTourQa]::GetDlgItem($main,$controlId);Assert($control-ne[IntPtr]::Zero) "Missing $label control ($controlId)."
    $cr=[QuickTourQa+Rect]::new();$fr=[QuickTourQa+Rect]::new();[void][QuickTourQa]::GetWindowRect($control,[ref]$cr);[void][QuickTourQa]::GetWindowRect($frame,[ref]$fr)
    Assert($fr.Left-le$cr.Left-and$fr.Top-le$cr.Top-and$fr.Right-ge$cr.Right-and$fr.Bottom-ge$cr.Bottom) "Quick Tour frame does not surround the actual $label control."
}
function Wait-TourClosed([IntPtr]$tour,[string]$message){Wait-Until{-not[QuickTourQa]::IsWindow($tour)} $message 5|Out-Null;Wait-Until{[QuickTourQa]::Find([uint32]$process.Id,'ctSpacesQuickTourHighlight','')-eq[IntPtr]::Zero} 'Quick Tour highlight survived dialog cleanup.' 5|Out-Null}
function Save-OverlayCapture([IntPtr]$tour,[IntPtr]$frame,[string]$name){
    New-Item -ItemType Directory -Path $artifactRoot -Force|Out-Null
    [void][QuickTourQa]::ShowWindow($main,9);[void][QuickTourQa]::SetForegroundWindow($tour);Start-Sleep -Milliseconds 250
    $left=[int]::MaxValue;$top=[int]::MaxValue;$right=[int]::MinValue;$bottom=[int]::MinValue
    foreach($window in @($main,$tour,$frame)){$r=[QuickTourQa+Rect]::new();if([QuickTourQa]::GetWindowRect($window,[ref]$r)){$left=[Math]::Min($left,$r.Left);$top=[Math]::Min($top,$r.Top);$right=[Math]::Max($right,$r.Right);$bottom=[Math]::Max($bottom,$r.Bottom)}}
    $width=$right-$left;$height=$bottom-$top;Assert($width-gt 0-and$height-gt 0-and$width-le 8192-and$height-le 8192) "Invalid overlay capture bounds: $left,$top to $right,$bottom."
    $bmp=[Drawing.Bitmap]::new($width,$height);$graphics=[Drawing.Graphics]::FromImage($bmp)
    try{$graphics.CopyFromScreen($left,$top,0,0,$bmp.Size,[Drawing.CopyPixelOperation]::SourceCopy);$path=Join-Path $artifactRoot ($run+'-'+$name+'.png');$bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png);$path}finally{$graphics.Dispose();$bmp.Dispose()}
}
function Capture-ThemeDpis([IntPtr]$tour,[string]$theme){
    $shots=@{};$seen=[Collections.Generic.HashSet[uint32]]::new()
    foreach($screen in [Windows.Forms.Screen]::AllScreens){
        [void][QuickTourQa]::SetWindowPos($main,[IntPtr]::Zero,$screen.WorkingArea.Left+40,$screen.WorkingArea.Top+40,0,0,0x1)
        $dpi=Wait-Until{$value=[QuickTourQa]::GetDpiForWindow($main);if($value){$value}} 'Main window did not acquire monitor DPI.' 5
        Start-Sleep -Milliseconds 250;$frame=Assert-TourLayout $tour;[void]$seen.Add([uint32]$dpi)
        if(-not$shots.ContainsKey([uint32]$dpi)){$shots[[uint32]$dpi]=Save-OverlayCapture $tour $frame (($theme-replace' ','-')+'-step0-'+$dpi+'dpi')}
    }
    foreach($expected in $ExpectedDpis){Assert($seen.Contains([uint32]$expected)) "No $expected-DPI monitor was exercised for $theme.";Assert($shots.ContainsKey([uint32]$expected)) "No $expected-DPI overlay capture was saved for $theme."}
    $shots
}

try {
    New-Item -ItemType Directory -Path $app,$data -Force|Out-Null
    Copy-Item -LiteralPath $exe -Destination $copiedExe
    New-Item -ItemType File -Path (Join-Path $app 'ctSpaces.portable')|Out-Null
    [IO.File]::WriteAllText($config,"[user]`r`nbrowser=chrome`r`ntheme_name=Marine`r`ntheme=5`r`n`r`n[guide]`r`nwelcome_handled=1`r`n",$utf8)
    Start-QaApp
    $beforeConfig=Get-StableFingerprint $config
    Assert(-not(Test-Path -LiteralPath (Join-Path $data 'Sites'))) 'Ordinary Quick Tour fixture unexpectedly began with client data.'

    # The guide button must close the modal guide before the modeless overlay disables the launcher.
    [void][QuickTourQa]::PostMessageW($main,0x111,[UIntPtr]41017,[IntPtr]::Zero)
    $guide=Wait-Until{[QuickTourQa]::Find([uint32]$process.Id,'#32770','ctSpaces Guided Walkthrough')} 'Guided walkthrough did not open.'
    Wait-Until{[QuickTourQa]::GetDlgItem($guide,1308)} 'Guided walkthrough omitted Quick tour button.' 5|Out-Null
    [void][QuickTourQa]::PostMessageW($guide,0x111,[UIntPtr]1308,[IntPtr]::Zero)
    $tour=Find-Tour;Wait-Until{-not[QuickTourQa]::IsWindow($guide)} 'Guide remained open behind Quick Tour.' 5|Out-Null
    $frame=Assert-TourLayout $tour;Assert-FrameContainsControl $frame 205 'CLIENT field'
    $marineShots=Capture-ThemeDpis $tour 'Marine'

    $firstBody=[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1311));Assert($firstBody.Length-gt 0) 'Quick Tour first body is empty.'
    [void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1,[IntPtr]::Zero);$secondBody=Wait-Until{$v=[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1311));if($v-and$v-ne$firstBody){$v}} 'Quick Tour Next did not change the body.'
    [void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1313,[IntPtr]::Zero);Wait-Until{[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1311))-eq$firstBody} 'Quick Tour Back did not restore the first body.'|Out-Null
    $steps=1;$seenBodies=[Collections.Generic.HashSet[string]]::new();[void]$seenBodies.Add($firstBody);$step3Shot='';$step8Shot=''
    while([QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1))-ne'Done'){
        $body=[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1311));[void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1,[IntPtr]::Zero)
        $nextBody=Wait-Until{$v=[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1311));if($v-and$v-ne$body){$v}} 'Quick Tour Next did not update its body.';[void]$seenBodies.Add($nextBody);$steps++;Assert($steps-lt 32) 'Quick Tour did not reach Done.'
        $frame=Assert-TourLayout $tour
        if($steps-eq 3){Assert-FrameContainsControl $frame 1 'primary action';$step3Shot=Save-OverlayCapture $tour $frame 'Marine-step2-primary-action'}
        if($steps-eq 4){$step3Shot=Save-OverlayCapture $tour $frame 'Marine-step3-pinned-clients'}
        if($steps-eq 9){Assert-FrameContainsControl $frame 201 'Options';$step8Shot=Save-OverlayCapture $tour $frame 'Marine-step8-options'}
    }
    Assert($seenBodies.Count-eq$steps) 'Quick Tour repeated a step body.';Assert($step3Shot-and$step8Shot) 'Quick Tour did not produce the requested later-step captures.'
    [void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1,[IntPtr]::Zero);Wait-TourClosed $tour 'Quick Tour did not close with Done.'
    Assert([QuickTourQa]::IsWindowEnabled($main)) 'Main launcher was not re-enabled after Done.'
    Assert((Get-StableFingerprint $config)-eq$beforeConfig) 'Quick Tour or guide handoff modified configuration.'
    Assert(-not(Test-Path -LiteralPath (Join-Path $data 'Sites'))) 'Quick Tour created fake client data.'

    $tour=Open-Tour;[void](Assert-TourLayout $tour);[void][QuickTourQa]::PostMessageW($tour,0x100,[UIntPtr]0x1B,[IntPtr]::Zero);Wait-TourClosed $tour 'Quick Tour did not close with Escape.';Assert([QuickTourQa]::IsWindowEnabled($main)) 'Main launcher remained disabled after Escape.'
    $tour=Open-Tour;[void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]2,[IntPtr]::Zero);Wait-TourClosed $tour 'Quick Tour did not close with Skip.';Assert([QuickTourQa]::IsWindowEnabled($main)) 'Main launcher remained disabled after Skip.'
    $tour=Open-Tour;[void][QuickTourQa]::PostMessageW($tour,0x10,[UIntPtr]::Zero,[IntPtr]::Zero);Wait-TourClosed $tour 'Quick Tour did not close with its title-bar X.';Assert([QuickTourQa]::IsWindowEnabled($main)) 'Main launcher remained disabled after title-bar close.'
    $tour=Open-Tour;[void][QuickTourQa]::ShowWindow($main,6);Wait-TourClosed $tour 'Minimizing the launcher did not dismiss Quick Tour.';Assert([QuickTourQa]::IsWindowEnabled($main)) 'Main launcher remained disabled after minimize cleanup.';[void][QuickTourQa]::ShowWindow($main,9)

    $gdiBefore=[QuickTourQa]::GetGuiResources($process.Handle,0)
    1..4|ForEach-Object{$again=Open-Tour;[void](Assert-TourLayout $again);[void][QuickTourQa]::PostMessageW($again,0x111,[UIntPtr]2,[IntPtr]::Zero);Wait-TourClosed $again 'Repeated Quick Tour did not close.'}
    $gdiAfter=[QuickTourQa]::GetGuiResources($process.Handle,0);Assert(($gdiAfter-$gdiBefore)-le 4) "Repeated Quick Tour lifecycle leaked GDI resources: $gdiBefore to $gdiAfter."
    Stop-QaApp

    # Restart in Gothic, capture both DPIs, then verify synchronous shortcut handoff into an exact isolated Edge profile.
    $gothicConfig=([IO.File]::ReadAllText($config)-replace'theme_name=Marine','theme_name=Dark - Gothic');[IO.File]::WriteAllText($config,$gothicConfig,$utf8)
    $client='__QuickTour_'+$run;$profile=Join-Path $data ('Sites\'+$client+'\Browsers\edge\Profile')
    New-Item -ItemType Directory -Path (Join-Path $profile 'Default') -Force|Out-Null
    [IO.File]::WriteAllText((Join-Path $data ('Sites\'+$client+'\ctSpaces-client-v2')),"ctSpaces-client-schema=2`r`n",$utf8)
    [IO.File]::WriteAllText((Join-Path $data ('Sites\'+$client+'\Browsers\edge\ctSpaces-browser-v2')),"ctSpaces-browser-schema=2`r`nbrowser=edge`r`n",$utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'ctSpaces'),"ctSpaces-profile=2`r`n",$utf8)
    [IO.File]::WriteAllText((Join-Path $profile 'Default\Preferences'),'{}',$utf8)
    Start-QaApp
    $tour=Open-Tour;$gothicShots=Capture-ThemeDpis $tour 'Dark Gothic'
    Assert([QuickTourQa]::CopyData($main,('--client "'+$client+'" --browser edge'))) 'Quick-Tour-time WM_COPYDATA launch was rejected.'
    Wait-TourClosed $tour 'WM_COPYDATA did not dismiss Quick Tour before launch handoff.'
    $owned=Wait-Until{@(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue|Where-Object{$_.CommandLine-and$_.CommandLine-match[regex]::Escape($profile)})} 'WM_COPYDATA did not launch the exact isolated Edge profile.' 20
    $otherClients=@(Get-ChildItem -LiteralPath (Join-Path $data 'Sites') -Directory|Where-Object{$_.Name-ne$client});Assert($otherClients.Count-eq 0) 'Quick Tour or handoff created an unexpected client profile.'
    foreach($edgeProcess in @($owned)){[QuickTourQa]::ClosePidWindows([uint32]$edgeProcess.ProcessId)}
    Wait-Until{@(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue|Where-Object{$_.CommandLine-and$_.CommandLine-match[regex]::Escape($profile)}).Count-eq 0} 'Owned Edge profile did not close normally.' 15|Out-Null
    Stop-QaApp

    Assert((Get-Fingerprint $liveConfig)-eq$liveConfigBefore) 'Live ctSpaces configuration changed.'
    $completed=$true
    [pscustomobject]@{QuickTourSteps=$steps;DistinctBodies=$seenBodies.Count;GuideButtonHandoff=$true;ActualAnchorChecks=@('CLIENT field','primary action','Options');OverlayFrameLifetime=$true;MarineCaptures=@($marineShots.Values);Step3=$step3Shot;Step8=$step8Shot;GothicCaptures=@($gothicShots.Values);ExpectedDpis=$ExpectedDpis;CloseMinimizeCleanup=$true;CopyDataLaunchHonored=$true;ConfigAndClientIsolation=$true;GdiDelta=($gdiAfter-$gdiBefore)}|ConvertTo-Json
} catch {
    New-Item -ItemType Directory -Path $artifactRoot -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $artifactRoot ($run+'-failure.txt')),$_.Exception.ToString(),$utf8);throw
} finally {
    foreach($edgeProcess in @($owned)){try{[QuickTourQa]::ClosePidWindows([uint32]$edgeProcess.ProcessId)}catch{}}
    if($process){try{Stop-QaApp}catch{}}
    if($completed-and(Test-Path -LiteralPath $qa)){Remove-Item -LiteralPath $qa -Recurse -Force}elseif(Test-Path -LiteralPath $qa){Write-Warning "Preserved Quick Tour failure fixture: $qa"}
    if($oldDpiContext-ne[IntPtr]::Zero){[void][QuickTourQa]::SetThreadDpiAwarenessContext($oldDpiContext)}
}
