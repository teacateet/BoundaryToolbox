#requires -Version 7.0
param([Parameter(Mandatory)][string]$Definitions,[Parameter(Mandatory)][string]$Evidence)
$ErrorActionPreference='Stop'
$defs=(Resolve-Path -LiteralPath $Definitions).Path
function Rows($name){(Get-Content -Raw -LiteralPath (Join-Path $defs ("DT_"+$name+".json")) | ConvertFrom-Json -AsHashtable -NoEnumerate)[0].Rows}
$scope=(Rows 'CharacterDefinition')['PROBE'].CustomScope
$guns=Rows 'WeaponDefinition'
$pods=Rows 'PodDefinition'
if('PROBE_GSW-DMR' -notin $scope.WeaponScope -or 'PROBE_TAC-EMP' -notin $scope.PodScope -or
   $guns.Contains('PROBE_GSW-DMR') -or $pods.Contains('PROBE_TAC-EMP') -or !$guns.Contains('GSW-DMR') -or !$pods.Contains('TAC-EMP')){
    throw 'Retail fixture does not match the role and definition tables'
}
$run=Join-Path ([IO.Path]::GetFullPath($Evidence)) ([guid]::NewGuid().ToString('N'))
$kit=Join-Path (Split-Path -Parent $PSScriptRoot) 'integrations/gameplay/community-first-loadout'
foreach($part in 'BrtInventory','SDK','Client'){[void][IO.Directory]::CreateDirectory((Join-Path $run $part))}
foreach($name in 'RespawnKit.hpp','RespawnWindow.hpp','RespawnItemId.hpp'){Copy-Item -LiteralPath (Join-Path $kit $name) -Destination (Join-Path $run 'BrtInventory')}
Copy-Item -LiteralPath (Join-Path $kit 'tests/respawn-sdk-fixture.hpp') -Destination (Join-Path $run 'BrtInventory/Reconnect.hpp')
Set-Content -LiteralPath (Join-Path $run 'SDK/ProjectBoundary_parameters.hpp') -Value '#pragma once'
Set-Content -LiteralPath (Join-Path $run 'Client/DeathMenu.hpp') -Value '#pragma once'
$vs=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$dev=& $vs -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Auxiliary\Build\vcvars64.bat' | Select-Object -First 1
if(!$dev){throw 'MSVC x64 not found'}
# 固定的编译命令只在本次证据目录运行。
Copy-Item -LiteralPath (Join-Path $kit 'tests/respawn-applied.cpp') -Destination $run
Set-Content -LiteralPath (Join-Path $run 'check.cmd') -Encoding ascii -Value @('@echo off',('call "'+$dev+'" >nul'),'if errorlevel 1 exit /b 1','cl /nologo /utf-8 /EHsc /std:c++20 /I BrtInventory respawn-applied.cpp /Fe:respawn-applied.exe','if errorlevel 1 exit /b 1','.\respawn-applied.exe')
Push-Location -LiteralPath $run
try { & ./check.cmd *> check.log;if($LASTEXITCODE){throw "C++ respawn fixture failed: $run/check.log"} } finally {Pop-Location}
Write-Output "PASS $run/check.log"
