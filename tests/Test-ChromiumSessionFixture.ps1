param()
$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'ChromiumSessionFixture.cs')
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixtureRoot = Join-Path $projectRoot ('build\session-parser-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
$script:checks = 0
function Assert-Session($value, [string]$message) {
    $script:checks++
    if (-not $value) { throw $message }
}
function New-SessionFixture([bool]$closeSecond = $false, [bool]$marker = $true, [int]$version = 3,
                           [bool]$selection = $true, [bool]$closeWindow = $false, [int]$pruneCommand = 0) {
    $memory = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($memory)
    $writer.Write([int]0x53534E53); $writer.Write($version)
    foreach ($tab in @(11,12)) {
        $writer.Write([uint16]9); $writer.Write([byte]0); $writer.Write([int]1); $writer.Write([int]$tab)
        # Keep a stale prior navigation; the selected index must choose current.
        foreach ($nav in @(0,1)) {
            $url = if ($nav -eq 0) { 'file:///stale.html' } elseif ($tab -eq 11) { 'file:///alpha.html' } else { 'file:///beta.html' }
            $urlBytes = [Text.Encoding]::UTF8.GetBytes($url)
            $padding = (4 - $urlBytes.Length % 4) % 4
            $payloadSize = 16 + $urlBytes.Length + $padding
            $writer.Write([uint16](1 + $payloadSize)); $writer.Write([byte]6)
            $writer.Write([int]($payloadSize - 4)); $writer.Write([int]$tab); $writer.Write([int]$nav); $writer.Write([int]$urlBytes.Length)
            $writer.Write($urlBytes); for ($i=0; $i-lt$padding; $i++) { $writer.Write([byte]0) }
        }
        if ($selection) { $writer.Write([uint16]9); $writer.Write([byte]7); $writer.Write([int]$tab); $writer.Write([int]1) }
    }
    if ($closeSecond) { $writer.Write([uint16]17); $writer.Write([byte]16); $writer.Write([int]12); $writer.Write([int]0); $writer.Write([long]0) }
    if ($closeWindow) { $writer.Write([uint16]17); $writer.Write([byte]17); $writer.Write([int]1); $writer.Write([int]0); $writer.Write([long]0) }
    if ($pruneCommand) { $writer.Write([uint16]9); $writer.Write([byte]$pruneCommand); $writer.Write([int]11); $writer.Write([int]1) }
    if ($marker) { $writer.Write([uint16]1); $writer.Write([byte]255) }
    $writer.Flush(); $result=$memory.ToArray(); $writer.Dispose(); $memory.Dispose()
    return ,$result
}
function Read-Fixture([byte[]]$bytes) {
    $path = Join-Path $fixtureRoot 'Session_test'
    [IO.File]::WriteAllBytes($path, $bytes)
    [ChromiumSessionFixture]::CurrentUrls($path)
}
function Assert-Rejected([byte[]]$bytes, [string]$message) {
    $rejected=$false; try { $null=Read-Fixture $bytes } catch { $rejected=$true }
    Assert-Session $rejected $message
}
try {
    $urls=@(Read-Fixture (New-SessionFixture))
    Assert-Session ($urls.Count-eq 2-and'file:///alpha.html'-in$urls-and'file:///beta.html'-in$urls) 'Both current fixture tabs must be present.'
    Assert-Session ('file:///stale.html'-notin$urls) 'Prior navigation text must not count as a current tab.'
    $urls=@(Read-Fixture (New-SessionFixture -closeSecond $true))
    Assert-Session ($urls.Count-eq 1-and'file:///beta.html'-notin$urls) 'A closed tab must not pass restoration checks.'
    Assert-Rejected (New-SessionFixture -selection $false) 'A missing selected navigation must not guess the expected URL.'
    $urls=@(Read-Fixture (New-SessionFixture -closeWindow $true))
    Assert-Session ($urls.Count-eq 0) 'Tabs belonging to a closed window must not count.'
    foreach ($prune in @(5,11,24)) { Assert-Rejected (New-SessionFixture -pruneCommand $prune) 'Unsupported navigation pruning must not count stale navigation records.' }
    Assert-Rejected (New-SessionFixture -marker $false) 'An unfinished initial snapshot must fail.'
    Assert-Rejected (New-SessionFixture -version 5) 'Encrypted or unknown formats must fail explicitly.'
    $bytes=New-SessionFixture
    Assert-Rejected ([byte[]]$bytes[0..($bytes.Length-2)]) 'A truncated command must fail.'
    $bytes=New-SessionFixture; $bytes[0]=0
    Assert-Rejected $bytes 'A wrong signature must fail.'
    $bytes=New-SessionFixture; $bytes[8]=255; $bytes[9]=255
    Assert-Rejected $bytes 'A command extending beyond the file must fail.'
    $bytes=New-SessionFixture; $bytes[10]=16
    Assert-Rejected $bytes 'A closed-tab command with a malformed payload must fail.'
    $bytes=New-SessionFixture; $bytes[10]=255
    Assert-Rejected $bytes 'A marker with a payload must fail.'
    Write-Output "$script:checks Chromium fixture-session checks passed."
} finally {
    $resolved=[IO.Path]::GetFullPath($fixtureRoot)
    if (-not $resolved.StartsWith((Join-Path $projectRoot 'build\session-parser-'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected parser fixture path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
