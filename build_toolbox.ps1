param(
    # 默认构建 release；需要 debug 二进制时传入 -Release:$false。
    [switch]$Release = $true
)

# 保证 PowerShell 里中文构建输出可读。
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

# 当 cargo 构建失败后立即停止，避免继续复制或清理文件。
$ErrorActionPreference = "Stop"

$targetDir = if ($Release) { "release" } else { "debug" }

Push-Location $PSScriptRoot
try {
    # 在 crate 目录内构建，确保 Cargo 找到正确的 manifest。
    if ($Release) {
        cargo build --locked --release --bin BRT
    }
    else {
        cargo build --locked --bin BRT
    }
    if ($LASTEXITCODE -ne 0) { throw "Cargo 构建失败，退出码 $LASTEXITCODE" }
}
finally {
    Pop-Location
}

$builtExe = Join-Path $PSScriptRoot "target\$targetDir\BRT.exe"
if (-not (Test-Path $builtExe)) {
    throw "未找到构建产物：$builtExe"
}

Write-Host "已生成：$builtExe"
