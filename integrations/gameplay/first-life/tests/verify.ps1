param(
    [string]$BaselinePointer = 'C:/BrtWork/QA/artifacts/brt-owner-selected/compile/current-run.txt',
    [string]$CandidatePointer = 'C:/BrtWork/QA/artifacts/brt-first-life-verified/current-run.txt',
    [string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('brt-first-life-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$runs = [ordered]@{
    baseline = (Get-Content -LiteralPath $BaselinePointer -Raw).Trim()
    candidate = (Get-Content -LiteralPath $CandidatePointer -Raw).Trim()
}
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $setup = Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if (!$setup) { throw 'Visual Studio 2022 x64 C++ tools required.' }
    & $setup.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$results = @()
foreach ($label in $runs.Keys) {
    $root = $runs[$label]
    $matrix = Get-Content -LiteralPath (Join-Path $root 'matrix.json') -Raw | ConvertFrom-Json
    foreach ($tag in @('main', 'v084')) {
        $version = $matrix | Where-Object tag -eq $tag
        if ($tag -eq 'main') {
            $module = 'main/ProjectReboundMainDLL'
            $stem = 'Server/LateJoin'
            $logHeader = 'Logging/LogManager.h'
        } else {
            $module = 'v084/Payload'
            $stem = 'ServerLogic/LateJoinManager'
            $logHeader = 'Debug/Debug.h'
        }
        $source = Join-Path $root "$module/$stem.cpp"
        $header = Join-Path $root "$module/$stem.h"
        if (!(Test-Path -LiteralPath $header -PathType Leaf) -and $label -eq 'baseline') {
            # 部分旧快照只存放改过的文件，从其构建清单读取原头文件。
            $header = Join-Path $version.compiledRoot "$stem.h"
        }
        $output = (New-Item -ItemType Directory -Path (Join-Path $OutDir "$label-$tag") -Force).FullName
        $sourceDir = (New-Item -ItemType Directory -Path (Join-Path $output (Split-Path $stem)) -Force).FullName
        Copy-Item -LiteralPath $source,$header -Destination $sourceDir
        Set-Content -LiteralPath (Join-Path $output 'test-manager.hpp') -Value ('#include "' + $stem + '.h"') -Encoding ascii
        # 源文件保持原样。仅将公开游戏接口、日志和外部模块换为替身。
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'engine-fixture.hpp') -Destination (Join-Path $output 'engine-fixture.hpp')
        Set-Content -LiteralPath (Join-Path $output 'SDK.hpp') -Value '#include "engine-fixture.hpp"' -Encoding ascii
        foreach ($relative in @('SDK/Engine_parameters.hpp','SDK/ProjectBoundary_parameters.hpp','BrtInventory/OwnerLogin.hpp','BrtInventory/Birth.hpp',$logHeader)) {
            $stub = Join-Path $output $relative
            New-Item -ItemType Directory -Path (Split-Path $stub) -Force | Out-Null
            Set-Content -LiteralPath $stub -Value '#pragma once' -Encoding ascii
        }
        Push-Location -LiteralPath $output
        try {
            Write-Output "$label $tag ($root)"
            & cl.exe /nologo /std:c++latest /utf-8 /EHsc "/I$output" "$stem.cpp" (Join-Path $PSScriptRoot 'first-life.test.cpp') '/Fe:first-life.exe'
            if ($LASTEXITCODE) { throw "Compile failed for $label $tag." }
            $checks = @(& .\first-life.exe $label)
            $exit = $LASTEXITCODE
            $checks | Write-Output
            if ($exit) { throw "Behavior regression failed for $label $tag." }
            $results += [pscustomobject]@{
                version = $tag; variant = $label; input = $source; sourceSha256 = (Get-FileHash -LiteralPath $source).Hash
                header = $header; headerSha256 = (Get-FileHash -LiteralPath $header).Hash; checks = $checks
            }
        } finally { Pop-Location }
    }
}
$report = Join-Path $OutDir 'first-life-results.json'
$results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $report -Encoding utf8
Write-Output "PASS first-life comparison: $report"
