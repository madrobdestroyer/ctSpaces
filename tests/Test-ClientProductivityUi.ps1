#requires -Version 7.0

param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\client-productivity-ui'),
    [switch]$NotesOnly
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
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
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
  [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr w);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr w);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr w);
  [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr w);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w,out Rect r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr w);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int width,int height,uint flags);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p,IntPtr address,UIntPtr size,uint type,uint protect);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr p,IntPtr address,UIntPtr size,uint type);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p,IntPtr address,byte[] bytes,UIntPtr size,out UIntPtr count);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p,IntPtr address,byte[] bytes,UIntPtr size,out UIntPtr count);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr p);
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
  public static string ControlText(IntPtr w){UIntPtr length;SendMessageTimeoutW(w,0xE,UIntPtr.Zero,IntPtr.Zero,3,3000,out length);var b=new StringBuilder(checked((int)Math.Min(length.ToUInt64()+1,32ul*1024*1024+1)));ReadText(w,0xD,(IntPtr)b.Capacity,b);return b.ToString();}
  public static bool SetWindowTextW(IntPtr w,string text){UIntPtr result;return SendMessageTimeoutTextW(w,0xC,UIntPtr.Zero,text,3,3000,out result);}
  public static IntPtr Find(uint pid,string cls,string title){IntPtr hit=IntPtr.Zero;EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner!=pid)return true;var b=new StringBuilder(256);GetClassNameW(w,b,b.Capacity);if(b.ToString()==cls&&(String.IsNullOrEmpty(title)||Text(w)==title)){hit=w;return false;}return true;},IntPtr.Zero);return hit;}
  public static IntPtr FindWithChild(uint pid,int childId){IntPtr hit=IntPtr.Zero;EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==pid&&GetDlgItem(w,childId)!=IntPtr.Zero){hit=w;return false;}return true;},IntPtr.Zero);return hit;}
  public static IntPtr[] Tooltips(uint pid){var found=new HashSet<IntPtr>();EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner!=pid)return true;var b=new StringBuilder(64);GetClassNameW(w,b,b.Capacity);if(String.Equals(b.ToString(),"tooltips_class32",StringComparison.OrdinalIgnoreCase))found.Add(w);EnumChildWindows(w,(child,unused)=>{uint childOwner;GetWindowThreadProcessId(child,out childOwner);if(childOwner==pid){var name=new StringBuilder(64);GetClassNameW(child,name,name.Capacity);if(String.Equals(name.ToString(),"tooltips_class32",StringComparison.OrdinalIgnoreCase))found.Add(child);}return true;},IntPtr.Zero);return true;},IntPtr.Zero);var result=new IntPtr[found.Count];found.CopyTo(result);return result;}
  public static string WindowTitles(uint pid){var titles=new StringBuilder();EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==pid){var title=Text(w);if(!String.IsNullOrEmpty(title)){if(titles.Length>0)titles.Append(" | ");titles.Append(title);}}return true;},IntPtr.Zero);return titles.ToString();}
  public static string ChildTexts(IntPtr parent){var texts=new StringBuilder();EnumChildWindows(parent,(w,p)=>{var title=Text(w);if(!String.IsNullOrEmpty(title)){if(texts.Length>0)texts.Append(" | ");texts.Append(title);}return true;},IntPtr.Zero);return texts.ToString();}
  public static string ChildIds(IntPtr parent){var text=new StringBuilder();EnumChildWindows(parent,(w,p)=>{text.Append(GetDlgCtrlID(w)+":"+Text(w)+" | ");return true;},IntPtr.Zero);return text.ToString();}
  public static long Query(IntPtr w,uint message,long parameter){UIntPtr result;if(!SendMessageTimeoutW(w,message,new UIntPtr(unchecked((ulong)parameter)),IntPtr.Zero,3,3000,out result))throw new Exception("UI query timed out: "+message);return unchecked((long)result.ToUInt64());}
  public static long HitTest(IntPtr w,int x,int y){UIntPtr result;if(!SendMessageTimeoutW(w,0x84,UIntPtr.Zero,new IntPtr((y<<16)|(x&0xffff)),3,3000,out result))throw new Exception("Frame hit test timed out");return unchecked((long)result.ToUInt64());}
  public static void Command(IntPtr w,uint id){UIntPtr result;if(!SendMessageTimeoutW(w,0x111,new UIntPtr(id),IntPtr.Zero,3,5000,out result))throw new Exception("UI command timed out: "+id);}
  public static void FocusControl(IntPtr parent,IntPtr child){UIntPtr result;if(!SendMessageTimeoutW(parent,0x28,new UIntPtr(unchecked((ulong)child.ToInt64())),new IntPtr(1),3,3000,out result))throw new Exception("Focus transfer timed out");}
  public static void NotifyComboSelection(IntPtr dialog,uint id,IntPtr combo){UIntPtr result;ulong command=id|(1ul<<16);if(!SendMessageTimeoutW(dialog,0x111,new UIntPtr(command),combo,3,5000,out result))throw new Exception("Combo selection notification timed out");}
  public static void NotifyComboAccepted(IntPtr dialog,uint id,IntPtr combo){UIntPtr result;ulong command=id|(9ul<<16);if(!SendMessageTimeoutW(dialog,0x111,new UIntPtr(command),combo,3,5000,out result))throw new Exception("Combo acceptance notification timed out");}
  public static void Key(IntPtr w,uint key){PostMessageW(w,0x100,new UIntPtr(key),IntPtr.Zero);PostMessageW(w,0x101,new UIntPtr(key),IntPtr.Zero);}
  public static void PasteLike(IntPtr w,string text){UIntPtr result;if(!SendMessageTimeoutTextW(w,0xC2,new UIntPtr(1),text,3,3000,out result))throw new Exception("Text insertion timed out");}
  public static long FindComboString(IntPtr w,string text){return SendMessageW(w,0x158,new IntPtr(-1),text).ToInt64();}
  public static byte[] CharacterFormat(IntPtr w){
    uint pid;GetWindowThreadProcessId(w,out pid);var p=OpenProcess(0x38,false,pid);
    if(p==IntPtr.Zero)throw new Exception("Cannot inspect QA editor format");
    IntPtr address=IntPtr.Zero;
    try{var bytes=new byte[116];Array.Copy(BitConverter.GetBytes(116),bytes,4);
      address=VirtualAllocEx(p,IntPtr.Zero,new UIntPtr(116),0x3000,4);UIntPtr count,result;
      if(address==IntPtr.Zero||!WriteProcessMemory(p,address,bytes,new UIntPtr(116),out count)||
         !SendMessageTimeoutW(w,0x43A,new UIntPtr(1),address,3,3000,out result)||
         !ReadProcessMemory(p,address,bytes,new UIntPtr(116),out count))throw new Exception("Cannot read QA character formatting");
      return bytes;
    }finally{if(address!=IntPtr.Zero)VirtualFreeEx(p,address,UIntPtr.Zero,0x8000);CloseHandle(p);}
  }
  public static void Select(IntPtr w,int first,int last){SendMessageW(w,0xB1,new IntPtr(first),new IntPtr(last));}
  static Task linkClick;
  public static void BeginLinkClick(IntPtr dialog,IntPtr edit,int length){
    uint pid;GetWindowThreadProcessId(edit,out pid);var process=OpenProcess(0x38,false,pid);
    // Click the first URL character so RichEdit generates its own EN_LINK.
    var point=new byte[8];var address=VirtualAllocEx(process,IntPtr.Zero,new UIntPtr(8),0x3000,4);
    IntPtr location;
    try{UIntPtr count,result;
      if(process==IntPtr.Zero||address==IntPtr.Zero||
         !SendMessageTimeoutW(edit,0x426,new UIntPtr((ulong)address.ToInt64()),IntPtr.Zero,3,3000,out result)||
         !ReadProcessMemory(process,address,point,new UIntPtr(8),out count))throw new Exception("Cannot locate QA URL text");
      var x=BitConverter.ToInt32(point,0)+4;var y=BitConverter.ToInt32(point,4)+8;
      location=new IntPtr((y<<16)|(x&0xffff));
    }finally{if(address!=IntPtr.Zero)VirtualFreeEx(process,address,UIntPtr.Zero,0x8000);CloseHandle(process);}
    linkClick=Task.Run(()=>{UIntPtr result;
      if(!SendMessageTimeoutW(edit,0x201,new UIntPtr(1),location,3,30000,out result)||
         !SendMessageTimeoutW(edit,0x202,UIntPtr.Zero,location,3,30000,out result))throw new Exception("URL click timed out");});
  }
  public static bool LinkClickFinished(){if(linkClick==null||!linkClick.IsCompleted)return false;if(linkClick.IsFaulted)throw linkClick.Exception;return true;}
  static System.Net.Sockets.TcpListener notesServer;
  static readonly System.Collections.Concurrent.ConcurrentDictionary<string,int> requests=new System.Collections.Concurrent.ConcurrentDictionary<string,int>();
  public static int StartNotesServer(){
    var server=new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback,0);server.Start();notesServer=server;
    Task.Run(()=>{try{while(true){var client=server.AcceptTcpClient();Task.Run(()=>{try{using(client)using(var stream=client.GetStream())using(var reader=new System.IO.StreamReader(stream,Encoding.ASCII,false,1024,true)){
      client.ReceiveTimeout=5000;var line=reader.ReadLine();if(String.IsNullOrEmpty(line))return;var path=line.Split(' ')[1];
      while(!String.IsNullOrEmpty(reader.ReadLine())){}requests.AddOrUpdate(path,1,(key,count)=>count+1);
      var body=Encoding.UTF8.GetBytes("<html><title>ctSpaces notes QA</title><body>Local notes link test</body></html>");
      var header=Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+body.Length+"\r\nConnection: close\r\n\r\n");
      stream.Write(header,0,header.Length);stream.Write(body,0,body.Length);
    }}catch(System.IO.IOException){}catch(System.Net.Sockets.SocketException){}});}}catch(System.Net.Sockets.SocketException){}});return ((System.Net.IPEndPoint)server.LocalEndpoint).Port;
  }
  public static bool SawNotesRequest(string path){return requests.ContainsKey(path);}
  public static void StopNotesServer(){if(notesServer!=null){notesServer.Stop();notesServer=null;}}
  public static bool CopyDataCommand(IntPtr w,string value){IntPtr text=Marshal.StringToHGlobalUni(value),data=IntPtr.Zero;try{var c=new CopyData{Tag=new UIntPtr(0x43545350u),Bytes=checked((uint)((value.Length+1)*2)),Text=text};data=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(CopyData)));Marshal.StructureToPtr(c,data,false);UIntPtr result;return SendMessageTimeoutW(w,0x4A,UIntPtr.Zero,data,3,5000,out result)&&result!=UIntPtr.Zero;}finally{if(data!=IntPtr.Zero)Marshal.FreeHGlobal(data);Marshal.FreeHGlobal(text);}}
  public static void ClosePidWindows(uint pid){EnumWindows((w,p)=>{uint owner;GetWindowThreadProcessId(w,out owner);if(owner==pid)PostMessageW(w,0x10,UIntPtr.Zero,IntPtr.Zero);return true;},IntPtr.Zero);}
}
'@
$oldDpiContext = [ProductivityQa]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))

