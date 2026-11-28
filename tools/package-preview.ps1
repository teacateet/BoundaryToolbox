#requires -Version 7.0
[CmdletBinding()]
param([string]$OutputDirectory, [string]$Executable, [string]$GameplayComponent, [string]$OfflineDirectory)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (!$Executable) { $Executable = Join-Path $root 'target/release/BRT.exe' }
$exe = Get-Item -LiteralPath $Executable
$info = $exe.VersionInfo
if ($info.ProductName -cne 'Boundary Resurrection Toolkit' -or $info.OriginalFilename -cne 'BRT.exe') { throw 'Unexpected Windows product metadata' }
$version = [version]::Parse($info.ProductVersion).ToString()
. (Join-Path $PSScriptRoot 'preview-freeze.ps1')
Assert-PreviewVersionAvailable (Join-Path $root 'target') $version $exe.FullName
$guideSource = Join-Path $root 'docs/PLAYER_GUIDE.md'
Assert-PreviewDownloadLinks $version (Get-Content -LiteralPath (Join-Path $root 'README.md') -Raw -Encoding utf8) (Get-Content -LiteralPath $guideSource -Raw -Encoding utf8)
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root 'target/player-preview' }
$out = [IO.Directory]::CreateDirectory($OutputDirectory).FullName
$zipPath = Join-Path $out "BRT-$version-preview-win64.zip"
if (Test-Path -LiteralPath $zipPath) { throw "已有同名玩家包：$zipPath。请保留它，使用新目录打包。" }
$guide = 'BRT-玩家安装指南.html'
$guidePath = Join-Path $out $guide
$body = (ConvertFrom-Markdown -LiteralPath $guideSource).Html
if ($version -eq '19.20.43') {
    # 下列联机章节来自尚未合并的上一轮草稿；候选包只展示与实装一致的房间契约。
    $lines=Get-Content -LiteralPath $guideSource -Encoding utf8
    $guideLines=$lines | Where-Object { $_ -notmatch '若手动关闭高级设置里的「无网卡模式」' }
    $network=Get-Content -Raw -LiteralPath (Join-Path $root 'docs/BRT-ROOM-DIRECTORY.md') -Encoding utf8
    $body=(ConvertFrom-Markdown -InputObject (($guideLines -join "`n")+"`n`n"+$network)).Html
}
$template = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'player-guide.template.html') -Raw -Encoding utf8
if (!$template.Contains('<!--GUIDE-->') -or !$body.Contains('<table>')) { throw 'Unexpected player guide markup' }
[IO.File]::WriteAllText($guidePath, $template.Replace('<!--GUIDE-->', $body), [Text.UTF8Encoding]::new($false))
$files = [ordered]@{'BRT.exe' = $exe.FullName; $guide = $guidePath}
if ([version]$version -ge [version]'19.20.35') {
    $label, $sha = switch ($version) {
        '19.20.35' { 'live39', '6982f63b4542429157b3a48f99bf2daa38cf7616d3dc47e1381817f221c1a30e' }
        '19.20.36' { 'live40', '0c18c5bfc9d923be2bf6162e68ddc70f014eb0a79e4eec7a37f91bef9b860941' }
        '19.20.37' { 'live41', 'b6195bf1a68e6ffe005888baf22dd12f91c4c8938e69de48702160dcad4b2bc0' }
        '19.20.38' { 'live42', 'd679ad4f972b8a974a3dafb2e23d72076b14bbfa25448e203f1f0e92a3899c05' }
        '19.20.39' { 'live43', 'dbacaf4a67fd953c90864df4b0205edb4c58b52784a9b028bc930a54017f6711' }
        '19.20.40' { 'live44-radar', '40780fd95da980868e15ba8f6af898206ea67f6d66561c3f29a7097ef7673104' }
        '19.20.41' { 'live45', 'aac377cf254d20793b165ba66213cc06caaf0dae300e22688b0c265fce9a3506' }
        '19.20.42' { 'live46', '99358bf3bf67d46095406ab72e0007b27d55529e6962f9b8eca37aa77d02222f' }
        '19.20.43' { 'live47', '97fee916b272158a83f7888b0bbd77550c337730d3be378f30d6e82b1d76c7d6' }
        default { throw 'Unpinned player component version' }
    }
    if (!$GameplayComponent -or (Get-FileHash -LiteralPath $GameplayComponent).Hash -ne $sha) {
        throw 'Player release requires the verified distributable gameplay component.'
    }
    $files["components/gameplay/$label/Payload.dll"] = (Resolve-Path -LiteralPath $GameplayComponent).Path
}
if ($version -in '19.20.41','19.20.42','19.20.43'){
    if (!$OfflineDirectory) {throw 'Use prepare-offline-deps.ps1 and specify its output directory.'}
    foreach($pin in Get-Content -LiteralPath (Join-Path $PSScriptRoot 'offline-components.sha256')){
        if ($pin -cnotmatch '^([a-f0-9]{64})  (base|server|node)\.zip$'){throw 'Invalid offline dependency pin'}
        $expected=$Matches[1];$name=$Matches[2]+'.zip';$path=Join-Path $OfflineDirectory $name
        if ((Get-FileHash -LiteralPath $path).Hash -ine $expected){throw "Incorrect offline dependency: $name"}
        $files.Add("components/offline/$name",(Resolve-Path -LiteralPath $path).Path)
    }
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
if ($version -eq '19.20.43') { $files['工具箱联机说明.md'] = Join-Path $root 'docs/BRT-ROOM-DIRECTORY.md' }
$manifest = Join-Path $out 'SHA256SUMS.txt'
$files['components/party-match/party-lobby.cjs'] = Join-Path $root 'integrations/party-match/party-lobby.cjs'
$lines = foreach ($item in $files.GetEnumerator()) { '{0}  {1}' -f (Get-FileHash -LiteralPath $item.Value).Hash.ToLowerInvariant(), $item.Key }
[IO.File]::WriteAllLines($manifest, [string[]]$lines, [Text.UTF8Encoding]::new($false))
$files['SHA256SUMS.txt'] = $manifest
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
    [IO.File]::Move($temp, $zipPath, $false)
} finally { if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) } }
Get-FileHash -LiteralPath $zipPath | Select-Object Path,Hash
