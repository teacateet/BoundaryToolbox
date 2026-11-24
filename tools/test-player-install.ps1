#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Archive, [Parameter(Mandatory)][string]$RetailWin64,
      [Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference = 'Stop'
$run = Join-Path ([IO.Directory]::CreateDirectory($EvidenceRoot).FullName) ([guid]::NewGuid().ToString('N'))
$player = Join-Path $run 'unpacked'
$target = Join-Path $run 'QA 游戏目录/ProjectBoundary/Binaries/Win64'
$ship = 'ProjectBoundarySteam-Win64-Shipping.exe'
$expected = '6982f63b4542429157b3a48f99bf2daa38cf7616d3dc47e1381817f221c1a30e'
function Digest([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Check([bool]$condition, [string]$message) { if (!$condition) { throw $message } }
function Install([string]$label, [bool]$success, [string]$message) {
    $stdout = Join-Path $run "$label.out"
    $stderr = Join-Path $run "$label.err"
    $proc = Start-Process -FilePath (Join-Path $player 'BRT.exe') -ArgumentList @('--install-community', ('"' + $target + '"')) -WorkingDirectory $player -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
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
Install 'fresh' $true 'live39 已就绪'
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
# 用安装程序刚下载并验证的上游包模拟旧版玩家。
$upstream = [IO.Compression.ZipFile]::OpenRead((Join-Path $player 'orbital_profile/downloads/community/rebound-0.8.4.zip'))
try { [IO.Compression.ZipFileExtensions]::ExtractToFile($upstream.GetEntry('Payload.dll'), $dll, $true) } finally { $upstream.Dispose() }
$old = Digest $dll
Check ($old -eq 'acaaaf17837ac418cf1350c586ca7c06c8d62f04348e0a9dc3e4d6c77d43e455') 'Unexpected upstream component'
Install 'upgrade' $true '已备份旧组件'
$backups = @(Get-ChildItem -LiteralPath (Join-Path $target '.brt-payload-backups') -Filter Payload.dll -Recurse -File)
Check (($backups.Count -eq 1) -and ((Digest $backups[0].FullName) -eq $old) -and ((Digest $dll) -eq $expected)) 'Upgrade or backup verification failed'
Install 'after-upgrade' $true '保持不变'
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
