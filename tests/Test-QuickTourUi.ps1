param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\quick-tour-ui'),
    [string[]]$ExpectedDpis = @()
)

$ErrorActionPreference = 'Stop'
$ExpectedDpis = @($ExpectedDpis | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' } | ForEach-Object { [uint32]$_ })
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
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
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
  [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr w);
  [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr w,IntPtr dc);
  [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc,IntPtr obj);
  [DllImport("gdi32.dll",CharSet=CharSet.Unicode)] static extern IntPtr CreateFontW(int h,int w,int e,int o,int weight,uint italic,uint underline,uint strike,uint charset,uint outPrecision,uint clipPrecision,uint quality,uint pitchAndFamily,string face);
  [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr obj);
  [DllImport("kernel32.dll")] static extern int MulDiv(int number,int numerator,int denominator);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int DrawTextW(IntPtr dc,string text,int count,ref Rect rect,uint format);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageW(IntPtr w,uint m,IntPtr p,IntPtr l);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool SendMessageTimeoutW(IntPtr w,uint m,UIntPtr p,IntPtr l,uint f,uint ms,out UIntPtr result);
  public static string Text(IntPtr w){var b=new StringBuilder(32768);GetWindowTextW(w,b,b.Capacity);return b.ToString();}
  public static string ControlText(IntPtr w){var b=new StringBuilder(32768);ReadText(w,0xD,(IntPtr)b.Capacity,b);return b.ToString();}
  static IntPtr CreateUiFont(IntPtr w,bool strong){return CreateFontW(-MulDiv(10,(int)GetDpiForWindow(w),72),0,0,0,strong?600:400,0,0,0,1,0,0,5,2|32,"Segoe UI Variable Text");}
  public static bool TextFits(IntPtr w,bool wrap){Rect client;if(!GetClientRect(w,out client))return false;string text=Text(w);IntPtr dc=GetDC(w);if(dc==IntPtr.Zero)return false;IntPtr font=CreateUiFont(w,!wrap),old=IntPtr.Zero;try{if(font==IntPtr.Zero)return false;old=SelectObject(dc,font);if(old==IntPtr.Zero||old==new IntPtr(-1))return false;Rect needed=new Rect{Left=0,Top=0,Right=client.Right-client.Left,Bottom=0};uint format=0x400u|0x800u|(wrap?0x10u:0x20u);DrawTextW(dc,text,-1,ref needed,format);return needed.Right-needed.Left<=client.Right-client.Left&&needed.Bottom-needed.Top<=client.Bottom-client.Top;}finally{if(old!=IntPtr.Zero&&old!=new IntPtr(-1))SelectObject(dc,old);if(font!=IntPtr.Zero)DeleteObject(font);ReleaseDC(w,dc);}}
  public static string TextFitDetails(IntPtr w,bool wrap){Rect client;if(!GetClientRect(w,out client))return "GetClientRect failed";string text=Text(w);IntPtr dc=GetDC(w);if(dc==IntPtr.Zero)return "GetDC failed";IntPtr font=CreateUiFont(w,!wrap),old=IntPtr.Zero;try{if(font!=IntPtr.Zero)old=SelectObject(dc,font);Rect needed=new Rect{Left=0,Top=0,Right=client.Right-client.Left,Bottom=0};uint format=0x400u|0x800u|(wrap?0x10u:0x20u);int drawn=DrawTextW(dc,text,-1,ref needed,format);return String.Format("client={0}x{1}; needed={2}x{3}; drawn={4}; dpi={5}; testFont=0x{6:X}; old=0x{7:X}",client.Right-client.Left,client.Bottom-client.Top,needed.Right-needed.Left,needed.Bottom-needed.Top,drawn,GetDpiForWindow(w),font.ToInt64(),old.ToInt64());}finally{if(old!=IntPtr.Zero&&old!=new IntPtr(-1))SelectObject(dc,old);if(font!=IntPtr.Zero)DeleteObject(font);ReleaseDC(w,dc);}}
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
    if([QuickTourQa]::IsWindowVisible([QuickTourQa]::GetDlgItem($tour,1314))){Assert-InBounds $tour 1314}
    $frame=Find-Frame;$region=[QuickTourQa+Rect]::new();Assert(([QuickTourQa]::GetWindowRgnBox($frame,[ref]$region))-gt 1) 'Quick Tour highlight has no hollow shaped frame region.'
    $tr=[QuickTourQa+Rect]::new();[void][QuickTourQa]::GetWindowRect($tour,[ref]$tr);$work=[Windows.Forms.Screen]::FromHandle($tour).WorkingArea
    Assert($tr.Left-ge$work.Left-and$tr.Top-ge$work.Top-and$tr.Right-le$work.Right-and$tr.Bottom-le$work.Bottom) 'Quick Tour callout is outside the monitor work area.'
    $frame
}
function Assert-FrameContainsControl([IntPtr]$frame,[int]$controlId,[string]$label){
    $control=[QuickTourQa]::GetDlgItem($main,$controlId);Assert($control-ne[IntPtr]::Zero) "Missing $label control ($controlId)."
    try{Wait-Until{$cr=[QuickTourQa+Rect]::new();$fr=[QuickTourQa+Rect]::new();[void][QuickTourQa]::GetWindowRect($control,[ref]$cr);[void][QuickTourQa]::GetWindowRect($frame,[ref]$fr);$fr.Left-le$cr.Left-and$fr.Top-le$cr.Top-and$fr.Right-ge$cr.Right-and$fr.Bottom-ge$cr.Bottom} "Quick Tour frame did not settle around the actual $label control." 2|Out-Null}
    catch{$cr=[QuickTourQa+Rect]::new();$fr=[QuickTourQa+Rect]::new();[void][QuickTourQa]::GetWindowRect($control,[ref]$cr);[void][QuickTourQa]::GetWindowRect($frame,[ref]$fr);throw "Quick Tour frame does not surround the actual $label control. control=$($cr.Left),$($cr.Top),$($cr.Right),$($cr.Bottom); frame=$($fr.Left),$($fr.Top),$($fr.Right),$($fr.Bottom)."}
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
function Capture-CurrentStepDpis([IntPtr]$tour,[string]$theme,[string]$slug){
    $shots=[Collections.Generic.List[string]]::new();$seen=[Collections.Generic.HashSet[uint32]]::new()
    foreach($screen in [Windows.Forms.Screen]::AllScreens){
        [void][QuickTourQa]::SetWindowPos($main,[IntPtr]::Zero,$screen.WorkingArea.Left+40,$screen.WorkingArea.Top+40,0,0,0x1)
        $dpi=Wait-Until{$value=[QuickTourQa]::GetDpiForWindow($main);if($value){$value}} 'Main window did not acquire monitor DPI.' 5
        Wait-Until{[QuickTourQa]::GetDpiForWindow($tour)-eq$dpi} "Quick Tour did not follow the launcher to its $dpi-DPI monitor." 5|Out-Null
        Start-Sleep -Milliseconds 250;$frame=Assert-TourLayout $tour;[void]$seen.Add([uint32]$dpi)
        Assert([QuickTourQa]::TextFits([QuickTourQa]::GetDlgItem($tour,1310),$false)) "Quick Tour $theme/$slug title clips at $dpi DPI."
        $bodyControl=[QuickTourQa]::GetDlgItem($tour,1311)
        if(-not[QuickTourQa]::TextFits($bodyControl,$true)){$failureShot=Save-OverlayCapture $tour $frame (($theme-replace' ','-')+'-'+$slug+'-'+$dpi+'dpi-text-failure');throw "Quick Tour $theme/$slug body clips at $dpi DPI ($([QuickTourQa]::TextFitDetails($bodyControl,$true))). Capture: $failureShot"}
        if(-not(@($shots|Where-Object{$_-match('-'+$dpi+'dpi\.png$')}).Count)){$shots.Add((Save-OverlayCapture $tour $frame (($theme-replace' ','-')+'-'+$slug+'-'+$dpi+'dpi')))}
    }
    foreach($expected in $ExpectedDpis){Assert($seen.Contains([uint32]$expected)) "No $expected-DPI monitor was exercised for $theme/$slug.";Assert(@($shots|Where-Object{$_-match('-'+$expected+'dpi\.png$')}).Count-eq 1) "No $expected-DPI overlay capture was saved for $theme/$slug."}
    @($shots)
}

$expectedSteps=@(
    [pscustomobject]@{Title='Choose or name a client';Needles=@('CLIENT field','never changes this field');Demo='';Slug='client'},
    [pscustomobject]@{Title='Choose a browser';Needles=@('Edge, Chrome, Brave, or Firefox','separate slot','5.2 client','binds the existing data in place');Demo='Illustration only — no actions performed. Client A has separate Chrome and Firefox browser slots with separate sign-ins, cookies, history, extensions, and sessions.';Slug='browser'},
    [pscustomobject]@{Title='Create, Open, or Show';Needles=@('create a new slot','open an existing one','show an already-open window');Demo='';Slug='primary'},
    [pscustomobject]@{Title='Pin favorite clients';Needles=@('pushpin','favorites');Demo='';Slug='pin-intro'},
    [pscustomobject]@{Title="Use a pinned client's menu";Needles=@('Right-click a visible pin','Select, Open, Restore tabs','preview below is inert');Demo='Illustration only — no actions performed. Pinned menu: Select client; Open client; Open copied link; Restore tabs; Create desktop shortcut. Highlighted: Open client.';Slug='pinned-menu'},
    [pscustomobject]@{Title='Open a copied link';Needles=@('complete http(s) URL','selected browser and isolated client','other clipboard text is ignored','open Chromium slot receives the URL and comes forward','already-open isolated Firefox slot cannot accept another command-line URL');Demo='Illustration only — no actions performed. Pinned menu: Select client; Open client; Open copied link; Restore tabs; Create desktop shortcut. Highlighted: Open copied link.';Slug='copied-link'},
    [pscustomobject]@{Title='Create a Desktop shortcut';Needles=@('Right-click a pin','drag a visible pin onto the Windows Desktop','one managed client shortcut','selected browser','recreate it to change that browser');Demo='Illustration only — no actions performed. Pinned menu: Select client; Open client; Open copied link; Restore tabs; Create desktop shortcut. Highlighted: Create desktop shortcut. Right-click > Create desktop shortcut OR Pin Client A → Desktop.';Slug='desktop-shortcut'},
    [pscustomobject]@{Title='Reorder pinned clients';Needles=@('Drag visible pins left or right','saved for the next ctSpaces launch','illustration does not reorder anything');Demo='Illustration only — no actions performed. Before: Client A, Client B. After: Client B, Client A. Reorder pinned clients.';Slug='reorder-pins'},
    [pscustomobject]@{Title='Switch between open sessions';Needles=@('tabs across the top','Select a tab to show','use x to close','overflow');Demo='';Slug='sessions'},
    [pscustomobject]@{Title='Reorder session tabs';Needles=@('Drag open-session tabs left or right','current launcher run','not saved for the next ctSpaces launch');Demo='Illustration only — no actions performed. Before: Client A, Client B. After: Client B, Client A. Reorder session tabs.';Slug='reorder-tabs'},
    [pscustomobject]@{Title='Open the client folder';Needles=@('File Explorer','Do not edit profile files while a browser is open');Demo='';Slug='folder'},
    [pscustomobject]@{Title='Choose whether to restore tabs';Needles=@('separately for the selected client and browser','does not erase cookies, sign-ins, or browsing data');Demo='';Slug='restore-tabs'},
    [pscustomobject]@{Title='Open a temporary profile';Needles=@('disposable profile','Windows releases its files','Do not store important work');Demo='';Slug='temporary'},
    [pscustomobject]@{Title='Save or discard Default changes';Needles=@('Edit Default profile','future clients only','Save keeps approved starter','No/Discard keeps the previous Default','Never sign a real client');Demo='Illustration only — no actions performed. Options > Edit Default profile. Save applies approved starter changes to future clients. No/Discard keeps the previous Default.';Slug='default-save-discard'},
    [pscustomobject]@{Title='Rename a client';Needles=@('every slot for that client to be closed','preserving browser slots','pinned position','Restore tabs choices','verified managed shortcut');Demo='Illustration only — no actions performed. Options > Rename Client. Client A becomes Client North; browser data and managed settings are kept.';Slug='rename-client'},
    [pscustomobject]@{Title='Archive and restore a client';Needles=@('hides a closed client without deleting browser data','Archived Clients','normal picker','not a backup');Demo='Illustration only — no actions performed. Options > Archive Client hides Client A. Options > Archived Clients restores Client A.';Slug='archive-restore'},
    [pscustomobject]@{Title='Put client names first';Needles=@('Client name first in window titles','browser and Alt+Tab titles','windows already open');Demo='Illustration only — no actions performed. Options > Client name first in window titles. Example: Client A — Browser.';Slug='client-first-titles'},
    [pscustomobject]@{Title='Clean up inactive clients';Needles=@('three calendar months','Unknown history begins a fresh tracking period','Archived clients can appear','selection, acknowledgement, and fresh safety checks','permanently deletes','Cannot be undone','back up first');Demo='Illustration only — no actions performed. Clean Up Inactive Clients... Select Client A/B, acknowledge, Delete Selected.';Slug='inactive-cleanup'},
    [pscustomobject]@{Title='Delete multiple clients now';Needles=@('no inactivity wait','archived clients','recheck open/safety state','all browser data','existing backups are retained','permanently deletes','Cannot be undone','back up first');Demo='Illustration only — no actions performed. Delete Multiple Clients... Select Client A/B, acknowledge, Delete Selected.';Slug='manual-delete'},
    [pscustomobject]@{Title='Open Options';Needles=@('client management','backup and restore','full guide','Quick tour');Demo='';Slug='options'}
)
$pinStepTitles=@('Pin favorite clients',"Use a pinned client's menu",'Open a copied link','Create a Desktop shortcut','Reorder pinned clients')
$sessionStepTitles=@('Switch between open sessions','Reorder session tabs')
$anchorByTitle=@{'Choose or name a client'=205;'Create, Open, or Show'=1;'Choose whether to restore tabs'=207;'Open a temporary profile'=200;'Save or discard Default changes'=201;'Rename a client'=201;'Archive and restore a client'=201;'Put client names first'=201;'Clean up inactive clients'=201;'Delete multiple clients now'=201;'Open Options'=201}

function Assert-CurrentStep([IntPtr]$tour,$step,[int]$index,[bool]$expectPinned,[bool]$expectSession){
    $title=Wait-Until{$value=[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1310));if($value){$value}} "Quick Tour title $index did not become ready."
    Assert($title-eq$step.Title) "Quick Tour step $($index+1) title mismatch: '$title'."
    $body=[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1311));foreach($needle in $step.Needles){Assert($body.Contains($needle)) "Quick Tour '$title' is missing '$needle'."}
    Assert([QuickTourQa]::TextFits([QuickTourQa]::GetDlgItem($tour,1310),$false)) "Quick Tour '$title' title is clipped."
    $bodyControl=[QuickTourQa]::GetDlgItem($tour,1311)
    if(-not[QuickTourQa]::TextFits($bodyControl,$true)){$failureFrame=Find-Frame;$failureShot=Save-OverlayCapture $tour $failureFrame (($title-replace'[^A-Za-z0-9]+','-')+'-text-failure');throw "Quick Tour '$title' body is clipped ($([QuickTourQa]::TextFitDetails($bodyControl,$true))). Capture: $failureShot"}
    Assert(([QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1312)))-eq("$($index+1) / 20")) "Quick Tour '$title' count is wrong."
    $demo=[QuickTourQa]::GetDlgItem($tour,1314);Assert($demo-ne[IntPtr]::Zero) 'Quick Tour demo control is missing.'
    if($step.Demo){Assert([QuickTourQa]::IsWindowVisible($demo)) "Quick Tour '$title' illustration is hidden.";Assert(([QuickTourQa]::ControlText($demo))-eq$step.Demo) "Quick Tour '$title' accessible illustration text is wrong."}else{Assert(-not[QuickTourQa]::IsWindowVisible($demo)) "Quick Tour '$title' showed an unrelated illustration.";Assert(([QuickTourQa]::ControlText($demo))-eq'') "Quick Tour '$title' retained stale illustration text."}
    if($pinStepTitles-contains$title){if($expectPinned){Assert(-not$body.Contains('No clients are pinned')) "Populated pin step '$title' claimed the pin row was empty."}else{Assert($body.Contains('No clients are pinned')) "Empty pin step '$title' omitted its no-fake-client explanation."}}
    if($sessionStepTitles-contains$title){if($expectSession){Assert(-not$body.Contains('No client browser is open')) "Populated session step '$title' claimed sessions were empty."}else{Assert($body.Contains('No client browser is open')) "Empty session step '$title' omitted its real-New-tab explanation."}}
    $frame=Assert-TourLayout $tour
    if($anchorByTitle.ContainsKey($title)){Assert-FrameContainsControl $frame $anchorByTitle[$title] $title}
    if($title-eq'Open the client folder'){$folder=[QuickTourQa]::GetDlgItem($main,204);if([QuickTourQa]::IsWindowVisible($folder)){Assert-FrameContainsControl $frame 204 'client folder icon'}else{Assert-FrameContainsControl $frame 205 'CLIENT field folder fallback';foreach($needle in @('No existing client is selected','CLIENT field is highlighted','Choose a client to reveal its folder icon','opens File Explorer','Do not edit profile files while a browser is open')){Assert($body.Contains($needle)) "Empty folder step omitted '$needle'."}}}
    if(($pinStepTitles-contains$title)-and-not$expectPinned){Assert-FrameContainsControl $frame 202 'real pushpin'}
    [pscustomobject]@{Title=$title;Body=$body;Frame=$frame}
}

