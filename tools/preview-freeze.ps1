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

# README 的下载按钮、版本说明和随包指南必须和待打包 EXE 同版，避免玩家点进旧包。
function Assert-PreviewDownloadLinks {
    param([string]$Version, [string]$Readme, [string]$Guide)
    $releases = 'https://github.com/teacateet/BoundaryToolbox/releases'
    $download = "$releases/download/v$Version/BRT-$Version-preview-win64.zip"
    $buttons = [regex]::Matches($Readme, '(?m)^\*\*\[下载 Windows x64 玩家工具箱[^\r\n]+')
    $expectedButton = "**[下载 Windows x64 玩家工具箱（ZIP，$Version 预览版）]($download)**"
    if ($buttons.Count -ne 1 -or !$buttons[0].Value.StartsWith($expectedButton, [StringComparison]::Ordinal) -or
        !$buttons[0].Value.Contains("[版本说明与校验值]($releases/tag/v$Version)")) {
        throw "README 的玩家下载按钮或版本说明未更新为 BRT $Version。"
    }
    if (!$Guide.Contains("**Windows x64 · BRT $Version ·") -or
        !$Guide.Contains("[下载 Windows x64 玩家包]($download)")) {
        throw "玩家指南的版本或下载地址未更新为 BRT $Version。"
    }
}
