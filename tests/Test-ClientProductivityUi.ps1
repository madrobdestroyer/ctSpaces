#requires -Version 7.0

param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\client-productivity-ui')
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe = [IO.Path]::GetFullPath($ExePath)
$run = [Guid]::NewGuid().ToString('N').Substring(0,10)
$qa = Join-Path $root ('build\cp-' + $run)
$app = Join-Path $qa 'a'
$data = Join-Path $app 'd'
$copy = Join-Path $app 'qa.exe'
$utf8 = [Text.UTF8Encoding]::new($false)
$liveConfig = Join-Path $env:LOCALAPPDATA 'InfinitySys\ctSpaces\config.ini'
$liveBefore = if(Test-Path -LiteralPath $liveConfig -PathType Leaf){(Get-FileHash -LiteralPath $liveConfig -Algorithm SHA256).Hash}else{'<missing>'}
$clientA = 'North Alpha '+$run
$clientB = 'North Beta '+$run
$clientC = 'South Gamma '+$run
$names = @($clientA,$clientB,$clientC)
$process = $null
$main = [IntPtr]::Zero
$owned = @()
$completed = $false
$oldDpiContext = [IntPtr]::Zero
if(-not(Test-Path -LiteralPath $exe -PathType Leaf)){throw "Missing executable: $exe"}

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class ProductivityQa {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct ComboBoxInfo { public int Size; public Rect Item,Button; public int ButtonState; public IntPtr Combo,Edit,List; }
  [StructLayout(LayoutKind.Sequential)] struct CopyData { public UIntPtr Tag; public uint Bytes; public IntPtr Text; }
  delegate bool EnumProc(IntPtr w,IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f,IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent,EnumProc f,IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint p);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr w,StringBuilder b,int n);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr w,StringBuilder b,int n);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr w,uint m,IntPtr p,StringBuilder b);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr w,int id);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr w);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr w);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w,out Rect r);
  [DllImport("user32.dll")] public static extern bool GetComboBoxInfo(IntPtr w,ref ComboBoxInfo info);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr w,uint m,UIntPtr p,IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
  [DllImport("user32.dll")] static extern bool SendMessageTimeoutW(IntPtr w,uint m,UIntPtr p,IntPtr l,uint flags,uint timeout,out UIntPtr result);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageTimeoutW")] static extern bool SendMessageTimeoutTextW(IntPtr w,uint m,UIntPtr p,string text,uint flags,uint timeout,out UIntPtr result);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageW(IntPtr w,uint m,IntPtr p,string text);
  [DllImport("user32.dll")] static extern IntPtr SendMessageW(IntPtr w,uint m,IntPtr p,IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr w,IntPtr dc,uint flags);
  public static string Text(IntPtr w){var b=new StringBuilder(32768);GetWindowTextW(w,b,b.Capacity);return b.ToString();}
  public static string ControlText(IntPtr w){var b=new StringBuilder(32768);ReadText(w,0xD,(IntPtr)b.Capacity,b);return b.ToString();}
  public static bool SetWindowTextW(IntPtr w,string text){UIntPtr result;return SendMessageTimeoutTextW(w,0xC,UIntPtr.Zero,text,3,3000,out result);}
  public static IntPtr Find(uint pid,string cls,string title){IntPtr hit=IntPtr.Zero;EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner!=pid)return true;var b=new StringBuilder(256);GetClassNameW(w,b,b.Capacity);if(b.ToString()==cls&&(String.IsNullOrEmpty(title)||Text(w)==title)){hit=w;return false;}return true;},IntPtr.Zero);return hit;}
  public static IntPtr FindWithChild(uint pid,int childId){IntPtr hit=IntPtr.Zero;EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==pid&&GetDlgItem(w,childId)!=IntPtr.Zero){hit=w;return false;}return true;},IntPtr.Zero);return hit;}
  public static string WindowTitles(uint pid){var titles=new StringBuilder();EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==pid){var title=Text(w);if(!String.IsNullOrEmpty(title)){if(titles.Length>0)titles.Append(" | ");titles.Append(title);}}return true;},IntPtr.Zero);return titles.ToString();}
  public static string ChildTexts(IntPtr parent){var texts=new StringBuilder();EnumChildWindows(parent,(w,p)=>{var title=Text(w);if(!String.IsNullOrEmpty(title)){if(texts.Length>0)texts.Append(" | ");texts.Append(title);}return true;},IntPtr.Zero);return texts.ToString();}
  public static long Query(IntPtr w,uint message,long parameter){UIntPtr result;if(!SendMessageTimeoutW(w,message,new UIntPtr(unchecked((ulong)parameter)),IntPtr.Zero,3,3000,out result))throw new Exception("UI query timed out: "+message);return unchecked((long)result.ToUInt64());}
  public static void Command(IntPtr w,uint id){UIntPtr result;if(!SendMessageTimeoutW(w,0x111,new UIntPtr(id),IntPtr.Zero,3,5000,out result))throw new Exception("UI command timed out: "+id);}
  public static void FocusControl(IntPtr parent,IntPtr child){UIntPtr result;if(!SendMessageTimeoutW(parent,0x28,new UIntPtr(unchecked((ulong)child.ToInt64())),new IntPtr(1),3,3000,out result))throw new Exception("Focus transfer timed out");}
  public static void NotifyComboSelection(IntPtr dialog,uint id,IntPtr combo){UIntPtr result;ulong command=id|(1ul<<16);if(!SendMessageTimeoutW(dialog,0x111,new UIntPtr(command),combo,3,5000,out result))throw new Exception("Combo selection notification timed out");}
  public static void NotifyComboAccepted(IntPtr dialog,uint id,IntPtr combo){UIntPtr result;ulong command=id|(9ul<<16);if(!SendMessageTimeoutW(dialog,0x111,new UIntPtr(command),combo,3,5000,out result))throw new Exception("Combo acceptance notification timed out");}
  public static void Key(IntPtr w,uint key){PostMessageW(w,0x100,new UIntPtr(key),IntPtr.Zero);PostMessageW(w,0x101,new UIntPtr(key),IntPtr.Zero);}
  public static void PasteLike(IntPtr w,string text){UIntPtr result;if(!SendMessageTimeoutTextW(w,0xC2,UIntPtr.Zero,text,3,3000,out result))throw new Exception("Text insertion timed out");}
  public static long FindComboString(IntPtr w,string text){return SendMessageW(w,0x158,new IntPtr(-1),text).ToInt64();}
  public static bool CopyDataCommand(IntPtr w,string value){IntPtr text=Marshal.StringToHGlobalUni(value),data=IntPtr.Zero;try{var c=new CopyData{Tag=new UIntPtr(0x43545350u),Bytes=checked((uint)((value.Length+1)*2)),Text=text};data=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(CopyData)));Marshal.StructureToPtr(c,data,false);UIntPtr result;return SendMessageTimeoutW(w,0x4A,UIntPtr.Zero,data,3,5000,out result)&&result!=UIntPtr.Zero;}finally{if(data!=IntPtr.Zero)Marshal.FreeHGlobal(data);Marshal.FreeHGlobal(text);}}
  public static void ClosePidWindows(uint pid){EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==pid)PostMessageW(w,0x10,UIntPtr.Zero,IntPtr.Zero);return true;},IntPtr.Zero);}
}
'@
$oldDpiContext = [ProductivityQa]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))

