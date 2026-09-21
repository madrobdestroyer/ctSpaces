param(
    [Parameter(Mandatory = $true)]
    [string]$SourceProfile,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$maximumJsonBytes = 16MB
$maximumBookmarksBytes = 64MB
$maximumFaviconsBytes = 256MB
$maximumOutputBytes = 2GB
$maximumOutputItems = 100000
$maximumBookmarkUrls = 100000
$maximumCopyDepth = 128
$reservedGeneratedItems = 16
$extensionIdPattern = '^[a-p]{32}$'
$plannedOutputBytes = [int64]0
$plannedOutputItems = [int64]0

function Initialize-NativeSqlite {
    if ('CtSpacesDefaultSqlite' -as [type]) {
        return
    }

    $systemRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $sqliteDll = [IO.Path]::GetFullPath(
        (Join-Path $systemRoot 'System32\winsqlite3.dll'))
    if (-not (Test-Path -LiteralPath $sqliteDll -PathType Leaf)) {
        throw 'Windows SQLite is unavailable; the Favicons database cannot be proven safe.'
    }
    $sqliteItem = Get-Item -LiteralPath $sqliteDll -Force
    if (($sqliteItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The Windows SQLite library resolves through a reparse point.'
    }

    $sqliteDllLiteral = $sqliteDll.Replace('"', '""')
    $nativeSource = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class CtSpacesDefaultSqlite
{
    private const string DllName = @"$sqliteDllLiteral";

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_open_v2(
        byte[] filename, out IntPtr database, int flags, IntPtr vfs);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_close_v2(IntPtr database);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_busy_timeout(IntPtr database, int milliseconds);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr sqlite3_errmsg(IntPtr database);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_exec(
        IntPtr database, byte[] sql, IntPtr callback, IntPtr argument,
        out IntPtr errorMessage);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern void sqlite3_free(IntPtr memory);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_prepare_v2(
        IntPtr database, byte[] sql, int byteCount, out IntPtr statement,
        IntPtr tail);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_step(IntPtr statement);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_finalize(IntPtr statement);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_reset(IntPtr statement);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_clear_bindings(IntPtr statement);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_bind_text(
        IntPtr statement, int index, byte[] value, int byteCount,
        IntPtr destructor);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr sqlite3_column_text(IntPtr statement, int column);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    public static extern int sqlite3_column_bytes(IntPtr statement, int column);

    public static string Utf8(IntPtr value, int length)
    {
        if (value == IntPtr.Zero || length <= 0)
            return String.Empty;
        byte[] bytes = new byte[length];
        Marshal.Copy(value, bytes, 0, length);
        return new UTF8Encoding(false, true).GetString(bytes);
    }

    public static string Utf8Z(IntPtr value)
    {
        if (value == IntPtr.Zero)
            return String.Empty;
        int length = 0;
        while (Marshal.ReadByte(value, length) != 0)
            checked { length++; }
        return Utf8(value, length);
    }
}
"@
    Add-Type -TypeDefinition $nativeSource -Language CSharp
}

function ConvertTo-Utf8Z {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value)

    $encoded = [Text.Encoding]::UTF8.GetBytes($Value)
    $terminated = New-Object byte[] ($encoded.Length + 1)
    if ($encoded.Length -gt 0) {
        [Array]::Copy($encoded, $terminated, $encoded.Length)
    }
    return $terminated
}

function Get-SqliteErrorMessage {
    param([Parameter(Mandatory = $true)][IntPtr]$Database)

    if ($Database -eq [IntPtr]::Zero) {
        return 'unknown SQLite error'
    }
    return [CtSpacesDefaultSqlite]::Utf8Z(
        [CtSpacesDefaultSqlite]::sqlite3_errmsg($Database))
}

function Invoke-SqliteExec {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Database,
        [Parameter(Mandatory = $true)][string]$Sql
    )

    $errorPointer = [IntPtr]::Zero
    $sqlBytes = ConvertTo-Utf8Z $Sql
    $result = [CtSpacesDefaultSqlite]::sqlite3_exec(
        $Database, $sqlBytes, [IntPtr]::Zero, [IntPtr]::Zero,
        [ref]$errorPointer)
    if ($result -eq 0) {
        return
    }

    $message = Get-SqliteErrorMessage $Database
    if ($errorPointer -ne [IntPtr]::Zero) {
        try {
            $message = [CtSpacesDefaultSqlite]::Utf8Z($errorPointer)
        }
        finally {
            [CtSpacesDefaultSqlite]::sqlite3_free($errorPointer)
        }
    }
    throw "SQLite rejected the Favicons database operation: $message"
}

