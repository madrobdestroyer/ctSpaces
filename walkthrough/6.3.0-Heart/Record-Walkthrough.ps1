param([ValidateSet('ctSpaces','Notes')][string]$Video,[switch]$Smoke,[switch]$FastReview,[string]$ReviewChapter)
$ErrorActionPreference='Stop'
if($ReviewChapter-and-not$FastReview){throw 'A chapter subset is only allowed for a rehearsal'}
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $root 'tests/QaProcess.ps1')
$timing=Get-Content (Join-Path $PSScriptRoot 'timing.json') -Raw | ConvertFrom-Json
$plan=$timing.videos | Where-Object id -eq $Video
$run=Join-Path $PSScriptRoot ($Video+'-recording-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$fixture=Join-Path $root ('build/vh-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $fixture | Out-Null
$app=Join-Path $fixture 'qa.exe';Copy-Item (Join-Path $root 'dist/x64/Release/ctSpaces.exe') $app
New-Item -ItemType File -Path (Join-Path $fixture 'ctSpaces.portable') | Out-Null
$data=Join-Path $fixture 'd';New-Item -ItemType Directory -Path $data | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $data 'config.ini'),"[user]`r`nbrowser=edge`r`ntheme_name=Dark - Gothic`r`n[guide]`r`nwelcome_handled=1`r`n[notes_window]`r`nhelp_seen=1`r`nlast_client=Beacon Works (Demo)`r`nwidth=1000`r`nheight=650`r`n",$utf8)
foreach($name in @('Northstar Office (Demo)','Beacon Works (Demo)','Harbor Labs (Demo)','Riverview Services (Demo)')) {
 $client=Join-Path $data ('Sites/'+$name);$profile=Join-Path $client 'Browsers/edge/Profile';New-Item -ItemType Directory -Path (Join-Path $profile 'Default') -Force | Out-Null
 [IO.File]::WriteAllText((Join-Path $client 'ctSpaces-client-v2'),"ctSpaces-client-schema=2`r`n",$utf8)
 [IO.File]::WriteAllText((Join-Path $client 'Browsers/edge/ctSpaces-browser-v2'),"ctSpaces-browser-schema=2`r`nbrowser=edge`r`n",$utf8)
 [IO.File]::WriteAllText((Join-Path $profile 'ctSpaces'),"ctSpaces-profile=2`r`n",$utf8)
 [IO.File]::WriteAllText((Join-Path $profile 'Default/Preferences'),'{}',$utf8)
}
New-Item -ItemType File -Path (Join-Path $data 'video-fixture.marker') | Out-Null
& (Join-Path $PSScriptRoot 'tools/CaptureNative.exe') --seed-demo $data
if($LASTEXITCODE-ne0){throw 'Could not prepare the owned Trash demonstration'}
Add-Type -TypeDefinition @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class VideoUi {
 public static uint Owner; [StructLayout(LayoutKind.Sequential)] struct Gui {public uint Size,Flags;public IntPtr Active,Focus,Capture,Menu,Move,Caret;public Rect Rect;} [DllImport("user32.dll")]static extern bool GetGUIThreadInfo(uint thread,ref Gui g); [DllImport("user32.dll")]static extern bool GetPhysicalCursorPos(out Point p); public static string State(IntPtr w){uint pid;uint thread=GetWindowThreadProcessId(w,out pid);var g=new Gui{Size=(uint)Marshal.SizeOf(typeof(Gui))};GetGUIThreadInfo(thread,ref g);Point p;GetPhysicalCursorPos(out p);return "focus="+GetDlgCtrlID(g.Focus)+" capture="+GetDlgCtrlID(g.Capture)+" move="+g.Move+" physical="+p.X+","+p.Y;}
 delegate bool EnumProc(IntPtr w,IntPtr p);
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential)] struct Point {public int X,Y;}
 [StructLayout(LayoutKind.Sequential)] struct ComboInfo {public int Size;public Rect Item,Button;public uint State;public IntPtr Combo,Edit,List;}
 [StructLayout(LayoutKind.Sequential)] struct Mouse {public int X,Y;public uint Data,Flags,Time;public IntPtr Extra;}
 [StructLayout(LayoutKind.Sequential)] struct Keyboard {public ushort Key,Scan;public uint Flags,Time;public IntPtr Extra;}
 [StructLayout(LayoutKind.Explicit)] struct Union {[FieldOffset(0)]public Mouse Mouse;[FieldOffset(0)]public Keyboard Key;}
 [StructLayout(LayoutKind.Sequential)] struct Input {public uint Type;public Union Data;}
 [DllImport("user32.dll")]static extern uint SendInput(uint n,Input[] inputs,int size);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc proc,IntPtr p); [DllImport("user32.dll")]static extern bool EnumChildWindows(IntPtr parent,EnumProc proc,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassNameW(IntPtr w,StringBuilder b,int size);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowTextW(IntPtr w,StringBuilder b,int size);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr w);
 [DllImport("user32.dll")]public static extern bool IsWindowEnabled(IntPtr w);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr w);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr w,int id);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr w,out Rect r);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr w,out Rect r);
 [DllImport("user32.dll")]static extern bool ClientToScreen(IntPtr w,ref Point p);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int cx,int cy,uint flags);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr w,int command);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr w);
 [DllImport("user32.dll")]static extern bool SetPhysicalCursorPos(int x,int y);
 [DllImport("user32.dll")]static extern IntPtr WindowFromPoint(Point p);
 [DllImport("user32.dll")]static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]static extern bool GetComboBoxInfo(IntPtr combo,ref ComboInfo info);
 [DllImport("user32.dll")]public static extern IntPtr GetAncestor(IntPtr w,uint flag);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr w);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern bool PostMessageW(IntPtr w,uint m,UIntPtr p,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr SendMessageW(IntPtr w,uint m,IntPtr p,IntPtr l);
 [DllImport("user32.dll",EntryPoint="SendMessageW",CharSet=CharSet.Unicode)]static extern IntPtr ReadText(IntPtr w,uint m,IntPtr p,StringBuilder b);
 [DllImport("user32.dll")]static extern bool GetMenuItemRect(IntPtr w,IntPtr menu,uint item,out Rect r);
 [DllImport("user32.dll")]static extern int GetMenuItemCount(IntPtr menu);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetMenuStringW(IntPtr menu,uint item,StringBuilder b,int size,uint flags);
 [DllImport("user32.dll")]static extern int GetDlgCtrlID(IntPtr w);
 public static uint Pid(IntPtr w){uint p;GetWindowThreadProcessId(w,out p);return p;}
 public static string Text(IntPtr w){var b=new StringBuilder(20000);ReadText(w,0xD,(IntPtr)b.Capacity,b);return b.ToString();}
 public static IntPtr Find(uint pid,string cls,int child){IntPtr hit=IntPtr.Zero;EnumWindows((w,p)=>{var b=new StringBuilder(128);GetClassNameW(w,b,b.Capacity);if(Pid(w)==pid&&IsWindowVisible(w)&&(String.IsNullOrEmpty(cls)||b.ToString()==cls)&&(child==0||GetDlgItem(w,child)!=IntPtr.Zero)){if(hit!=IntPtr.Zero)throw new Exception("Ambiguous native target");hit=w;}return true;},IntPtr.Zero);return hit;}
 public static IntPtr Button(IntPtr owner,string name){IntPtr found=IntPtr.Zero;EnumChildWindows(owner,(w,p)=>{var kind=new StringBuilder(128);GetClassNameW(w,kind,kind.Capacity);if(kind.ToString()=="Button"&&Text(w).Replace("&","")==name)found=w;return true;},IntPtr.Zero);return found;}
 public static IntPtr Named(uint pid,string title){IntPtr found=IntPtr.Zero;EnumWindows((w,p)=>{if(Pid(w)==pid&&IsWindowVisible(w)&&Text(w)==title)found=w;return true;},IntPtr.Zero);return found;}
 public static string Windows(uint pid){string result="";EnumWindows((w,p)=>{if(Pid(w)==pid&&IsWindowVisible(w)){var b=new StringBuilder(128);GetClassNameW(w,b,b.Capacity);result+=b+" | "+Text(w)+" | notes="+GetDlgItem(w,1334)+" | picker="+GetDlgItem(w,1558)+"\n";}return true;},IntPtr.Zero);return result;}
 [DllImport("kernel32.dll")]static extern IntPtr OpenProcess(uint rights,bool inherit,uint pid);
 [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr p);
 [DllImport("kernel32.dll")]static extern IntPtr VirtualAllocEx(IntPtr p,IntPtr address,UIntPtr size,uint type,uint protect);
 [DllImport("kernel32.dll")]static extern bool VirtualFreeEx(IntPtr p,IntPtr address,UIntPtr size,uint type);
 [DllImport("kernel32.dll")]static extern bool ReadProcessMemory(IntPtr p,IntPtr address,byte[] buffer,UIntPtr size,out UIntPtr read);
 public static Rect TabBounds(IntPtr tab,int index){if(Pid(tab)!=Owner)throw new Exception("Unowned tab");IntPtr p=OpenProcess(0x1038,false,Owner),address=IntPtr.Zero;if(p==IntPtr.Zero)throw new Exception("Cannot read owned tab bounds");try{address=VirtualAllocEx(p,IntPtr.Zero,(UIntPtr)16,0x3000,4);if(address==IntPtr.Zero)throw new Exception("Tab buffer unavailable");if(SendMessageW(tab,0x130A,(IntPtr)index,address)==IntPtr.Zero)throw new Exception("Tab bounds unavailable");var bytes=new byte[16];UIntPtr read;if(!ReadProcessMemory(p,address,bytes,(UIntPtr)16,out read)||read.ToUInt32()!=16)throw new Exception("Tab bounds read failed");return new Rect{Left=BitConverter.ToInt32(bytes,0),Top=BitConverter.ToInt32(bytes,4),Right=BitConverter.ToInt32(bytes,8),Bottom=BitConverter.ToInt32(bytes,12)};}finally{if(address!=IntPtr.Zero)VirtualFreeEx(p,address,UIntPtr.Zero,0x8000);CloseHandle(p);}}
 public static void ClickAt(IntPtr w,int x,int y){SetThreadDpiAwarenessContext((IntPtr)(-4));if(!IsWindowVisible(w)||!IsWindowEnabled(w)||Pid(w)!=Owner)throw new Exception("Target is unavailable or unowned");if(Pid(GetForegroundWindow())!=Owner)SetForegroundWindow(GetAncestor(w,2));SetPhysicalCursorPos(x,y);System.Threading.Thread.Sleep(60);if(Pid(WindowFromPoint(new Point{X=x,Y=y}))!=Owner)throw new Exception("Click is occluded by process "+Pid(WindowFromPoint(new Point{X=x,Y=y}))+"; target="+Owner);Console.WriteLine("CLICK requested="+GetDlgCtrlID(w)+" hit="+GetDlgCtrlID(WindowFromPoint(new Point{X=x,Y=y}))+" text="+Text(WindowFromPoint(new Point{X=x,Y=y}))+" at="+x+","+y);var a=new[]{new Input{Type=0,Data=new Union{Mouse=new Mouse{Flags=2}}},new Input{Type=0,Data=new Union{Mouse=new Mouse{Flags=4}}}};if(SendInput(1,new[]{a[0]},Marshal.SizeOf(typeof(Input)))!=1)throw new Exception("Mouse down failed");System.Threading.Thread.Sleep(100);if(SendInput(1,new[]{a[1]},Marshal.SizeOf(typeof(Input)))!=1)throw new Exception("Mouse up failed");Console.WriteLine(State(w));}
 public static void Click(IntPtr w){SetThreadDpiAwarenessContext((IntPtr)(-4));Rect r;if(!GetWindowRect(w,out r))throw new Exception("Missing bounds");ClickAt(w,(r.Left+r.Right)/2,(r.Top+r.Bottom)/2);}
 public static void Relative(IntPtr w,int x,int y){SetThreadDpiAwarenessContext((IntPtr)(-4));var p=new Point{X=x,Y=y};ClientToScreen(w,ref p);ClickAt(w,p.X,p.Y);}
 public static void Key(params ushort[] keys){if(Pid(GetForegroundWindow())!=Owner)throw new Exception("Keyboard target lost foreground");var a=new Input[keys.Length*2];for(int i=0;i<keys.Length;i++){a[i]=new Input{Type=1,Data=new Union{Key=new Keyboard{Key=keys[i]}}};a[a.Length-1-i]=new Input{Type=1,Data=new Union{Key=new Keyboard{Key=keys[i],Flags=2}}};}if(SendInput((uint)a.Length,a,Marshal.SizeOf(typeof(Input)))!=a.Length)throw new Exception("Key failed");}
 public static void Type(string text){foreach(char c in text){if(Pid(GetForegroundWindow())!=Owner)throw new Exception("Typing target lost foreground");if(c=='\r')continue;if(c=='\n'){Key(13);continue;}var a=new[]{new Input{Type=1,Data=new Union{Key=new Keyboard{Scan=c,Flags=4}}},new Input{Type=1,Data=new Union{Key=new Keyboard{Scan=c,Flags=6}}}};if(SendInput(2,a,Marshal.SizeOf(typeof(Input)))!=2)throw new Exception("Typing failed");System.Threading.Thread.Sleep(60);}}
 public static void Combo(IntPtr w,int index){SetThreadDpiAwarenessContext((IntPtr)(-4));Click(w);var info=new ComboInfo{Size=Marshal.SizeOf(typeof(ComboInfo))};for(int i=0;i<50;i++){GetComboBoxInfo(w,ref info);if(IsWindowVisible(info.List))break;System.Threading.Thread.Sleep(50);}if(!IsWindowVisible(info.List))throw new Exception("Dropdown missing");int top=SendMessageW(w,0x15B,IntPtr.Zero,IntPtr.Zero).ToInt32(),height=SendMessageW(w,0x154,IntPtr.Zero,IntPtr.Zero).ToInt32();if(height<=0||index<top)throw new Exception("Dropdown item bounds invalid");Relative(info.List,35,(index-top)*height+height/2);}
 public static void FirstMatch(IntPtr combo){var info=new ComboInfo{Size=Marshal.SizeOf(typeof(ComboInfo))};if(!GetComboBoxInfo(combo,ref info)||!IsWindowVisible(info.List))throw new Exception("Client matches not visible");Relative(info.List,70,12);}
 public static void Menu(IntPtr popup,string label){var menu=SendMessageW(popup,0x1E1,IntPtr.Zero,IntPtr.Zero);for(uint i=0;i<GetMenuItemCount(menu);i++){var b=new StringBuilder(256);GetMenuStringW(menu,i,b,b.Capacity,0x400);if(b.ToString().IndexOf(label,StringComparison.OrdinalIgnoreCase)>=0){Rect r;if(!GetMenuItemRect(IntPtr.Zero,menu,i,out r))throw new Exception("Menu bounds unavailable");ClickAt(popup,(r.Left+r.Right)/2,(r.Top+r.Bottom)/2);return;}}throw new Exception("Menu item missing: "+label);}

}
'@
[void][VideoUi]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$process=$null;$recorder=$null;$main=[IntPtr]::Zero;$notes=[IntPtr]::Zero;$browserPids=@();$events=@()
function Wait([scriptblock]$Check,[string]$Reason,[int]$Seconds=15){$end=[DateTime]::UtcNow.AddSeconds($Seconds);do{$value=&$Check;if($value -is [IntPtr]){if($value-ne[IntPtr]::Zero){return $value}}elseif($value){return $value};Start-Sleep -Milliseconds 90}while([DateTime]::UtcNow-lt$end);throw $Reason}
function Own([int]$Child,[string]$Class=$null){Wait{[VideoUi]::Find([uint32]$process.Id,$Class,$Child)} "Missing native control $Child"}
function Click([IntPtr]$Window,[int]$Id){$child=[VideoUi]::GetDlgItem($Window,$Id);if($child-eq[IntPtr]::Zero-or[VideoUi]::Pid($child)-ne$process.Id){throw "Unowned/missing control $Id"};Wait{[VideoUi]::IsWindowVisible($child)-and[VideoUi]::IsWindowEnabled($child)-and[VideoUi]::IsWindowEnabled([VideoUi]::GetAncestor($child,2))} "Unavailable control $Id; visible=$([VideoUi]::IsWindowVisible($child)), enabled=$([VideoUi]::IsWindowEnabled($child))" | Out-Null;[VideoUi]::Click($child);Start-Sleep -Milliseconds 450}
function Fill([IntPtr]$Window,[int]$Id,[string]$Text){Click $Window $Id;[VideoUi]::Key(17,65);[VideoUi]::Type($Text);Wait{[VideoUi]::Text([VideoUi]::GetDlgItem($Window,$Id)).Replace("`r",'') -eq $Text.Replace("`r",'')} "Text input queue did not finish for control $Id" 8 | Out-Null;$actual=[VideoUi]::Text([VideoUi]::GetDlgItem($Window,$Id));if($actual.Replace("`r",'')-ne$Text.Replace("`r",'')){throw "Text did not reach control $Id; expected='$Text', actual='$actual'"}}
function Menu([string]$Label){$popup=Own 0 '#32768';[VideoUi]::Menu($popup,$Label);Start-Sleep -Milliseconds 300}
function Select-Client([string]$Name){Fill $main 206 $Name;if($Name-ne'Oakridge Demo'){[VideoUi]::FirstMatch([VideoUi]::GetDlgItem($main,102));Start-Sleep -Milliseconds 500};[VideoUi]::Key(27);Start-Sleep -Milliseconds 650}
function Stage([IntPtr]$Window,[int]$X=350,[int]$Y=280,[int]$Width=0,[int]$Height=0){[void][VideoUi]::SetWindowPos($Window,[IntPtr]::new(-1),$X,$Y,$Width,$Height, $(if($Width){0x40}else{0x41}));[void][VideoUi]::SetForegroundWindow($Window);Start-Sleep -Milliseconds 350}
function Browser([string]$Client,[string]$BrowserName='msedge.exe'){$profile=Join-Path $data ('Sites/'+$Client);$owned=Wait{@(Get-QaProcess -Filter ("Name='"+$BrowserName+"'")|Where-Object{$_.CommandLine-and$_.CommandLine.Contains($profile)})} 'Owned browser did not start';$browser=Wait{foreach($p in $owned){$w=[VideoUi]::Find([uint32]$p.ProcessId,'Chrome_WidgetWin_1',0);if($w-ne[IntPtr]::Zero){return $w}}} 'Owned browser window missing';$script:browserPids+=@($owned.ProcessId);[IO.File]::WriteAllText((Join-Path $run 'allowed-pids.txt'),(@($process.Id)+$browserPids -join ' '),$utf8);Stage $browser 130 100 1260 800;return $browser}
function Finish-Chapter($Chapter){$elapsed=$clock.Elapsed.TotalSeconds;$target=[double]$Chapter.start+[double]$Chapter.duration;if($elapsed-gt$target+0.8){throw "Chapter exceeded narration slot: $($Chapter.id), $elapsed > $target"};while($clock.Elapsed.TotalSeconds-lt$target){Start-Sleep -Milliseconds 200}}
try {
 $process=Start-Process -FilePath $app -ArgumentList ('--qa-instance=walkthrough-'+[IO.Path]::GetFileName($run)+' --qa-data-dir="'+$data+'"') -PassThru
 [VideoUi]::Owner=[uint32]$process.Id
 $main=Own 206 'ctSpacesLauncherClass';Stage $main
 Wait{[VideoUi]::IsWindowEnabled([VideoUi]::GetDlgItem($main,206))} 'Launcher did not become ready' | Out-Null
 if($Video-eq'Notes'){Select-Client 'Beacon Works (Demo)';Click $main 210;if([VideoUi]::Find([uint32]$process.Id,$null,1334)-eq[IntPtr]::Zero){Click $main 210};$notes=Own 1334;Stage $notes 150 95 1250 815}
 [IO.File]::WriteAllText((Join-Path $run 'allowed-pids.txt'),[string]$process.Id,$utf8)
 $raw=Join-Path $run 'live.mp4';$stop=Join-Path $run 'stop';$ready=Join-Path $run 'ready'
 $recorder=Start-Process -FilePath (Join-Path $PSScriptRoot 'tools/CaptureNative.exe') -ArgumentList ('"'+$raw+'" "'+(Join-Path $run 'allowed-pids.txt')+'" "'+$stop+'" "'+$ready+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'capture-receipt.json') -RedirectStandardError (Join-Path $run 'capture-errors.log')
 Wait{Test-Path $ready} 'Recorder did not initialize' | Out-Null;$clock=[Diagnostics.Stopwatch]::StartNew()
 foreach($chapter in $plan.chapters) {if($ReviewChapter-and$chapter.id-ne$ReviewChapter){continue}
  Write-Output ('RECORD '+$chapter.id);$events+=@{id=$chapter.id;start=$clock.Elapsed.TotalSeconds}
  switch($chapter.id) {
   'app-start' {$surface=[VideoUi]::GetDlgItem($main,205);$r=[VideoUi+Rect]::new();[void][VideoUi]::GetClientRect($surface,[ref]$r);[VideoUi]::Relative($surface,$r.Right-15,$r.Bottom/2);Start-Sleep -Seconds 2;[VideoUi]::Key(27)}
   'app-create' {Select-Client 'Oakridge Demo';Click $main 1;$browser=Browser 'Oakridge Demo';Stage $main 200 650}
   'app-find' {[void][VideoUi]::ShowWindow($browser,6);Stage $main;Fill $main 206 'Beacon';Start-Sleep -Seconds 1;[VideoUi]::FirstMatch([VideoUi]::GetDlgItem($main,102));Start-Sleep -Milliseconds 500;Select-Client 'Beacon Works (Demo)';Click $main 202;Select-Client 'Northstar Office (Demo)';Click $main 202}
   'app-browser' {$r=[VideoUi+Rect]::new();[void][VideoUi]::GetClientRect($main,[ref]$r);[VideoUi]::Relative($main,60,$r.Bottom-20);Menu 'Google Chrome';Select-Client 'Beacon Works (Demo)';Click $main 207;Click $main 207;Click $main 1;$chrome=Browser 'Beacon Works (Demo)' 'chrome.exe';Stage $main 200 650}
   'app-tools' {Click $main 201;Start-Sleep -Seconds 3;Menu 'Guided walkthrough';$guide=Own 1301;Stage $guide 170 100 1150 780;Click $guide 1;Click $guide 2}
   'app-close' {Click $main 209;$confirm=Own 6 '#32770';Click $confirm 7;Click $main 209;$confirm=Own 6 '#32770';Click $confirm 6;Wait{-not[VideoUi]::IsWindowEnabled([VideoUi]::GetDlgItem($main,209))} 'Client sessions did not close'|Out-Null;Select-Client 'Beacon Works (Demo)';Click $main 210;if([VideoUi]::Find([uint32]$process.Id,$null,1334)-eq[IntPtr]::Zero){Click $main 210};$notes=Own 1334;Stage $notes 150 95 1250 815}
   'notes-start' {Click $notes 1555;$picker=Own 1558;Fill $picker 1558 'Harbor';Start-Sleep -Seconds 2;[VideoUi]::Key(27);Click $notes 1555;$picker=Own 1558;Fill $picker 1558 'Beacon';Click $picker 1559;[VideoUi]::Key(13);Start-Sleep -Milliseconds 600;$notes=Own 1334}
   'notes-write' {Click $notes 1335;Click $notes 1317;[VideoUi]::Type("Verify queues after rollout.`nNext: confirm the vendor window.");Start-Sleep -Seconds 1;$tabs=[VideoUi]::GetDlgItem($notes,1334);$index=[VideoUi]::SendMessageW($tabs,0x130B,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32();$rect=[VideoUi]::TabBounds($tabs,$index);[VideoUi]::Relative($tabs,($rect.Left+$rect.Right)/2,($rect.Top+$rect.Bottom)/2);Start-Sleep -Milliseconds 80;[VideoUi]::Relative($tabs,($rect.Left+$rect.Right)/2,($rect.Top+$rect.Bottom)/2);$rename=Own 1337;Fill $rename 1337 'Rollout follow-up';Click $rename 1;Click $notes 1317;[VideoUi]::Key(17,16,33)}
   'notes-details' {Click $notes 1557;$details=Own 1544;Fill $details 1520 '1048';Fill $details 1521 'https://tickets.example.test/1048';[VideoUi]::Combo([VideoUi]::GetDlgItem($details,1543),3);Fill $details 1544 'printing, rollout';Click $notes 1556}
   'notes-template' {Click $notes 45040;$manager=Own 1547;Stage $manager 140 95 1270 810;Click $manager 1515;Fill $manager 1547 'Rollout follow-up';Fill $manager 1548 'Use after a printer change';Fill $manager 1513 "Rollout follow-up`nCompleted:`nNext action:";Click $manager 1517;Click $manager 1550;$notes=Own 1334}
   'notes-snippet' {Click $notes 1317;[VideoUi]::Key(17,35);[VideoUi]::Key(13);Click $notes 45045;$manager=Own 1547;Stage $manager 140 95 1270 810;Click $manager 1515;Fill $manager 1547 'Keep original queue';Fill $manager 1548 'Before closing a printer rollout';Fill $manager 1513 'Keep the original queue until the test print succeeds.';Click $manager 1517;Click $manager 1550;$notes=Own 1334}
   'notes-code' {Click $notes 1317;[VideoUi]::Key(17,35);[VideoUi]::Key(13);[VideoUi]::Combo([VideoUi]::GetDlgItem($notes,1320),3);$code=Own 1546;[VideoUi]::Combo([VideoUi]::GetDlgItem($code,1545),1);Fill $code 1546 "Get-Printer -ComputerName PRINT01 |`n  Select-Object Name, DriverName";Click $code 1;Click $notes 45048;[VideoUi]::Type('Test queue installed; awaiting vendor confirmation.')}
   'notes-library' {Click $notes 1500;$library=Own 1512;Stage $library 135 100 1260 800;Fill $library 1510 'rollout';[VideoUi]::Combo([VideoUi]::GetDlgItem($library,1535),1);Start-Sleep -Seconds 2;Click $library 2;Click $notes 1551;$trash=Own 1512;Stage $trash 135 100 1260 800;[VideoUi]::Relative([VideoUi]::GetDlgItem($trash,1512),70,12);Start-Sleep -Seconds 1;Click $trash 1517;Start-Sleep -Seconds 1;Click $trash 2}
   'notes-actions' {Click $notes 1501;$actions=Own 1510;Fill $actions 1510 'handover';Start-Sleep -Seconds 2;[VideoUi]::Key(13);$save=Own 1 '#32770';[VideoUi]::Key(18,78);[VideoUi]::Type((Join-Path $run 'Demo-Handover.html'));[VideoUi]::Key(13);Wait{-not[VideoUi]::IsWindow($save)} 'Save dialog did not complete'|Out-Null;$message=Wait{[VideoUi]::Named([uint32]$process.Id,'Handover exported')} 'Export confirmation missing';$ok=Wait{[VideoUi]::Button($message,'OK')} 'Export OK button not found';[VideoUi]::Click($ok);Wait{-not[VideoUi]::IsWindow($message)-and[VideoUi]::IsWindowEnabled($notes)} 'Export confirmation did not close'|Out-Null;Wait{Test-Path (Join-Path $run 'Demo-Handover.html')} 'Handover was not exported'|Out-Null;Click $notes 45042;$help=Own 1552;Start-Sleep -Seconds 3;Click $help 1}
  }
  if($Smoke){Start-Sleep -Seconds 2;break};if(-not$FastReview){Finish-Chapter $chapter}
 }
 [IO.File]::WriteAllText($stop,'stop',$utf8);[void]$recorder.WaitForExit(30000);if(-not$recorder.HasExited-or$recorder.ExitCode-ne0){throw 'Native recording failed'}
 $result=@{video=$Video;raw=$raw;run=$run;exeSha256=(Get-FileHash $app).Hash;chapters=$events;duration=$clock.Elapsed.TotalSeconds;completed=$true;source='real application clicks and typing';capture='explicitly allowed disposable process windows only'}
 $result|ConvertTo-Json -Depth 6|Set-Content (Join-Path $run 'interaction-receipt.json');if(-not$Smoke-and-not$FastReview){$result|ConvertTo-Json -Depth 6|Set-Content (Join-Path $PSScriptRoot ($Video+'-recording.json'))}
} catch {if($process){Write-Output ([VideoUi]::Windows([uint32]$process.Id))};throw} finally {
 if($recorder-and-not$recorder.HasExited){[IO.File]::WriteAllText((Join-Path $run 'stop'),'stop',$utf8);[void]$recorder.WaitForExit(30000)}
 if($notes-ne[IntPtr]::Zero-and[VideoUi]::IsWindow($notes)){[void][VideoUi]::PostMessageW($notes,0x10,[UIntPtr]::Zero,[IntPtr]::Zero)}
 foreach($browserName in @('msedge.exe','chrome.exe')) {foreach($owned in @(Get-QaProcess -Filter ("Name='"+$browserName+"'")|Where-Object{$_.CommandLine-and$_.CommandLine.Contains($data)})) {$window=[VideoUi]::Find([uint32]$owned.ProcessId,'Chrome_WidgetWin_1',0);if($window-ne[IntPtr]::Zero){[void][VideoUi]::PostMessageW($window,0x10,[UIntPtr]::Zero,[IntPtr]::Zero)}}}
 if($main-ne[IntPtr]::Zero){[void][VideoUi]::PostMessageW($main,0x10,[UIntPtr]::Zero,[IntPtr]::Zero)}
 if($process){[void]$process.WaitForExit(8000);if(-not$process.HasExited){$process.Kill()}}
}
