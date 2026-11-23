param([Parameter(Mandatory)][string]$CommunityModule)
$ErrorActionPreference = 'Stop'
$out = (New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) ('BrtSelectedLogin-' + [guid]::NewGuid().ToString('N')))).FullName
$unit = Join-Path $out 'BrtInventory'
foreach ($dir in @($unit, (Join-Path $unit 'SDK'), (Join-Path $out 'BrtGrapple'))) {
    New-Item -ItemType Directory -Path $dir | Out-Null
}
Copy-Item -LiteralPath (Join-Path $CommunityModule 'BrtInventory/OwnerLogin.cpp') -Destination $unit
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'selected-login-fixture.hpp') -Destination (Join-Path $unit 'OwnerLogin.hpp')
foreach ($rel in @('BrtInventory/Retry.hpp','BrtInventory/SDK/ProjectBoundary_classes.hpp','BrtGrapple/Observer.hpp')) {
    [IO.File]::WriteAllText((Join-Path $out $rel), '#pragma once')
}
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $dev = Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    & $dev.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
Push-Location -LiteralPath $out
try {
    & cl.exe /nologo /std:c++20 /EHsc /utf-8 "/I$unit" (Join-Path $unit 'OwnerLogin.cpp') (Join-Path $PSScriptRoot 'selected-login.cpp') /Fe:selected-login.exe
    if ($LASTEXITCODE) {throw 'Login test did not compile'}
    & ./selected-login.exe
    if ($LASTEXITCODE) {throw 'Login regression failed'}
} finally {Pop-Location}