function Invoke-SqliteTextQuery {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Database,
        [Parameter(Mandatory = $true)][string]$Sql,
        [int]$Column = 0
    )

    $statement = [IntPtr]::Zero
    $sqlBytes = ConvertTo-Utf8Z $Sql
    $result = [CtSpacesDefaultSqlite]::sqlite3_prepare_v2(
        $Database, $sqlBytes, -1, [ref]$statement, [IntPtr]::Zero)
    if ($result -ne 0) {
        throw "SQLite could not prepare a Favicons proof query: $(Get-SqliteErrorMessage $Database)"
    }

    try {
        while ($true) {
            $result = [CtSpacesDefaultSqlite]::sqlite3_step($statement)
            if ($result -eq 101) {
                break
            }
            if ($result -ne 100) {
                throw "SQLite could not complete a Favicons proof query: $(Get-SqliteErrorMessage $Database)"
            }

            $length = [CtSpacesDefaultSqlite]::sqlite3_column_bytes(
                $statement, $Column)
            $pointer = [CtSpacesDefaultSqlite]::sqlite3_column_text(
                $statement, $Column)
            Write-Output ([CtSpacesDefaultSqlite]::Utf8($pointer, $length))
        }
    }
    finally {
        if ($statement -ne [IntPtr]::Zero) {
            [void][CtSpacesDefaultSqlite]::sqlite3_finalize($statement)
        }
    }
}

function Open-SqliteDatabaseForSanitizing {
    param([Parameter(Mandatory = $true)][string]$Path)

    Initialize-NativeSqlite
    $database = [IntPtr]::Zero
    $pathBytes = ConvertTo-Utf8Z ([IO.Path]::GetFullPath($Path))
    $result = [CtSpacesDefaultSqlite]::sqlite3_open_v2(
        $pathBytes, [ref]$database, 0x00000002 -bor 0x00010000,
        [IntPtr]::Zero)
    if ($result -ne 0) {
        $message = Get-SqliteErrorMessage $database
        if ($database -ne [IntPtr]::Zero) {
            [void][CtSpacesDefaultSqlite]::sqlite3_close_v2($database)
        }
        throw "Favicons is not a writable closed SQLite database: $message"
    }
    if ([CtSpacesDefaultSqlite]::sqlite3_busy_timeout($database, 0) -ne 0) {
        [void][CtSpacesDefaultSqlite]::sqlite3_close_v2($database)
        throw 'SQLite could not disable waiting on a busy Favicons database.'
    }
    return $database
}

function Assert-SqliteIntegrity {
    param([Parameter(Mandatory = $true)][IntPtr]$Database)

    $results = @(Invoke-SqliteTextQuery $Database 'PRAGMA integrity_check')
    if ($results.Count -ne 1 -or $results[0] -cne 'ok') {
        throw 'The Favicons SQLite integrity check failed.'
    }
}

function Assert-SupportedFaviconsSchema {
    param([Parameter(Mandatory = $true)][IntPtr]$Database)

    $expectedTables = @('favicon_bitmaps', 'favicons', 'icon_mapping', 'meta')
    $tables = @(
        Invoke-SqliteTextQuery $Database `
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name"
    )
    if ([string]::Join('|', $tables) -cne
        [string]::Join('|', $expectedTables)) {
        throw 'Favicons contains an unexpected or missing SQLite table.'
    }

    $unexpectedObjects = @(
        Invoke-SqliteTextQuery $Database `
            "SELECT name FROM sqlite_master WHERE type IN ('trigger','view') ORDER BY name"
    )
    if ($unexpectedObjects.Count -ne 0) {
        throw 'Favicons contains an unexpected SQLite trigger or view.'
    }

    $expectedColumns = [ordered]@{
        meta = @('key', 'value')
        icon_mapping = @('id', 'page_url', 'icon_id', 'page_url_type')
        favicons = @('id', 'url', 'icon_type')
        favicon_bitmaps = @(
            'id', 'icon_id', 'last_updated', 'image_data', 'width', 'height',
            'last_requested'
        )
    }
    foreach ($table in $expectedColumns.Keys) {
        $columns = @(
            Invoke-SqliteTextQuery $Database "PRAGMA table_info($table)" -Column 1
        )
        if ([string]::Join('|', $columns) -cne
            [string]::Join('|', $expectedColumns[$table])) {
            throw "Favicons has an unsupported $table schema."
        }
    }

    $version = @(
        Invoke-SqliteTextQuery $Database `
            "SELECT value FROM meta WHERE key='version'"
    )
    $compatibleVersion = @(
        Invoke-SqliteTextQuery $Database `
            "SELECT value FROM meta WHERE key='last_compatible_version'"
    )
    if ($version.Count -ne 1 -or $version[0] -cne '9' -or
        $compatibleVersion.Count -ne 1 -or
        $compatibleVersion[0] -cne '9') {
        throw 'Favicons has an unsupported Chromium schema version.'
    }
}

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return [IO.Path]::GetFullPath($Path)
}

