param([Parameter(Mandatory)][string]$CommunityRoot,[Parameter(Mandatory)][string]$OutDir)
$ErrorActionPreference='Stop'
$src=(Resolve-Path -LiteralPath $CommunityRoot).Path
$run=Join-Path ([IO.Path]::GetFullPath($OutDir)) ('death-v084-'+[guid]::NewGuid().ToString('N'))
$copy=Join-Path $run 'source'
$files=@('Payload/Utility/UIFix.cpp','Payload/ClientLogic/ClientLogic.cpp','Payload/Payload.vcxproj')
foreach($file in $files){
    $out=Join-Path $copy $file
    New-Item -Path (Split-Path -Parent $out) -ItemType Directory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $src $file) -Destination $out
}
& git -C $copy init -q
foreach($name in 'v084.patch','sdk-v084.patch'){
    $p=Join-Path $PSScriptRoot $name
    & git -C $copy apply --check $p
    if($LASTEXITCODE){throw "Cannot apply $name"}
    & git -C $copy apply $p
    if($LASTEXITCODE){throw "Patch failed: $name"}
    & git -C $copy apply --reverse --check $p
    if($LASTEXITCODE){throw "Cannot reverse $name"}
}
$utility=Join-Path $copy 'Payload/Utility'
foreach($name in 'DeathMenu.hpp','DeathMenu.sdk.hpp','DeathMenuTravel.hpp','DeathMenu.v084.hpp'){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $utility
}
if(-not(Get-Command cl.exe -ErrorAction SilentlyContinue)){
    $dev=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if(-not $dev){throw 'Visual Studio C++ Build Tools required'}
    & $dev.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$utf8=[Text.UTF8Encoding]::new($false)
$cases=64
$results=[ordered]@{}
foreach($kind in 'original','candidate'){
    $dir=Join-Path $run $kind
    New-Item -Path $dir -ItemType Directory | Out-Null
    $tree=if($kind -eq 'original'){$src}else{$copy}
    foreach($file in $files[0..1]){
        $include=Split-Path -Parent (Join-Path $src $file)
        $obj=Join-Path $dir ([IO.Path]::GetFileNameWithoutExtension($file)+'.obj')
        & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /bigobj /W0 /c ("/I"+$include) ("/Fo"+$obj) (Join-Path $tree $file)
        if($LASTEXITCODE){throw "Full $kind $file failed to compile"}
    }
    $ui=[IO.File]::ReadAllText((Join-Path $tree $files[0]))
    $logic=[IO.File]::ReadAllText((Join-Path $tree $files[1]))
    $tick=[regex]::Match($ui,'(?m)^[ ]+if \(Object && \(funcName == "ReceiveTick".*?\)\)\r?\n[ ]+BrtDeathMenuV084::CharacterTick\(\);').Value
    $include=[regex]::Match($ui,'(?m)^#include "DeathMenu\.v084\.hpp"').Value
    $begin=[regex]::Matches($logic,'const auto receipt = BrtDeathMenuTravel::Begin\(\);')
    $end=[regex]::Matches($logic,'BrtDeathMenuTravel::Complete\(receipt, travelWorld, [A-Za-z]+, [A-Za-z]+, target\);')
    $beginText='';$endAuto='';$endReconnect='';$race=''
    if($kind -eq 'candidate'){
        if(-not $tick -or -not $include -or $begin.Count -ne 2 -or $end.Count -ne 2 -or
            $ui.IndexOf($tick) -gt $ui.IndexOf('if (!UIFixDebugEnabled)')){throw 'Travel or UI entry missing'}
        $beginText=$begin[0].Value
        $endReconnect=$end[0].Value
        $endAuto=$end[1].Value
        $race=@'
    auto old=BrtDeathMenuTravel::Begin();
    auto newer=BrtDeathMenuTravel::Begin();
    BrtDeathMenuTravel::Complete(newer,world,world->OwningGameInstance,p,"192.0.2.1:7908");
    std::thread late([&]{BrtDeathMenuTravel::Complete(old,world,world->OwningGameInstance,p,"127.0.0.1:7908");});
    late.join();
'@
    }elseif($tick -or $begin.Count -or $end.Count){throw 'Original source already patched'}
    $inc=@"
$include
inline void EntryTravel(SDK::UWorld* travelWorld,SDK::UPBLocalPlayer* LP,const std::string* arg){
    auto GI=travelWorld->OwningGameInstance; const auto& target=*arg;
    std::thread worker([&]{$beginText $endAuto}); worker.join();
}
inline void EntryReconnect(SDK::UWorld* travelWorld,SDK::UPBLocalPlayer* LocalPlayer,const std::string& target){
    auto GameInstance=travelWorld->OwningGameInstance; $beginText $endReconnect
}
inline void EntryReordered(SDK::UWorld* world,SDK::UPBLocalPlayer* p){$race}
inline void EntryTick(SDK::UWorld*,SDK::UPBLocalPlayer*){
    SDK::UObject spectator; auto* Object=&spectator; std::string funcName="Function Engine.Actor.ReceiveTick";
    $tick
}
"@
    [IO.File]::WriteAllText((Join-Path $dir 'death-menu-entry.inc'),$inc,$utf8)
    $exe=Join-Path $dir 'test.exe'
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /W3 /DTEST_V084 ("/I"+$dir) ("/I"+$utility) ("/Fo"+$dir+"/test.obj") ("/Fe:"+$exe) (Join-Path $PSScriptRoot 'death-menu.test.cpp')
    if($LASTEXITCODE){throw 'Cannot compile v084 regression'}
    $lines=@(& $exe);$status=$LASTEXITCODE
    [IO.File]::WriteAllLines((Join-Path $dir 'tests.log'),[string[]]$lines,$utf8)
    $passed=@($lines -match '^PASS ');$failed=@($lines -match '^FAIL ')
    if($lines.Count -ne $cases -or $passed.Count+$failed.Count -ne $cases){throw "Incomplete $kind output"}
    if($kind -eq 'original'){
        if($status -ne 1 -or $passed.Count -ne 39){throw "Unexpected original results: $dir"}
    }elseif($status -ne 0 -or $failed.Count){throw "Candidate failed: $dir"}
    $results[$kind]=[ordered]@{passed=$passed.Count;failed=$failed;fullSourceCompiled=$true}
}
[IO.File]::WriteAllText((Join-Path $run 'receipt.json'),([ordered]@{layout='v084';reversible=$true;results=$results}|ConvertTo-Json -Depth 5),$utf8)
Write-Output ("V0.8.4 original {0}/{2}, candidate {1}/{2}. {3}" -f $results.original.passed,$results.candidate.passed,$cases,$run)
