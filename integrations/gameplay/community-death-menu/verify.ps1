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
    'cancelled selection returns focus','confirmed role deactivates only owned role widget','unrelated modal keeps focus',
    'possession after early menu close restores all input','early native close waits for the acknowledged role',
    'missing selection layout and input survive possession','stock death selection restores a late input lock only once',
    'new modal after deployment retains its input','foreign pawn never receives redeploy focus',
    'confirmed deployment waits for inactive before its single restart','pending death restart cannot cross a role change',
    'unacknowledged client restart is not issued twice','blocked restart never follows another death',
    'native death menu can open confirm and close between samples','same frame duplicate deployment of the previous role restarts only once',
    'rapid choices wait for the last confirmed role','transient modal before possession does not consume a confirmed deployment',
    'repeat escape on an empty stack does not open a second unconfirmed selection',
    'nested widget callbacks cannot consume deployment before confirm returns','cancel of a reopened native page preserves the earlier confirmed deployment',
    'role acknowledgement and the actually possessed role must both match',
    'fresh Escape on the first sampled death with an empty stack opens once',
    'new Leave on first death sample still waits for native permission',
    'hidden inactive owned previous option does not block next death',
    'fresh death key reuses a verified previous Leave after native menu reopens',
    'the verified stale Leave is recognized after focus already returned',
    'cancel after birth asks the server once about the still living previously owned pawn',
    'actual bound Cancel and natural control prime the very first next death key',
    'working control behind a returned native Leave does not keep deployment pending',
    'bound stock Cancel callback reaches the possession check without a separate native event',
    'the same returned Leave retains its widget while the server checks the missing pawn',
    'nested bound and native Cancel still produce only one request',
    'a sampled dead release permits the native down-only rise to reopen only the verified Leave',
    'confirmed repeat behind the twice returned original Leave closes once and permits a fourth death key',
    'locked repeat waits for real local control before retiring its page without taking focus',
    'a different next prompt or a held fourth death key keeps the original UI after successful close',
    'confirmed different role behind the same returned Leave closes for its real new pawn and preserves the next death key',
    'sustained input lock behind verified returned Leave closes page and restores controls once',
    'changed prompt or a fresh menu key during the lock cancels recovery',
    'a real modal opened between closing a stuck page and restoring controls retains focus',
    'a dropped native close retries for the live matching role and returns viewport controls',
    'a second dropped close and duplicate confirmation never cause a third request',
    'an intervening modal or a foreign first person view cannot retry a lost close',
    'persistent input lock after real Cancel and returned Leave restores the witnessed living pawn',
    'a different modal or a fresh menu key revokes post-Cancel focus recovery'
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
    $cancelled = [regex]::Match($unit,'BrtDeathMenu::OptionClicked\(sender, option\);').Value
    $dispatch = [IO.File]::ReadAllText($(if($kind -eq 'candidate'){$hook}else{Join-Path $CommunityRoot $hookRel}))
    $popupGate = [regex]::Match($dispatch,'(?s)if\s*\(Parms\s*&&\s*(?<rule>[^;{}]*?)\)\s*BrtOptionClicked\(Object,')
    $popupRule = if($popupGate.Success){$popupGate.Groups['rule'].Value.Replace('EventInfo.FullName','fullName')}else{'false'}
    $roleAction = [regex]::Match($dispatch,'BrtDeathMenu::NativeRoleAction roleAction\(roleClick \|\| roleConfirm\);').Value
    $finishAction = [regex]::Match($dispatch,'roleAction\.Finish\(\);').Value
    if($kind -eq 'candidate' -and (-not $confirmed -or -not $cancelled -or -not $roleAction -or -not $finishAction -or -not $popupGate.Success -or
        ($dispatch.IndexOf('BrtOptionClicked(Object,') -gt $dispatch.IndexOf('ProcessEventClient.call(Object, Function, Parms);')) -or
        ($dispatch.IndexOf('BrtRoleConfirmed(Object,') -gt $dispatch.IndexOf('ProcessEventClient.call(Object, Function, Parms);')))){throw 'Role dispatch order changed'}
    if($kind -eq 'candidate' -and (-not $tick -or -not $travel -or -not $header)){throw 'Missing gameplay entry points'}
    if($kind -eq 'original' -and ($tick -or $travel -or $header)){throw 'Source is already patched'}
    $bridge = @"
$header
inline void EntryTravel(SDK::UWorld* world,SDK::UPBLocalPlayer* localPlayer,const std::string* connectTarget){$travel}
inline void EntryTick(SDK::UWorld* world,SDK::UPBLocalPlayer* localPlayer){$tick}
inline void EntryConfirm(SDK::UObject* sender,const SDK::FName& role){$confirmed}
inline void EntryCancel(SDK::UObject* sender,int option){$cancelled}
inline void EntryPopupEvent(SDK::UObject* sender,std::string_view fullName,const int* index){if(index && ($popupRule)){int option=*index; (void)option; $cancelled}}
inline void EntryRoleAction(const std::function<void()>& original){constexpr bool roleClick=true, roleConfirm=false; $roleAction original(); $finishAction}
"@
    [IO.File]::WriteAllText((Join-Path $d 'death-menu-entry.inc'),$bridge,$utf8)
    $exe = Join-Path $d 'death-menu-test.exe'
    # MSVC 查找引号 include 时也会搜索外层源文件目录。隔离 fixture，免得读到维护树里的其他版本。
    $fixtureCopy = Join-Path $d 'death-menu.test.cpp'
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'death-menu.test.cpp') -Destination $fixtureCopy
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /W4 /wd4100 ("/I"+$d) ("/I"+$dest) ("/Fo"+$d+"/test.obj") ("/Fe:"+$exe) $fixtureCopy
    if($LASTEXITCODE){throw "Cannot compile the $kind test program"}
    $lines = @(& $exe)
    $code = $LASTEXITCODE
    [IO.File]::WriteAllLines((Join-Path $d 'tests.log'),[string[]]$lines,$utf8)
    $passed = @($lines -match '^PASS ')
    $failed = @($lines -match '^FAIL ' | ForEach-Object {$_.Substring(5)})
    if($passed.Count+$failed.Count -ne 157 -or $lines.Count -ne 157){throw "Incomplete $kind results"}
    if($kind -eq 'original'){
        if($code -ne 1 -or (Compare-Object $expectedBaseline $failed)){throw "Unexpected original results in $d"}
    }elseif($code -ne 0 -or $failed.Count){throw "Candidate failures in $d"}
    $results[$kind] = [ordered]@{passed=$passed.Count;failed=$failed;fullSourceCompiled=$true;sourceSHA256=(Get-FileHash -LiteralPath $file).Hash;exe=$exe}
}
$receipt = [ordered]@{source=$sourceRel;sourcePatch=(Get-FileHash -LiteralPath $patch).Hash;logic=(Get-FileHash -LiteralPath (Join-Path $dest 'DeathMenu.hpp')).Hash;adapter=(Get-FileHash -LiteralPath (Join-Path $dest 'DeathMenu.sdk.hpp')).Hash;reversible=$true;results=$results}
[IO.File]::WriteAllText((Join-Path $run 'receipt.json'),($receipt|ConvertTo-Json -Depth 7),$utf8)
Write-Output ("Original {0}/157, candidate {1}/157. {2}" -f $results.original.passed,$results.candidate.passed,$run)
