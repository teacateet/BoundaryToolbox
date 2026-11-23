param(
    [Parameter(Mandatory)][string]$CommunityRoot,
    [Parameter(Mandatory)][string]$OutDir,
    [ValidateSet('v084','split')][string]$Layout='v084'
)
$ErrorActionPreference='Stop'
$CommunityRoot=(Resolve-Path -LiteralPath $CommunityRoot).Path
$rel=if($Layout -eq 'v084'){'Payload/Utility/UIFix.cpp'}else{'ProjectReboundMainDLL/Client/UIShake.cpp'}
$upstream=Join-Path $CommunityRoot $rel
$include=Split-Path -Parent $upstream
$run=Join-Path ([IO.Path]::GetFullPath($OutDir)) ('reticle-'+[guid]::NewGuid().ToString('N'))
$copy=Join-Path $run 'source'
$candidate=Join-Path $copy $rel
$dest=Split-Path -Parent $candidate
$patch=Join-Path $PSScriptRoot ($Layout+'.patch')
New-Item -ItemType Directory -Path $dest -Force | Out-Null
Copy-Item -LiteralPath $upstream -Destination $candidate
& git -C $copy init -q
if($LASTEXITCODE){throw 'Cannot create the isolated source copy'}
& git -C $copy apply --check $patch
if($LASTEXITCODE){throw 'Source does not match the community patch'}
& git -C $copy apply $patch
if($LASTEXITCODE){throw 'Cannot apply the community patch'}
& git -C $copy apply --reverse --check $patch
if($LASTEXITCODE){throw 'Cannot reverse the community patch'}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'SparrowReticle.hpp') -Destination $dest
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../community-radar/UiJournal.hpp') -Destination $dest
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../community-pod-view/ShoulderSlots.hpp') -Destination $dest
if(-not(Get-Command cl.exe -ErrorAction SilentlyContinue)){
    $dev=Get-ChildItem -Path 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if(-not $dev){throw 'Visual Studio C++ Build Tools required'}
    & $dev.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$utf8=[Text.UTF8Encoding]::new($false)
$results=[ordered]@{}
$expected=@(
    'switch to rifle with stale selected pod','other shoulder wins selection','pending undeploy hides before hands update',
    'death event before replicated state','tick cannot reopen a dead life before replication',
    'unpossess after inventory is cleared','end play with visible reticle','local pod owns widget without explicit UMG viewer',
    'game changes authored inner opacity while parked','old widget hidden when replaced','lost slot retires retained local reticle',
    'former root restores on canvas rebuild','first event after detachment uses confirmed former local owner',
    'first frame local slot hides reticle while pod owner is pending','first death hides pending right slot reticle',
    'owner arriving after death leaves reticle hidden'
)
foreach($kind in 'original','candidate'){
    $dir=Join-Path $run $kind
    New-Item -ItemType Directory -Path $dir | Out-Null
    $file=if($kind -eq 'original'){$upstream}else{$candidate}
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /bigobj /W0 /c ("/I"+$include) ("/Fo"+$dir+"/ui.obj") $file
    if($LASTEXITCODE){throw "Full $Layout $kind UI source does not compile"}
    $unit=[IO.File]::ReadAllText($file)
    $functionStart=$unit.IndexOf('void HandleUICharacterClientEvent(')
    if($functionStart -lt 0){throw 'Missing original character UI callback'}
    $start=$unit.IndexOf('{',$functionStart)+1
    $end=$unit.IndexOf('    if (!UIFixDebugEnabled)',$start)
    if($end -lt $start){throw 'Unknown UI callback layout'}
    $entry=$unit.Substring($start,$end-$start)
    $header=[regex]::Match($unit,'(?m)^#include "SparrowReticle\.hpp"\r?$').Value
    [IO.File]::WriteAllText((Join-Path $dir 'reticle-entry.inc'),$header+"`nvoid ReticleCallback(APBCharacter* Object,const std::string& funcName){`n"+$entry+"`n}",$utf8)
    $exe=Join-Path $dir 'reticle-test.exe'
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /W4 /wd4100 ("/I"+$dir) ("/I"+$dest) ("/Fo"+$dir+"/test.obj") ("/Fe:"+$exe) (Join-Path $PSScriptRoot 'reticle.test.cpp')
    if($LASTEXITCODE){throw "Cannot compile $kind host"}
    $lines=@(& $exe)
    $code=$LASTEXITCODE
    [IO.File]::WriteAllLines((Join-Path $dir 'tests.log'),[string[]]$lines,$utf8)
    $failed=@($lines -match '^FAIL ' | ForEach-Object {$_.Substring(5)})
    $passed=@($lines -match '^PASS ')
    if($passed.Count+$failed.Count -ne 37 -or $lines.Count -ne 37){throw 'Incomplete regression results'}
    if($kind -eq 'original'){
        if($code -ne 1 -or (Compare-Object $expected $failed)){throw 'Unexpected baseline results'}
    }elseif($code -or $failed.Count){throw 'Candidate failed independent regression'}
    $results[$kind]=[ordered]@{passed=$passed.Count;failed=$failed;fullSourceCompiled=$true;sourceSHA256=(Get-FileHash -LiteralPath $file).Hash;exe=$exe}
}
$receipt=[ordered]@{layout=$Layout;source=$rel;patchSHA256=(Get-FileHash -LiteralPath $patch).Hash;headerSHA256=(Get-FileHash -LiteralPath (Join-Path $dest 'SparrowReticle.hpp')).Hash;reversible=$true;results=$results}
[IO.File]::WriteAllText((Join-Path $run 'receipt.json'),($receipt|ConvertTo-Json -Depth 7),$utf8)
Write-Output ("{0} original {1}/37, candidate {2}/37. {3}" -f $Layout,$results.original.passed,$results.candidate.passed,$run)
