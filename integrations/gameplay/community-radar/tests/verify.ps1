param([string]$OutDir = (Join-Path ([IO.Path]::GetTempPath()) ('BrtRadar-' + [guid]::NewGuid().ToString('N'))))
$ErrorActionPreference = 'Stop'
$headers = Split-Path -Parent $PSScriptRoot
$work = (New-Item -Type Directory -Path $OutDir -Force).FullName
Copy-Item -LiteralPath (Join-Path $headers 'MarkedHeadRadar.hpp') -Destination (Join-Path $work 'MarkedHeadRadar.hpp')
Copy-Item -LiteralPath (Join-Path $headers 'UiJournal.hpp') -Destination $work
$snap = (Get-FileHash -LiteralPath (Join-Path $work 'MarkedHeadRadar.hpp') -Algorithm SHA256).Hash
$compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $compiler) {
    $setup = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\Tools\Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if (-not $setup) { throw 'Visual Studio C++ x64 tools are required' }
    & $setup.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$outcomes = [ordered]@{headerSha256=$snap;environment='stateful local HUD fixture';scenarios=@{}}
$failures = 0
Push-Location -LiteralPath $work
try {
    foreach($variant in @('original','candidate')) {
        $flags=@('/nologo','/std:c++20','/EHsc','/utf-8',"/I$work","/I$PSScriptRoot")
        if($variant -eq 'original') { $flags += '/DRADAR_BASELINE' }
        & cl.exe @flags (Join-Path $PSScriptRoot 'radar.test.cpp') "/Fo$variant.obj" "/Fe:$variant.exe"
        if ($LASTEXITCODE) { throw "$variant compilation failed" }
        $runner=Join-Path $work "$variant.exe"
        $lines=@(& $runner)
        $first=$LASTEXITCODE
        $lines += @(& $runner '--no-local-pve')
        $second=$LASTEXITCODE
        $lines | Set-Content -Encoding utf8 -LiteralPath (Join-Path $work "$variant.log")
        $fail=@($lines | Where-Object {$_ -match '^FAIL '})
        $pass=@($lines | Where-Object {$_ -match '^PASS '})
        $outcomes.scenarios[$variant]=[ordered]@{passed=$pass.Count;failed=$fail.Count;failureNames=$fail}
        Write-Output "$variant passed=$($pass.Count) failed=$($fail.Count)"
        if($variant -eq 'candidate') {
            $fail | Write-Output
            $failures=$first -bor $second
        }
    }
    [IO.File]::WriteAllText((Join-Path $work 'result.json'),($outcomes | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
    if($failures) { throw "Candidate regressions. See $work\result.json" }
    Write-Output "PASS $work\result.json"
} finally { Pop-Location }
