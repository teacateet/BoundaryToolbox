param(
    [Parameter(Mandatory=$true)][string]$Community
)
$ErrorActionPreference = 'Stop'
$sdk = (Resolve-Path -LiteralPath $Community).Path
if (Test-Path -LiteralPath (Join-Path $sdk 'ProjectReboundMainDLL/SDK/ProjectBoundary_classes.hpp')) {
    $sdk = Join-Path $sdk 'ProjectReboundMainDLL'
}
if (-not (Test-Path -LiteralPath (Join-Path $sdk 'SDK/ProjectBoundary_classes.hpp'))) {
    throw 'ProjectRebound game declarations missing'
}
$launcher = Get-ChildItem -Path 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
if (-not $launcher) { throw 'Visual Studio 2022 C++ tools required' }
& $launcher.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Push-Location -LiteralPath $PSScriptRoot
try {
    $out = New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot 'build')
    & cl.exe /nologo /std:c++17 /EHsc /utf-8 /W4 /WX ('/Fo' + $out.FullName + '\') ('/Fe' + (Join-Path $out.FullName 'round_live32_test.exe')) round_live32_test.cpp
    if ($LASTEXITCODE) { throw 'C++17 gameplay build failed' }
    & (Join-Path $out.FullName 'round_live32_test.exe')
    if ($LASTEXITCODE) { throw 'Gameplay regression failed' }
    & cl.exe /nologo /std:c++20 /EHsc /utf-8 /W0 /Zs ('/I' + $sdk) declaration-check.cpp
    if ($LASTEXITCODE) { throw 'Retail declaration verification failed' }
    'PASS ProjectBoundary public declarations and native game/UI calls (syntax only)'
} finally { Pop-Location }
