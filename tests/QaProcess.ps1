# Read-only native process snapshots for fixture-bound browser tests. WMI is not
# required; callers still prove exact executable, command line and start time
# before sending a close request. Only explicit name/PID filters are accepted.
if (-not ('CtQaProcessSnapshot' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CtQaProcessSnapshot {
 [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr process);
 [DllImport("ntdll.dll")] static extern int NtQueryInformationProcess(IntPtr process,int kind,IntPtr buffer,int length,out int needed);
 public static string CommandLine(int pid) {
  IntPtr process=OpenProcess(0x1000,false,pid);if(process==IntPtr.Zero) return null;
  IntPtr buffer=Marshal.AllocHGlobal(131072);
  try {
   int needed;if(NtQueryInformationProcess(process,60,buffer,131072,out needed)<0) return null;
   int length=(ushort)Marshal.ReadInt16(buffer);IntPtr value=Marshal.ReadIntPtr(buffer,IntPtr.Size==8 ? 8 : 4);
   long offset=value.ToInt64()-buffer.ToInt64();
   if((length&1)!=0 || offset<0 || offset+length>131072) return null;
   return Marshal.PtrToStringUni(value,length/2);
  } finally {Marshal.FreeHGlobal(buffer);CloseHandle(process);}
 }
}
'@
}
function Get-QaProcess {
 [CmdletBinding()] param([ValidateSet('Win32_Process')][string]$ClassName,[Parameter(Mandatory)][string]$Filter)
 if ($Filter -match "^\s*Name\s*=\s*'([^']+)'\s*$") {
  $items=[Diagnostics.Process]::GetProcessesByName([IO.Path]::GetFileNameWithoutExtension($Matches[1]))
 } elseif ($Filter -match '^\s*ProcessId\s*=\s*(\d+)\s*$') {
  try {$items=@([Diagnostics.Process]::GetProcessById([int]$Matches[1]))} catch [ArgumentException] {return}
 } else {throw 'Only exact process name or PID filters are supported.'}
 foreach($item in $items) {
  try {
   $started=$item.StartTime;$path=$item.MainModule.FileName
   $command=[CtQaProcessSnapshot]::CommandLine($item.Id)
   if($command -and -not $item.HasExited) {
    [PSCustomObject]@{ProcessId=$item.Id;Name=[IO.Path]::GetFileName($path);ExecutablePath=$path;CommandLine=$command;CreationDate=$started}
   }
  } catch [InvalidOperationException] {} catch [ComponentModel.Win32Exception] {}
  finally {$item.Dispose()}
 }
}
