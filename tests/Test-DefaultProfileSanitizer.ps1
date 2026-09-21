param(
    [string]$SanitizerPath =
        (Join-Path $PSScriptRoot '..\tools\Sanitize-DefaultProfileDirectory.ps1')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-SanitizerCheck {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Write-Utf8Json {
    param(
        [Parameter(Mandatory = $true)][object]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $json = $Value | ConvertTo-Json -Depth 100 -Compress
    [IO.File]::WriteAllText($Path, $json, (New-Object Text.UTF8Encoding($false)))
}

$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCommand) {
    throw 'Python is required only by this test to synthesize and inspect SQLite fixtures.'
}
$python = $pythonCommand.Source

function New-TestFaviconsDatabase {
    param([Parameter(Mandatory = $true)][string]$Path)

    $script = @'
import sqlite3, sys
path = sys.argv[1]
connection = sqlite3.connect(path)
connection.executescript('''
CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR);
CREATE TABLE icon_mapping(id INTEGER PRIMARY KEY,page_url LONGVARCHAR NOT NULL,icon_id INTEGER,page_url_type INTEGER DEFAULT 0);
CREATE TABLE favicons(id INTEGER PRIMARY KEY,url LONGVARCHAR NOT NULL,icon_type INTEGER DEFAULT 1);
CREATE TABLE favicon_bitmaps(id INTEGER PRIMARY KEY,icon_id INTEGER NOT NULL,last_updated INTEGER DEFAULT 0,image_data BLOB,width INTEGER DEFAULT 0,height INTEGER DEFAULT 0,last_requested INTEGER DEFAULT 0);
CREATE INDEX icon_mapping_page_url_idx ON icon_mapping(page_url);
CREATE INDEX icon_mapping_icon_id_idx ON icon_mapping(icon_id);
CREATE INDEX favicons_url ON favicons(url);
CREATE INDEX favicon_bitmaps_icon_id ON favicon_bitmaps(icon_id);
''')
connection.executemany('INSERT INTO meta(key,value) VALUES(?,?)', [
    ('mmap_status', '-1'), ('version', '9'), ('last_compatible_version', '9')])
connection.executemany('INSERT INTO icon_mapping(id,page_url,icon_id,page_url_type) VALUES(?,?,?,0)', [
    (1, 'https://example.test/', 1),
    (2, 'https://private.test/account', 1),
    (3, 'https://www.bing.com/search?q=private', 2)])
connection.executemany('INSERT INTO favicons(id,url,icon_type) VALUES(?,?,1)', [
    (1, 'https://example.test/favicon.ico'),
    (2, 'https://private.test/favicon.ico'),
    (3, 'https://orphan.test/favicon.ico')])
connection.executemany('INSERT INTO favicon_bitmaps(id,icon_id,last_updated,image_data,width,height,last_requested) VALUES(?,?,?,?,?,?,?)', [
    (1, 1, 111, b'allowed-icon', 16, 16, 222),
    (2, 2, 333, b'private-icon', 16, 16, 444),
    (3, 3, 555, b'orphan-icon', 16, 16, 666)])
connection.commit()
connection.close()
'@
    & $python -c $script $Path
    if ($LASTEXITCODE -ne 0) {
        throw 'Python could not create the Favicons test fixture.'
    }
}

function Get-TestFaviconsAudit {
    param([Parameter(Mandatory = $true)][string]$Path)

    $script = @'
import json, sqlite3, sys
path = sys.argv[1]
connection = sqlite3.connect('file:' + path.replace('\\', '/') + '?mode=ro', uri=True)
result = {
    'integrity': [row[0] for row in connection.execute('PRAGMA integrity_check')],
    'urls': [row[0] for row in connection.execute('SELECT page_url FROM icon_mapping ORDER BY id')],
    'mappings': connection.execute('SELECT COUNT(*) FROM icon_mapping').fetchone()[0],
    'icons': connection.execute('SELECT COUNT(*) FROM favicons').fetchone()[0],
    'bitmaps': connection.execute('SELECT COUNT(*) FROM favicon_bitmaps').fetchone()[0],
    'timestamped': connection.execute('SELECT COUNT(*) FROM favicon_bitmaps WHERE last_updated <> 0 OR last_requested <> 0').fetchone()[0],
    'orphan_icons': connection.execute('SELECT COUNT(*) FROM favicons WHERE NOT EXISTS (SELECT 1 FROM icon_mapping WHERE icon_mapping.icon_id=favicons.id)').fetchone()[0],
    'orphan_bitmaps': connection.execute('SELECT COUNT(*) FROM favicon_bitmaps WHERE NOT EXISTS (SELECT 1 FROM icon_mapping WHERE icon_mapping.icon_id=favicon_bitmaps.icon_id)').fetchone()[0],
    'freelist': connection.execute('PRAGMA freelist_count').fetchone()[0],
}
connection.close()
raw = open(path, 'rb').read()
result['removed_bytes_survive'] = any(value in raw for value in (
    b'https://private.test/account', b'https://www.bing.com/search?q=private',
    b'https://private.test/favicon.ico', b'private-icon', b'orphan-icon'))
print(json.dumps(result))
'@
    $json = & $python -c $script $Path
    if ($LASTEXITCODE -ne 0) {
        throw 'Python could not inspect the sanitized Favicons test fixture.'
    }
    return $json | ConvertFrom-Json
}

$sanitizer = (Resolve-Path -LiteralPath $SanitizerPath).Path
$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('ctSpaces-default-sanitizer-' + [guid]::NewGuid().ToString('N'))
$sourceRoot = Join-Path $testRoot 'Source'
$sourceDefault = Join-Path $sourceRoot 'Default'
$outputRoot = Join-Path $testRoot 'Sanitized'
$sitesRoot = Join-Path $sourceRoot 'Sites\ExistingClient'
$validExtensionId = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
$invalidExtensionId = 'qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq'

try {
    New-Item -ItemType Directory -Path $sourceDefault -Force | Out-Null
    New-Item -ItemType Directory -Path $sitesRoot -Force | Out-Null
    New-Item -ItemType Directory `
        -Path (Join-Path $sourceDefault "Extensions\$validExtensionId\1.0_0") `
        -Force | Out-Null
    New-Item -ItemType Directory `
        -Path (Join-Path $sourceDefault "Extensions\$invalidExtensionId\1.0_0") `
        -Force | Out-Null

    $bookmarkJson =
        '{"roots":{"bookmark_bar":{"children":[{"name":"Starter","url":"https://example.test/"}]}}}'
    [IO.File]::WriteAllText(
        (Join-Path $sourceDefault 'Bookmarks'),
        $bookmarkJson,
        (New-Object Text.UTF8Encoding($false)))
    Set-Content -LiteralPath (Join-Path $sourceDefault 'Bookmarks.bak') `
                -Value 'private bookmark backup'
    New-TestFaviconsDatabase (Join-Path $sourceDefault 'Favicons')
    Set-Content -LiteralPath (Join-Path $sitesRoot 'sentinel.txt') `
                -Value 'existing-client-data'
    Set-Content -LiteralPath `
        (Join-Path $sourceDefault "Extensions\$validExtensionId\1.0_0\manifest.json") `
        -Value '{"name":"Starter Extension","version":"1.0"}'
    Set-Content -LiteralPath `
        (Join-Path $sourceDefault "Extensions\$invalidExtensionId\1.0_0\manifest.json") `
        -Value '{"name":"Invalid Extension","version":"1.0"}'

    foreach ($relativePath in @(
        'History',
        'Cookies',
        'Login Data',
        'Web Data',
        'Shortcuts',
        'Sessions\Session_1',
        'Session Storage\CURRENT',
        'Local Storage\leveldb\CURRENT',
        'WebStorage\QuotaManager',
        'IndexedDB\https_example.test_0.indexeddb.leveldb\CURRENT',
        'Network\Cookies',
        'Cache\Cache_Data\data_0',
        'Code Cache\js\index',
        'GPUCache\data_0',
        'Local Extension Settings\aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\CURRENT'
    )) {
        $path = Join-Path $sourceDefault $relativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) `
                 -Force | Out-Null
        Set-Content -LiteralPath $path -Value "private:$relativePath"
    }
    Set-Content -LiteralPath (Join-Path $sourceRoot 'Local State') `
                -Value 'account and machine state'

    $preferences = [ordered]@{
        account_info = @([ordered]@{ email = 'private@example.test' })
        signin = [ordered]@{ allowed = $true }
        sync = [ordered]@{ requested = $true }
        gaia_cookie = [ordered]@{ hash = 'private' }
        password_manager = [ordered]@{ leak_detection = $true }
        autofill = [ordered]@{ enabled = $true }
        bookmark_bar = [ordered]@{
            show_on_all_tabs = $true
            show_only_on_ntp = $false
            private_future_field = 'drop-me'
        }
        intl = [ordered]@{
            selected_languages = 'en-US,en'
            future_locale_state = 'drop-me'
        }
        spellcheck = [ordered]@{
            dictionaries = @('en-US')
            dictionary = ''
            private_future_field = 'drop-me'
        }
        toolbar = [ordered]@{
            pinned_cast_migration_complete = $true
            pinned_chrome_labs_migration_complete = $true
            future_state = 'drop-me'
        }
        extensions = [ordered]@{
            commands = [ordered]@{
                'windows:Alt+0' = [ordered]@{
                    command_name = 'open'
                    extension = $validExtensionId
                    global = $false
                    runtime_state = 'drop-me'
                }
                'windows:Alt+1' = [ordered]@{
                    command_name = 'bad'
                    extension = $invalidExtensionId
                    global = $false
                }
            }
            ui = [ordered]@{
                allow_chrome_webstore = $true
                future_private_state = 'drop-me'
            }
            pinned_extensions = @($validExtensionId, $invalidExtensionId)
        }
    }
    Write-Utf8Json $preferences (Join-Path $sourceDefault 'Preferences')

    $validSecureEntry = [ordered]@{
        account_extension_type = 0
        active_permissions = [ordered]@{ api = @('storage') }
        commands = [ordered]@{ open = [ordered]@{ was_assigned = $true } }
        creation_flags = 9
        disable_reasons = @()
        from_webstore = $true
        granted_permissions = [ordered]@{ api = @('storage') }
        location = 1
        manifest = [ordered]@{
            name = 'Starter Extension'
            version = '1.0'
            manifest_version = 3
        }
        path = "$validExtensionId\1.0_0"
        was_installed_by_default = $false
        was_installed_by_oem = $false
        withholding_permissions = $false
        content_settings = @([ordered]@{ primary_pattern = 'https://private.test' })
        incognito_content_settings = @('drop-me')
        incognito_preferences = [ordered]@{ site = 'private.test' }
        preferences = [ordered]@{ account = 'private' }
        regular_only_preferences = [ordered]@{ site = 'private.test' }
        events = @('drop-me')
        serviceworkerevents = @('drop-me')
        service_worker_registration_info = [ordered]@{ version = '1.0' }
        first_install_time = '123456789'
        last_update_time = '123456790'
        lastpingday = '123456791'
    }
    $securePreferences = [ordered]@{
        account_state = 'drop-me'
        extensions = [ordered]@{
            settings = [ordered]@{
                $validExtensionId = $validSecureEntry
                $invalidExtensionId = [ordered]@{
                    path = "$invalidExtensionId\1.0_0"
                    manifest = [ordered]@{ name = 'Invalid Extension' }
                }
            }
        }
    }
    $securePreferencesPath = Join-Path $sourceDefault 'Secure Preferences'
    Write-Utf8Json $securePreferences $securePreferencesPath

    $result = & $sanitizer -SourceProfile $sourceRoot -OutputRoot $outputRoot
    Assert-SanitizerCheck ($result.Extensions -eq 1) `
        'The sanitizer did not report exactly one valid extension.'
    Assert-SanitizerCheck ($result.BookmarkUrls -eq 1 -and
                           $result.FaviconMappings -eq 1) `
        'The sanitizer did not retain exactly the bookmarked favicon mapping.'

    $cleanDefault = Join-Path $outputRoot 'Default'
    foreach ($requiredPath in @(
        'Bookmarks',
        'Favicons',
        'Preferences',
        'Secure Preferences',
        "Extensions\$validExtensionId\1.0_0\manifest.json"
    )) {
        Assert-SanitizerCheck `
            (Test-Path -LiteralPath (Join-Path $cleanDefault $requiredPath)) `
            "Required starter content is missing: $requiredPath"
    }
    foreach ($forbiddenPath in @(
        'Bookmarks.bak', 'Favicons-journal', 'Favicons-wal', 'Favicons-shm',
        'History', 'Cookies', 'Login Data',
        'Web Data', 'Shortcuts', 'Sessions', 'Session Storage',
        'Local Storage', 'WebStorage', 'IndexedDB', 'Network', 'Cache',
        'Code Cache', 'GPUCache', 'Local Extension Settings',
        "Extensions\$invalidExtensionId"
    )) {
        Assert-SanitizerCheck `
            (-not (Test-Path -LiteralPath (Join-Path $cleanDefault $forbiddenPath))) `
            "Private or derived browser data was copied: $forbiddenPath"
    }
    Assert-SanitizerCheck `
        (-not (Test-Path -LiteralPath (Join-Path $outputRoot 'Local State'))) `
        'Local State was copied into the sanitized starter.'

    $allowedOutputPatterns = @(
        '^Default$',
        '^Default/(Bookmarks|Favicons|Preferences|Secure Preferences)$',
        '^Default/Extensions$',
        "^Default/Extensions/$validExtensionId(/.*)?$"
    )
    foreach ($item in Get-ChildItem -LiteralPath $outputRoot -Force -Recurse) {
        $relative = $item.FullName.Substring($outputRoot.Length).TrimStart('\', '/') `
            -replace '\\', '/'
        Assert-SanitizerCheck `
            (@($allowedOutputPatterns | Where-Object { $relative -match $_ }).Count -gt 0) `
            "Unexpected sanitized output path: $relative"
    }

    $faviconAudit = Get-TestFaviconsAudit (Join-Path $cleanDefault 'Favicons')
    Assert-SanitizerCheck `
        (@($faviconAudit.integrity).Count -eq 1 -and
         @($faviconAudit.integrity)[0] -ceq 'ok') `
        'Sanitized Favicons did not pass SQLite integrity_check.'
    Assert-SanitizerCheck `
        (@($faviconAudit.urls).Count -eq 1 -and
         @($faviconAudit.urls)[0] -ceq 'https://example.test/') `
        'Sanitized Favicons contains a URL outside the exact Bookmarks set.'
    Assert-SanitizerCheck `
        ($faviconAudit.mappings -eq 1 -and $faviconAudit.icons -eq 1 -and
         $faviconAudit.bitmaps -eq 1 -and $faviconAudit.orphan_icons -eq 0 -and
         $faviconAudit.orphan_bitmaps -eq 0) `
        'Sanitized Favicons did not remove non-bookmark mappings and orphan data.'
    Assert-SanitizerCheck `
        ($faviconAudit.timestamped -eq 0 -and $faviconAudit.freelist -eq 0) `
        'Sanitized Favicons retained timestamps or freelist pages.'
    Assert-SanitizerCheck (-not $faviconAudit.removed_bytes_survive) `
        'Removed browsing URLs or icon bytes still survive in Favicons.'

    $cleanPreferences = Get-Content -Raw `
        -LiteralPath (Join-Path $cleanDefault 'Preferences') | ConvertFrom-Json
    foreach ($privateKey in @(
        'account_info', 'signin', 'sync', 'gaia_cookie', 'password_manager',
        'autofill'
    )) {
        Assert-SanitizerCheck `
            (-not $cleanPreferences.PSObject.Properties[$privateKey]) `
            "Private Preferences key survived: $privateKey"
    }
    Assert-SanitizerCheck `
        ($cleanPreferences.bookmark_bar.show_on_all_tabs -eq $true) `
        'The deliberate bookmark-bar configuration was not preserved.'
    Assert-SanitizerCheck `
        (-not $cleanPreferences.bookmark_bar.PSObject.Properties['private_future_field']) `
        'An unapproved bookmark-bar field survived.'
    Assert-SanitizerCheck `
        ($cleanPreferences.extensions.pinned_extensions.Count -eq 1 -and
         $cleanPreferences.extensions.pinned_extensions[0] -eq $validExtensionId) `
        'Pinned extensions were not filtered to copied extension IDs.'
    Assert-SanitizerCheck `
        (-not $cleanPreferences.extensions.commands.PSObject.Properties['windows:Alt+1']) `
        'A command for an excluded extension survived.'
    Assert-SanitizerCheck `
        (-not $cleanPreferences.extensions.commands.'windows:Alt+0'.PSObject.Properties['runtime_state']) `
        'An unapproved extension-command field survived.'

    $cleanSecure = Get-Content -Raw `
        -LiteralPath (Join-Path $cleanDefault 'Secure Preferences') |
        ConvertFrom-Json
    $secureIds = @($cleanSecure.extensions.settings.PSObject.Properties.Name)
    Assert-SanitizerCheck `
        ($secureIds.Count -eq 1 -and $secureIds[0] -eq $validExtensionId) `
        'Secure Preferences was not filtered to the copied extension set.'
    $cleanSecureEntry = $cleanSecure.extensions.settings.$validExtensionId
    foreach ($runtimeKey in @(
        'content_settings', 'incognito_content_settings',
        'incognito_preferences', 'preferences', 'regular_only_preferences',
        'events', 'serviceworkerevents', 'service_worker_registration_info',
        'first_install_time', 'last_update_time', 'lastpingday'
    )) {
        Assert-SanitizerCheck `
            (-not $cleanSecureEntry.PSObject.Properties[$runtimeKey]) `
            "Extension runtime field survived: $runtimeKey"
    }
    Assert-SanitizerCheck `
        ($cleanSecureEntry.manifest.name -eq 'Starter Extension' -and
         $cleanSecureEntry.path -eq "$validExtensionId\1.0_0") `
        'Required extension install metadata was not preserved.'

    $validSecureEntry['path'] = "$validExtensionId\..\$invalidExtensionId\1.0_0"
    Write-Utf8Json $securePreferences $securePreferencesPath
    $escapingPathRejected = $false
    try {
        & $sanitizer -SourceProfile $sourceRoot `
            -OutputRoot (Join-Path $testRoot 'EscapingPathOutput') | Out-Null
    }
    catch {
        $escapingPathRejected = $_.Exception.Message -match
            'package path|outside|escaping|mismatched'
    }
    Assert-SanitizerCheck $escapingPathRejected `
        'Secure Preferences accepted an extension path outside the exact copied package.'
    $validSecureEntry['path'] = "$validExtensionId\1.0_0"
    Write-Utf8Json $securePreferences $securePreferencesPath

    Assert-SanitizerCheck `
        ((Get-Content -Raw -LiteralPath (Join-Path $sitesRoot 'sentinel.txt')).Trim() `
            -eq 'existing-client-data') `
        'The sanitizer modified an existing Sites client.'
    Assert-SanitizerCheck `
        (Test-Path -LiteralPath (Join-Path $sourceDefault 'Network\Cookies')) `
        'The sanitizer modified its source browser profile.'

    [IO.File]::WriteAllText(
        (Join-Path $sourceDefault 'Bookmarks'),
        '{"roots":{"bookmark_bar":{"children":[]}}}',
        (New-Object Text.UTF8Encoding($false)))
    $emptyOutput = Join-Path $testRoot 'EmptyBookmarksOutput'
    $emptyResult = & $sanitizer -SourceProfile $sourceRoot `
        -OutputRoot $emptyOutput
    $emptyAudit = Get-TestFaviconsAudit `
        (Join-Path $emptyOutput 'Default\Favicons')
    Assert-SanitizerCheck `
        ($emptyResult.BookmarkUrls -eq 0 -and
         $emptyResult.FaviconMappings -eq 0 -and
         $emptyAudit.mappings -eq 0 -and $emptyAudit.icons -eq 0 -and
         $emptyAudit.bitmaps -eq 0 -and
         @($emptyAudit.integrity)[0] -ceq 'ok') `
        'Empty Bookmarks did not produce a valid empty Favicons database.'
    [IO.File]::WriteAllText(
        (Join-Path $sourceDefault 'Bookmarks'), $bookmarkJson,
        (New-Object Text.UTF8Encoding($false)))

    $sourceFavicons = Join-Path $sourceDefault 'Favicons'
    $faviconsBackup = Join-Path $testRoot 'Favicons.good'
    [IO.File]::Copy($sourceFavicons, $faviconsBackup)
    try {
        [IO.File]::WriteAllText(
            $sourceFavicons, 'not a SQLite database',
            (New-Object Text.UTF8Encoding($false)))
        $corruptRejected = $false
        try {
            & $sanitizer -SourceProfile $sourceRoot `
                -OutputRoot (Join-Path $testRoot 'CorruptOutput') | Out-Null
        }
        catch {
            $corruptRejected = $_.Exception.Message -match 'Favicons|SQLite'
        }
        Assert-SanitizerCheck $corruptRejected `
            'An existing Favicons database that could not be proven safe was accepted.'
    }
    finally {
        [IO.File]::Copy($faviconsBackup, $sourceFavicons, $true)
        [IO.File]::Delete($faviconsBackup)
    }

    $activeSidecar = $sourceFavicons + '-wal'
    [IO.File]::WriteAllBytes($activeSidecar, [byte[]](1, 2, 3, 4))
    try {
        $sidecarRejected = $false
        try {
            & $sanitizer -SourceProfile $sourceRoot `
                -OutputRoot (Join-Path $testRoot 'SidecarOutput') | Out-Null
        }
        catch {
            $sidecarRejected = $_.Exception.Message -match 'sidecar|Close the browser'
        }
        Assert-SanitizerCheck $sidecarRejected `
            'An active Favicons SQLite sidecar was accepted.'
    }
    finally {
        [IO.File]::Delete($activeSidecar)
    }

    $missingFavicons = Join-Path $testRoot 'Favicons.moved'
    [IO.File]::Move($sourceFavicons, $missingFavicons)
    try {
        $missingRejected = $false
        try {
            & $sanitizer -SourceProfile $sourceRoot `
                -OutputRoot (Join-Path $testRoot 'MissingFaviconsOutput') | Out-Null
        }
        catch {
            $missingRejected = $_.Exception.Message -match 'required Favicons|missing'
        }
        Assert-SanitizerCheck $missingRejected `
            'A starter profile without required Favicons was accepted.'
    }
    finally {
        [IO.File]::Move($missingFavicons, $sourceFavicons)
    }

    $oversizedOutput = Join-Path $testRoot 'OversizedOutput'
    $bookmarkStream = [IO.File]::Open(
        (Join-Path $sourceDefault 'Bookmarks'),
        [IO.FileMode]::Open,
        [IO.FileAccess]::Write,
        [IO.FileShare]::None)
    try {
        $bookmarkStream.SetLength(64MB + 1)
    }
    finally {
        $bookmarkStream.Dispose()
    }
    $oversizedRejected = $false
    try {
        & $sanitizer -SourceProfile $sourceRoot -OutputRoot $oversizedOutput |
            Out-Null
    }
    catch {
        $oversizedRejected = $_.Exception.Message -match 'Bookmarks.*limit'
    }
    Assert-SanitizerCheck $oversizedRejected `
        'An oversized Bookmarks file was not rejected before copying.'

    Write-Host 'Default profile sanitizer checks passed.'
}
finally {
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $tempPrefix = $tempRoot + [IO.Path]::DirectorySeparatorChar
    if ($resolvedTestRoot.StartsWith(
            $tempPrefix, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTestRoot)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
