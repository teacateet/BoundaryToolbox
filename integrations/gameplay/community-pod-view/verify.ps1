param(
    [Parameter(Mandatory)][string]$CommunityRoot,
    [Parameter(Mandatory)][string]$OutDir,
    [ValidateSet('v084','split')][string]$Layout='v084'
)
$ErrorActionPreference='Stop'
$src=(Resolve-Path -LiteralPath $CommunityRoot).Path
$relative=if($Layout -eq 'v084'){'Payload/Utility/UIFix.cpp'}else{'ProjectReboundMainDLL/Client/UIShake.cpp'}
$module=Split-Path -Parent (Split-Path -Parent (Join-Path $src $relative))
$sdk=Join-Path $module 'SDK/ProjectBoundary_classes.hpp'
$source=Join-Path $src $relative
$baseInclude=Split-Path -Parent $source
$run=Join-Path ([IO.Path]::GetFullPath($OutDir)) ('podview-'+$Layout+'-'+[guid]::NewGuid().ToString('N'))
$tree=Join-Path $run 'source'
$new=Join-Path $tree $relative
$dest=Split-Path -Parent $new
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -LiteralPath $source -Destination $new
& git -C $tree init -q
$patch=Join-Path $PSScriptRoot ($Layout+'.patch')
& git -C $tree apply --check $patch
if($LASTEXITCODE){throw 'Source is incompatible'}
& git -C $tree apply $patch
if($LASTEXITCODE){throw 'Cannot apply patch'}
& git -C $tree apply --reverse --check $patch
if($LASTEXITCODE){throw 'Cannot reverse patch'}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'PodView.hpp') -Destination $dest
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../community-radar/UiJournal.hpp') -Destination $dest
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../community-pod-view/ShoulderSlots.hpp') -Destination $dest
$retailPath=Join-Path $PSScriptRoot '../equipment/catalog/retail-source.json'
$retail=Get-Content -Raw -LiteralPath $retailPath | ConvertFrom-Json -AsHashtable
$baseClasses=[IO.File]::ReadAllText($sdk)
$rows=@()
$records=@()
foreach($pair in $retail.pods.GetEnumerator()){
    $id=$pair.Key
    if($id -notmatch '^[A-Za-z0-9_-]+$'){throw 'Unexpected catalog ID'}
    $asset=$pair.Value.asset
    $native=''
    for($i=0;$i -lt 12;$i++){
        $parent=$retail.assets[$asset].super
        if(-not $parent){throw "Missing retail parent for $id"}
        if($parent.ObjectPath -eq '/Script/ProjectBoundary'){
            $native='A'+([regex]::Match($parent.ObjectName,"^Class'([^']+)'$").Groups[1].Value)
            break
        }
        $asset=$parent.ObjectPath -replace '\.\d+$','.uasset'
    }
    $type=$native
    $deploy=$false
    for($i=0;$i -lt 12 -and $type -ne 'APBLauncher';$i++){
        if($type -eq 'APBLauncher_Deploy'){$deploy=$true}
        $m=[regex]::Match($baseClasses,'(?m)^class '+[regex]::Escape($type)+'(?: final)? : public (\w+)')
        if(-not $m.Success){throw "SDK inheritance missing for $type"}
        $type=$m.Groups[1].Value
    }
    if($type -ne 'APBLauncher'){throw "Not a launcher: $id"}
    $rows+=('{"'+$id+'",'+$deploy.ToString().ToLowerInvariant()+'},')
    $records+=[ordered]@{id=$id;class=$pair.Value.class;native=$native;usesController=$deploy}
}
if($rows.Count -ne 27){throw "Wrong retail row count"}
$utf8=[Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllLines((Join-Path $run 'pod-catalog.inc'),[string[]]$rows,$utf8)
[IO.File]::WriteAllText((Join-Path $run 'catalog.json'),($records|ConvertTo-Json -Depth 3),$utf8)
if(-not(Get-Command cl.exe -ErrorAction SilentlyContinue)){
    $dev=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if(-not $dev){throw 'Visual Studio C++ Build Tools required'}
    & $dev.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$expectedPass=@('native camera exclusion stays present','authored mesh visibility preserved','active auxiliary mode while firing rifle','transferred equipment released from old view','unrelated remote owner leaves viewport intact','formerly local flag cannot claim a new ownerless pod','contradictory owner beats stale local shoulder')
$checkCount=$rows.Count * 4 + 24
$results=[ordered]@{}
foreach($kind in 'original','candidate'){
    $dir=Join-Path $run $kind
    New-Item -ItemType Directory -Path $dir | Out-Null
    $file=if($kind -eq 'original'){$source}else{$new}
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /bigobj /W0 /c ("/I"+$baseInclude) ("/Fo"+$dir+"/full.obj") $file
    if($LASTEXITCODE){throw "Full $kind character UI source did not compile"}
    $unit=[IO.File]::ReadAllText($file)
    $header=[regex]::Match($unit,'(?m)^#include "PodView\.hpp"').Value
    $call=[regex]::Match($unit,'BrtPodView::OnCharacter\(static_cast<APBCharacter\*>\(Object\), funcName\);').Value
    if($kind -eq 'candidate' -and (-not $call -or -not $header -or $unit.IndexOf($call) -gt $unit.IndexOf('if (!UIFixDebugEnabled)'))){throw 'Character business callback is unreachable'}
    [IO.File]::WriteAllText((Join-Path $dir 'pod-view-entry.inc'),$header+"`nusing namespace SDK;`ninline void Entry(APBCharacter* Object,const std::string& funcName){"+$call+"}`n",$utf8)
    $exe=Join-Path $dir 'test.exe'
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /W4 /wd4100 ("/I"+$dir) ("/I"+$dest) ("/I"+$run) ("/Fo"+$dir+"/test.obj") ("/Fe:"+$exe) (Join-Path $PSScriptRoot 'pod-view.test.cpp')
    if($LASTEXITCODE){throw 'Controlled UI tests did not compile'}
    $lines=@(& $exe);$code=$LASTEXITCODE
    [IO.File]::WriteAllLines((Join-Path $dir 'tests.log'),[string[]]$lines,$utf8)
    $passed=@($lines -match '^PASS ' | ForEach-Object {$_.Substring(5)})
    $failed=@($lines -match '^FAIL ' | ForEach-Object {$_.Substring(5)})
    if($lines.Count -ne $checkCount -or $passed.Count+$failed.Count -ne $checkCount){throw "Incomplete $kind suite"}
    if($kind -eq 'original'){
        if($code -ne 1 -or (Compare-Object $passed $expectedPass)){throw "Unexpected original results: $dir"}
    }elseif($code -ne 0 -or $failed.Count){throw "Candidate regression: $dir"}
    $results[$kind]=[ordered]@{pass=$passed.Count;fail=$failed;sourceHash=(Get-FileHash -LiteralPath $file).Hash}
}
$receipt=[ordered]@{layout=$Layout;catalogHash=(Get-FileHash -LiteralPath $retailPath).Hash;podRows=27;shoulders=2;total=$checkCount;sourceHash=(Get-FileHash -LiteralPath (Join-Path $dest 'PodView.hpp')).Hash;reversible=$true;results=$results}
[IO.File]::WriteAllText((Join-Path $run 'receipt.json'),($receipt|ConvertTo-Json -Depth 5),$utf8)
Write-Output ("{0} original {1}/{4}, candidate {2}/{4}. {3}" -f $Layout,$results.original.pass,$results.candidate.pass,$run,$checkCount)
