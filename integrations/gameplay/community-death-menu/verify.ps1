param(
    [Parameter(Mandatory)][string]$CommunityRoot,
    [Parameter(Mandatory)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
$sourceRel = 'ProjectReboundMainDLL/Client/AutoConnect.cpp'
$upstream = Join-Path (Resolve-Path -LiteralPath $CommunityRoot).Path $sourceRel
$includeDir = Split-Path -Parent $upstream
$run = Join-Path ([IO.Path]::GetFullPath($OutDir)) ('death-menu-'+[guid]::NewGuid().ToString('N'))
$copy = Join-Path $run 'source'
$candidate = Join-Path $copy $sourceRel
$dest = Split-Path -Parent $candidate
$patch = Join-Path $PSScriptRoot 'split.patch'
New-Item -ItemType Directory -Path $dest -Force | Out-Null
Copy-Item -LiteralPath $upstream -Destination $candidate
$hookRel = 'ProjectReboundMainDLL/Hooking/ClientHooks.cpp'
$hook = Join-Path $copy $hookRel
New-Item -ItemType Directory -Path (Split-Path -Parent $hook) -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $CommunityRoot $hookRel) -Destination $hook
& git -C $copy init -q
if($LASTEXITCODE){throw 'Cannot initialize the source copy'}
& git -C $copy apply --check $patch
if($LASTEXITCODE){throw 'Unexpected original source layout'}
& git -C $copy apply $patch
if($LASTEXITCODE){throw 'Cannot apply split patch'}
& git -C $copy apply --reverse --check $patch
if($LASTEXITCODE){throw 'Cannot reverse split patch'}
foreach($name in 'DeathMenu.hpp','DeathMenu.sdk.hpp'){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $dest
}
if(-not(Get-Command cl.exe -ErrorAction SilentlyContinue)){
    $dev = Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if(-not $dev){throw 'Visual Studio C++ Build Tools required'}
    & $dev.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$utf8 = [Text.UTF8Encoding]::new($false)
$expectedBaseline = @(
    'recorded Chinese Leave Match opens roles','English Leave Match opens roles',
    'second stock Chinese confirmation title','stock English command confirmation',
    'easy round opens roles','hard round opens roles','remapped active device key',
    'real gamepad action from engine table','late original prompt after a physical edge',
    'prompt initializes after appearing','deferred close opens once after original removal',
    'root without explicit owner uses local subsystem','modifier binding with required key',
    'pressed recomputes active widget without reentry','second prompt interrupts queued opening',
    'pooled widget reused for Quit interrupts opening','second intentional press interrupts deferred opening',
    'becoming alive between close and open','CanSelectRole revoked before opening',
    'death press during cooldown opens roles when allowed','two deaths each open roles only on their own press',
    'death press on an empty stack opens roles without a leave prompt','empty stack death press waits for original role permission',
    'changed role returns game focus once','same role explicit confirmation returns focus','server role acknowledgment precedes closing',
    'cancelled selection returns focus','confirmed role deactivates only owned role widget','unrelated modal keeps focus'
)
$results = [ordered]@{}
foreach($kind in 'original','candidate'){
    $d = Join-Path $run $kind
    New-Item -ItemType Directory -Path $d | Out-Null
    $file = if($kind -eq 'original'){$upstream}else{$candidate}
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /bigobj /W0 /c ("/I"+$includeDir) ("/Fo"+$d+"/full.obj") $file
    if($LASTEXITCODE){throw "Full $kind AutoConnect translation unit does not compile"}
    $unit = [IO.File]::ReadAllText($file)
    $tick = [regex]::Match($unit,'(?m)^\s*(BrtDeathMenu::Tick\(world, localPlayer\);)\r?$').Groups[1].Value
    $travel = [regex]::Match($unit,'(?m)^\s*(BrtDeathMenu::Travel\(world, localPlayer, \*connectTarget\);)\r?$').Groups[1].Value
    $header = [regex]::Match($unit,'(?m)^#include "DeathMenu\.sdk\.hpp"').Value
    $confirmed = [regex]::Match($unit,'BrtDeathMenu::Confirmed\(sender, role\);').Value
    if($kind -eq 'candidate' -and (-not $tick -or -not $travel -or -not $header)){throw 'Missing gameplay entry points'}
    if($kind -eq 'original' -and ($tick -or $travel -or $header)){throw 'Source is already patched'}
    $bridge = @"
$header
inline void EntryTravel(SDK::UWorld* world,SDK::UPBLocalPlayer* localPlayer,const std::string* connectTarget){$travel}
inline void EntryTick(SDK::UWorld* world,SDK::UPBLocalPlayer* localPlayer){$tick}
inline void EntryConfirm(SDK::UObject* sender,const SDK::FName& role){$confirmed}
"@
    [IO.File]::WriteAllText((Join-Path $d 'death-menu-entry.inc'),$bridge,$utf8)
    $exe = Join-Path $d 'death-menu-test.exe'
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /W4 /wd4100 ("/I"+$d) ("/I"+$dest) ("/Fo"+$d+"/test.obj") ("/Fe:"+$exe) (Join-Path $PSScriptRoot 'death-menu.test.cpp')
    if($LASTEXITCODE){throw "Cannot compile the $kind test program"}
    $lines = @(& $exe)
    $code = $LASTEXITCODE
    [IO.File]::WriteAllLines((Join-Path $d 'tests.log'),[string[]]$lines,$utf8)
    $passed = @($lines -match '^PASS ')
    $failed = @($lines -match '^FAIL ' | ForEach-Object {$_.Substring(5)})
    if($passed.Count+$failed.Count -ne 66 -or $lines.Count -ne 66){throw "Incomplete $kind results"}
    if($kind -eq 'original'){
        if($code -ne 1 -or (Compare-Object $expectedBaseline $failed)){throw "Unexpected original results in $d"}
    }elseif($code -ne 0 -or $failed.Count){throw "Candidate failures in $d"}
    $results[$kind] = [ordered]@{passed=$passed.Count;failed=$failed;fullSourceCompiled=$true;sourceSHA256=(Get-FileHash -LiteralPath $file).Hash;exe=$exe}
}
$receipt = [ordered]@{source=$sourceRel;sourcePatch=(Get-FileHash -LiteralPath $patch).Hash;logic=(Get-FileHash -LiteralPath (Join-Path $dest 'DeathMenu.hpp')).Hash;adapter=(Get-FileHash -LiteralPath (Join-Path $dest 'DeathMenu.sdk.hpp')).Hash;reversible=$true;results=$results}
[IO.File]::WriteAllText((Join-Path $run 'receipt.json'),($receipt|ConvertTo-Json -Depth 7),$utf8)
Write-Output ("Original {0}/66, candidate {1}/66. {2}" -f $results.original.passed,$results.candidate.passed,$run)
