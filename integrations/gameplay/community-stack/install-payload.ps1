#requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][string]$Payload,
    [Parameter(Mandatory)][string]$Win64,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedInstalledHash,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedPayloadHash
)
$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$source = (Resolve-Path -LiteralPath $Payload).Path
$root = (Resolve-Path -LiteralPath $Win64).Path
$destination = Join-Path $root 'Payload.dll'
$game = Join-Path $root 'ProjectBoundarySteam-Win64-Shipping.exe'
foreach ($required in @($game, (Join-Path $root 'dxgi.dll'), $destination)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing installed component: $required" }
}
if ($source -eq $destination) { throw 'Source and installed DLL must be different files' }
if ((Get-Sha256 $source) -ne $ExpectedPayloadHash) { throw 'Candidate DLL checksum mismatch' }
if ((Get-Sha256 $destination) -ne $ExpectedInstalledHash) {
    throw 'Installed DLL changed; refusing to overwrite another change'
}

# 只接受完整的 x64 DLL，避免把上一轮的静态库当成运行组件。
$bytes = [IO.File]::ReadAllBytes($source)
if ($bytes.Length -lt 512 -or [BitConverter]::ToUInt16($bytes, 0) -ne 0x5A4D) { throw 'Candidate is not a PE image' }
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
if ($pe -lt 0x40 -or $pe -gt $bytes.Length - 26 -or
    [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or
    [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664 -or
    [BitConverter]::ToUInt16($bytes, $pe + 24) -ne 0x20B -or
    !([BitConverter]::ToUInt16($bytes, $pe + 22) -band 0x2000)) {
    throw 'Candidate must be a linked x64 PE32+ DLL'
}

# 客户端和专服共用这一个文件；有任一进程运行就保留当前组件。
$running = @(Get-CimInstance Win32_Process -Filter "Name='ProjectBoundarySteam-Win64-Shipping.exe' OR Name='ProjectReboundServerLauncher.exe'" |
    Where-Object { !$_.ExecutablePath -or $_.ExecutablePath.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) })
if ($running.Count) { throw ('Close the game and its server before changing Payload.dll; PIDs: ' + ($running.ProcessId -join ', ')) }
if (!$PSCmdlet.ShouldProcess($destination, 'Install linked gameplay DLL with checksum-verified rollback copy')) { return }

$id = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$backupRoot = Join-Path $root ('.brt-payload-backups/' + $id)
[void][IO.Directory]::CreateDirectory($backupRoot)
$backup = Join-Path $backupRoot 'Payload.dll'
$receiptPath = Join-Path $backupRoot 'receipt.json'
$temporary = Join-Path $root ('.brt-payload-' + $id + '.dll')
$receipt = [ordered]@{
    status = 'prepared'
    installedAtUtc = [DateTime]::UtcNow.ToString('o')
    source = $source
    destination = $destination
    backup = $backup
    beforeSha256 = $ExpectedInstalledHash.ToLowerInvariant()
    afterSha256 = $ExpectedPayloadHash.ToLowerInvariant()
    runtimeAcceptance = 'pending Steam ordinary match'
}
try {
    [IO.File]::Copy($source, $temporary, $false)
    if ((Get-Sha256 $temporary) -ne $ExpectedPayloadHash -or (Get-Sha256 $destination) -ne $ExpectedInstalledHash) {
        throw 'DLL changed before replacement; installation cancelled'
    }
    $receipt | ConvertTo-Json | Set-Content -LiteralPath $receiptPath -Encoding utf8
    [IO.File]::Replace($temporary, $destination, $backup)
    if ((Get-Sha256 $destination) -ne $ExpectedPayloadHash -or (Get-Sha256 $backup) -ne $ExpectedInstalledHash) {
        throw "Replacement verification failed; preserve backup at $backup"
    }
    $receipt.status = 'installed'
    $receipt | ConvertTo-Json | Set-Content -LiteralPath $receiptPath -Encoding utf8
    [pscustomobject]$receipt
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}

# 回退时仍运行同一脚本：Payload 指向 backup，两个 Expected 哈希互换。
# 不改玩家存档、Steam 游戏程序、工具箱版本或上游安装记录。
