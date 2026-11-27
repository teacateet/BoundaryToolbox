#requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][string]$Win64,
    [switch]$Restore
)
$ErrorActionPreference = 'Stop'
$release = '40780fd95da980868e15ba8f6af898206ea67f6d66561c3f29a7097ef7673104'
$retail = '181c49ffb522b3eb01014c84fd9d3a2a5c0b66ae80a6a6addff4bdd6f8125843'
$supported = @(
    'b6195bf1a68e6ffe005888baf22dd12f91c4c8938e69de48702160dcad4b2bc0', # live41 玩家包
    'd679ad4f972b8a974a3dafb2e23d72076b14bbfa25448e203f1f0e92a3899c05', # live42 玩家包
    'dbacaf4a67fd953c90864df4b0205edb4c58b52784a9b028bc930a54017f6711'  # live43 玩家包
)
$package = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$root = (Resolve-Path -LiteralPath $Win64).Path
$dest = Join-Path $root 'Payload.dll'
$exe = Join-Path $root 'ProjectBoundarySteam-Win64-Shipping.exe'
$backups = Join-Path $root '.brt-payload-backups'

function Assert-Plain([string]$Path, [bool]$Directory) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer -ne $Directory -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "路径不是普通文件或目录：$Path"
    }
}
function Get-Digest([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

Assert-Plain $root $true
foreach ($file in @($exe, $dest, (Join-Path $root 'dxgi.dll'))) { Assert-Plain $file $false }
if ((Get-Digest $exe) -ne $retail) { throw '仅支持 Steam《边境》Build 14530908。' }
if (Test-Path -LiteralPath $backups) { Assert-Plain $backups $true }
$current = Get-Digest $dest
if ($current -ne $release -and $current -notin $supported) { throw '现有组件不是已验证的玩家版，已保留原文件。' }
if ($Restore) {
    if ($current -ne $release) { Write-Output '已在原玩家版，无需回退。'; return }
    Assert-Plain $backups $true
    $source = $null
    $latest = [DateTimeOffset]::MinValue
    # 只恢复本次专项安装留下且哈希仍一致的原件；不从其他更新的备份猜测版本。
    foreach ($dir in Get-ChildItem -LiteralPath $backups -Directory -Force) {
        if ($dir.Name -notmatch '^\d{8}T\d{6}Z-[0-9a-f]{8}$' -or ($dir.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
        $record = Join-Path $dir.FullName 'receipt.json'
        if (!(Test-Path -LiteralPath $record -PathType Leaf)) { continue }
        Assert-Plain $record $false
        $r = Get-Content -LiteralPath $record -Raw -Encoding utf8 | ConvertFrom-Json
        if ($r.status -ne 'installed' -or $r.afterSha256 -ne $release -or $r.beforeSha256 -notin $supported -or $r.destination -ne $dest) { continue }
        # 目录名只精确到秒，末尾是随机值。用回执的完整安装时间选最近一次。
        $stamp = [DateTimeOffset]$r.installedAtUtc
        if ($stamp -gt $latest) {
            $latest = $stamp
            $source = Join-Path $dir.FullName 'Payload.dll'
            $expected = $r.beforeSha256
        }
    }
    if (!$source) { throw '未找到当前安装对应的原件，未执行回退。' }
    Assert-Plain $source $false
    if ((Get-Digest $source) -ne $expected) { throw '原件哈希与安装回执不符，未执行回退。' }
} else {
    $source = Join-Path $package 'components/gameplay/live44-radar/Payload.dll'
    Assert-Plain $source $false
    if ((Get-Digest $source) -ne $release) { throw '专项玩家组件校验失败。' }
    if ($current -eq $release) { Write-Output '雷达专项已安装，无需重复替换。'; return }
    $expected = $release
}
$helper = Join-Path $package 'integrations/gameplay/community-stack/install-payload.ps1'
if ($PSCmdlet.ShouldProcess($dest, $(if ($Restore) { '恢复雷达专项安装前的原件' } else { '安装本机雷达专项玩家组件' }))) {
    & $helper -Payload $source -Win64 $root -ExpectedInstalledHash $current -ExpectedPayloadHash $expected -Confirm:$false
}
