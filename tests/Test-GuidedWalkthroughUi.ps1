param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\walkthrough-20260921\guided-walkthrough-ui')
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedExe = [IO.Path]::GetFullPath($ExePath)
$artifactRoot = [IO.Path]::GetFullPath($ArtifactDirectory)
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 10)
$qaRoot = Join-Path $projectRoot ('build\guide-ui-' + $runId)
$utf8 = [Text.UTF8Encoding]::new($false)
$launchers = [Collections.Generic.List[object]]::new()
$completed = $false
$oldDpiContext = [IntPtr]::Zero
$liveConfig = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'
$liveClient = Join-Path $env:LOCALAPPDATA ('InfinitySys\ctSpaces\Sites\__GuideUi_' + $runId)
function Get-Fingerprint($Path) { if (Test-Path -LiteralPath $Path -PathType Leaf) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { '<missing>' } }
$liveConfigBefore = Get-Fingerprint $liveConfig
if (-not (Test-Path -LiteralPath $resolvedExe -PathType Leaf)) { throw "Missing executable: $resolvedExe" }
if (Test-Path -LiteralPath $liveClient) { throw "Refusing existing live QA client: $liveClient" }

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class GuideUiQa {
  delegate bool EnumProc(IntPtr w, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] struct CopyDataStruct { public UIntPtr Tag; public uint Bytes; public IntPtr Text; }
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr w, EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w, out uint p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr w, StringBuilder b, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr w, StringBuilder b, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr w, int id);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr w);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr w);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w, out Rect r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr w, out Rect r);
  [DllImport("user32.dll")] public static extern int MapWindowPoints(IntPtr f, IntPtr t, ref Rect r, uint n);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr w);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr w, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr w, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr w, uint m, UIntPtr p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr w, uint m, IntPtr p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr w, uint m, IntPtr p, StringBuilder b);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr SendText(IntPtr w, uint m, IntPtr p, string text);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern bool SendMessageTimeoutW(IntPtr w, uint m, UIntPtr p, IntPtr l, uint f, uint ms, out UIntPtr result);
  [DllImport("user32.dll")] static extern int GetMenuItemCount(IntPtr menu);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetMenuStringW(IntPtr menu, uint item, StringBuilder text, int size, uint flags);
  [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint kind);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern uint GetPrivateProfileStringW(string s, string k, string d, StringBuilder b, uint n, string p);
  public static string Text(IntPtr w) { var b=new StringBuilder(32768); GetWindowTextW(w,b,b.Capacity); return b.ToString(); }
  public static string ControlText(IntPtr w) { var b=new StringBuilder(32768); ReadText(w,0xD,(IntPtr)b.Capacity,b); return b.ToString(); }
  public static IntPtr Find(uint pid, string cls, int child) { IntPtr hit=IntPtr.Zero; EnumWindows((w,p)=>{ uint owner; GetWindowThreadProcessId(w,out owner); if(owner!=pid)return true; var b=new StringBuilder(128); GetClassNameW(w,b,b.Capacity); if((String.IsNullOrEmpty(cls)||b.ToString()==cls)&&(child==0||GetDlgItem(w,child)!=IntPtr.Zero)){hit=w;return false;} return true;},IntPtr.Zero); return hit; }
  public static IntPtr FindDialog(uint pid, string title) { IntPtr hit=IntPtr.Zero; EnumWindows((w,p)=>{ uint owner; GetWindowThreadProcessId(w,out owner); var b=new StringBuilder(32); GetClassNameW(w,b,b.Capacity); if(owner==pid&&b.ToString()=="#32770"&&Text(w)==title){hit=w;return false;} return true;},IntPtr.Zero); return hit; }
  public static Rect ChildRect(IntPtr parent, int id) { Rect r; if(!GetWindowRect(GetDlgItem(parent,id),out r))throw new Exception("missing control "+id); MapWindowPoints(IntPtr.Zero,parent,ref r,2); return r; }
  public static string Ini(string path,string section,string key) { var b=new StringBuilder(2048); GetPrivateProfileStringW(section,key,"",b,(uint)b.Capacity,path); return b.ToString(); }
  public static int ComboFind(IntPtr combo,string text) { return SendText(combo,0x158,(IntPtr)(-1),text).ToInt32(); }
  public static string ChildText(IntPtr w) { var a=new List<string>(); EnumChildWindows(w,(c,p)=>{var t=Text(c);if(!String.IsNullOrWhiteSpace(t))a.Add(t);return true;},IntPtr.Zero);return String.Join(" | ",a); }
  public static string MenuLabels(IntPtr popup) { IntPtr menu=SendMessageW(popup,0x1E1,IntPtr.Zero,IntPtr.Zero);var a=new List<string>();for(uint i=0;i<GetMenuItemCount(menu);++i){var b=new StringBuilder(256);GetMenuStringW(menu,i,b,b.Capacity,0x400);if(b.Length>0)a.Add(b.ToString());}return String.Join("|",a); }
  public static bool CopyData(IntPtr w,string value) { IntPtr text=Marshal.StringToHGlobalUni(value), data=IntPtr.Zero; try { var c=new CopyDataStruct{Tag=new UIntPtr(0x43545350u),Bytes=checked((uint)((value.Length+1)*2)),Text=text}; data=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(CopyDataStruct))); Marshal.StructureToPtr(c,data,false); UIntPtr result; return SendMessageTimeoutW(w,0x4A,UIntPtr.Zero,data,3,5000,out result)&&result!=UIntPtr.Zero; } finally {if(data!=IntPtr.Zero)Marshal.FreeHGlobal(data);Marshal.FreeHGlobal(text);} }
  public static void ClosePidWindows(uint pid) { EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==pid)PostMessageW(w,0x10,UIntPtr.Zero,IntPtr.Zero);return true;},IntPtr.Zero); }
}
'@
$oldDpiContext = [GuideUiQa]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))

