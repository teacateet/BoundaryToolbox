param(
    [Parameter(Mandatory)][string]$CommunityRoot,
    [Parameter(Mandatory)][string]$OutDir,
    [ValidateSet('v084','split')][string]$Layout='split',
    [string]$SdkObjectDir
)
$ErrorActionPreference='Stop'
$tree=(Resolve-Path -LiteralPath $CommunityRoot).Path
$sub=if($Layout -eq 'v084'){'Payload'}else{'ProjectReboundMainDLL'}
$src=Join-Path $tree $sub
$run=Join-Path ([IO.Path]::GetFullPath($OutDir)) ('death-link-'+$Layout+'-'+[guid]::NewGuid().ToString('N'))
$copy=Join-Path $run $sub
New-Item -Path $copy -ItemType Directory -Force | Out-Null
$project=Join-Path $copy 'Payload.vcxproj'
Copy-Item -LiteralPath (Join-Path $src 'Payload.vcxproj') -Destination $project
& git -C $run init -q
& git -C $run apply (Join-Path $PSScriptRoot ('sdk-'+$Layout+'.patch'))
if($LASTEXITCODE){throw 'Project files do not match'}
$units=@('Basic','CoreUObject_functions','Engine_functions','ProjectBoundary_functions','CommonUI_functions','UMG_functions','PBMainMenuManager_BP_functions','UMG_Prompt_functions')
foreach($unit in $units){
    if(-not ([IO.File]::ReadAllText($project)).Contains(('Include="SDK\'+$unit+'.cpp"'))){throw "Not enabled in the actual project: $unit"}
}
if(-not(Get-Command cl.exe -ErrorAction SilentlyContinue)){
    $dev=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if(-not $dev){throw 'Visual Studio C++ Build Tools required'}
    & $dev.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$entry=if($Layout -eq 'v084'){'#include "DeathMenu.v084.hpp"
static auto volatile Dispatch = &BrtDeathMenuV084::CharacterTick;'}else{'#include "DeathMenu.sdk.hpp"
static auto volatile Dispatch = &BrtDeathMenu::Tick;'}
$driver='#include "SDK.hpp"
'+$entry+'
int main() { return Dispatch ? 0 : 1; }
'
$utf8=[Text.UTF8Encoding]::new($false)
$driverFile=Join-Path $run 'check.cpp'
[IO.File]::WriteAllText($driverFile,$driver,$utf8)
$flags=@('/nologo','/std:c++20','/EHsc','/MD','/utf-8','/W0','/bigobj','/Gy','/Gw','/Zc:inline','/c')
$entryObj=Join-Path $run 'entry.obj'
& cl.exe @flags ("/I"+$src) ("/I"+$PSScriptRoot) ("/Fo"+$entryObj) $driverFile
if($LASTEXITCODE){throw 'Entry object does not compile'}
$objects=@($entryObj)
foreach($unit in $units){
    $obj=Join-Path $run ($unit+'.obj')
    if($SdkObjectDir -and (Test-Path -LiteralPath (Join-Path $SdkObjectDir ($unit+'.obj')))){
        $obj=(Resolve-Path -LiteralPath (Join-Path $SdkObjectDir ($unit+'.obj'))).Path
    }else{
        & cl.exe @flags ("/I"+$src) ("/Fo"+$obj) (Join-Path $src ('SDK/'+$unit+'.cpp'))
        if($LASTEXITCODE){throw "Cannot compile $unit"}
    }
    $objects+=@($obj)
}
$exe=Join-Path $run 'sdk-ui-check.exe'
$before=@(& link.exe /NOLOGO /INCREMENTAL:NO /OPT:REF ("/OUT:"+$exe) $objects[0..($objects.Count-2)] 2>&1)
$baseline=$LASTEXITCODE
[IO.File]::WriteAllLines((Join-Path $run 'original-link.log'),[string[]]$before,$utf8)
if($baseline -eq 0 -or -not ($before -match 'BP_OnHandleBackAction')){throw 'Missing expected original link error'}
$after=@(& link.exe /NOLOGO /INCREMENTAL:NO /OPT:REF ("/OUT:"+$exe) $objects 2>&1)
$corrected=$LASTEXITCODE
[IO.File]::WriteAllLines((Join-Path $run 'candidate-link.log'),[string[]]$after,$utf8)
if($corrected){throw "Final SDK link failed in $run"}
& $exe
if($LASTEXITCODE){throw 'Expected linked entry is missing'}
$receipt=[ordered]@{layout=$Layout;originalMissing='UUMG_Prompt_C::BP_OnHandleBackAction';candidateLinked=$true;units=$units;sourcePatch=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot ('sdk-'+$Layout+'.patch'))).Hash;executable=(Get-FileHash -LiteralPath $exe).Hash}
[IO.File]::WriteAllText((Join-Path $run 'receipt.json'),($receipt|ConvertTo-Json -Depth 3),$utf8)
Write-Output ("Verified original missing prompt wrapper and complete {0} link. {1}" -f $Layout,$run)