function Assert($condition,[string]$message){if(-not$condition){throw $message}}
function Wait-Until([scriptblock]$check,[string]$failure,[int]$seconds=12){$end=[DateTime]::UtcNow.AddSeconds($seconds);do{$value=&$check;if($value-is[IntPtr]){if($value-ne[IntPtr]::Zero){return $value}}elseif($value){return $value};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end);throw $failure}
function Fingerprint([string]$path){if(Test-Path -LiteralPath $path -PathType Leaf){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}else{'<missing>'}}
function Save-Shot([IntPtr]$window,[string]$label){New-Item -ItemType Directory -Path $ArtifactDirectory -Force|Out-Null;$r=[ProductivityQa+Rect]::new();Assert([ProductivityQa]::GetWindowRect($window,[ref]$r)) "Cannot size $label screenshot.";$bmp=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();try{Assert([ProductivityQa]::PrintWindow($window,$dc,2)) "Cannot capture $label."}finally{$g.ReleaseHdc($dc);$g.Dispose()};$path=Join-Path $ArtifactDirectory ($run+'-'+$label+'.png');$bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png);$bmp.Dispose();$path}
function Save-VisibleShot([IntPtr]$window,[string]$label){New-Item -ItemType Directory -Path $ArtifactDirectory -Force|Out-Null;[void][ProductivityQa]::SetForegroundWindow($window);Start-Sleep -Milliseconds 250;$r=[ProductivityQa+Rect]::new();Assert([ProductivityQa]::GetWindowRect($window,[ref]$r)) "Cannot size $label screenshot.";$bmp=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top);$g=[Drawing.Graphics]::FromImage($bmp);try{$g.CopyFromScreen($r.Left,$r.Top,0,0,$bmp.Size,[Drawing.CopyPixelOperation]::SourceCopy);$path=Join-Path $ArtifactDirectory ($run+'-'+$label+'.png');$bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png);$path}finally{$g.Dispose();$bmp.Dispose()}}
function Save-FilterShot([IntPtr]$main,[IntPtr]$combo){New-Item -ItemType Directory -Path $ArtifactDirectory -Force|Out-Null;Start-Sleep -Milliseconds 250;$r=[ProductivityQa+Rect]::new();Assert([ProductivityQa]::GetWindowRect($main,[ref]$r)) 'Cannot size filter screenshot.';$info=[ProductivityQa+ComboBoxInfo]::new();$info.Size=[Runtime.InteropServices.Marshal]::SizeOf($info);Assert([ProductivityQa]::GetComboBoxInfo($combo,[ref]$info)-and$info.List-ne[IntPtr]::Zero-and[ProductivityQa]::IsWindowVisible($info.List)) 'Filtered suggestion list is not visible for screenshot.';$listRect=[ProductivityQa+Rect]::new();Assert([ProductivityQa]::GetWindowRect($info.List,[ref]$listRect)) 'Cannot size suggestion list.';$r.Left=[Math]::Min($r.Left,$listRect.Left);$r.Top=[Math]::Min($r.Top,$listRect.Top);$r.Right=[Math]::Max($r.Right,$listRect.Right);$r.Bottom=[Math]::Max($r.Bottom,$listRect.Bottom);$bmp=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top);$g=[Drawing.Graphics]::FromImage($bmp);try{$g.CopyFromScreen($r.Left,$r.Top,0,0,$bmp.Size,[Drawing.CopyPixelOperation]::SourceCopy);$path=Join-Path $ArtifactDirectory ($run+'-filter-and-close-all.png');$bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png);$path}finally{$g.Dispose();$bmp.Dispose()}}
function Owned-Edge([string]$name){$profile=Join-Path $data ('Sites\'+$name+'\Browsers\edge\Profile');@(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue|Where-Object{$_.CommandLine-and$_.CommandLine.Contains($profile)})}
function Wait-Notes {Wait-Until{[ProductivityQa]::FindWithChild([uint32]$process.Id,1317)} 'Client Notes dialog did not open.'}
function Open-Notes([string]$name){Assert([ProductivityQa]::SetWindowTextW([ProductivityQa]::GetDlgItem($main,206),$name)) 'Could not select existing client.';Assert([ProductivityQa]::PostMessageW($main,0x111,[UIntPtr]41020,[IntPtr]::Zero)) 'Could not request Client Notes.';Wait-Notes}
function Confirm-CloseAll {
    $button=[ProductivityQa]::GetDlgItem($main,209)
    Wait-Until{[ProductivityQa]::IsWindowEnabled([ProductivityQa]::GetDlgItem($main,206))} 'Launcher did not finish browser launch before Close all.' 20|Out-Null
    Wait-Until{[ProductivityQa]::IsWindowEnabled($button)} 'Close all did not enable for tracked client sessions.'|Out-Null
    Assert([ProductivityQa]::PostMessageW($button,0xF5,[UIntPtr]::Zero,[IntPtr]::Zero)) 'Could not click Close all.'
    $prompt=Wait-Until{[ProductivityQa]::Find([uint32]$process.Id,'#32770','Close all clients')} 'Close all did not ask for confirmation.'
    [ProductivityQa]::Command($prompt,6)
    Wait-Until{-not[ProductivityQa]::IsWindow($prompt)} 'Close all confirmation did not close.'|Out-Null
}

try {
    New-Item -ItemType Directory -Path $app,$data -Force|Out-Null
    Copy-Item -LiteralPath $exe -Destination $copy
    New-Item -ItemType File -Path (Join-Path $app 'ctSpaces.portable')|Out-Null
    [IO.File]::WriteAllText((Join-Path $data 'config.ini'),"[user]`r`nbrowser=edge`r`ntheme_name=Marine`r`n`r`n[guide]`r`nwelcome_handled=1`r`n",$utf8)
    foreach($name in $names){$clientRoot=Join-Path $data ('Sites\'+$name);$profile=Join-Path $clientRoot 'Browsers\edge\Profile';New-Item -ItemType Directory -Path (Join-Path $profile 'Default') -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $clientRoot 'ctSpaces-client-v2'),"ctSpaces-client-schema=2`r`n",$utf8);[IO.File]::WriteAllText((Join-Path $clientRoot 'Browsers\edge\ctSpaces-browser-v2'),"ctSpaces-browser-schema=2`r`nbrowser=edge`r`n",$utf8);[IO.File]::WriteAllText((Join-Path $profile 'ctSpaces'),"ctSpaces-profile=2`r`n",$utf8);[IO.File]::WriteAllText((Join-Path $profile 'Default\Preferences'),'{}',$utf8)}
    $process=Start-Process -FilePath $copy -ArgumentList ('--qa-instance='+$run+' --qa-data-dir="'+$data+'"') -PassThru
    [void]$process.WaitForInputIdle(10000)
    $main=Wait-Until{[ProductivityQa]::Find([uint32]$process.Id,'ctSpacesLauncherClass',$null)} 'QA launcher did not open.'
    $edit=[ProductivityQa]::GetDlgItem($main,206);$combo=[ProductivityQa]::GetDlgItem($main,102)
    Assert($edit-ne[IntPtr]::Zero-and$combo-ne[IntPtr]::Zero) 'Client picker controls are missing.'
    Assert([ProductivityQa]::Query($combo,0x146,0)-eq 3) 'Fixture clients were not listed.'
    [void][ProductivityQa]::SetForegroundWindow($main)
    [ProductivityQa]::FocusControl($main,$edit)
    Assert([ProductivityQa]::SetWindowTextW($edit,'North')) 'Cannot enter substring filter.'
    Wait-Until{[ProductivityQa]::Query($combo,0x146,0)-eq 2} 'Substring filter did not show both North clients.'|Out-Null
    Assert([ProductivityQa]::Query($combo,0x157,0)-eq 1) 'Substring matches did not open the suggestion list.'
    Assert([ProductivityQa]::ControlText($edit)-eq'North') 'Filtering replaced the typed substring.'
    $filterShot=Save-FilterShot $main $combo
    [ProductivityQa]::Key($edit,0x28);Start-Sleep -Milliseconds 150
    Assert([ProductivityQa]::ControlText($edit)-eq'North') 'Browsing suggestions replaced the typed substring.'
    [ProductivityQa]::Key($edit,0x1B);Start-Sleep -Milliseconds 150
    Assert([ProductivityQa]::ControlText($edit)-eq'North') 'Escape lost the typed substring.'
    [ProductivityQa]::PasteLike($edit,'Z');Start-Sleep -Milliseconds 150
    Assert(([ProductivityQa]::ControlText($edit)).Contains('Z')) 'Inserted text did not reach the picker.'
    [ProductivityQa]::Key($edit,0x08);Start-Sleep -Milliseconds 150
    Assert([ProductivityQa]::ControlText($edit)-eq'North') 'Backspace did not restore the substring.'
    [ProductivityQa]::Key($edit,0x28)
    [void][ProductivityQa]::Query($combo,0x14F,0)
    Start-Sleep -Milliseconds 150
    Assert([ProductivityQa]::ControlText($edit)-eq'North') "Closing suggestions after arrow browsing lost the typed substring: '$([ProductivityQa]::ControlText($edit))'."
    [void][ProductivityQa]::Query($combo,0x14E,0)
    [ProductivityQa]::NotifyComboAccepted($main,102,$combo)
    Wait-Until{[ProductivityQa]::ControlText($edit)-in @($clientA,$clientB)} 'Choosing a filtered match did not fill the selected client.'|Out-Null
    [ProductivityQa]::SetWindowTextW($edit,$clientA)|Out-Null
    $notes=Open-Notes $clientA
    $notesEdit=[ProductivityQa]::GetDlgItem($notes,1317);Assert($notesEdit-ne[IntPtr]::Zero) 'Notes editor is missing.'
    $marine=Save-VisibleShot $notes 'notes-marine'
    [ProductivityQa]::PasteLike($notesEdit,'draft that should be discarded')
    [void][ProductivityQa]::PostMessageW($notes,0x111,[UIntPtr]2,[IntPtr]::Zero)
    $prompt=Wait-Until{[ProductivityQa]::Find([uint32]$process.Id,'#32770','Client Notes')} 'Unsaved notes did not ask before closing.'
    [ProductivityQa]::Command($prompt,7)
    Wait-Until{-not[ProductivityQa]::IsWindow($notes)} 'Discard did not close Client Notes.'|Out-Null
    $notes=Open-Notes $clientA;$notesEdit=[ProductivityQa]::GetDlgItem($notes,1317)
    Assert([ProductivityQa]::ControlText($notesEdit)-eq'') 'Discard changed the saved note.'
    $saved='A local note for '+$clientA
    [ProductivityQa]::PasteLike($notesEdit,$saved)
    [ProductivityQa]::Command($notes,1)
    Wait-Until{-not[ProductivityQa]::IsWindow($notes)} 'Save did not close Client Notes.'|Out-Null
    $noteFile=Join-Path $data ('Sites\'+$clientA+'\ctSpaces-client-notes.txt')
    Wait-Until{Test-Path -LiteralPath $noteFile -PathType Leaf} 'Saved notes file is missing.'|Out-Null
    $notes=Open-Notes $clientA;Assert([ProductivityQa]::ControlText([ProductivityQa]::GetDlgItem($notes,1317))-eq$saved) 'Saved notes did not reopen.'
    [ProductivityQa]::Command($notes,2);Wait-Until{-not[ProductivityQa]::IsWindow($notes)} 'Clean Cancel did not close notes.'|Out-Null
    [void][ProductivityQa]::PostMessageW($main,0x111,[UIntPtr]41006,[IntPtr]::Zero)
    $theme=Wait-Until{[ProductivityQa]::FindWithChild([uint32]$process.Id,5201)} 'Theme dialog did not open.'
    $themeCombo=[ProductivityQa]::GetDlgItem($theme,5201)
    $gothic=[ProductivityQa]::FindComboString($themeCombo,'Dark - Gothic')
    Assert($gothic-ge 0) 'Dark theme was not available.'
    [void][ProductivityQa]::Query($themeCombo,0x14E,$gothic)
    [ProductivityQa]::NotifyComboSelection($theme,5201,$themeCombo)
    [ProductivityQa]::Command($theme,5202)
    Wait-Until{-not[ProductivityQa]::IsWindow($theme)} 'Theme dialog did not apply.'|Out-Null
    $notes=Open-Notes $clientA
    $gothicShot=Save-VisibleShot $notes 'notes-gothic'
    Assert((Fingerprint $marine)-ne(Fingerprint $gothicShot)) 'Notes did not repaint for the dark theme.'
    [ProductivityQa]::Command($notes,2);Wait-Until{-not[ProductivityQa]::IsWindow($notes)} 'Dark themed notes did not close.'|Out-Null
    Wait-Until{[ProductivityQa]::IsWindowEnabled($edit)} 'Launcher did not reenable after Client Notes.'|Out-Null
    $noteHash=Fingerprint $noteFile
    $zeroButton=[ProductivityQa]::GetDlgItem($main,209);Assert($zeroButton-ne[IntPtr]::Zero) 'Close all button is missing.'
    Assert(-not[ProductivityQa]::IsWindowEnabled($zeroButton)) 'Close all should be disabled with zero tracked sessions.'
    Assert(-not$process.HasExited) 'Launcher exited with zero sessions.'
    $newName='North Alpha New '+$run
    [ProductivityQa]::SetWindowTextW($edit,$newName)|Out-Null
    Assert([ProductivityQa]::ControlText($edit)-eq$newName) 'New client name was replaced by a filtered existing name.'
    [ProductivityQa]::Key($edit,0x1B)
    Assert([ProductivityQa]::ControlText($edit)-eq$newName) 'Closing suggestions lost the new client name.'
    [ProductivityQa]::Key($edit,0x73)
    Assert([ProductivityQa]::ControlText($edit)-eq$newName) 'Reopening suggestions lost the new client name.'
    Assert([ProductivityQa]::Query($combo,0x147,0)-eq -1) 'Unique new name unexpectedly selected an existing client.'
    Assert([ProductivityQa]::IsWindowEnabled($edit)) 'CLIENT field is disabled before Enter.'
    [ProductivityQa]::Key($edit,0x0D)
    try {Wait-Until{Test-Path -LiteralPath (Join-Path $data ('Sites\'+$newName)) -PathType Container} 'Enter did not create the new client.' 25|Out-Null}
    catch {$errorDialog=[ProductivityQa]::Find([uint32]$process.Id,'#32770','Cannot Open Profile');$details=if($errorDialog-ne[IntPtr]::Zero){[ProductivityQa]::ChildTexts($errorDialog)}else{''};throw "Enter did not create '$newName'; edit='$([ProductivityQa]::ControlText($edit))', dropped=$([ProductivityQa]::Query($combo,0x157,0)), selected=$([ProductivityQa]::Query($combo,0x147,0)), enabled=$([ProductivityQa]::IsWindowEnabled($edit)), windows='$([ProductivityQa]::WindowTitles([uint32]$process.Id))', details='$details'."}
    $owned+=Wait-Until{@(Owned-Edge $newName)} 'New client Edge session did not launch.' 25
    Assert(@(Owned-Edge $clientA).Count-eq 0) 'Entering a new name opened a filtered existing client.'
    Assert([ProductivityQa]::PostMessageW($zeroButton,0xF5,[UIntPtr]::Zero,[IntPtr]::Zero)) 'Could not open Close all confirmation.'
    $cancelPrompt=Wait-Until{[ProductivityQa]::Find([uint32]$process.Id,'#32770','Close all clients')} 'Close all did not show confirmation for one session.'
    [ProductivityQa]::Command($cancelPrompt,7)
    Wait-Until{-not[ProductivityQa]::IsWindow($cancelPrompt)} 'Close all cancellation did not dismiss the prompt.'|Out-Null
    Assert(@(Owned-Edge $newName).Count-gt 0) 'Cancelling Close all closed the browser.'
    Confirm-CloseAll
    Wait-Until{@(Owned-Edge $newName).Count-eq 0} 'Close all left the single Edge session running.' 25|Out-Null
    foreach($name in @($clientA,$clientB)){Assert([ProductivityQa]::CopyDataCommand($main,('--client "'+$name+'" --browser edge'))) "Could not launch isolated Edge for $name.";$owned+=Wait-Until{@(Owned-Edge $name)} "Isolated Edge did not launch for $name." 25;Wait-Until{[ProductivityQa]::IsWindowEnabled($edit)} "Launcher did not finish tracking $name." 20|Out-Null}
    Confirm-CloseAll
    foreach($name in @($clientA,$clientB)){Wait-Until{@(Owned-Edge $name).Count-eq 0} "Close all left $name running." 25|Out-Null}
    $process.Refresh();Assert(-not$process.HasExited) 'Close all exited the launcher after browser sessions.'
    Assert((Fingerprint $noteFile)-eq$noteHash) 'Closing browsers changed Client Notes.'
    Assert((Fingerprint $liveConfig)-eq$liveBefore) 'Live ctSpaces configuration changed.'
    $completed=$true
    [pscustomobject]@{FilterMatches=2;TypedTextPreserved=$true;SelectionAndClickAway=$true;NewNameCreatedWithoutOpeningExisting=$true;CancelDiscardAndReopen=$true;NotesSaved=$true;CloseAllZeroOneAndMultiple=$true;CloseAllCancellationPreservesSession=$true;LauncherRetained=$true;FilterAndLauncherCapture=$filterShot;MarineNotesCapture=$marine;GothicNotesCapture=$gothicShot;LiveConfigUntouched=$true}|ConvertTo-Json
} finally {
    foreach($edge in @($owned)){if($edge){try{[ProductivityQa]::ClosePidWindows([uint32]$edge.ProcessId)}catch{}}}
    foreach($name in @($clientA,$clientB,$newName)){if($name){foreach($edge in @(Owned-Edge $name)){try{[ProductivityQa]::ClosePidWindows([uint32]$edge.ProcessId)}catch{}}}}
    if($main-ne[IntPtr]::Zero-and[ProductivityQa]::IsWindow($main)){[void][ProductivityQa]::PostMessageW($main,0x10,[UIntPtr]::Zero,[IntPtr]::Zero)}
    if($process){[void]$process.WaitForExit(5000);if(-not$process.HasExited){$process.Kill();[void]$process.WaitForExit(5000)};$process.Dispose()}
    if($completed-and(Test-Path -LiteralPath $qa)){
        $verifiedQa=[IO.Path]::GetFullPath($qa)
        $verifiedBuild=[IO.Path]::GetFullPath((Join-Path $root 'build'))
        Assert($verifiedQa.StartsWith(($verifiedBuild+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)) 'QA cleanup target escaped the build directory.'
        Remove-Item -LiteralPath $verifiedQa -Recurse -Force
    }elseif(Test-Path -LiteralPath $qa){Write-Warning "Preserved failed QA fixture: $qa"}
    if($oldDpiContext-ne[IntPtr]::Zero){[void][ProductivityQa]::SetThreadDpiAwarenessContext($oldDpiContext)}
}
