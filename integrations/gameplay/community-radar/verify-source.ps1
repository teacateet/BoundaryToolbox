param(
    [Parameter(Mandatory)][string]$CommunityRoot,
    [Parameter(Mandatory)][string]$OutputDir,
    [ValidateSet('v084','split')][string]$Layout='v084',
    [string]$ProjectFile
)
$ErrorActionPreference='Stop'
$module=if($Layout -eq 'v084'){'Payload'}else{'ProjectReboundMainDLL'}
$ui=if($Layout -eq 'v084'){'Utility/UIFix.cpp'}else{'Client/UIShake.cpp'}
$original=(Resolve-Path -LiteralPath (Join-Path $CommunityRoot $module)).Path
$entry=Join-Path $original $ui
if(!$ProjectFile){$ProjectFile=Join-Path $original 'Payload.vcxproj'}
$work=Join-Path ([IO.Path]::GetFullPath($OutputDir)) ($Layout+'-'+[guid]::NewGuid().ToString('N'))
$source=Join-Path $work $module
$candidate=Join-Path $source $ui
$dest=Split-Path -Parent $candidate
New-Item -ItemType Directory -Path $dest -Force | Out-Null
Copy-Item -LiteralPath $entry -Destination $candidate
Copy-Item -LiteralPath $ProjectFile -Destination (Join-Path $source 'Payload.vcxproj')
$reticle=Join-Path (Split-Path -Parent $PSScriptRoot) 'community-reticle'
git -C $work init -q
if($LASTEXITCODE){throw 'Cannot create isolated source copy'}
foreach($part in $reticle,$PSScriptRoot){
    $patch=Join-Path $part ($Layout+'.patch')
    git -C $work apply --whitespace=nowarn $patch
    if($LASTEXITCODE){throw "Cannot apply $patch"}
    git -C $work apply --reverse --check $patch
    if($LASTEXITCODE){throw "Cannot reverse $patch"}
}
Copy-Item -LiteralPath (Join-Path $reticle 'SparrowReticle.hpp') -Destination $dest
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'MarkedHeadRadar.hpp') -Destination $dest
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'UiJournal.hpp') -Destination $dest
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../community-pod-view/ShoulderSlots.hpp') -Destination $dest
if(!(Get-Command cl.exe -ErrorAction SilentlyContinue)){
    $dev=Get-ChildItem -Path 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if(!$dev){throw 'MSVC x64 is required'}
    & $dev.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$units=@($candidate)
[xml]$manifest=Get-Content -LiteralPath (Join-Path $source 'Payload.vcxproj') -Raw
foreach($name in 'UMG_HelmetHUD_MiniMap_functions','UMG_MinimapNode_functions'){
    $expected='SDK\'+$name+'.cpp'
    if(@($manifest.Project.ItemGroup.ClCompile | Where-Object {$_.Include -eq $expected}).Count -ne 1){throw "Not exactly one build item for $expected"}
    $units+=Join-Path $original $expected
}
$objects=@()
foreach($unit in $units){
    $obj=Join-Path $work ([IO.Path]::GetFileNameWithoutExtension($unit)+'.obj')
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /bigobj /W0 /c ("/I"+(Split-Path -Parent $entry)) ("/Fo"+$obj) $unit
    if($LASTEXITCODE){throw "Unable to compile $unit"}
    $objects+=$obj
}
$symbols=& dumpbin.exe /nologo /symbols $objects[0]
if($LASTEXITCODE){throw 'Unable to read UI object symbols'}
foreach($symbol in '?K2_AddWidgetToCanvasPanel@UUMG_HelmetHUD_MiniMap_C','?UpdateNode@UUMG_MinimapNode_C'){
    if(-not ($symbols -cmatch ([regex]::Escape($symbol)))){throw "UI does not refer to $symbol"}
    $defined=& dumpbin.exe /nologo /symbols $objects[1] $objects[2]
    if(-not ($defined | Where-Object {$_ -notmatch 'UNDEF' -and $_ -cmatch ([regex]::Escape($symbol))})){throw "SDK does not define $symbol"}
}
$library=Join-Path $work 'radar-ui.lib'
& lib.exe /nologo ("/OUT:"+$library) $objects
if($LASTEXITCODE){throw 'Unable to archive UI and both widget implementations'}
$receipt=[ordered]@{layout=$Layout;sourceSHA256=(Get-FileHash -LiteralPath $entry).Hash;implementationSHA256=(Get-FileHash -LiteralPath (Join-Path $dest 'MarkedHeadRadar.hpp')).Hash;combinedWithSparrow=$true;compiledUnits=($units | ForEach-Object {[IO.Path]::GetFileName($_)});concreteWidgetSymbolsVerified=$true;archive=$library;retailMatch=$false}
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $work 'receipt.json') -Encoding utf8NoBOM
Write-Output ("{0}: {1}" -f $Layout,$library)
