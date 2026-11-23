param(
    [Parameter(Mandatory)][string]$CommunityRoot,
    [Parameter(Mandatory)][string]$OutDir,
    [ValidateSet('v084','split')][string]$Layout = 'v084'
)
$ErrorActionPreference = 'Stop'
$CommunityRoot = (Resolve-Path -LiteralPath $CommunityRoot).Path
$rel = if ($Layout -eq 'v084') { 'Payload/Utility/LauncherFix.cpp' } else { 'ProjectReboundMainDLL/Client/SideMountFixClient.cpp' }
$upstream = Join-Path $CommunityRoot $rel
$include = Split-Path -Parent $upstream
$patch = Join-Path $PSScriptRoot ($Layout + '.patch')
$test = Join-Path $PSScriptRoot 'callback.test.cpp'
$OutDir = [IO.Path]::GetFullPath($OutDir)
$run = Join-Path $OutDir ('presentation-' + [Guid]::NewGuid().ToString('N'))
$copy = Join-Path $run 'source'
$candidate = Join-Path $copy $rel
New-Item -ItemType Directory -Path (Split-Path -Parent $candidate) -Force | Out-Null
Copy-Item -LiteralPath $upstream -Destination $candidate
& git -C $copy init -q
if ($LASTEXITCODE) { throw 'Cannot initialize isolated source copy' }
& git -C $copy apply --check $patch
if ($LASTEXITCODE) { throw 'Patch does not match this community source' }
& git -C $copy apply $patch
if ($LASTEXITCODE) { throw 'Cannot apply patch to isolated source copy' }
& git -C $copy apply --reverse --check $patch
if ($LASTEXITCODE) { throw 'Cannot reverse the candidate patch' }

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $dev = Get-ChildItem -Path 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if (-not $dev) { throw 'Visual Studio 2022 C++ Build Tools required' }
    & $dev.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
# 旧缺陷的独立动作。其余行为必须在修改前后都通过。
$knownFailures = @(
    'duplicate reload does not replay',
    'opposite parked launcher does not display a round',
    'returning to gun ignores stale selection',
    'first person stow before controller update',
    'dead owner does not show round',
    'removed launcher does not use stale selected pointer',
    'old player slots cannot authorize new owner',
    'no owner leaves normal state intact',
    'unrelated pawn cannot provide equipment',
    'invalid former owner cannot restore round',
    'inactive auxiliary parked in opposite slot'
)
$results = [ordered]@{}
$utf8 = [Text.UTF8Encoding]::new($false)
foreach ($case in 'original','candidate') {
    $dir = Join-Path $run $case
    New-Item -ItemType Directory -Path $dir | Out-Null
    $file = if ($case -eq 'original') { $upstream } else { $candidate }
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /bigobj /W0 /c ("/I"+$include) ("/Fo"+$dir+"/community.obj") $file
    if ($LASTEXITCODE) { throw "Cannot compile full $case translation unit with its SDK" }
    $text = [IO.File]::ReadAllText($file)
    $start = $text.IndexOf('bool HandleLauncherClientEvent(')
    if ($start -lt 0) { throw 'Missing client callback' }
    $body = $text.Substring($start)
    $end = [regex]::Match($body,'(?m)^\}\r?$')
    if (-not $end.Success) { throw 'Missing function end' }
    [IO.File]::WriteAllText((Join-Path $dir 'callback.inc'),$body.Substring(0, $end.Index+$end.Length),$utf8)
    $exe = Join-Path $dir 'callback-test.exe'
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /W4 /wd4100 ("/I"+$dir) ("/Fo"+$dir+"/test.obj") ("/Fe:"+$exe) $test
    if ($LASTEXITCODE) { throw "Cannot compile $case regression executable" }
    $checks = @(& $exe)
    $status = $LASTEXITCODE
    [IO.File]::WriteAllLines((Join-Path $dir 'result.log'),[string[]]$checks,$utf8)
    $passed = @($checks -match '^PASS ')
    $failed = @($checks -match '^FAIL ' | ForEach-Object { $_.Substring(5) })
    if ($passed.Count + $failed.Count -ne 21 -or $checks.Count -ne 21) { throw 'Expected 21 complete checks' }
    if ($case -eq 'original') {
        if ($status -ne 1 -or (Compare-Object $knownFailures $failed)) { throw 'Unexpected baseline regression results' }
    } elseif ($status -ne 0 -or $failed.Count) { throw 'Candidate has failing checks' }
    $results[$case] = [ordered]@{ exitCode=$status; passed=$passed.Count; failed=$failed.Count; failures=$failed; fullTranslationUnitCompiled=$true; sourceSHA256=(Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash; executable=$exe }
}
$receipt = [ordered]@{layout=$Layout; source=$rel; patchSHA256=(Get-FileHash -LiteralPath $patch).Hash; reversible=$true; checks=$results}
[IO.File]::WriteAllText((Join-Path $run 'results.json'),($receipt | ConvertTo-Json -Depth 8),$utf8)
Write-Output ("{0} original {1}/21, candidate {2}/21; {3}" -f $Layout,$results.original.passed,$results.candidate.passed,$run)
