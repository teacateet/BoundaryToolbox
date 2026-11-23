param(
    [string]$Community = $env:BRT_SDK_ROOT,
    [string]$FixtureDirectory = $env:BRT_SPAWN_CASES
)
$ErrorActionPreference = 'Stop'
if (-not $Community) {
    $Community = @((Join-Path $PSScriptRoot 'ProjectReboundMainDLL'), (Join-Path $PSScriptRoot '..\ProjectReboundMainDLL')) |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'SDK\ProjectBoundary_classes.hpp') } | Select-Object -First 1
}
if ($Community -and (Test-Path -LiteralPath (Join-Path $Community 'ProjectReboundMainDLL\Libs\json.hpp'))) {
    $Community = Join-Path $Community 'ProjectReboundMainDLL'
}
if (-not $Community -or -not (Test-Path -LiteralPath (Join-Path $Community 'Libs\json.hpp'))) {
    throw 'Set BRT_SDK_ROOT to the ProjectRebound checkout or ProjectReboundMainDLL directory'
}
$Community = (Resolve-Path -LiteralPath $Community).Path
if (-not $FixtureDirectory) { $FixtureDirectory = Join-Path $PSScriptRoot 'cases' }
$FixtureDirectory = (Resolve-Path -LiteralPath $FixtureDirectory).Path
$installed = Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\Tools\Launch-VsDevShell.ps1' -File |
    Select-Object -First 1
if (-not $installed) { throw 'Visual Studio 2022 Build Tools required' }
& $installed.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Push-Location -LiteralPath $PSScriptRoot
try {
    $buildRoot = if ($env:BRT_SPAWN_BUILD) {$env:BRT_SPAWN_BUILD} else {Join-Path $PSScriptRoot 'build'}
    $buildDir = New-Item -ItemType Directory -Path $buildRoot -Force
    $includeJson = '/I' + (Join-Path $Community 'Libs')
    $objectFlag = '/Fo' + $buildDir.FullName + '\'
    $executable = Join-Path $buildDir.FullName 'verify.exe'
    & cl.exe /nologo /std:c++17 /EHsc /MD /utf-8 /W4 $includeJson $objectFlag ("/Fe" + $executable) verify.cpp RoomHttp.cpp
    if ($LASTEXITCODE) { throw 'C++ application build failed' }
    & $executable $FixtureDirectory
    if ($LASTEXITCODE) { throw 'C++ fixture failed' }
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /W0 /Zs $includeJson ("/I" + $Community) declaration-check.cpp
    if ($LASTEXITCODE) { throw 'ProjectBoundary declaration check failed' }
    'PASS ProjectBoundary role, weapon, parts and inventory declarations'
} finally { Pop-Location }