function Test-IsSameOrChildPath {
    param(
        [Parameter(Mandatory = $true)][string]$Candidate,
        [Parameter(Mandatory = $true)][string]$Root
    )

    $candidatePath = Get-FullPath $Candidate
    $rootPath = (Get-FullPath $Root).TrimEnd([IO.Path]::DirectorySeparatorChar,
                                             [IO.Path]::AltDirectorySeparatorChar)
    if ([string]::Equals($candidatePath, $rootPath,
                         [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }

    $prefix = $rootPath + [IO.Path]::DirectorySeparatorChar
    return $candidatePath.StartsWith($prefix,
                                     [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NotReparsePoint {
    param([Parameter(Mandatory = $true)][IO.FileSystemInfo]$Item)

    if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Reparse points are not allowed in the Default profile: $($Item.FullName)"
    }
}

function Add-IncludedItemToBudget {
    param([Parameter(Mandatory = $true)][IO.FileSystemInfo]$Item)

    Assert-NotReparsePoint $Item
    if ($script:plannedOutputItems -ge
        ($maximumOutputItems - $reservedGeneratedItems)) {
        throw 'The selected starter content contains too many items.'
    }
    $script:plannedOutputItems++

    if (-not $Item.PSIsContainer) {
        if ($Item.Length -gt ($maximumOutputBytes -
                              $script:plannedOutputBytes)) {
            throw 'The selected starter content exceeds the size limit.'
        }
        $script:plannedOutputBytes += $Item.Length
    }
}

function Get-BoundedRegularFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][int64]$MaximumBytes,
        [switch]$AllowEmpty
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "The Default profile is missing its required $Label file."
    }
    $item = Get-Item -LiteralPath $Path -Force
    Assert-NotReparsePoint $item
    if ($item.PSIsContainer) {
        throw "The required $Label path is not a regular file."
    }
    if ((-not $AllowEmpty -and $item.Length -le 0) -or
        $item.Length -gt $MaximumBytes) {
        throw "The $Label file is empty or exceeds the starter safety limit."
    }
    return $item
}

function Copy-LockedRegularFile {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][int64]$MaximumBytes,
        [switch]$AllowEmpty
    )

    $item = Get-BoundedRegularFile -Path $Source -Label $Label `
        -MaximumBytes $MaximumBytes -AllowEmpty:$AllowEmpty
    Add-IncludedItemToBudget $item

    $sourceStream = $null
    try {
        $sourceStream = [IO.File]::Open(
            $item.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::None)
    }
    catch {
        throw "The $Label file is open or changed while it was being sanitized. Close the browser and try again."
    }

    try {
        if ($sourceStream.Length -ne $item.Length -or
            $sourceStream.Length -gt $MaximumBytes -or
            (-not $AllowEmpty -and $sourceStream.Length -le 0)) {
            throw "The $Label file changed while it was being sanitized."
        }

        $destinationStream = [IO.File]::Open(
            $Destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
            [IO.FileShare]::None)
        try {
            $sourceStream.CopyTo($destinationStream, 1MB)
            $destinationStream.Flush($true)
            if ($destinationStream.Length -ne $sourceStream.Length) {
                throw "The $Label file was not copied completely."
            }
        }
        finally {
            $destinationStream.Dispose()
        }
    }
    finally {
        $sourceStream.Dispose()
    }
}

function Copy-IncludedTreeSafely {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [int]$Depth = 0
    )

    if ($Depth -gt $maximumCopyDepth) {
        throw 'An extension package exceeds the maximum directory depth.'
    }
    $sourceItem = Get-Item -LiteralPath $Source -Force
    Assert-NotReparsePoint $sourceItem
    if (-not $sourceItem.PSIsContainer) {
        throw "An extension package path is not a directory: $Source"
    }
    Add-IncludedItemToBudget $sourceItem
    New-Item -ItemType Directory -Path $Destination | Out-Null

    foreach ($child in Get-ChildItem -LiteralPath $sourceItem.FullName -Force) {
        Assert-NotReparsePoint $child
        if (-not (Test-IsSameOrChildPath $child.FullName $sourceItem.FullName)) {
            throw 'An extension package entry escapes its package directory.'
        }
        $destinationChild = Join-Path $Destination $child.Name
        if ($child.PSIsContainer) {
            Copy-IncludedTreeSafely -Source $child.FullName `
                -Destination $destinationChild -Depth ($Depth + 1)
        }
        else {
            Copy-LockedRegularFile -Source $child.FullName `
                -Destination $destinationChild -Label 'extension package' `
                -MaximumBytes $maximumOutputBytes -AllowEmpty
        }
    }
}

