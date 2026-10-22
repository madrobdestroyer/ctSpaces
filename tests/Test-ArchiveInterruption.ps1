#requires -Version 7.0
param(
    [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\ArchiveInterruption')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
    throw 'Archive interruption evidence must remain below the workspace build directory.'
}
New-Item -ItemType Directory -Path $artifacts -Force|Out-Null
$output=Join-Path $artifacts 'bin';$intermediate=Join-Path $artifacts 'obj'
$hashes=@('BackupEngine.cpp','BackupEngine.h','InProc7z.cpp','InProc7z.h','tests\ArchiveInterruptionTests.cpp','tests\ArchiveInterruptionTests.vcxproj','version.h','dist\x64\Release\7zip.lib')|ForEach-Object{
    $path=Join-Path $root $_;[pscustomobject]@{Path=$path;Sha256=(Get-FileHash -LiteralPath $path).Hash}
}
$hashes|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
function Invoke-HiddenArchiveTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
    $info=[Diagnostics.ProcessStartInfo]::new($Exe)
    $info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    foreach($argument in $NativeArguments){$info.ArgumentList.Add($argument)}
    $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
    try{
        try{if(-not $process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}catch [InvalidOperationException]{if(-not $process.HasExited){throw}}
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        $process.WaitForExit();$contents=$stdout.Result+$stderr.Result
        [IO.File]::WriteAllText($Log,$contents)
        [pscustomobject]@{Exe=$Exe;BinarySha256=(Get-FileHash -LiteralPath $Exe).Hash;Arguments=$NativeArguments;ExitCode=$process.ExitCode;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath ($Log+'.json')
        if($process.ExitCode -ne 0){throw "Archive interruption tool failed; inspect $Log"}
        return $contents
    }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not (Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
$null=Invoke-HiddenArchiveTool $msbuild @((Join-Path $PSScriptRoot 'ArchiveInterruptionTests.vcxproj'),'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64',('/p:OutDir='+$output+'\'),('/p:IntDir='+$intermediate+'\'),'/v:minimal') (Join-Path $artifacts 'build.log')
foreach($item in $hashes){if((Get-FileHash -LiteralPath $item.Path).Hash -ne $item.Sha256){throw 'Archive source changed during native compilation.'}}
# Native fixture paths stay short enough to test production path budgets.
$fixture=Join-Path $root ('build\ai-'+[guid]::NewGuid().ToString('N').Substring(0,12))
$native=Invoke-HiddenArchiveTool (Join-Path $output 'ArchiveInterruptionTests.exe') @($fixture) (Join-Path $artifacts 'native.log')
if($native -notmatch 'Archive interruption checks=34 failures=0 seed=0x5A91D3'){
    throw 'The complete interruption, retry and malformed-stream scope was not confirmed.'
}
Write-Output $native