function Assert($condition,[string]$message){if(-not$condition){throw $message}}
function Wait-Until([scriptblock]$check,[string]$failure,[int]$seconds=12){$end=[DateTime]::UtcNow.AddSeconds($seconds);do{$value=&$check;if($value-is[IntPtr]){if($value-ne[IntPtr]::Zero){return $value}}elseif($value){return $value};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end);throw $failure}
function Fingerprint([string]$path){if(Test-Path -LiteralPath $path -PathType Leaf){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}else{'<missing>'}}
function Assert-TooltipPalette([uint32]$qaProcessId,[string]$theme,[long]$background,[long]$foreground){$tips=@([ProductivityQa]::Tooltips($qaProcessId));Assert($tips.Count-ge 6) "$theme has too few owned tooltip windows: $($tips.Count).";Assert($background-ne$foreground) "$theme tooltip colors have no contrast.";foreach($tip in $tips){$actualBackground=[ProductivityQa]::Query($tip,0x416,0);$actualForeground=[ProductivityQa]::Query($tip,0x417,0);Assert($actualBackground-eq$background-and$actualForeground-eq$foreground) "$theme tooltip $tip has colors $actualBackground/$actualForeground, expected $background/$foreground."};$tips.Count}
function Save-Shot([IntPtr]$window,[string]$label){New-Item -ItemType Directory -Path $ArtifactDirectory -Force|Out-Null;$r=[ProductivityQa+Rect]::new();Assert([ProductivityQa]::GetWindowRect($window,[ref]$r)) "Cannot size $label screenshot.";$bmp=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top);$g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc();try{Assert([ProductivityQa]::PrintWindow($window,$dc,2)) "Cannot capture $label."}finally{$g.ReleaseHdc($dc);$g.Dispose()};$path=Join-Path $ArtifactDirectory ($run+'-'+$label+'.png');$bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png);$bmp.Dispose();$path}
function Save-VisibleShot([IntPtr]$window,[string]$label){New-Item -ItemType Directory -Path $ArtifactDirectory -Force|Out-Null;[void][ProductivityQa]::SetForegroundWindow($window);Start-Sleep -Milliseconds 250;$r=[ProductivityQa+Rect]::new();Assert([ProductivityQa]::GetWindowRect($window,[ref]$r)) "Cannot size $label screenshot.";$bmp=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top);$g=[Drawing.Graphics]::FromImage($bmp);try{$notesDc=$g.GetHdc();try{Assert([ProductivityQa]::PrintWindow($window,$notesDc,2)) 'Cannot render QA window.'}finally{$g.ReleaseHdc($notesDc)};$path=Join-Path $ArtifactDirectory ($run+'-'+$label+'.png');$bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png);$path}finally{$g.Dispose();$bmp.Dispose()}}
function Save-FilterShot([IntPtr]$main,[IntPtr]$combo){New-Item -ItemType Directory -Path $ArtifactDirectory -Force|Out-Null;Start-Sleep -Milliseconds 250;$r=[ProductivityQa+Rect]::new();Assert([ProductivityQa]::GetWindowRect($main,[ref]$r)) 'Cannot size filter screenshot.';$info=[ProductivityQa+ComboBoxInfo]::new();$info.Size=[Runtime.InteropServices.Marshal]::SizeOf($info);Assert([ProductivityQa]::GetComboBoxInfo($combo,[ref]$info)-and$info.List-ne[IntPtr]::Zero-and[ProductivityQa]::IsWindowVisible($info.List)) 'Filtered suggestion list is not visible for screenshot.';$listRect=[ProductivityQa+Rect]::new();Assert([ProductivityQa]::GetWindowRect($info.List,[ref]$listRect)) 'Cannot size suggestion list.';$r.Left=[Math]::Min($r.Left,$listRect.Left);$r.Top=[Math]::Min($r.Top,$listRect.Top);$r.Right=[Math]::Max($r.Right,$listRect.Right);$r.Bottom=[Math]::Max($r.Bottom,$listRect.Bottom);$bmp=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top);$g=[Drawing.Graphics]::FromImage($bmp);try{$g.CopyFromScreen($r.Left,$r.Top,0,0,$bmp.Size,[Drawing.CopyPixelOperation]::SourceCopy);$path=Join-Path $ArtifactDirectory ($run+'-filter-and-close-all.png');$bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png);$path}finally{$g.Dispose();$bmp.Dispose()}}
function Owned-Edge([string]$name){$profile=Join-Path $data ('Sites\'+$name+'\Browsers\edge\Profile');@(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" -ErrorAction SilentlyContinue|Where-Object{$_.CommandLine-and$_.CommandLine.Contains($profile)})}
function Wait-Notes {Wait-Until{[ProductivityQa]::FindWithChild([uint32]$process.Id,1317)} 'Client Notes dialog did not open.'}
function Open-Notes([string]$name){Assert([ProductivityQa]::SetWindowTextW([ProductivityQa]::GetDlgItem($main,206),$name)) 'Could not select existing client.';Assert([ProductivityQa]::PostMessageW($main,0x111,[UIntPtr]41020,[IntPtr]::Zero)) 'Could not request Client Notes.';$opened=Wait-Notes;Wait-Saved $opened;$opened}
function Wait-Saved([IntPtr]$notes){try{Wait-Until{[ProductivityQa]::ControlText([ProductivityQa]::GetDlgItem($notes,1333))-eq'Saved automatically'} 'Notes did not autosave.'|Out-Null}catch{throw ('Notes did not autosave at test line '+$MyInvocation.ScriptLineNumber+': '+[ProductivityQa]::ControlText([ProductivityQa]::GetDlgItem($notes,1333)))}}
function Switch-Note([IntPtr]$notes,[int]$index){
    $tabs=[ProductivityQa]::GetDlgItem($notes,1334)
    [ProductivityQa]::FocusControl($notes,$tabs)
    for($i=0;$i-lt 32-and[ProductivityQa]::Query($tabs,0x130B,0)-ne$index;$i++){
        $key=if([ProductivityQa]::Query($tabs,0x130B,0)-lt$index){0x27}else{0x25}
        [ProductivityQa]::Key($tabs,$key);Start-Sleep -Milliseconds 100
    }
    Assert([ProductivityQa]::Query($tabs,0x130B,0)-eq$index) 'Ticket tab did not switch.'
}
function Name-Note([IntPtr]$notes,[string]$name,[switch]$Rename){
    [void][ProductivityQa]::PostMessageW($notes,0x111,[UIntPtr]$(if($Rename){1336}else{1335}),[IntPtr]::Zero)
    $naming=Wait-Until{[ProductivityQa]::FindWithChild([uint32]$process.Id,1337)} 'Ticket name dialog did not open.'
    [void][ProductivityQa]::SetWindowTextW([ProductivityQa]::GetDlgItem($naming,1337),$name)
    [ProductivityQa]::Command($naming,1)
    Wait-Until{-not[ProductivityQa]::IsWindow($naming)} 'Ticket name did not apply.'|Out-Null
    Wait-Saved $notes
}
function Notebook-Pages([string]$path){
    $bytes=[IO.File]::ReadAllBytes($path);$reader=[IO.BinaryReader]::new([IO.MemoryStream]::new($bytes))
    try{
        Assert([Text.Encoding]::ASCII.GetString($reader.ReadBytes(8))-eq'CTNBOOK1') 'Notebook magic differs.'
        Assert($reader.ReadUInt32()-eq 1) 'Notebook version differs.'
        $count=$reader.ReadUInt32()
        for($i=0;$i-lt$count;$i++){
            $n=$reader.ReadUInt32();$r=$reader.ReadUInt32()
            [pscustomobject]@{Name=[Text.Encoding]::UTF8.GetString($reader.ReadBytes($n));Rtf=[Text.Encoding]::ASCII.GetString($reader.ReadBytes($r))}
        }
        Assert($reader.BaseStream.Position-eq$bytes.Length) 'Notebook has trailing bytes.'
    }finally{$reader.Dispose()}
}
function Assert-NotesSizeAfterRestart {
    $sized=Open-Notes $clientA
    $dpi=[ProductivityQa]::GetDpiForWindow($sized)
    [void][ProductivityQa]::SetWindowPos($sized,[IntPtr]::Zero,0,0,[int](1000*$dpi/96),[int](680*$dpi/96),0x16)
    [ProductivityQa]::Command($sized,2)
    Wait-Until{-not[ProductivityQa]::IsWindow($sized)} 'Size test did not close notes.'|Out-Null
    # Restart only the disposable launcher after all QA browser sessions have closed.
    $process.Kill();[void]$process.WaitForExit(5000);$process.Dispose()
    $script:process=Start-Process -FilePath $copy -ArgumentList ('--qa-instance='+$run+' --qa-data-dir="'+$data+'"') -PassThru
    [void]$process.WaitForInputIdle(10000)
    $script:main=Wait-Until{[ProductivityQa]::Find([uint32]$process.Id,'ctSpacesLauncherClass',$null)} 'QA launcher restart failed.'
    $sized=Open-Notes $clientA
    $bounds=[ProductivityQa+Rect]::new();[void][ProductivityQa]::GetWindowRect($sized,[ref]$bounds)
    $dpi=[ProductivityQa]::GetDpiForWindow($sized)
    Assert([Math]::Abs(($bounds.Right-$bounds.Left)*96/$dpi-1000)-le 2) 'Width did not survive restarting ctSpaces.'
    Assert([Math]::Abs(($bounds.Bottom-$bounds.Top)*96/$dpi-680)-le 2) 'Height did not survive restarting ctSpaces.'
    [void][ProductivityQa]::PostMessageW($sized,0x112,[UIntPtr]0xF030,[IntPtr]::Zero)
    Wait-Until{[ProductivityQa]::IsZoomed($sized)} 'Notes could not maximize.'|Out-Null
    [ProductivityQa]::Command($sized,2);Wait-Until{-not[ProductivityQa]::IsWindow($sized)} 'Maximized notes did not close.'|Out-Null
    $sized=Open-Notes $clientA
    Assert([ProductivityQa]::IsZoomed($sized)) 'Maximized preference was not restored.'
    [void][ProductivityQa]::PostMessageW($sized,0x112,[UIntPtr]0xF120,[IntPtr]::Zero)
    Wait-Until{-not[ProductivityQa]::IsZoomed($sized)} 'Notes could not restore from maximized.'|Out-Null
    [ProductivityQa]::Command($sized,2);Wait-Until{-not[ProductivityQa]::IsWindow($sized)} 'Restored notes did not close.'|Out-Null
}
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
    $marineTooltipCount=Assert-TooltipPalette ([uint32]$process.Id) 'Marine' 0xD8E0C8 0
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
    $legacyFile=Join-Path $data ('Sites\'+$clientA+'\ctSpaces-client-notes.txt')
    [IO.File]::WriteAllText($legacyFile,'Existing legacy reminder',$utf8)
    $legacyHash=Fingerprint $legacyFile
    $notes=Open-Notes $clientA
    $notesEdit=[ProductivityQa]::GetDlgItem($notes,1317);Assert($notesEdit-ne[IntPtr]::Zero) 'Notes editor is missing.'
    $firstBounds=[ProductivityQa+Rect]::new();[void][ProductivityQa]::GetWindowRect($notes,[ref]$firstBounds)
    $firstDpi=[ProductivityQa]::GetDpiForWindow($notes)
    Assert([Math]::Abs(($firstBounds.Right-$firstBounds.Left)*96/$firstDpi-960)-le 2) "Notes compact width differed: $($firstBounds.Right-$firstBounds.Left) at DPI $firstDpi."
    Assert([Math]::Abs(($firstBounds.Bottom-$firstBounds.Top)*96/$firstDpi-640)-le 2) "Notes compact height differed: $($firstBounds.Bottom-$firstBounds.Top) at DPI $firstDpi."
    Assert([ProductivityQa]::ControlText($notesEdit)-eq'Existing legacy reminder') 'Legacy note did not open in Notes.'
    Assert([ProductivityQa]::GetDlgItem($notes,1)-eq[IntPtr]::Zero) 'Manual Save button remains.'
    Assert([ProductivityQa]::ControlText([ProductivityQa]::GetDlgItem($notes,2))-eq'Close notes') 'Caption close control differs.'
    $closeBounds=[ProductivityQa+Rect]::new();[void][ProductivityQa]::GetWindowRect([ProductivityQa]::GetDlgItem($notes,2),[ref]$closeBounds)
    Assert(($closeBounds.Top-$firstBounds.Top)*96/$firstDpi-lt 10) 'Close control is not in the caption.'
    Assert([ProductivityQa]::HitTest($notes,($firstBounds.Left+50),($firstBounds.Top+20))-eq 2) 'Custom caption cannot drag the window.'
    Assert([ProductivityQa]::HitTest($notes,($firstBounds.Left+2),($firstBounds.Top+2))-eq 13) 'Custom frame cannot resize from its corner.'
    $noteFile=Join-Path $data ('Sites\'+$clientA+'\ctSpaces-client-notes.ctn')
    Assert(-not(Test-Path $noteFile)) 'Opening notes created a notebook before an edit.'
    $marine=Save-VisibleShot $notes 'notes-marine'
    [void][ProductivityQa]::SetWindowPos($notes,[IntPtr]::Zero,0,0,[int](1000*$firstDpi/96),[int](700*$firstDpi/96),0x16)
    $saved='A local note for '+$clientA
    [ProductivityQa]::Select($notesEdit,0,-1)
    [ProductivityQa]::PasteLike($notesEdit,$saved)
    [ProductivityQa]::Command($notes,2)
    Wait-Until{-not[ProductivityQa]::IsWindow($notes)} 'Close did not flush autosave.'|Out-Null
    $notes=Open-Notes $clientA;Assert([ProductivityQa]::ControlText([ProductivityQa]::GetDlgItem($notes,1317))-eq$saved) 'Saved notes did not reopen.'
    $reopenedBounds=[ProductivityQa+Rect]::new();[void][ProductivityQa]::GetWindowRect($notes,[ref]$reopenedBounds)
    $reopenedDpi=[ProductivityQa]::GetDpiForWindow($notes)
    Assert([Math]::Abs(($reopenedBounds.Right-$reopenedBounds.Left)*96/$reopenedDpi-1000)-le 2) 'Preferred width did not reopen.'
    Assert([Math]::Abs(($reopenedBounds.Bottom-$reopenedBounds.Top)*96/$reopenedDpi-700)-le 2) 'Preferred height did not reopen.'
    $qaConfig=[IO.File]::ReadAllText((Join-Path $data 'config.ini'))
    Assert($qaConfig.Contains('[notes_window]')-and$qaConfig.Contains('width=1000')-and$qaConfig.Contains('height=700')) 'Preferred size was not persisted to disk.'
    Assert((Notebook-Pages $noteFile).Name-eq'Notes') 'Initial tab is not named Notes.'
    Assert((Fingerprint $legacyFile)-eq$legacyHash) 'Migration changed the legacy TXT.'
    Name-Note $notes 'INC-12345'
    $notesEdit=[ProductivityQa]::GetDlgItem($notes,1317)
    Assert([ProductivityQa]::ControlText($notesEdit)-eq'') 'New ticket inherited another note.'
    $ticketText='Ticket investigation'
    [ProductivityQa]::PasteLike($notesEdit,$ticketText);Wait-Saved $notes
    [ProductivityQa]::Select($notesEdit,0,$ticketText.Length)
    [ProductivityQa]::Command($notes,1322);Wait-Saved $notes
    Assert(([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat($notesEdit),8)-band 1)-eq 1) 'Bold was not applied.'
    [ProductivityQa]::Command($notes,1325);Wait-Saved $notes
    [ProductivityQa]::Command($notes,1326);Wait-Saved $notes
    $ticketName='INC-12345 '+[char]0x00e9
    Name-Note $notes $ticketName -Rename
    Name-Note $notes 'Follow up'
    [ProductivityQa]::PasteLike($notesEdit,'Second ticket');Wait-Saved $notes
    [ProductivityQa]::Select($notesEdit,0,-1)
    [ProductivityQa]::Command($notes,1323);[ProductivityQa]::Command($notes,1324);Wait-Saved $notes
    Assert(([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat($notesEdit),8)-band 6)-eq 6) 'Italic and underline did not apply.'
    $style=[ProductivityQa]::GetDlgItem($notes,1320)
    [void][ProductivityQa]::Query($style,0x14E,1);[ProductivityQa]::NotifyComboSelection($notes,1320,$style);Wait-Saved $notes
    Assert([BitConverter]::ToInt32([ProductivityQa]::CharacterFormat($notesEdit),12)-eq 560) 'Heading font size differs.'
    $size=[ProductivityQa]::GetDlgItem($notes,1321)
    [void][ProductivityQa]::Query($size,0x14E,5);[ProductivityQa]::NotifyComboSelection($notes,1321,$size);Wait-Saved $notes
    Assert([BitConverter]::ToInt32([ProductivityQa]::CharacterFormat($notesEdit),12)-eq 360) 'Font size did not apply.'
    [ProductivityQa]::Command($notes,1327);Wait-Saved $notes
    [ProductivityQa]::Command($notes,1332);Wait-Saved $notes
    Assert(([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat($notesEdit),8)-band 7)-eq 0) 'Clear formatting retained bold/italic/underline.'
    [ProductivityQa]::Command($notes,1325);Wait-Saved $notes
    Assert([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat($notesEdit),20)-eq 0x181818) 'Yellow highlight lacks dark foreground.'
    Switch-Note $notes 0;Switch-Note $notes 2
    [ProductivityQa]::Select($notesEdit,0,-1)
    Assert([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat($notesEdit),20)-eq 0x181818) 'Highlight foreground was lost while reopening the tab.'
    [ProductivityQa]::Command($notes,1332);Wait-Saved $notes
    [ProductivityQa]::Select($notesEdit,0,0);[ProductivityQa]::Command($notes,1328);Wait-Saved $notes
    $checkText=[ProductivityQa]::ControlText($notesEdit)
    Assert($checkText.StartsWith([string][char]0x2610)) 'Checklist marker was not inserted.'
    [ProductivityQa]::Command($notes,1330);Wait-Saved $notes
    Assert(-not([ProductivityQa]::ControlText($notesEdit)).StartsWith([string][char]0x2610)) 'Undo did not reverse the checklist.'
    [ProductivityQa]::Command($notes,1331);Wait-Saved $notes
    Assert([ProductivityQa]::ControlText($notesEdit)-eq$checkText) 'Redo did not restore the checklist.'
    [ProductivityQa]::Select($notesEdit,0,-1);[ProductivityQa]::PasteLike($notesEdit,'https://example.com/ticket');Wait-Saved $notes
    [ProductivityQa]::Select($notesEdit,0,-1);[ProductivityQa]::Command($notes,1329);Wait-Saved $notes
    [ProductivityQa]::Select($notesEdit,0,'https://example.com/ticket'.Length)
    Assert(([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat($notesEdit),8)-band 0x20)-eq 0x20) 'Link formatting was not applied to the URL.'
    Switch-Note $notes 0
    Assert([ProductivityQa]::ControlText($notesEdit)-eq$saved) 'Notes changed while switching tickets.'
    Switch-Note $notes 1
    Assert([ProductivityQa]::ControlText($notesEdit)-eq$ticketText) 'Ticket draft was lost while switching.'
    [ProductivityQa]::Select($notesEdit,0,$ticketText.Length)
    Assert(([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat($notesEdit),8)-band 1)-eq 1) 'Ticket formatting was lost while switching.'
    [void][ProductivityQa]::PostMessageW($notes,0x111,[UIntPtr]1335,[IntPtr]::Zero)
    $naming=Wait-Until{[ProductivityQa]::FindWithChild([uint32]$process.Id,1337)} 'Name dialog did not open for duplicate test.'
    [void][ProductivityQa]::SetWindowTextW([ProductivityQa]::GetDlgItem($naming,1337),'notes')
    [void][ProductivityQa]::PostMessageW($naming,0x111,[UIntPtr]1,[IntPtr]::Zero)
    $prompt=Wait-Until{[ProductivityQa]::Find([uint32]$process.Id,'#32770','Invalid tab name')} 'Duplicate name was not rejected.'
    $ok=Wait-Until{[ProductivityQa]::GetDlgItem($prompt,2)} ('Duplicate-name message has no OK button: '+[ProductivityQa]::ChildIds($prompt))
    Assert([ProductivityQa]::ControlText($ok)-eq'OK') 'Validation message button differs.'
    [void][ProductivityQa]::PostMessageW($ok,0xF5,[UIntPtr]::Zero,[IntPtr]::Zero)
    Wait-Until{-not[ProductivityQa]::IsWindow($prompt)} ('Duplicate-name warning did not close: '+[ProductivityQa]::ChildTexts($prompt))|Out-Null
    Wait-Until{[ProductivityQa]::IsWindowEnabled($naming)} 'Naming dialog remained disabled after warning.'|Out-Null
    [void][ProductivityQa]::PostMessageW($naming,0x10,[UIntPtr]::Zero,[IntPtr]::Zero)
    Wait-Until{-not[ProductivityQa]::IsWindow($naming)} ('Name Cancel did not close: '+[ProductivityQa]::ChildTexts($naming))|Out-Null
    Assert(@(Notebook-Pages $noteFile).Count-eq 3) 'Duplicate tab changed the notebook.'
    $beforeLock=Fingerprint $noteFile
    $held=[IO.File]::Open($noteFile,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
        [ProductivityQa]::Select($notesEdit,$ticketText.Length,$ticketText.Length)
        [ProductivityQa]::PasteLike($notesEdit,' retained draft')
        Wait-Until{[ProductivityQa]::ControlText([ProductivityQa]::GetDlgItem($notes,1333)).StartsWith('Could not save')} 'Locked file did not report autosave failure.'|Out-Null
        Assert((Fingerprint $noteFile)-eq$beforeLock) 'Failed autosave replaced locked notes.'
        Assert([ProductivityQa]::ControlText($notesEdit).Contains('retained draft')) 'Failed autosave lost the draft.'
    }finally{$held.Dispose()}
    [ProductivityQa]::PasteLike($notesEdit,'!');Wait-Saved $notes
    Assert((Fingerprint $noteFile)-ne$beforeLock) 'Autosave did not recover after file unlock.'
    [ProductivityQa]::Command($notes,2);Wait-Until{-not[ProductivityQa]::IsWindow($notes)} 'Clean Cancel did not close notes.'|Out-Null
    $notes=Open-Notes $clientA;$notesEdit=[ProductivityQa]::GetDlgItem($notes,1317)
    Switch-Note $notes 1
    Assert([ProductivityQa]::ControlText($notesEdit)-eq($ticketText+' retained draft!')) 'Ticket text did not persist on reopen.'
    [ProductivityQa]::Select($notesEdit,0,$ticketText.Length)
    Assert(([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat($notesEdit),8)-band 1)-eq 1) 'Formatting did not persist on reopen.'
    Name-Note $notes 'Long document'
    $longText=('Large document log line. '+[char]0x00e9+"`r`n")*50000
    [ProductivityQa]::PasteLike($notesEdit,$longText);Wait-Saved $notes
    Assert(@(Notebook-Pages $noteFile)[3].Rtf.Length-gt(1024*1024)) 'Large document did not exceed the former RTF cap.'
    Switch-Note $notes 0;Switch-Note $notes 3
    Assert(([ProductivityQa]::ControlText($notesEdit)).Replace("`r`n","`n").Replace("`r","`n")-eq$longText.Replace("`r`n","`n")) 'Large document changed while switching tabs.'
    [ProductivityQa]::Command($notes,2);Wait-Until{-not[ProductivityQa]::IsWindow($notes)} 'Reopened notes did not close.'|Out-Null
    [void][ProductivityQa]::PostMessageW($main,0x111,[UIntPtr]41006,[IntPtr]::Zero)
    $theme=Wait-Until{[ProductivityQa]::FindWithChild([uint32]$process.Id,5201)} 'Theme dialog did not open.'
    $themeCombo=[ProductivityQa]::GetDlgItem($theme,5201)
    $gothic=[ProductivityQa]::FindComboString($themeCombo,'Dark - Gothic')
    Assert($gothic-ge 0) 'Dark theme was not available.'
    [void][ProductivityQa]::Query($themeCombo,0x14E,$gothic)
    [ProductivityQa]::NotifyComboSelection($theme,5201,$themeCombo)
    [ProductivityQa]::Command($theme,5202)
    Wait-Until{-not[ProductivityQa]::IsWindow($theme)} 'Theme dialog did not apply.'|Out-Null
    $gothicTooltipCount=Assert-TooltipPalette ([uint32]$process.Id) 'Dark Gothic' 0x272326 0xE5ECF2
    $notes=Open-Notes $clientA
    Switch-Note $notes 1
    $dpiObserved=[Collections.Generic.HashSet[uint32]]::new()
    $originalBounds=[ProductivityQa+Rect]::new();[void][ProductivityQa]::GetWindowRect($notes,[ref]$originalBounds)
    $beforeEditor=[ProductivityQa+Rect]::new();[void][ProductivityQa]::GetWindowRect([ProductivityQa]::GetDlgItem($notes,1317),[ref]$beforeEditor)
    $windowWidth=$originalBounds.Right-$originalBounds.Left;$windowHeight=$originalBounds.Bottom-$originalBounds.Top
    [void][ProductivityQa]::SetWindowPos($notes,[IntPtr]::Zero,0,0,$windowWidth+140,$windowHeight+90,0x16)
    $afterEditor=[ProductivityQa+Rect]::new();[void][ProductivityQa]::GetWindowRect([ProductivityQa]::GetDlgItem($notes,1317),[ref]$afterEditor)
    Assert(($afterEditor.Right-$afterEditor.Left)-eq($beforeEditor.Right-$beforeEditor.Left+140)) 'Resizing did not grow the editor width.'
    Assert(($afterEditor.Bottom-$afterEditor.Top)-eq($beforeEditor.Bottom-$beforeEditor.Top+90)) 'Resizing did not grow the editor height.'
    $notesDpi=[ProductivityQa]::GetDpiForWindow($notes)
    [void][ProductivityQa]::SetWindowPos($notes,[IntPtr]::Zero,0,0,[int](900*$notesDpi/96),[int](540*$notesDpi/96),0x16)
    $small=[ProductivityQa+Rect]::new();[void][ProductivityQa]::GetWindowRect($notes,[ref]$small)
    $lastTool=[ProductivityQa+Rect]::new();[void][ProductivityQa]::GetWindowRect([ProductivityQa]::GetDlgItem($notes,1332),[ref]$lastTool)
    Assert($lastTool.Right-lt$small.Right) 'Toolbar clipped at the minimum window width.'
    [void][ProductivityQa]::SetWindowPos($notes,[IntPtr]::Zero,0,0,$windowWidth,$windowHeight,0x16)
    foreach($screen in [Windows.Forms.Screen]::AllScreens){
        [void][ProductivityQa]::SetWindowPos($notes,[IntPtr]::Zero,$screen.WorkingArea.Left+20,$screen.WorkingArea.Top+20,0,0,0x15)
        Start-Sleep -Milliseconds 350
        [void]$dpiObserved.Add([ProductivityQa]::GetDpiForWindow($notes))
        [ProductivityQa]::Select([ProductivityQa]::GetDlgItem($notes,1317),0,$ticketText.Length)
        Assert(([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat([ProductivityQa]::GetDlgItem($notes,1317)),8)-band 1)-eq 1) 'DPI change altered formatting.'
        Assert([ProductivityQa]::ControlText([ProductivityQa]::GetDlgItem($notes,1333))-eq'Saved automatically') 'DPI move marked the note dirty.'
    }
    Switch-Note $notes 2;Name-Note $notes 'INC-1245' -Rename
    $previewEdit=[ProductivityQa]::GetDlgItem($notes,1317)
    $preview="Client reminders`r`n`r`nKeep useful details and tasks together for this client.`r`n`r`nMonthly tasks`r`n"+[char]0x2610+" Review account access`r`n"+[char]0x2611+" Update the contact list`r`n"+[char]0x2611+" Confirm the next maintenance window`r`n`r`nImportant`r`nBefore making changes: confirm the maintenance window with the client.`r`n`r`nUseful links`r`nhttps://support.example.com/client`r`n`r`nLast discussed with the client on October 9."
    $preview=$preview.Replace("`r`n`r`n","`r`n").Replace("`r`nLast discussed","`r`n`r`nLast discussed")
    [ProductivityQa]::Select($previewEdit,0,-1);[ProductivityQa]::PasteLike($previewEdit,$preview)
    [ProductivityQa]::Select($previewEdit,0,-1);[ProductivityQa]::Command($notes,1332)
    [ProductivityQa]::Select($previewEdit,0,'Client reminders'.Length)
    [void][ProductivityQa]::Query([ProductivityQa]::GetDlgItem($notes,1320),0x14E,1)
    [ProductivityQa]::NotifyComboSelection($notes,1320,[ProductivityQa]::GetDlgItem($notes,1320));Wait-Saved $notes
    foreach($label in @('Monthly tasks','Important','Useful links')){
        $at=[ProductivityQa]::ControlText($previewEdit).Replace("`r`n","`r").IndexOf($label)
        [ProductivityQa]::Select($previewEdit,$at,($at+$label.Length))
        [void][ProductivityQa]::Query([ProductivityQa]::GetDlgItem($notes,1320),0x14E,2)
        [ProductivityQa]::NotifyComboSelection($notes,1320,[ProductivityQa]::GetDlgItem($notes,1320))
    }
    foreach($label in @('Review account access','Update the contact list','Confirm the next maintenance window')){
        $at=[ProductivityQa]::ControlText($previewEdit).Replace("`r`n","`r").IndexOf($label)
        [ProductivityQa]::Select($previewEdit,$at,$at);[ProductivityQa]::Command($notes,1328)
    }
    $at=[ProductivityQa]::ControlText($previewEdit).Replace("`r`n","`r").IndexOf('Review account access')
    [ProductivityQa]::Select($previewEdit,$at,($at+21))
    Assert(([BitConverter]::ToUInt32([ProductivityQa]::CharacterFormat($previewEdit),8)-band 8)-eq 8) 'Completed checklist text is not struck through.'
    foreach($pair in @(@('Before making changes:',1322),@('confirm the maintenance window with the client.',1325),@('Last discussed with the client on October 9.',1323))){
        $at=[ProductivityQa]::ControlText($previewEdit).Replace("`r`n","`r").IndexOf($pair[0])
        [ProductivityQa]::Select($previewEdit,$at,($at+$pair[0].Length));[ProductivityQa]::Command($notes,$pair[1])
    }
    Wait-Saved $notes
    foreach($screen in [Windows.Forms.Screen]::AllScreens){
        [void][ProductivityQa]::SetWindowPos($notes,[IntPtr]::Zero,$screen.WorkingArea.Left+20,$screen.WorkingArea.Top+20,0,0,0x15)
        Start-Sleep -Milliseconds 350
        if([ProductivityQa]::GetDpiForWindow($notes)-eq 144){break}
    }
    $previewDpi=[ProductivityQa]::GetDpiForWindow($notes)
    [void][ProductivityQa]::SetWindowPos($notes,[IntPtr]::Zero,0,0,[int](950*$previewDpi/96),[int](735*$previewDpi/96),0x16)
    $at=[ProductivityQa]::ControlText($previewEdit).Replace("`r`n","`r").IndexOf('https://support.example.com/client')
    [ProductivityQa]::Select($previewEdit,$at,($at+'https://support.example.com/client'.Length));[ProductivityQa]::Command($notes,1329);Wait-Saved $notes
    $at=[ProductivityQa]::ControlText($previewEdit).Replace("`r`n","`r").IndexOf('Keep useful details')
    [ProductivityQa]::Select($previewEdit,$at,$at);[ProductivityQa]::FocusControl($notes,[ProductivityQa]::GetDlgItem($notes,1334))
    [void][ProductivityQa]::Query($notes,0x127,0x10001)
    [void][ProductivityQa]::Query($previewEdit,0x115,6)
    $gothicShot=Save-VisibleShot $notes 'notes-gothic'
    Assert((Fingerprint $marine)-ne(Fingerprint $gothicShot)) 'Notes did not repaint for the dark theme.'
    [ProductivityQa]::Command($notes,2);Wait-Until{-not[ProductivityQa]::IsWindow($notes)} 'Dark themed notes did not close.'|Out-Null
    Wait-Until{[ProductivityQa]::IsWindowEnabled($edit)} 'Launcher did not reenable after Client Notes.'|Out-Null
    $noteHash=Fingerprint $noteFile
    $notes=Open-Notes $clientA;Switch-Note $notes 2
    $notesEdit=[ProductivityQa]::GetDlgItem($notes,1317)
    $notesPort=[ProductivityQa]::StartNotesServer()
    foreach($linkPath in @("/notes-first-$run","/notes-second-$run")){
        $linkUrl='http://127.0.0.1:'+$notesPort+$linkPath
        [ProductivityQa]::Select($notesEdit,0,-1);[ProductivityQa]::PasteLike($notesEdit,$linkUrl);Wait-Saved $notes
        [ProductivityQa]::BeginLinkClick($notes,$notesEdit,$linkUrl.Length)
        $prompt=Wait-Until{[ProductivityQa]::Find([uint32]$process.Id,'#32770','Open Link')} 'Note link did not ask to open.'
        Assert([ProductivityQa]::ChildTexts($prompt).Contains($clientA)) 'Link prompt does not identify the note client.'
        $yes=[ProductivityQa]::GetDlgItem($prompt,6)
        Assert($yes-ne[IntPtr]::Zero) ('Open Link Yes button missing: '+[ProductivityQa]::ChildTexts($prompt))
        [void][ProductivityQa]::PostMessageW($yes,0xF5,[UIntPtr]::Zero,[IntPtr]::Zero)
        Wait-Until{[ProductivityQa]::LinkClickFinished()} 'Note link handler did not finish.'|Out-Null
        try{Wait-Until{[ProductivityQa]::SawNotesRequest($linkPath)} 'The client browser did not visit the note URL.' 25|Out-Null}
        catch{throw ('The client browser did not visit '+$linkUrl+'; windows='+[ProductivityQa]::WindowTitles([uint32]$process.Id)+'; browser='+(@(Owned-Edge $clientA|Where-Object{-not$_.CommandLine.Contains('--type=')}).CommandLine -join ' | '))}
        $edgeOwner=@(Owned-Edge $clientA|Where-Object{-not$_.CommandLine.Contains('--type=')})
        Assert($edgeOwner.Count-eq 1) 'Note URL did not use exactly one client browser process.'
        if($linkPath.StartsWith('/notes-first')){$notesBrowserPid=$edgeOwner[0].ProcessId}else{Assert($edgeOwner[0].ProcessId-eq$notesBrowserPid) 'Second note link opened a different browser instance.'}
        Assert([ProductivityQa]::IsWindow($notes)) 'Opening the note link closed the editor.'
        Assert(@(Owned-Edge $clientB).Count-eq 0) 'Note link opened another client.'
    }
    [ProductivityQa]::ClosePidWindows([uint32]$notesBrowserPid)
    [ProductivityQa]::Command($notes,2);Wait-Until{-not[ProductivityQa]::IsWindow($notes)} 'Notes did not close after link launch.'|Out-Null
    Wait-Until{@(Owned-Edge $clientA).Count-eq 0} 'Note-link QA browser did not close.' 25|Out-Null
    [ProductivityQa]::StopNotesServer()
    Wait-Until{[ProductivityQa]::IsWindowEnabled($edit)} 'Launcher did not recover after note link launch.'|Out-Null
    $noteHash=Fingerprint $noteFile
    if($NotesOnly){
        Assert-NotesSizeAfterRestart
        Assert((Fingerprint $liveConfig)-eq$liveBefore) 'Notes test changed live configuration.'
        $completed=$true
        [pscustomobject]@{NotesAutosave=$true;TicketTabs=$true;ToolbarFormatting=$true;UndoRedo=$true;LargeDocument=$true;FailedSaveRetainsDraft=$true;ResizableEditor=$true;MinimumWidthFitsToolbar=$true;NoteLinksUseClientBrowser=$true;RepeatedLinksUseSameBrowser=$true;ObservedDpis=@($dpiObserved);MarineCapture=$marine;GothicCapture=$gothicShot}|ConvertTo-Json
        return
    }
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
    Wait-Until{[ProductivityQa]::IsWindowEnabled($edit)} 'Launcher did not recover after closing the single Edge session.' 20|Out-Null
    foreach($name in @($clientA,$clientB)){Assert([ProductivityQa]::CopyDataCommand($main,('--client "'+$name+'" --browser edge'))) "Could not launch isolated Edge for $name.";$owned+=Wait-Until{@(Owned-Edge $name)} "Isolated Edge did not launch for $name." 25;Wait-Until{[ProductivityQa]::IsWindowEnabled($edit)} "Launcher did not finish tracking $name." 20|Out-Null}
    Confirm-CloseAll
    foreach($name in @($clientA,$clientB)){Wait-Until{@(Owned-Edge $name).Count-eq 0} "Close all left $name running." 25|Out-Null}
    $process.Refresh();Assert(-not$process.HasExited) 'Close all exited the launcher after browser sessions.'
    Assert((Fingerprint $noteFile)-eq$noteHash) 'Closing browsers changed Client Notes.'
    Assert((Fingerprint $liveConfig)-eq$liveBefore) 'Live ctSpaces configuration changed.'
    Assert-NotesSizeAfterRestart
    $completed=$true
    [pscustomobject]@{FilterMatches=2;TypedTextPreserved=$true;SelectionAndClickAway=$true;NewNameCreatedWithoutOpeningExisting=$true;NotesAutosaved=$true;LegacyNotesPreserved=$true;NamedTicketTabs=$true;FormattingSurvivesReopen=$true;DuplicateNamesRejected=$true;FailedAutosaveRetainsDraft=$true;ObservedDpis=@($dpiObserved);CloseAllZeroOneAndMultiple=$true;CloseAllCancellationPreservesSession=$true;LauncherRetained=$true;MarineTooltipCount=$marineTooltipCount;GothicTooltipCount=$gothicTooltipCount;FilterAndLauncherCapture=$filterShot;MarineNotesCapture=$marine;GothicNotesCapture=$gothicShot;LiveConfigUntouched=$true}|ConvertTo-Json
} finally {
    [ProductivityQa]::StopNotesServer()
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