function Add-PropertyIfPresent {
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.IDictionary]$Target,
        [AllowNull()]
        [object]$Source,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    if ($null -ne $Source -and $Source.PSObject.Properties[$Name]) {
        $Target[$Name] = $Source.$Name
    }
}

function Read-JsonFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int64]$MaximumBytes = $maximumJsonBytes,
        [string]$Label = 'JSON'
    )

    $item = Get-BoundedRegularFile -Path $Path -Label $Label `
        -MaximumBytes $MaximumBytes
    $stream = $null
    try {
        $stream = [IO.File]::Open(
            $item.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::None)
    }
    catch {
        throw "The $Label file is open or changed while it was being read. Close the browser and try again."
    }

    try {
        if ($stream.Length -ne $item.Length -or $stream.Length -le 0 -or
            $stream.Length -gt $MaximumBytes) {
            throw "The $Label file changed while it was being read."
        }
        $encoding = [Text.UTF8Encoding]::new($false, $true)
        $reader = [IO.StreamReader]::new(
            $stream, $encoding, $true, 4096, $true)
        try {
            $json = $reader.ReadToEnd()
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }

    try {
        return $json | ConvertFrom-Json
    }
    catch {
        throw "The $Label file is not valid JSON."
    }
}

function Add-BookmarkUrls {
    param(
        [AllowNull()][object]$Node,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [Collections.Generic.HashSet[string]]$Urls,
        [Parameter(Mandatory = $true)][object]$State
    )

    if ($null -eq $Node -or $Node -is [string] -or
        $Node.GetType().IsPrimitive) {
        return
    }
    $State.Nodes++
    if ($State.Nodes -gt $maximumOutputItems) {
        throw 'Bookmarks contains too many JSON nodes.'
    }

    if ($Node -is [Collections.IDictionary]) {
        if ($Node.Contains('url')) {
            $url = $Node['url']
            if ($url -isnot [string] -or [string]::IsNullOrEmpty($url) -or
                $url.IndexOf([char]0) -ge 0) {
                throw 'Bookmarks contains an invalid URL value.'
            }
            [void]$Urls.Add($url)
        }
        foreach ($key in $Node.Keys) {
            if ([string]$key -cne 'url') {
                Add-BookmarkUrls -Node $Node[$key] -Urls $Urls -State $State
            }
        }
        return
    }

    if ($Node -is [Collections.IEnumerable]) {
        foreach ($entry in $Node) {
            Add-BookmarkUrls -Node $entry -Urls $Urls -State $State
        }
        return
    }

    $properties = @($Node.PSObject.Properties)
    $urlProperty = $Node.PSObject.Properties['url']
    if ($urlProperty) {
        $url = $urlProperty.Value
        if ($url -isnot [string] -or [string]::IsNullOrEmpty($url) -or
            $url.IndexOf([char]0) -ge 0) {
            throw 'Bookmarks contains an invalid URL value.'
        }
        [void]$Urls.Add($url)
    }
    foreach ($property in $properties) {
        if ($property.Name -cne 'url') {
            Add-BookmarkUrls -Node $property.Value -Urls $Urls -State $State
        }
    }
}

function Get-BookmarkUrlSet {
    param([Parameter(Mandatory = $true)][object]$Bookmarks)

    $roots = $Bookmarks.PSObject.Properties['roots']
    if (-not $roots) {
        throw 'Bookmarks does not contain a roots object.'
    }
    $urls = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    $state = [pscustomobject]@{ Nodes = 0 }
    Add-BookmarkUrls -Node $roots.Value -Urls $urls -State $state
    if ($urls.Count -gt $maximumBookmarkUrls) {
        throw 'Bookmarks contains too many unique URLs.'
    }
    return ,$urls
}

function Assert-ClosedFaviconsSource {
    param([Parameter(Mandatory = $true)][string]$Path)

    foreach ($suffix in @('-journal', '-wal', '-shm')) {
        $sidecarPath = $Path + $suffix
        if (-not (Test-Path -LiteralPath $sidecarPath)) {
            continue
        }
        $sidecar = Get-Item -LiteralPath $sidecarPath -Force
        Assert-NotReparsePoint $sidecar
        if ($sidecar.PSIsContainer -or $sidecar.Length -ne 0) {
            throw "Favicons has an active SQLite sidecar ($suffix). Close the browser and try again."
        }
    }
}

function Assert-FaviconsProof {
    param(
        [Parameter(Mandatory = $true)][IntPtr]$Database,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [Collections.Generic.HashSet[string]]$BookmarkUrls
    )

    Assert-SqliteIntegrity $Database
    foreach ($pageUrl in @(
        Invoke-SqliteTextQuery $Database `
            'SELECT page_url FROM icon_mapping ORDER BY id'
    )) {
        if (-not $BookmarkUrls.Contains($pageUrl)) {
            throw 'Favicons still contains a URL that is not present in Bookmarks.'
        }
    }
    $orphanBitmaps = @(
        Invoke-SqliteTextQuery $Database @'
SELECT COUNT(*) FROM favicon_bitmaps
WHERE NOT EXISTS (
    SELECT 1 FROM icon_mapping WHERE icon_mapping.icon_id = favicon_bitmaps.icon_id
)
'@
    )
    $orphanIcons = @(
        Invoke-SqliteTextQuery $Database @'
SELECT COUNT(*) FROM favicons
WHERE NOT EXISTS (
    SELECT 1 FROM icon_mapping WHERE icon_mapping.icon_id = favicons.id
)
'@
    )
    $timestampedBitmaps = @(
        Invoke-SqliteTextQuery $Database `
            'SELECT COUNT(*) FROM favicon_bitmaps WHERE last_updated <> 0 OR last_requested <> 0'
    )
    if ($orphanBitmaps.Count -ne 1 -or $orphanBitmaps[0] -cne '0' -or
        $orphanIcons.Count -ne 1 -or $orphanIcons[0] -cne '0' -or
        $timestampedBitmaps.Count -ne 1 -or
        $timestampedBitmaps[0] -cne '0') {
        throw 'Favicons still contains orphaned or timestamped icon data.'
    }
}

function Sanitize-FaviconsDatabase {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [Collections.Generic.HashSet[string]]$BookmarkUrls
    )

    $database = Open-SqliteDatabaseForSanitizing $Path
    $transactionOpen = $false
    try {
        Assert-SupportedFaviconsSchema $database
        Assert-SqliteIntegrity $database
        Invoke-SqliteExec $database 'PRAGMA journal_mode=DELETE'
        Invoke-SqliteExec $database 'PRAGMA synchronous=FULL'
        Invoke-SqliteExec $database 'PRAGMA secure_delete=ON'
        Invoke-SqliteExec $database 'PRAGMA temp_store=MEMORY'
        Invoke-SqliteExec $database 'BEGIN IMMEDIATE'
        $transactionOpen = $true
        Invoke-SqliteExec $database @'
CREATE TEMP TABLE ctspaces_allowed_urls(
    page_url TEXT PRIMARY KEY COLLATE BINARY
) WITHOUT ROWID
'@

        $insertStatement = [IntPtr]::Zero
        $insertSql = ConvertTo-Utf8Z `
            'INSERT OR IGNORE INTO ctspaces_allowed_urls(page_url) VALUES(?)'
        $result = [CtSpacesDefaultSqlite]::sqlite3_prepare_v2(
            $database, $insertSql, -1, [ref]$insertStatement,
            [IntPtr]::Zero)
        if ($result -ne 0) {
            throw "SQLite could not prepare the bookmark allowlist: $(Get-SqliteErrorMessage $database)"
        }
        try {
            foreach ($bookmarkUrl in $BookmarkUrls) {
                [void][CtSpacesDefaultSqlite]::sqlite3_reset($insertStatement)
                [void][CtSpacesDefaultSqlite]::sqlite3_clear_bindings(
                    $insertStatement)
                $urlBytes = ConvertTo-Utf8Z $bookmarkUrl
                $result = [CtSpacesDefaultSqlite]::sqlite3_bind_text(
                    $insertStatement, 1, $urlBytes, $urlBytes.Length - 1,
                    [IntPtr](-1))
                if ($result -ne 0) {
                    throw "SQLite could not bind a bookmark URL: $(Get-SqliteErrorMessage $database)"
                }
                $result = [CtSpacesDefaultSqlite]::sqlite3_step(
                    $insertStatement)
                if ($result -ne 101) {
                    throw "SQLite could not build the bookmark URL allowlist: $(Get-SqliteErrorMessage $database)"
                }
            }
        }
        finally {
            if ($insertStatement -ne [IntPtr]::Zero) {
                [void][CtSpacesDefaultSqlite]::sqlite3_finalize(
                    $insertStatement)
            }
        }

        Invoke-SqliteExec $database @'
DELETE FROM icon_mapping
WHERE page_url NOT IN (SELECT page_url FROM ctspaces_allowed_urls)
'@
        Invoke-SqliteExec $database @'
DELETE FROM favicon_bitmaps
WHERE NOT EXISTS (
    SELECT 1 FROM icon_mapping WHERE icon_mapping.icon_id = favicon_bitmaps.icon_id
)
'@
        Invoke-SqliteExec $database @'
DELETE FROM favicons
WHERE NOT EXISTS (
    SELECT 1 FROM icon_mapping WHERE icon_mapping.icon_id = favicons.id
)
'@
        Invoke-SqliteExec $database `
            'UPDATE favicon_bitmaps SET last_updated=0, last_requested=0'
        Assert-FaviconsProof -Database $database -BookmarkUrls $BookmarkUrls
        Invoke-SqliteExec $database 'COMMIT'
        $transactionOpen = $false

        # secure_delete plus VACUUM prevents removed browsing URLs from surviving
        # in freelist pages in the archive.
        Invoke-SqliteExec $database 'VACUUM'
        Assert-SupportedFaviconsSchema $database
        Assert-FaviconsProof -Database $database -BookmarkUrls $BookmarkUrls
        $freePages = @(Invoke-SqliteTextQuery $database 'PRAGMA freelist_count')
        if ($freePages.Count -ne 1 -or $freePages[0] -cne '0') {
            throw 'Favicons still contains SQLite freelist pages after sanitizing.'
        }
        $mappingCount = @(
            Invoke-SqliteTextQuery $database 'SELECT COUNT(*) FROM icon_mapping'
        )[0]
    }
    catch {
        $failure = $_
        if ($transactionOpen) {
            try {
                Invoke-SqliteExec $database 'ROLLBACK'
            }
            catch {
                # Preserve the proof failure that caused the rollback.
            }
        }
        throw $failure
    }
    finally {
        [void][CtSpacesDefaultSqlite]::sqlite3_close_v2($database)
    }

    foreach ($suffix in @('-journal', '-wal', '-shm')) {
        if (Test-Path -LiteralPath ($Path + $suffix)) {
            throw "The sanitized Favicons database emitted an unexpected $suffix sidecar."
        }
    }
    $item = Get-BoundedRegularFile -Path $Path -Label 'Favicons' `
        -MaximumBytes $maximumFaviconsBytes
    return [int64]$mappingCount
}

