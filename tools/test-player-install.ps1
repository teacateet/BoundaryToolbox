#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Archive, [Parameter(Mandatory)][string]$RetailWin64,
      [Parameter(Mandatory)][string]$EvidenceRoot, [string[]]$PreviousPlayerArchive)
$ErrorActionPreference = 'Stop'
$run = Join-Path ([IO.Directory]::CreateDirectory($EvidenceRoot).FullName) ([guid]::NewGuid().ToString('N'))
$player = Join-Path $run 'unpacked'
$target = Join-Path $run 'QA 游戏目录/ProjectBoundary/Binaries/Win64'
$ship = 'ProjectBoundarySteam-Win64-Shipping.exe'
$expected = 'e4f480c54a6f792e1b40eb7e61b642c89e7e755d6b78c448b9a2ec042465c63c'
function Digest([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Check([bool]$condition, [string]$message) { if (!$condition) { throw $message } }
function Install([string]$label, [bool]$success, [string]$message) {
    $stdout = Join-Path $run "$label.out"
    $stderr = Join-Path $run "$label.err"
    $noUpstream=@{HTTPS_PROXY='http://127.0.0.1:1';HTTP_PROXY='http://127.0.0.1:1';ALL_PROXY='http://127.0.0.1:1';NO_PROXY=''}
    $proc = Start-Process -FilePath (Join-Path $player 'BRT.exe') -ArgumentList @('--install-community', ('"' + $target + '"')) -WorkingDirectory $player -Environment $noUpstream -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (!$proc.WaitForExit(720000)) { $proc.Kill(); throw "Installer timeout: $label" }
    $output = [IO.File]::ReadAllText($stdout) + [IO.File]::ReadAllText($stderr)
    Check (($proc.ExitCode -eq 0) -eq $success) "Unexpected installer exit: $label $output"
    Check ($output.Contains($message)) "Missing installer result: $label $output"
    Write-Output "$label : $($output.Trim())"
}
[void][IO.Directory]::CreateDirectory($target)
[IO.Compression.ZipFile]::ExtractToDirectory((Resolve-Path -LiteralPath $Archive).Path, $player)
# 不操作 Steam 原目录，只复制测试所需的本体文件。
Copy-Item -LiteralPath (Join-Path $RetailWin64 $ship) -Destination (Join-Path $target $ship)
$gameHash = Digest (Join-Path $target $ship)
Check ($gameHash -eq '181c49ffb522b3eb01014c84fd9d3a2a5c0b66ae80a6a6addff4bdd6f8125843') 'Unsupported game build'
$dll = Join-Path $target 'Payload.dll'
$part = Join-Path $player 'components/offline/base.zip'
$hold = Join-Path $run 'base.zip'
Copy-Item -LiteralPath $part -Destination $hold
Remove-Item -LiteralPath $part
Install 'missing-dependency' $false '缺少离线组件 base.zip'
Check (@(Get-ChildItem -LiteralPath $target -Force).Count -eq 1) 'Missing offline component changed game directory'
[IO.File]::WriteAllText($part, 'QA corrupt offline component')
Install 'corrupt-dependency' $false '离线组件 base.zip 校验失败'
Check (@(Get-ChildItem -LiteralPath $target -Force).Count -eq 1) 'Corrupt offline component changed game directory'
Copy-Item -LiteralPath $hold -Destination $part -Force
Install 'fresh' $true 'live47 已就绪'
Check (!(Test-Path -LiteralPath (Join-Path $player 'orbital_profile/downloads/community'))) 'Offline install created a download cache'
Check ((Digest $dll) -eq $expected) 'Fresh install used the wrong component'
$server = Join-Path $target 'BoundaryMetaServer-main'
$node = Join-Path $target 'nodejs/node.exe'
Push-Location $server
try { & $node -e "for (const name of ['express','body-parser','protobufjs']) require(name); console.log(process.version)"; Check ($LASTEXITCODE -eq 0) 'Archive server dependencies unavailable' } finally { Pop-Location }
$config = Join-Path $target 'ServerLauncher/serverconfig.json'
$dataDir = Join-Path $server 'data/loadouts'
Check (!(Test-Path -LiteralPath $dataDir)) 'Package imported player data'
[void][IO.Directory]::CreateDirectory($dataDir)
$profile = Join-Path $dataDir 'qa-install.json'
[IO.File]::WriteAllText($profile, '{"qa":"keep"}')
$settings = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
$settings.serverName = 'QA preserve this room'
[IO.File]::WriteAllText($config, ($settings | ConvertTo-Json))
$profileHash = Digest $profile
$configHash = Digest $config
Install 'repeat' $true '保持不变'
Check (!(Test-Path -LiteralPath (Join-Path $target '.brt-payload-backups'))) 'Repeat created a spurious backup'
# 离线归档中的原版 DLL 与固定上游相同，用它模拟旧版玩家。
$upstream = [IO.Compression.ZipFile]::OpenRead((Join-Path $player 'components/offline/server.zip'))
try { [IO.Compression.ZipFileExtensions]::ExtractToFile($upstream.GetEntry('Payload.dll'), $dll, $true) } finally { $upstream.Dispose() }
$old = Digest $dll
Check ($old -eq 'acaaaf17837ac418cf1350c586ca7c06c8d62f04348e0a9dc3e4d6c77d43e455') 'Unexpected upstream component'
Install 'upgrade' $true '已备份旧组件'
$backups = @(Get-ChildItem -LiteralPath (Join-Path $target '.brt-payload-backups') -Filter Payload.dll -Recurse -File)
Check (($backups.Count -eq 1) -and ((Digest $backups[0].FullName) -eq $old) -and ((Digest $dll) -eq $expected)) 'Upgrade or backup verification failed'
Install 'after-upgrade' $true '保持不变'
$oldHashes = @{
    live47=@('882b0d50e80de5adcd6009153c7924d044bae9359aa0129b1d9cc5cf7ffd7967','97fee916b272158a83f7888b0bbd77550c337730d3be378f30d6e82b1d76c7d6','f5fb23005bdcb8a030fb01908e79e28d3cc76b83da55a4767467b8c9354d8720','64c18bfbc8f4b4649913d5798923bc064d1cf8dd2070dc3b4683ae40b377e4e6','4f70488812902f278091ee29ba7c1032cf1263dae1d4535eba2ddca69d762213')
    live46='99358bf3bf67d46095406ab72e0007b27d55529e6962f9b8eca37aa77d02222f'
    live45='aac377cf254d20793b165ba66213cc06caaf0dae300e22688b0c265fce9a3506'
    'live44-radar'='40780fd95da980868e15ba8f6af898206ea67f6d66561c3f29a7097ef7673104'
    live43='dbacaf4a67fd953c90864df4b0205edb4c58b52784a9b028bc930a54017f6711'
    live42='d679ad4f972b8a974a3dafb2e23d72076b14bbfa25448e203f1f0e92a3899c05'
    live41='b6195bf1a68e6ffe005888baf22dd12f91c4c8938e69de48702160dcad4b2bc0'
    live39='6982f63b4542429157b3a48f99bf2daa38cf7616d3dc47e1381817f221c1a30e'
    live40='0c18c5bfc9d923be2bf6162e68ddc70f014eb0a79e4eec7a37f91bef9b860941'
}
foreach ($previousPath in $PreviousPlayerArchive) {
    $previous = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $previousPath).Path)
    try {
        $entries = @($previous.Entries | Where-Object FullName -match '^components/gameplay/live\d+(?:-radar)?/Payload\.dll$')
        Check ($entries.Count -eq 1) 'Expected a single supported previous player component'
        $label = $entries[0].FullName.Split('/')[2]
        Check ($oldHashes.ContainsKey($label)) 'Unsupported previous player component'
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], $dll, $true)
    } finally { $previous.Dispose() }
    $from = Digest $dll
    Check ($from -in @($oldHashes[$label])) 'Previous player module digest mismatch'
    $beforeCount = $backups.Count
    Install "upgrade-from-player-$label-$($from.Substring(0,8))" $true '已备份旧组件'
    $backups = @(Get-ChildItem -LiteralPath (Join-Path $target '.brt-payload-backups') -Filter Payload.dll -Recurse -File)
    Check (($backups.Count -eq ($beforeCount + 1)) -and (@($backups | Where-Object { (Digest $_.FullName) -eq $from }).Count -eq 1) -and ((Digest $dll) -eq $expected)) 'Previous player build was not backed up and upgraded'
}
[IO.File]::WriteAllText($dll, 'qa unknown component')
$unknown = Digest $dll
Install 'unknown' $false '已保留原文件'
Check ((Digest $dll) -eq $unknown) 'An unknown component was overwritten'
Check (((Digest $profile) -eq $profileHash) -and ((Digest $config) -eq $configHash) -and ((Digest (Join-Path $target $ship)) -eq $gameHash)) 'User data, room config, or game binary changed'
$target = Join-Path $run 'unsupported/ProjectBoundary/Binaries/Win64'
[void][IO.Directory]::CreateDirectory($target)
[IO.File]::WriteAllText((Join-Path $target $ship), 'qa unsupported game build')
Install 'unsupported-game' $false 'Steam Build 14530908'
Check (@(Get-ChildItem -LiteralPath $target -Force).Count -eq 1) 'Unsupported game directory was changed'
Write-Output "PASS: $run"