function Wait-Until([scriptblock]$Check, [string]$Failure, [int]$Seconds = 12) {
    $end = [DateTime]::UtcNow.AddSeconds($Seconds)
    do { $value = & $Check; if ($value -is [IntPtr]) { if ($value -ne [IntPtr]::Zero) { return $value } } elseif ($value) { return $value }; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $end)
    throw $Failure
}
function New-Fixture([string]$Name, [string]$Config = '') {
    $app = Join-Path $qaRoot ($Name + '\app'); $data = Join-Path $app 'data'; New-Item -ItemType Directory -Path $app -Force | Out-Null
    $exe = Join-Path $app 'guide-qa.exe'; Copy-Item -LiteralPath $resolvedExe -Destination $exe; New-Item -ItemType File -Path (Join-Path $app 'ctSpaces.portable') | Out-Null
    if ($Config) { New-Item -ItemType Directory -Path $data -Force | Out-Null; [IO.File]::WriteAllText((Join-Path $data 'config.ini'), $Config, $utf8) }
    [pscustomobject]@{ Name=$Name; App=$app; Data=$data; Config=(Join-Path $data 'config.ini'); Exe=$exe; Process=$null; Main=[IntPtr]::Zero }
}
function Start-Fixture($Fixture, [switch]$StartupGuide, [switch]$Minimized) {
    $args = '--qa-instance=' + $runId + '-' + $Fixture.Name + ' --qa-data-dir="' + $Fixture.Data + '"'
    if ($StartupGuide) { $args += ' --qa-guide-startup' }
    $start=@{FilePath=$Fixture.Exe;ArgumentList=$args;PassThru=$true};if($Minimized){$start.WindowStyle='Minimized'};$Fixture.Process = Start-Process @start
    $launchers.Add($Fixture); [void]$Fixture.Process.WaitForInputIdle(10000)
    $Fixture.Main = Wait-Until { $Fixture.Process.Refresh(); if($Fixture.Process.HasExited){throw "$($Fixture.Name) exited: $($Fixture.Process.ExitCode)"}; [GuideUiQa]::Find([uint32]$Fixture.Process.Id,'ctSpacesLauncherClass',0) } "$($Fixture.Name) launcher did not open."
    $Fixture
}
function Find-Guide($Fixture, [int]$Seconds = 10) { Wait-Until { [GuideUiQa]::Find([uint32]$Fixture.Process.Id,'#32770',1301) } "$($Fixture.Name) guide did not open." $Seconds }
function Assert-NoGuide($Fixture) { Start-Sleep -Milliseconds 1200; if([GuideUiQa]::Find([uint32]$Fixture.Process.Id,'#32770',1301) -ne [IntPtr]::Zero){throw "$($Fixture.Name) unexpectedly opened the guide."} }
function Close-Guide([IntPtr]$Guide, [switch]$Escape) {
    if($Escape){[void][GuideUiQa]::PostMessageW($Guide,0x100,[UIntPtr]0x1B,[IntPtr]::Zero)}else{[void][GuideUiQa]::SendMessageW($Guide,0x111,[IntPtr]2,[IntPtr]::Zero)}
    Wait-Until { -not [GuideUiQa]::IsWindow($Guide) } 'Guide did not close.' 4 | Out-Null
}
function Stop-Fixture($Fixture) {
    if($Fixture.Main -ne [IntPtr]::Zero -and [GuideUiQa]::IsWindow($Fixture.Main)){[void][GuideUiQa]::PostMessageW($Fixture.Main,0x10,[UIntPtr]::Zero,[IntPtr]::Zero)}
    if($Fixture.Process){[void]$Fixture.Process.WaitForExit(5000);if(-not $Fixture.Process.HasExited){$Fixture.Process.Kill();[void]$Fixture.Process.WaitForExit(5000)};$Fixture.Process.Dispose()}; $launchers.Remove($Fixture)|Out-Null
}
function Open-Guide($Fixture, [int]$Command = 41017, [switch]$F1) { if($F1){[void][GuideUiQa]::PostMessageW($Fixture.Main,0x100,[UIntPtr]0x70,[IntPtr]::Zero)}else{[void][GuideUiQa]::PostMessageW($Fixture.Main,0x111,[UIntPtr]$Command,[IntPtr]::Zero)}; Find-Guide $Fixture }
function Ini($Fixture,[string]$Key){[GuideUiQa]::Ini($Fixture.Config,'guide',$Key)}
function Assert-Eq($Actual,$Expected,[string]$Message){if([string]$Actual -cne [string]$Expected){throw "$Message Expected '$Expected', got '$Actual'."}}
function Assert-Guide([IntPtr]$Guide,[int]$Topics,[string]$NextLabel,[string]$Caption){
    Assert-Eq ([GuideUiQa]::Text($Guide)) $Caption 'Guide caption mismatch.'; $list=[GuideUiQa]::GetDlgItem($Guide,1301); $count=[GuideUiQa]::SendMessageW($list,0x18B,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32(); Assert-Eq $count $Topics 'Topic count mismatch.'
    foreach($id in @(1301,1302,1303,1304,1305,1306,1308,1,2)){if([GuideUiQa]::GetDlgItem($Guide,$id)-eq [IntPtr]::Zero){throw "Guide control $id missing."}}
    Assert-Eq ([GuideUiQa]::ControlText([GuideUiQa]::GetDlgItem($Guide,1))) $NextLabel 'Next button label mismatch.'
    $client=[GuideUiQa+Rect]::new();[void][GuideUiQa]::GetClientRect($Guide,[ref]$client); foreach($id in @(1301,1302,1303,1304,1305,1306,1308,1,2)){$r=[GuideUiQa]::ChildRect($Guide,$id);if($r.Left-lt 0-or$r.Top-lt 0-or$r.Right-gt$client.Right-or$r.Bottom-gt$client.Bottom){throw "Guide control $id clips outside the client area."}}
    $quickTour=[GuideUiQa]::ChildRect($Guide,1308);$backBounds=[GuideUiQa]::ChildRect($Guide,1306);if($quickTour.Right -gt $backBounds.Left){throw 'Quick tour and Back buttons overlap.'}
    $back=[GuideUiQa]::ChildRect($Guide,1306);$nextRect=[GuideUiQa]::ChildRect($Guide,1);$close=[GuideUiQa]::ChildRect($Guide,2);if($back.Right -gt $nextRect.Left -or $nextRect.Right -gt $close.Left){throw "Guide footer buttons overlap: Back=$($back.Left),$($back.Right) Next=$($nextRect.Left),$($nextRect.Right) Close=$($close.Left),$($close.Right)."}
    $style=[GuideUiQa]::SendMessageW([GuideUiQa]::GetDlgItem($Guide,1304),0x00D5,[IntPtr]::Zero,[IntPtr]::Zero); if(-not [GuideUiQa]::ControlText([GuideUiQa]::GetDlgItem($Guide,1304)).Length){throw 'Guide body is empty.'}
}
function Select-GuideTopic([IntPtr]$Guide,[int]$Index){
    $list=[GuideUiQa]::GetDlgItem($Guide,1301)
    [void][GuideUiQa]::SendMessageW($list,0x186,[IntPtr]$Index,[IntPtr]::Zero) # LB_SETCURSEL
    Assert-Eq ([GuideUiQa]::SendMessageW($list,0x188,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()) $Index 'Guide topic selection did not change.'
    [void][GuideUiQa]::SendMessageW($Guide,0x111,[IntPtr](1301-bor(1-shl 16)),[IntPtr]$list)
    Start-Sleep -Milliseconds 80
}
function Assert-GuideBodyContains([IntPtr]$Guide,[int]$Index,[string[]]$Needles,[string]$Message){
    Select-GuideTopic $Guide $Index
    $body=[GuideUiQa]::ControlText([GuideUiQa]::GetDlgItem($Guide,1304))
    foreach($needle in $Needles){if(-not $body.Contains($needle)){throw "$Message Missing '$needle' in topic $Index."}}
}
function Assert-GuideBodyEndVisible([IntPtr]$Guide){
    $body=[GuideUiQa]::GetDlgItem($Guide,1304)
    $content=[GuideUiQa]::ControlText($body)
    $last=$content.Length-1
    if($last -lt 0){throw 'Cannot test an empty guide page.'}
    [void][GuideUiQa]::SendMessageW($body,0xB1,[IntPtr]$last,[IntPtr]$last) # EM_SETSEL: caret only
    [void][GuideUiQa]::SendMessageW($body,0xB7,[IntPtr]::Zero,[IntPtr]::Zero) # EM_SCROLLCARET
    $position=[GuideUiQa]::SendMessageW($body,0xD6,[IntPtr]$last,[IntPtr]::Zero).ToInt64() # EM_POSFROMCHAR
    $bodyRect=[GuideUiQa+Rect]::new();[void][GuideUiQa]::GetClientRect($body,[ref]$bodyRect)
    $y=($position -shr 16)-band 0xFFFF
    if($position -eq -1 -or $y -ge $bodyRect.Bottom){throw 'Last character of the expanded guide page is not reachable in the viewport.'}
}
function Save-Capture([IntPtr]$Window,[string]$Name){
    New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null; $r=[GuideUiQa+Rect]::new();[void][GuideUiQa]::GetWindowRect($Window,[ref]$r);$bmp=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();try{if(-not[GuideUiQa]::PrintWindow($Window,$dc,2)){throw 'PrintWindow failed.'}}finally{$g.ReleaseHdc($dc);$g.Dispose()};$path=Join-Path $artifactRoot ($runId+'-'+$Name+'.png');$bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png);$bmp.Dispose();$path
}

try {
    New-Item -ItemType Directory -Path $qaRoot -Force | Out-Null
    $fresh = Start-Fixture (New-Fixture 'fresh-start') -StartupGuide; $guide=Find-Guide $fresh
    $gothicShot=Save-Capture $guide 'gothic-welcome'; Assert-Guide $guide 12 'Start' 'Welcome to ctSpaces'; Assert-Eq (Ini $fresh 'welcome_pending') 1 'Fresh welcome was not pending.';$observedDpis=[Collections.Generic.HashSet[uint32]]::new();foreach($screen in [Windows.Forms.Screen]::AllScreens){[void][GuideUiQa]::SetWindowPos($guide,[IntPtr]::Zero,$screen.WorkingArea.Left+20,$screen.WorkingArea.Top+20,0,0,0x15);Start-Sleep -Milliseconds 300;[void]$observedDpis.Add([GuideUiQa]::GetDpiForWindow($guide));Assert-Guide $guide 12 'Start' 'Welcome to ctSpaces'}
    [void][GuideUiQa]::SendMessageW($guide,0x111,[IntPtr]1,[IntPtr]::Zero); Assert-Eq ([GuideUiQa]::ControlText([GuideUiQa]::GetDlgItem($guide,1302))) 'Create and open a client' 'Start did not advance.'
    Assert-Eq (Ini $fresh 'read_welcome') 1 'Start did not mark only the welcome topic.'; Assert-Eq (Ini $fresh 'read_create_open') '' 'Start marked the next topic too early.'; Assert-Eq (Ini $fresh 'welcome_handled') 1 'Start did not handle welcome.'; Close-Guide $guide; Stop-Fixture $fresh
    $fresh=Start-Fixture $fresh; Assert-NoGuide $fresh; $guide=Open-Guide $fresh -F1; Assert-Guide $guide 12 'Next' 'ctSpaces Guided Walkthrough'; Close-Guide $guide; Stop-Fixture $fresh

    $skip=Start-Fixture (New-Fixture 'fresh-skip') -StartupGuide; $guide=Find-Guide $skip; Close-Guide $guide; Assert-Eq (Ini $skip 'welcome_handled') 1 'Skip did not suppress startup replay.'; Assert-Eq (Ini $skip 'read_welcome') '' 'Skip falsely acknowledged a topic.'; Stop-Fixture $skip; $skip=Start-Fixture $skip; Assert-NoGuide $skip; Stop-Fixture $skip
    $esc=Start-Fixture (New-Fixture 'fresh-escape') -StartupGuide; $guide=Find-Guide $esc; Close-Guide $guide -Escape; Assert-Eq (Ini $esc 'welcome_handled') 1 'Escape did not suppress startup replay.'; Assert-Eq (Ini $esc 'read_welcome') '' 'Escape falsely acknowledged a topic.'; Stop-Fixture $esc
    $minimized=Start-Fixture (New-Fixture 'fresh-minimized') -StartupGuide -Minimized;Assert-NoGuide $minimized;Assert-Eq (Ini $minimized 'welcome_pending') 1 'Minimized startup lost pending welcome.';Assert-Eq (Ini $minimized 'welcome_handled') '' 'Minimized startup falsely handled welcome.';Stop-Fixture $minimized;$minimized=Start-Fixture $minimized -StartupGuide;$guide=Find-Guide $minimized;Close-Guide $guide;Assert-Eq (Ini $minimized 'welcome_handled') 1 'Later visible opt-in did not handle welcome.';Stop-Fixture $minimized

    $baseConfig="[user]`r`nbrowser=chrome`r`ntheme_name=Marine`r`nclient_title_first=0`r`n`r`n[restore_tabs]`r`nLegacy Client|chrome=0`r`n`r`n[guide]`r`nwelcome_handled=1`r`nwelcome_pending=0`r`nread_welcome=999`r`nread_updates=0`r`n"
    $existing=Start-Fixture (New-Fixture 'existing-5309' $baseConfig) -StartupGuide; Assert-NoGuide $existing
    [void][GuideUiQa]::PostMessageW($existing.Main,0x111,[UIntPtr]201,[IntPtr]::Zero);$popup=Wait-Until{[GuideUiQa]::Find([uint32]$existing.Process.Id,'#32768',0)} 'Options menu did not open.';$menuLabels=[GuideUiQa]::MenuLabels($popup);if(-not $menuLabels.Contains('Guided walkthrough (New)')-or-not $menuLabels.Contains("What's new (New)")){throw "Options menu omitted New guide labels: $menuLabels"};[void][GuideUiQa]::PostMessageW($popup,0x100,[UIntPtr]0x1B,[IntPtr]::Zero);[void][GuideUiQa]::PostMessageW($existing.Main,0x1F,[UIntPtr]::Zero,[IntPtr]::Zero);Wait-Until{-not[GuideUiQa]::IsWindow($popup)} 'Options menu did not close.' 4|Out-Null
    $guide=Open-Guide $existing -F1; Assert-Guide $guide 12 'Next' 'ctSpaces Guided Walkthrough'
    Assert-GuideBodyContains $guide 1 @('client icon','File Explorer','client folder','Do not edit or delete') 'Profile-folder guidance is incomplete.'
    Assert-GuideBodyContains $guide 3 @('overflow','show','Close') 'Session overflow guidance is incomplete.'
    Assert-GuideBodyContains $guide 4 @('unpin','Desktop','Click a pinned client to open it','Select client','without launching','Open client','Restore tabs') 'Pinned-client guidance is incomplete.'
    Assert-GuideBodyContains $guide 6 @('Remove Custom Icon','without removing browser data') 'Icon-removal guidance is incomplete.'
    $pinPageShots=[Collections.Generic.List[string]]::new()
    foreach($screen in [Windows.Forms.Screen]::AllScreens){
        [void][GuideUiQa]::SetWindowPos($guide,[IntPtr]::Zero,$screen.WorkingArea.Left+20,$screen.WorkingArea.Top+20,0,0,0x15)
        Start-Sleep -Milliseconds 300
        Select-GuideTopic $guide 4
        Assert-Guide $guide 12 'Next' 'ctSpaces Guided Walkthrough'
        $dpi=[GuideUiQa]::GetDpiForWindow($guide)
        $pinPageShots.Add((Save-Capture $guide ('pins-top-'+$dpi)))
        Assert-GuideBodyEndVisible $guide
        $pinPageShots.Add((Save-Capture $guide ('pins-bottom-'+$dpi)))
    }
    Select-GuideTopic $guide 0; $marineShot=Save-Capture $guide 'marine-manual'
    [void][GuideUiQa]::PostMessageW($existing.Main,0x111,[UIntPtr]41006,[IntPtr]::Zero);$themeDialog=Wait-Until{[GuideUiQa]::Find([uint32]$existing.Process.Id,'#32770',5201)} 'Theme dialog did not open over the guide.';$combo=[GuideUiQa]::GetDlgItem($themeDialog,5201);$gothicIndex=[GuideUiQa]::ComboFind($combo,'Dark - Gothic');if($gothicIndex-lt 0){throw 'Dark - Gothic was absent from Themes.'};[void][GuideUiQa]::SendMessageW($combo,0x14E,[IntPtr]$gothicIndex,[IntPtr]::Zero);[void][GuideUiQa]::SendMessageW($themeDialog,0x111,[IntPtr](5201-bor(1-shl 16)),$combo);Start-Sleep -Milliseconds 350;$themeFlipShot=Save-Capture $guide 'marine-to-gothic-live';if((Get-FileHash $themeFlipShot).Hash-eq(Get-FileHash $marineShot).Hash){throw 'Open guide did not repaint for a live theme preview.'};Assert-Guide $guide 12 'Next' 'ctSpaces Guided Walkthrough';[void][GuideUiQa]::PostMessageW($themeDialog,0x111,[UIntPtr]5203,[IntPtr]::Zero);Wait-Until{-not[GuideUiQa]::IsWindow($themeDialog)} 'Theme dialog did not close.' 4|Out-Null;Close-Guide $guide
    $gdiBefore=[GuideUiQa]::GetGuiResources($existing.Process.Handle,0); 1..4|ForEach-Object{$again=Open-Guide $existing;Assert-Guide $again 12 'Next' 'ctSpaces Guided Walkthrough';Close-Guide $again};$gdiAfter=[GuideUiQa]::GetGuiResources($existing.Process.Handle,0);if($gdiAfter-$gdiBefore-gt 4){throw "Repeated guide opens leaked GDI objects: $gdiBefore to $gdiAfter."}
    Assert-Eq ([GuideUiQa]::Ini($existing.Config,'user','browser')) 'chrome' 'Guide changed browser selection.'; Assert-Eq ([GuideUiQa]::Ini($existing.Config,'restore_tabs','Legacy Client|chrome')) 0 'Guide changed Restore tabs.'; if(Test-Path (Join-Path $existing.Data 'Sites')){throw 'Guide created or changed Sites data.'}
    $guide=Open-Guide $existing 41018; Assert-Guide $guide 1 'Done' 'ctSpaces Guided Walkthrough'; Assert-Eq (Ini $existing 'read_updates') 0 "What's New was acknowledged on open."; [void][GuideUiQa]::SendMessageW($guide,0x111,[IntPtr]1,[IntPtr]::Zero); Wait-Until{-not[GuideUiQa]::IsWindow($guide)} "What's New Done did not close." 4|Out-Null
    Assert-Eq (Ini $existing 'read_updates') 2 "What's New Done did not persist revision 2."; Assert-Eq (Ini $existing 'read_welcome') 999 'A future guide revision was downgraded.';[void][GuideUiQa]::PostMessageW($existing.Main,0x111,[UIntPtr]41018,[IntPtr]::Zero);$caughtUp=Wait-Until{[GuideUiQa]::FindDialog([uint32]$existing.Process.Id,"What's New")} "Caught-up What's New notice did not appear.";$caughtText=Wait-Until{$snapshot=[GuideUiQa]::ChildText($caughtUp);if($snapshot.Contains('up to date')){$snapshot}} "Caught-up notice text did not become ready.";$caughtRect=[GuideUiQa+Rect]::new();[void][GuideUiQa]::GetWindowRect($caughtUp,[ref]$caughtRect);$caughtDpi=[GuideUiQa]::GetDpiForWindow($caughtUp);if((($caughtRect.Bottom-$caughtRect.Top)*96/$caughtDpi)-gt 300){throw "Caught-up notice is not compact."};[void][GuideUiQa]::PostMessageW($caughtUp,0x111,[UIntPtr]7,[IntPtr]::Zero);Wait-Until{-not[GuideUiQa]::IsWindow($caughtUp)} 'Caught-up notice did not close.' 4|Out-Null; Stop-Fixture $existing

    $locked=Start-Fixture (New-Fixture 'locked-config' $baseConfig); $guide=Open-Guide $locked 41018; $lock=[IO.File]::Open($locked.Config,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{[void][GuideUiQa]::PostMessageW($guide,0x111,[UIntPtr]1,[IntPtr]::Zero);$errorDialog=Wait-Until{[GuideUiQa]::FindDialog([uint32]$locked.Process.Id,'Configuration Was Not Saved')} 'Locked config did not report a visible save error.'; Assert-Eq (Ini $locked 'read_updates') 0 'Locked save falsely acknowledged the update.'; if(-not[GuideUiQa]::IsWindow($guide)){throw 'Guide closed after failed persistence.'};[void][GuideUiQa]::PostMessageW($errorDialog,0x10,[UIntPtr]::Zero,[IntPtr]::Zero);Wait-Until{-not[GuideUiQa]::IsWindow($errorDialog)} 'Save-error dialog did not close.' 4|Out-Null}finally{$lock.Dispose()};Close-Guide $guide;Stop-Fixture $locked

    $client='__GuideUi_'+$runId;$handoffConfig="[user]`r`nbrowser=edge`r`ntheme_name=Dark - Gothic`r`n`r`n[guide]`r`nwelcome_handled=1`r`nread_updates=0`r`n";$handoff=New-Fixture 'copydata' $handoffConfig
    $profile=Join-Path $handoff.Data ('Sites\'+$client+'\Browsers\edge\Profile');New-Item -ItemType Directory -Path (Join-Path $profile 'Default') -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $handoff.Data ('Sites\'+$client+'\ctSpaces-client-v2')),"ctSpaces-client-schema=2`r`n",$utf8);[IO.File]::WriteAllText((Join-Path $handoff.Data ('Sites\'+$client+'\Browsers\edge\ctSpaces-browser-v2')),"ctSpaces-browser-schema=2`r`nbrowser=edge`r`n",$utf8);[IO.File]::WriteAllText((Join-Path $profile 'ctSpaces'),"ctSpaces-profile=2`r`n",$utf8);[IO.File]::WriteAllText((Join-Path $profile 'Default\Preferences'),'{}',$utf8)
    $handoff=Start-Fixture $handoff;$guide=Open-Guide $handoff 41018;if(-not[GuideUiQa]::CopyData($handoff.Main,('--client "'+$client+'" --browser edge'))){throw 'Guide-time WM_COPYDATA launch was rejected.'};Wait-Until{-not[GuideUiQa]::IsWindow($guide)} 'WM_COPYDATA did not dismiss the guide.' 4|Out-Null;Assert-Eq (Ini $handoff 'read_updates') 0 'External dismissal falsely acknowledged the guide.'
    $owned=Wait-Until{@(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue|Where-Object{$_.CommandLine -and $_.CommandLine -match [regex]::Escape($profile)} )} 'WM_COPYDATA did not launch the exact isolated Edge profile.' 20
    foreach($p in @($owned)){[GuideUiQa]::ClosePidWindows([uint32]$p.ProcessId)};Wait-Until{@(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue|Where-Object{$_.CommandLine -and $_.CommandLine -match [regex]::Escape($profile)}).Count-eq 0} 'Owned Edge profile did not close normally.' 15|Out-Null;Stop-Fixture $handoff

    if((Get-Fingerprint $liveConfig)-cne$liveConfigBefore){throw 'Live config changed.'};if(Test-Path -LiteralPath $liveClient){throw 'Live Sites received the QA client.'}
    $completed=$true; [pscustomobject]@{FreshStartSkipEscape=$true;MinimizedDefersWelcome=$true;RestartNoAuto=$true;Existing5309NoAuto=$true;OptionsNewLabels=$true;F1ManualReplay=$true;LiveThemeFlip=$true;NextMarksOnlyCurrent=$true;WhatsNewDone=$true;WhatsNewCaughtUpCompact=$true;FutureRevisionPreserved=$true;GuideOnlyPreferences=$true;LockedConfigSafe=$true;CopyDataLaunchHonored=$true;RepeatedOpenGdiDelta=($gdiAfter-$gdiBefore);ObservedDpis=@($observedDpis|Sort-Object);GothicScreenshot=$gothicShot;MarineScreenshot=$marineShot;ThemeFlipScreenshot=$themeFlipShot;LiveStateUnchanged=$true}|ConvertTo-Json
} catch { New-Item -ItemType Directory -Path $artifactRoot -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $artifactRoot ($runId+'-failure.txt')),$_.Exception.ToString(),$utf8);throw
} finally {
    foreach($f in @($launchers)){try{Stop-Fixture $f}catch{}}
    if($completed-and(Test-Path -LiteralPath $qaRoot)){Remove-Item -LiteralPath $qaRoot -Recurse -Force}else{if(Test-Path -LiteralPath $qaRoot){Write-Warning "Preserved guided-walkthrough failure fixture: $qaRoot"}}
    if($oldDpiContext -ne [IntPtr]::Zero){[void][GuideUiQa]::SetThreadDpiAwarenessContext($oldDpiContext)}
}
