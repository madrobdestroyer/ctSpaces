$ErrorActionPreference = 'Stop'

function Assert-RetentionPath {
    param([string]$Path, [string]$Workspace, [switch]$AllowInstalled, [switch]$Absent)
    $full = [IO.Path]::GetFullPath($Path)
    if (![IO.Path]::IsPathFullyQualified($Path) -or $full -ne $Path) { throw "Noncanonical path: $Path" }
    $installed = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'InfinitySys\ctSpaces\ctSpaces.exe'
    if (!$Path.StartsWith($Workspace+'\', [StringComparison]::OrdinalIgnoreCase) -and
        !($AllowInstalled -and $Path -eq $installed)) { throw "Path outside the retained scope: $Path" }
    $present = Test-Path -LiteralPath $Path
    if ($Absent -and $present) { throw "Destination already exists: $Path" }
    if (!$Absent -and !$present) { throw "Required file is missing: $Path" }
    for ($part = $Path; $part; $part = [IO.Path]::GetDirectoryName($part)) {
        if (!(Test-Path -LiteralPath $part)) { continue }
        $item = Get-Item -LiteralPath $part -Force
        if ($item.LinkType -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Linked path refused: $part" }
    }
}

function Get-RetentionRecord {
    param([string]$Path, [string]$Workspace, [switch]$AllowInstalled)
    Assert-RetentionPath $Path $Workspace -AllowInstalled:$AllowInstalled
    $before = Get-Item -LiteralPath $Path -Force
    if ($before -isnot [IO.FileInfo]) { throw "Expected an ordinary file: $Path" }
    $length = $before.Length
    $written = $before.LastWriteTimeUtc.Ticks
    $created = $before.CreationTimeUtc.Ticks
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    $after = Get-Item -LiteralPath $Path -Force
    if ($after.Length -ne $length -or $after.LastWriteTimeUtc.Ticks -ne $written -or
        $after.CreationTimeUtc.Ticks -ne $created) { throw "File changed during verification: $Path" }
    [pscustomobject]@{Path=$after.FullName; Bytes=$length; Sha256=$hash}
}

function Confirm-RetentionRecord {
    param($Record, [string]$Workspace, [switch]$AllowInstalled)
    if (!$Record.Path -or $Record.Sha256 -cnotmatch '^[A-F0-9]{64}$' -or $Record.Bytes -lt 0) { throw 'Invalid retained file record.' }
    $actual = Get-RetentionRecord $Record.Path $Workspace -AllowInstalled:$AllowInstalled
    if ($actual.Sha256 -ne $Record.Sha256 -or $actual.Bytes -ne $Record.Bytes) { throw "Retained file differs: $($Record.Path)" }
    $actual
}

function Read-Retention {
    param([string]$Manifest, [string]$Workspace)
    $Workspace = [IO.Path]::GetFullPath($Workspace).TrimEnd('\')
    $manifestRecord = Get-RetentionRecord $Manifest $Workspace
    $data = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
    if ($data.SchemaVersion -ne 1 -or $data.Version -ne '6.3.5.0' -or $data.Workspace -ne $Workspace -or
        !$data.Current.Build -or !$data.Rollback.Build -or $data.Current.Build -eq $data.Rollback.Build) { throw 'Unexpected retention manifest.' }
    foreach ($group in $data.Current, $data.Rollback) {
        foreach ($kind in 'Exe','Pdb','ReviewMetadata') {
            if (!$group.$kind.Path) { throw "Missing retained $kind." }
        }
        if ($group.Exe.Sha256 -notlike ($group.Build+'*')) { throw 'Build identifier does not match its executable.' }
    }
    if (!@($data.PublicRelease).Count -or !@($data.BuildInputs).Count -or !@($data.ProductionSourcePaths).Count) { throw 'Required public/build/source inventory is empty.' }
    [pscustomobject]@{Data=$data; Manifest=$manifestRecord; Workspace=$Workspace}
}

function Confirm-Retention {
    param($Retention, [switch]$RefreshQaInputs)
    $data = $Retention.Data
    $workspace = $Retention.Workspace
    $records = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    $payloads = @($data.Current.Exe,$data.Current.Pdb,$data.Current.ReviewMetadata,
        $data.Rollback.Exe,$data.Rollback.Pdb,$data.Rollback.ReviewMetadata) + @($data.PublicRelease) + @($data.BuildInputs)
    foreach ($row in $payloads) {
        $actual = Confirm-RetentionRecord $row $workspace -AllowInstalled:($row.Path -eq $data.Current.Exe.Path)
        if ($records.ContainsKey($actual.Path) -and $records[$actual.Path].Sha256 -ne $actual.Sha256) { throw "Conflicting retained identity: $($actual.Path)" }
        $records[$actual.Path] = $actual
    }
    $changes = @()
    $qa = @(foreach ($row in @($data.QaInputs)) {
        if ($RefreshQaInputs) {
            $actual = Get-RetentionRecord $row.Path $workspace
            if ($actual.Sha256 -ne $row.Sha256 -or $actual.Bytes -ne $row.Bytes) {
                $changes += [pscustomobject]@{Path=$row.Path; PreviousSha256=$row.Sha256; Sha256=$actual.Sha256}
            }
            $actual
        } else { Confirm-RetentionRecord $row $workspace }
    })
    foreach ($row in $qa) { $records[$row.Path] = $row }
    $null = Confirm-RetentionRecord $Retention.Manifest $workspace
    [pscustomobject]@{Status='Passed'; Version=$data.Version; Current=$data.Current.Build; Rollback=$data.Rollback.Build;
        Manifest=$Retention.Manifest; Protected=@($records.Values | Sort-Object Path); QaInputs=$qa; QaInputChanges=$changes}
}

function Write-RetentionJson {
    param([string]$Path, $Value)
    $stream = [IO.FileStream]::new($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $Value -Depth 16)+[Environment]::NewLine)
        $stream.Write($bytes,0,$bytes.Length)
    } finally { $stream.Dispose() }
}

function Assert-RetentionOutput {
    param([string]$Directory, [string]$Workspace)
    Assert-RetentionPath $Directory $Workspace -Absent
    $retained = Join-Path $Workspace 'build\retained'
    if (!$Directory.StartsWith((Join-Path $Workspace 'build\'),[StringComparison]::OrdinalIgnoreCase) -or
        $Directory -eq $retained -or $Directory.StartsWith($retained+'\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Evidence must use a fresh directory under build, outside retained payloads.'
    }
}

function New-RetentionBaseline {
    param($Retention, [string]$Directory, [switch]$RefreshQaInputs)
    $workspace = $Retention.Workspace
    Assert-RetentionOutput $Directory $workspace
    $verified = Confirm-Retention $Retention -RefreshQaInputs:$RefreshQaInputs
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $listed = & git -C $workspace ls-files -z --cached --others --exclude-standard
    if ($LASTEXITCODE) { throw 'Could not inventory current source files.' }
    foreach ($relative in ($listed -join "`n").Split([char]0,[StringSplitOptions]::RemoveEmptyEntries)) {
        $path = Join-Path $workspace $relative
        Assert-RetentionPath $path $workspace
        if ((Get-Item -LiteralPath $path -Force).PSIsContainer) { continue }
        $null = $paths.Add($path)
    }
    $production = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($path in @($Retention.Data.ProductionSourcePaths)) {
        $null = $paths.Add($path)
        $null = $production.Add($path)
    }
    foreach ($row in @($verified.QaInputs) + @($Retention.Data.BuildInputs)) { $null = $paths.Add($row.Path) }
    # New root implementation files belong to the next production input set.
    foreach ($path in $paths) {
        if ([IO.Path]::GetDirectoryName($path) -eq $workspace -and [IO.Path]::GetExtension($path) -in '.cpp','.h','.inl','.rc','.vcxproj','.props') {
            $null = $production.Add($path)
        }
    }
    $source = @(foreach ($path in ($paths | Sort-Object)) { Get-RetentionRecord $path $workspace })
    [IO.Directory]::CreateDirectory($Directory) | Out-Null
    $validation = @(foreach ($record in $source) {
        $snapshot = Join-Path (Join-Path $Directory 'source') $record.Path.Substring($workspace.Length+1)
        Assert-RetentionPath $snapshot $workspace -Absent
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($snapshot)) | Out-Null
        [IO.File]::Copy($record.Path,$snapshot,$false)
        $copy = Get-RetentionRecord $snapshot $workspace
        $current = Confirm-RetentionRecord $record $workspace
        if ($copy.Bytes -ne $current.Bytes -or $copy.Sha256 -ne $current.Sha256) { throw "Snapshot differs: $snapshot" }
        [pscustomobject]@{Path=$current.Path; Snapshot=$snapshot; Bytes=$current.Bytes; Sha256=$current.Sha256}
    })
    $protected = @($verified.Protected) + @($Retention.Manifest)
    $productionRows = @($validation | Where-Object { $production.Contains($_.Path) })
    Write-RetentionJson (Join-Path $Directory 'protected-before.json') $protected
    Write-RetentionJson (Join-Path $Directory 'production-inputs.json') $productionRows
    Write-RetentionJson (Join-Path $Directory 'validation-inputs.json') $validation
    $null = Confirm-Retention $Retention -RefreshQaInputs:$RefreshQaInputs
    foreach ($record in $verified.Protected) { $null = Confirm-RetentionRecord $record $workspace -AllowInstalled:($record.Path -eq $Retention.Data.Current.Exe.Path) }
    $result = [ordered]@{Status='Passed'; AtUtc=[DateTime]::UtcNow.ToString('o'); Version=$Retention.Data.Version;
        Current=$Retention.Data.Current; Rollback=$Retention.Data.Rollback; Retention=$Retention.Manifest;
        Protected=$protected.Count; ProductionInputs=$productionRows.Count; ValidationInputs=$validation.Count;
        ProductionManifest=(Join-Path $Directory 'production-inputs.json'); ValidationManifest=(Join-Path $Directory 'validation-inputs.json');
        Snapshot=(Join-Path $Directory 'source'); QaInputChanges=$verified.QaInputChanges;
        HistoricalRollbackPathsImported=$false; AppsLaunched=$false; InstalledFilesWritten=$false}
    Write-RetentionJson (Join-Path $Directory 'baseline.json') $result
    [pscustomobject]$result
}

function Resolve-RetainedExecutable {
    param([string]$Workspace, [string]$ExePath='')
    if (![string]::IsNullOrWhiteSpace($ExePath)) { return [IO.Path]::GetFullPath($ExePath) }
    $workspace = [IO.Path]::GetFullPath($Workspace).TrimEnd('\')
    $manifest = Join-Path $workspace 'build\retained\retention.json'
    if (!(Test-Path -LiteralPath $manifest)) { return (Join-Path $workspace 'dist\x64\Release\ctSpaces.exe') }
    $retention = Read-Retention $manifest $workspace
    $current = Confirm-RetentionRecord $retention.Data.Current.Exe $workspace -AllowInstalled
    $null = Confirm-RetentionRecord $retention.Manifest $workspace
    $current.Path
}

function New-RetentionRollbackStage {
    param($Retention, [string]$Directory)
    $workspace = $Retention.Workspace
    Assert-RetentionOutput $Directory $workspace
    $before = Confirm-Retention $Retention
    [IO.Directory]::CreateDirectory($Directory) | Out-Null
    $copies = @(foreach ($name in @(@('Exe','ctSpaces.exe'),@('Pdb','ctSpaces.pdb'),@('ReviewMetadata','review.json'))) {
        $row = $Retention.Data.Rollback.($name[0])
        $target = Join-Path $Directory $name[1]
        [IO.File]::Copy($row.Path,$target,$false)
        $copy = Get-RetentionRecord $target $workspace
        if ($copy.Sha256 -ne $row.Sha256 -or $copy.Bytes -ne $row.Bytes) { throw "Rollback stage differs: $target" }
        $copy
    })
    $null = Confirm-Retention $Retention
    $result = [pscustomobject]@{Status='Passed'; AtUtc=[DateTime]::UtcNow.ToString('o'); Version=$Retention.Data.Version;
        Rollback=$Retention.Data.Rollback.Build; Manifest=$Retention.Manifest; Files=$copies;
        InstalledFilesWritten=$false; AppsLaunched=$false}
    Write-RetentionJson (Join-Path $Directory 'result.json') $result
    $result
}

Export-ModuleMember -Function Read-Retention,Confirm-Retention,New-RetentionBaseline,New-RetentionRollbackStage,Resolve-RetainedExecutable
