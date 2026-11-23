param([string]$Community = $env:BRT_SDK_ROOT, [string]$FixtureDirectory = $env:BRT_SPAWN_CASES)
$ErrorActionPreference = 'Stop'
if (-not $Community) {
    $Community = @((Join-Path $PSScriptRoot 'ProjectReboundMainDLL'),
        (Join-Path $PSScriptRoot '..\ProjectReboundMainDLL')) |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'SDK\ProjectBoundary_classes.hpp') } | Select-Object -First 1
}
if ($Community -and (Test-Path -LiteralPath (Join-Path $Community 'ProjectReboundMainDLL\SDK\ProjectBoundary_classes.hpp'))) {
    $Community = Join-Path $Community 'ProjectReboundMainDLL'
}
if (-not $Community -or -not (Test-Path -LiteralPath (Join-Path $Community 'Libs\json.hpp'))) {
    throw 'Set BRT_SDK_ROOT to the ProjectRebound checkout or ProjectReboundMainDLL'
}
$Community = (Resolve-Path -LiteralPath $Community).Path
if (-not $FixtureDirectory) { $FixtureDirectory = Join-Path $PSScriptRoot 'cases' }
$FixtureDirectory = (Resolve-Path -LiteralPath $FixtureDirectory).Path
$loadCompiler = Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\Tools\Launch-VsDevShell.ps1' -File | Select-Object -First 1
if (-not $loadCompiler) { throw 'Visual Studio 2022 Build Tools required' }
& $loadCompiler.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Push-Location -LiteralPath $PSScriptRoot
try {
    $build = (New-Item -ItemType Directory -Path $(if ($env:BRT_SPAWN_BUILD) {$env:BRT_SPAWN_BUILD} else {Join-Path $PSScriptRoot 'build'}) -Force).FullName
    $json = '/I' + (Join-Path $Community 'Libs')
    $test = Join-Path $build 'OwnedBirth.test.exe'
    & cl.exe /nologo /std:c++17 /EHsc /MD /utf-8 /W4 $json ('/Fo' + (Join-Path $build 'OwnedBirth.test.obj')) ('/Fe' + $test) OwnedBirth.test.cpp
    if ($LASTEXITCODE) { throw 'OwnedBirth test build failed' }
    & $test $FixtureDirectory
    if ($LASTEXITCODE) { throw 'OwnedBirth targeted test failed' }
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 /W0 /c $json ('/I' + $Community) ('/Fo' + (Join-Path $build 'OwnedBirth.sdk.obj')) OwnedBirth.sdk.cpp
    if ($LASTEXITCODE) { throw 'OwnedBirth SDK object build failed' }
    'PASS concrete APBPlayerController/APBCharacter/APBWeapon calls compiled into an object file'
} finally { Pop-Location }
