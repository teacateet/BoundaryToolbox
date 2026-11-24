#requires -Version 7.0
[CmdletBinding()]
param([string]$OutputDirectory, [string]$Executable, [string]$GameplayComponent)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (!$Executable) { $Executable = Join-Path $root 'target/release/BRT.exe' }
$exe = Get-Item -LiteralPath $Executable
$info = $exe.VersionInfo
if ($info.ProductName -cne 'Boundary Resurrection Toolkit' -or $info.OriginalFilename -cne 'BRT.exe') { throw 'Unexpected Windows product metadata' }
$version = [version]::Parse($info.ProductVersion).ToString()
. (Join-Path $PSScriptRoot 'preview-freeze.ps1')
Assert-PreviewVersionAvailable (Join-Path $root 'target') $version $exe.FullName
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root 'target/player-preview' }
$out = [IO.Directory]::CreateDirectory($OutputDirectory).FullName
$guide = 'BRT-玩家安装指南.html'
$guidePath = Join-Path $out $guide
$body = (ConvertFrom-Markdown -LiteralPath (Join-Path $root 'docs/PLAYER_GUIDE.md')).Html
$template = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'player-guide.template.html') -Raw -Encoding utf8
if (!$template.Contains('<!--GUIDE-->') -or !$body.Contains('<table>')) { throw 'Unexpected player guide markup' }
[IO.File]::WriteAllText($guidePath, $template.Replace('<!--GUIDE-->', $body), [Text.UTF8Encoding]::new($false))
$files = [ordered]@{'BRT.exe' = $exe.FullName; $guide = $guidePath}
if ([version]$version -ge [version]'19.20.35') {
    $label, $sha = switch ($version) {
        '19.20.35' { 'live39', '6982f63b4542429157b3a48f99bf2daa38cf7616d3dc47e1381817f221c1a30e' }
        '19.20.36' { 'live40', '0c18c5bfc9d923be2bf6162e68ddc70f014eb0a79e4eec7a37f91bef9b860941' }
        '19.20.37' { 'live41', 'b6195bf1a68e6ffe005888baf22dd12f91c4c8938e69de48702160dcad4b2bc0' }
        default { throw 'Unpinned player component version' }
    }
    if (!$GameplayComponent -or (Get-FileHash -LiteralPath $GameplayComponent).Hash -ne $sha) {
        throw 'Player release requires the verified distributable gameplay component.'
    }
    $files["components/gameplay/$label/Payload.dll"] = (Resolve-Path -LiteralPath $GameplayComponent).Path
}
$files['components/SOURCES.txt'] = Join-Path $root 'tools/component-sources.txt'
$files['components/vnt-LICENSE.txt'] = Join-Path $root 'vendor/vnt/LICENSE'
$files['components/fixed-server/local-pve-host.cjs'] = Join-Path $root 'integrations/fixed-server/local-pve-host.cjs'
foreach ($rel in @('profile-runtime/release-084-LICENSE.txt','profile-runtime/release-084-code.json.gz','profile-runtime/prepare.cjs','profile-runtime/release-084.patch','profile-runtime/rest-role-key.patch','profile-runtime/pack-release.cjs','loadout/deploy.cjs','loadout/projection.cjs','loadout/native-role-write.patch','match-ui/role-read-case.patch','native-menu/native-menu.cjs','native-menu/playlist.patch')) {
    $files["components/$rel"] = Join-Path $root "integrations/$rel"
}
$files['components/loadout/native-equip-appearance.patch'] = Join-Path $root 'integrations/loadout/native-equip-appearance.patch'
foreach ($name in @('legacy-client.cjs','legacy-client.patch')) { $files["components/loadout/$name"] = Join-Path $root "integrations/loadout/$name" }
foreach ($name in @('probe-secondary.patch','retail-weapon-defaults.patch','retail-role-weapons.patch','retail-equipment-defaults.patch','room-spawn.patch','room-spawn.cjs','spawn-parts.cjs','receiver-main.json','role-paint-scope.cjs','role-paint-scopes.json','room-spawn-client.cjs')) {
    $files["components/loadout/$name"] = Join-Path $root "integrations/loadout/$name"
}
foreach ($name in @('session-progress.cjs','session-progress.patch','ready-room.cjs','ready-room.patch','region-catalog.patch','region-qos.patch','local-ticket.patch','menu-trace.cjs')) {
    $files["components/native-menu/$name"] = Join-Path $root "integrations/native-menu/$name"
}
$manifest = Join-Path $out 'SHA256SUMS.txt'
$lines = foreach ($item in $files.GetEnumerator()) { '{0}  {1}' -f (Get-FileHash -LiteralPath $item.Value).Hash.ToLowerInvariant(), $item.Key }
[IO.File]::WriteAllLines($manifest, [string[]]$lines, [Text.UTF8Encoding]::new($false))
$files['SHA256SUMS.txt'] = $manifest
$zipPath = Join-Path $out "BRT-$version-preview-win64.zip"
$temp = Join-Path $out ([Guid]::NewGuid().ToString('N') + '.zip')
try {
    $zip = [IO.Compression.ZipFile]::Open($temp, [IO.Compression.ZipArchiveMode]::Create)
    try { foreach ($item in $files.GetEnumerator()) { [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $item.Value, $item.Key, [IO.Compression.CompressionLevel]::Optimal) } } finally { $zip.Dispose() }
    $zip = [IO.Compression.ZipFile]::OpenRead($temp)
    try {
        if ($zip.Entries.Count -ne $files.Count) { throw 'Unexpected archive contents' }
        foreach ($item in $files.GetEnumerator()) {
            $entry = $zip.GetEntry($item.Key)
            $stream = $entry.Open()
            try { $sha = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)) } finally { $stream.Dispose() }
            if ($sha -ne (Get-FileHash -LiteralPath $item.Value).Hash) { throw "Archive checksum mismatch: $($item.Key)" }
        }
    } finally { $zip.Dispose() }
    [IO.File]::Move($temp, $zipPath, $true)
} finally { if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) } }
Get-FileHash -LiteralPath $zipPath | Select-Object Path,Hash
