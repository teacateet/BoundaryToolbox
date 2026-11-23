# 已撤回的玩家版本不能因维护编译仍沿用同一个版本号而再次产包。
function Assert-PreviewVersionAvailable {
    param([string]$AuditRoot, [string]$Version, [string]$Executable)
    $record = Join-Path $AuditRoot "release-audit-$Version/receipt.json"
    if (!(Test-Path -LiteralPath $record -PathType Leaf)) { return }
    $audit = Get-Content -LiteralPath $record -Raw -Encoding utf8 | ConvertFrom-Json -ErrorAction Stop
    if ([string]$audit.version -cne $Version) { throw "Invalid freeze receipt for BRT $Version" }
    if ($audit.playerHandoff -eq 'withheld') {
        throw "BRT $Version 已撤下玩家验收；保留冻结包，局内验证完成后请使用新的版本号。"
    }
    if ($audit.exeSha256 -and $audit.exeSha256 -ine (Get-FileHash -Algorithm SHA256 -LiteralPath $Executable).Hash) {
        throw "BRT $Version 已冻结为另一份 EXE，不能用同一版本号覆盖。"
    }
}