function Write-Utf8Json {
    param(
        [Parameter(Mandatory = $true)][object]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $json = $Value | ConvertTo-Json -Depth 100 -Compress
    $encoding = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($Path, $json, $encoding)
}

function Get-ValidatedExtensionRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$ExtensionId,
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$SourceExtensions,
        [Parameter(Mandatory = $true)][string]$CleanExtensions
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        throw "Secure Preferences has no package path for extension $ExtensionId."
    }
    $normalized = $RelativePath.Replace('/', '\')
    if ([IO.Path]::IsPathRooted($normalized) -or
        $normalized.StartsWith('\', [StringComparison]::Ordinal) -or
        $normalized.IndexOf(':') -ge 0) {
        throw "Secure Preferences has a rooted package path for extension $ExtensionId."
    }
    $segments = @($normalized.Split([IO.Path]::DirectorySeparatorChar))
    if ($segments.Count -lt 2 -or
        $segments[0] -cne $ExtensionId -or
        @($segments | Where-Object {
            [string]::IsNullOrEmpty($_) -or $_ -ceq '.' -or $_ -ceq '..'
        }).Count -ne 0) {
        throw "Secure Preferences has an escaping or mismatched package path for extension $ExtensionId."
    }

    $sourceIdRoot = Get-FullPath (Join-Path $SourceExtensions $ExtensionId)
    $sourceTarget = Get-FullPath (Join-Path $SourceExtensions $normalized)
    if (-not (Test-IsSameOrChildPath $sourceTarget $sourceIdRoot) -or
        [string]::Equals($sourceTarget, $sourceIdRoot,
                         [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $sourceTarget -PathType Container)) {
        throw "Secure Preferences points outside the copied package for extension $ExtensionId."
    }
    $sourceCursor = $sourceIdRoot
    foreach ($segment in $segments | Select-Object -Skip 1) {
        $sourceCursor = Join-Path $sourceCursor $segment
        $sourceCursorItem = Get-Item -LiteralPath $sourceCursor -Force
        Assert-NotReparsePoint $sourceCursorItem
    }

    $cleanIdRoot = Get-FullPath (Join-Path $CleanExtensions $ExtensionId)
    $cleanTarget = Get-FullPath (Join-Path $CleanExtensions $normalized)
    if (-not (Test-IsSameOrChildPath $cleanTarget $cleanIdRoot) -or
        [string]::Equals($cleanTarget, $cleanIdRoot,
                         [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $cleanTarget -PathType Container)) {
        throw "The copied package target is missing for extension $ExtensionId."
    }
    Assert-NotReparsePoint (Get-Item -LiteralPath $cleanTarget -Force)
    return $normalized
}

$sourcePath = Get-FullPath $SourceProfile
$outputPath = Get-FullPath $OutputRoot
if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
    throw "The Default profile folder does not exist: $sourcePath"
}
if (Test-IsSameOrChildPath $outputPath $sourcePath) {
    throw 'The sanitized output folder cannot be inside the source profile.'
}
if (Test-Path -LiteralPath $outputPath) {
    throw "The sanitized output folder already exists: $outputPath"
}

$sourceItem = Get-Item -LiteralPath $sourcePath -Force
Assert-NotReparsePoint $sourceItem

$sourceDefault = Join-Path $sourcePath 'Default'
if (-not (Test-Path -LiteralPath $sourceDefault -PathType Container)) {
    throw 'The browser profile does not contain a Default directory.'
}
$sourceDefaultItem = Get-Item -LiteralPath $sourceDefault -Force
Assert-NotReparsePoint $sourceDefaultItem

$cleanDefault = Join-Path $outputPath 'Default'
New-Item -ItemType Directory -Path $cleanDefault -Force | Out-Null

$sourceBookmarks = Join-Path $sourceDefault 'Bookmarks'
$cleanBookmarks = Join-Path $cleanDefault 'Bookmarks'
Copy-LockedRegularFile -Source $sourceBookmarks -Destination $cleanBookmarks `
    -Label 'Bookmarks' -MaximumBytes $maximumBookmarksBytes
$bookmarks = Read-JsonFile -Path $cleanBookmarks `
    -MaximumBytes $maximumBookmarksBytes -Label 'Bookmarks'
$bookmarkUrls = Get-BookmarkUrlSet $bookmarks

$sourceFavicons = Join-Path $sourceDefault 'Favicons'
$cleanFavicons = Join-Path $cleanDefault 'Favicons'
Assert-ClosedFaviconsSource $sourceFavicons
Copy-LockedRegularFile -Source $sourceFavicons -Destination $cleanFavicons `
    -Label 'Favicons' -MaximumBytes $maximumFaviconsBytes
$faviconMappingCount = Sanitize-FaviconsDatabase -Path $cleanFavicons `
    -BookmarkUrls $bookmarkUrls

$installedIds = [Collections.Generic.List[string]]::new()
$sourceExtensions = Join-Path $sourceDefault 'Extensions'
if (Test-Path -LiteralPath $sourceExtensions -PathType Container) {
    $sourceExtensionsItem = Get-Item -LiteralPath $sourceExtensions -Force
    Assert-NotReparsePoint $sourceExtensionsItem
    $cleanExtensions = Join-Path $cleanDefault 'Extensions'
    New-Item -ItemType Directory -Path $cleanExtensions -Force | Out-Null

    foreach ($extension in Get-ChildItem -LiteralPath $sourceExtensions -Force) {
        if (-not $extension.PSIsContainer -or
            $extension.Name -cnotmatch $extensionIdPattern) {
            continue
        }
        Copy-IncludedTreeSafely -Source $extension.FullName `
            -Destination (Join-Path $cleanExtensions $extension.Name)
        $installedIds.Add($extension.Name)
    }
}

$preferences = Read-JsonFile -Path (Join-Path $sourceDefault 'Preferences') `
    -Label 'Preferences'
$cleanBrowser = [ordered]@{
    check_default_browser = $false
    show_prompt_before_closing_tabs = $true
    show_toolbar_bookmarks_button = $true
}
$cleanDistribution = [ordered]@{
    import_bookmarks = $false
    import_history = $false
    import_search_engine = $false
    make_chrome_default_for_user = $false
    skip_first_run_ui = $true
}
$cleanExtensionsPreferences = [ordered]@{}
if ($preferences.PSObject.Properties['extensions']) {
    if ($preferences.extensions.PSObject.Properties['commands']) {
        $cleanCommands = [ordered]@{}
        foreach ($command in $preferences.extensions.commands.PSObject.Properties) {
            $sourceCommand = $command.Value
            if (-not $sourceCommand.PSObject.Properties['extension'] -or
                -not $installedIds.Contains([string]$sourceCommand.extension)) {
                continue
            }
            $cleanCommand = [ordered]@{}
            foreach ($name in @('command_name', 'extension', 'global')) {
                Add-PropertyIfPresent $cleanCommand $sourceCommand $name
            }
            if ($cleanCommand.Count -gt 0) {
                $cleanCommands[$command.Name] = $cleanCommand
            }
        }
        if ($cleanCommands.Count -gt 0) {
            $cleanExtensionsPreferences['commands'] = $cleanCommands
        }
    }
    if ($preferences.extensions.PSObject.Properties['ui']) {
        $cleanExtensionUi = [ordered]@{}
        Add-PropertyIfPresent $cleanExtensionUi $preferences.extensions.ui `
                              'allow_chrome_webstore'
        if ($cleanExtensionUi.Count -gt 0) {
            $cleanExtensionsPreferences['ui'] = $cleanExtensionUi
        }
    }
    if ($preferences.extensions.PSObject.Properties['pinned_extensions']) {
        $cleanExtensionsPreferences['pinned_extensions'] = @(
            $preferences.extensions.pinned_extensions |
                Where-Object { $installedIds.Contains([string]$_) }
        )
    }
}
$cleanProfile = [ordered]@{
    avatar_index = 0
    exit_type = 'Normal'
    exited_cleanly = $true
    name = 'Default'
    using_default_avatar = $true
    using_gaia_avatar = $false
}
$cleanPreferences = [ordered]@{
    browser = $cleanBrowser
    distribution = $cleanDistribution
    extensions = $cleanExtensionsPreferences
    profile = $cleanProfile
}
foreach ($section in @(
    [pscustomobject]@{
        Name = 'bookmark_bar'
        Properties = @('show_on_all_tabs', 'show_only_on_ntp')
    },
    [pscustomobject]@{
        Name = 'intl'
        Properties = @('selected_languages')
    },
    [pscustomobject]@{
        Name = 'spellcheck'
        Properties = @('dictionaries', 'dictionary')
    },
    [pscustomobject]@{
        Name = 'toolbar'
        Properties = @('pinned_cast_migration_complete',
                       'pinned_chrome_labs_migration_complete')
    }
)) {
    $sourceSection = $preferences.PSObject.Properties[$section.Name]
    if (-not $sourceSection) {
        continue
    }
    $cleanSection = [ordered]@{}
    foreach ($name in $section.Properties) {
        Add-PropertyIfPresent $cleanSection $sourceSection.Value $name
    }
    if ($cleanSection.Count -gt 0) {
        $cleanPreferences[$section.Name] = $cleanSection
    }
}
Write-Utf8Json $cleanPreferences (Join-Path $cleanDefault 'Preferences')

$cleanSettings = [ordered]@{}
$securePreferencesPath = Join-Path $sourceDefault 'Secure Preferences'
$securePreferences = Read-JsonFile -Path $securePreferencesPath `
    -Label 'Secure Preferences'
$secureSettings = $null
if ($securePreferences.PSObject.Properties['extensions'] -and
    $securePreferences.extensions.PSObject.Properties['settings']) {
    $secureSettings = $securePreferences.extensions.settings
}
foreach ($id in $installedIds) {
    if ($null -eq $secureSettings) {
        throw "Secure Preferences is missing install metadata for extension $id."
    }
    $exactEntries = @(
        $secureSettings.PSObject.Properties |
            Where-Object { $_.Name -ceq $id }
    )
    if ($exactEntries.Count -ne 1) {
        throw "Secure Preferences is missing exact install metadata for extension $id."
    }
    $entry = $exactEntries[0]
    $pathProperty = $entry.Value.PSObject.Properties['path']
    if (-not $pathProperty -or $pathProperty.Value -isnot [string]) {
        throw "Secure Preferences has no valid package path for extension $id."
    }
    $validatedPath = Get-ValidatedExtensionRelativePath `
        -ExtensionId $id -RelativePath ([string]$pathProperty.Value) `
        -SourceExtensions $sourceExtensions -CleanExtensions $cleanExtensions

    $cleanEntry = [ordered]@{}
    foreach ($name in @(
        'account_extension_type',
        'ack_external',
        'active_permissions',
        'commands',
        'creation_flags',
        'disable_reasons',
        'from_webstore',
        'granted_permissions',
        'location',
        'manifest',
        'was_installed_by_default',
        'was_installed_by_oem',
        'withholding_permissions'
    )) {
        Add-PropertyIfPresent $cleanEntry $entry.Value $name
    }
    $cleanEntry['path'] = $validatedPath
    $cleanSettings[$id] = $cleanEntry
}
$cleanSecurePreferences = [ordered]@{
    extensions = [ordered]@{
        settings = $cleanSettings
    }
}
Write-Utf8Json $cleanSecurePreferences (Join-Path $cleanDefault 'Secure Preferences')

$outputItems = @(Get-ChildItem -LiteralPath $outputPath -Force -Recurse)
if ($outputItems.Count -gt $maximumOutputItems) {
    throw 'The sanitized Default profile contains too many items.'
}
$outputBytes = [int64]0
foreach ($item in $outputItems) {
    Assert-NotReparsePoint $item
    if (-not $item.PSIsContainer) {
        if ($item.Length -gt ($maximumOutputBytes - $outputBytes)) {
            throw 'The sanitized Default profile exceeds the size limit.'
        }
        $outputBytes += $item.Length
    }
}

[pscustomobject]@{
    Output = $outputPath
    Items = $outputItems.Count
    Bytes = $outputBytes
    Extensions = $installedIds.Count
    BookmarkUrls = $bookmarkUrls.Count
    FaviconMappings = $faviconMappingCount
}
