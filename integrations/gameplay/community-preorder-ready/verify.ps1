#requires -Version 7.0
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$builder = Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\Tools\Launch-VsDevShell.ps1' -File | Select-Object -First 1
& $builder.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$out = [IO.Directory]::CreateDirectory($OutputDirectory).FullName
foreach ($test in @('gate', 'native-table')) {
    & cl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 (Join-Path $PSScriptRoot "$test.test.cpp") "/Fo$out\$test.obj" "/Fe$out\$test.test.exe"
    if ($LASTEXITCODE) { throw "$test regression failed to compile" }
    & (Join-Path $out "$test.test.exe")
    if ($LASTEXITCODE) { throw "$test regression failed" }
}