function Exercise-Tour([IntPtr]$tour,[string]$theme,[bool]$expectPinned,[bool]$expectSession,[bool]$captureDemos){
    $seenBodies=[Collections.Generic.HashSet[string]]::new();$captures=[Collections.Generic.List[string]]::new()
    for($index=0;$index-lt$expectedSteps.Count;$index++){
        $step=$expectedSteps[$index];$state=Assert-CurrentStep $tour $step $index $expectPinned $expectSession;Assert($seenBodies.Add($state.Body)) "Quick Tour repeated body text at '$($step.Title)'."
        if($captureDemos-and$step.Demo){foreach($shot in @(Capture-CurrentStepDpis $tour $theme $step.Slug)){$captures.Add($shot)}}
        if($index+1-lt$expectedSteps.Count){[void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1,[IntPtr]::Zero);$nextTitle=$expectedSteps[$index+1].Title;Wait-Until{[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1310))-eq$nextTitle} "Quick Tour did not advance to '$nextTitle'."|Out-Null}
    }
    Assert(([QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1)))-eq'Done') 'Quick Tour final button is not Done.'
    [void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1,[IntPtr]::Zero);Wait-TourClosed $tour 'Quick Tour did not close with Done.'
    [pscustomobject]@{Bodies=$seenBodies;Captures=$captures}
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
    $first=Assert-CurrentStep $tour $expectedSteps[0] 0 $false $false
    [void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1,[IntPtr]::Zero);Wait-Until{[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1310))-eq$expectedSteps[1].Title} 'Quick Tour Next did not advance.'|Out-Null
    [void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1313,[IntPtr]::Zero);Wait-Until{[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1310))-eq$expectedSteps[0].Title} 'Quick Tour Back did not restore the first step.'|Out-Null
    $marineResult=Exercise-Tour $tour 'Marine' $false $false $true
    $steps=$expectedSteps.Count;$seenBodies=$marineResult.Bodies;$marineShots=@($marineResult.Captures)
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

    # Restart in Gothic with two real QA-only pinned clients. The tour must point
    # at the populated row without reordering it or touching either profile.
    $client='__QuickTourA_'+$run;$clientB='__QuickTourB_'+$run;$profile=Join-Path $data ('Sites\'+$client+'\Browsers\edge\Profile')
    foreach($name in @($client,$clientB)){
        $clientRoot=Join-Path $data ('Sites\'+$name);$clientProfile=Join-Path $clientRoot 'Browsers\edge\Profile';New-Item -ItemType Directory -Path (Join-Path $clientProfile 'Default') -Force|Out-Null
        [IO.File]::WriteAllText((Join-Path $clientRoot 'ctSpaces-client-v2'),"ctSpaces-client-schema=2`r`n",$utf8)
        [IO.File]::WriteAllText((Join-Path $clientRoot 'Browsers\edge\ctSpaces-browser-v2'),"ctSpaces-browser-schema=2`r`nbrowser=edge`r`n",$utf8)
        [IO.File]::WriteAllText((Join-Path $clientProfile 'ctSpaces'),"ctSpaces-profile=2`r`n",$utf8)
        [IO.File]::WriteAllText((Join-Path $clientProfile 'Default\Preferences'),'{}',$utf8)
    }
    $gothicConfig=([IO.File]::ReadAllText($config)-replace'theme_name=Marine','theme_name=Dark - Gothic')+"`r`n[pinned]`r`ncount=2`r`nclient0=$client`r`nclient1=$clientB`r`n";[IO.File]::WriteAllText($config,$gothicConfig,$utf8)
    Start-QaApp;$pinnedConfigBefore=Get-StableFingerprint $config;$markerABefore=Get-Fingerprint (Join-Path $data ('Sites\'+$client+'\ctSpaces-client-v2'));$markerBBefore=Get-Fingerprint (Join-Path $data ('Sites\'+$clientB+'\ctSpaces-client-v2'))
    $tour=Open-Tour;$gothicResult=Exercise-Tour $tour 'Dark Gothic' $true $false $true;$gothicShots=@($gothicResult.Captures)
    Assert((Get-StableFingerprint $config)-eq$pinnedConfigBefore) 'Populated Quick Tour changed pinned order or preferences.';Assert((Get-Fingerprint (Join-Path $data ('Sites\'+$client+'\ctSpaces-client-v2')))-eq$markerABefore-and(Get-Fingerprint (Join-Path $data ('Sites\'+$clientB+'\ctSpaces-client-v2')))-eq$markerBBefore) 'Populated Quick Tour changed client markers.'

    # A real isolated QA session is permitted setup. WM_COPYDATA must first close
    # the overlay, then the reopened tour must recognize the populated session.
    $tour=Open-Tour;Assert([QuickTourQa]::CopyData($main,('--client "'+$client+'" --browser edge'))) 'Quick-Tour-time WM_COPYDATA launch was rejected.'
    Wait-TourClosed $tour 'WM_COPYDATA did not dismiss Quick Tour before launch handoff.'
    $owned=Wait-Until{@(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue|Where-Object{$_.CommandLine-and$_.CommandLine-match[regex]::Escape($profile)})} 'WM_COPYDATA did not launch the exact isolated Edge profile.' 20
    Wait-Until{[QuickTourQa]::IsWindowEnabled($main)} 'Launcher did not finish the isolated Edge handoff.' 15|Out-Null
    $sessionConfigBefore=Get-StableFingerprint $config;$tour=Open-Tour
    for($index=0;$index-lt 8;$index++){[void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1,[IntPtr]::Zero);$next=$expectedSteps[$index+1].Title;Wait-Until{[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1310))-eq$next} "Quick Tour did not reach populated session step '$next'."|Out-Null}
    $sessionState=Assert-CurrentStep $tour $expectedSteps[8] 8 $true $true;$populatedSessionShot=Save-OverlayCapture $tour $sessionState.Frame 'Dark-Gothic-populated-session'
    [void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]1,[IntPtr]::Zero);Wait-Until{[QuickTourQa]::ControlText([QuickTourQa]::GetDlgItem($tour,1310))-eq$expectedSteps[9].Title} 'Quick Tour did not reach populated session reorder step.'|Out-Null
    [void](Assert-CurrentStep $tour $expectedSteps[9] 9 $true $true);[void][QuickTourQa]::PostMessageW($tour,0x111,[UIntPtr]2,[IntPtr]::Zero);Wait-TourClosed $tour 'Populated session tour did not close.'
    Assert((Get-StableFingerprint $config)-eq$sessionConfigBefore) 'Populated-session tour changed preferences or pinned order.'
    $otherClients=@(Get-ChildItem -LiteralPath (Join-Path $data 'Sites') -Directory|Where-Object{$_.Name-ne$client-and$_.Name-ne$clientB});Assert($otherClients.Count-eq 0) 'Quick Tour or handoff created an unexpected client profile.'
    foreach($edgeProcess in @($owned)){[QuickTourQa]::ClosePidWindows([uint32]$edgeProcess.ProcessId)}
    Wait-Until{@(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue|Where-Object{$_.CommandLine-and$_.CommandLine-match[regex]::Escape($profile)}).Count-eq 0} 'Owned Edge profile did not close normally.' 15|Out-Null
    Stop-QaApp

    Assert((Get-Fingerprint $liveConfig)-eq$liveConfigBefore) 'Live ctSpaces configuration changed.'
    $completed=$true
    [pscustomobject]@{QuickTourSteps=$steps;DistinctBodies=$seenBodies.Count;DemoSteps=12;GuideButtonHandoff=$true;ActualAnchorChecks=@('CLIENT field','primary action','empty-state pushpin','client folder','Restore tabs','Temporary','Options');StateSensitiveCopyChecks=@('populated pin guidance','empty session guidance','populated session guidance');OverlayFrameLifetime=$true;MarineCaptures=$marineShots;GothicCaptures=$gothicShots;PopulatedSessionCapture=$populatedSessionShot;ExpectedDpis=$ExpectedDpis;CloseMinimizeCleanup=$true;CopyDataLaunchHonored=$true;ConfigPinnedAndClientIsolation=$true;GdiDelta=($gdiAfter-$gdiBefore)}|ConvertTo-Json
} catch {
    New-Item -ItemType Directory -Path $artifactRoot -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $artifactRoot ($run+'-failure.txt')),$_.Exception.ToString(),$utf8);throw
} finally {
    foreach($edgeProcess in @($owned)){try{[QuickTourQa]::ClosePidWindows([uint32]$edgeProcess.ProcessId)}catch{}}
    if($process){try{Stop-QaApp}catch{}}
    if($completed-and(Test-Path -LiteralPath $qa)){Remove-Item -LiteralPath $qa -Recurse -Force}elseif(Test-Path -LiteralPath $qa){Write-Warning "Preserved Quick Tour failure fixture: $qa"}
    if($oldDpiContext-ne[IntPtr]::Zero){[void][QuickTourQa]::SetThreadDpiAwarenessContext($oldDpiContext)}
}
