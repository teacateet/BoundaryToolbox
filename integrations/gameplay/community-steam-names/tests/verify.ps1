#requires -Version 7
param([string]$OutDirectory=(Join-Path ([IO.Path]::GetTempPath()) ('BrtSteamPersona-'+[guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop'
$work=(New-Item -ItemType Directory -Force -Path $OutDirectory).FullName
if(-not (Get-Command cl.exe -ErrorAction SilentlyContinue)){
    $setup=Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\Tools\Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if(-not $setup){throw 'Visual Studio x64 tools are required'}
    & $setup.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$exe=Join-Path $work 'persona-test.exe'
& cl.exe /nologo /std:c++20 /EHsc /utf-8 "/Fo$(Join-Path $work 'persona-test.obj')" "/Fe$exe" (Join-Path $PSScriptRoot 'persona.test.cpp')
if($LASTEXITCODE){throw 'Steam name fixture compilation failed'}
& $exe
if($LASTEXITCODE){throw 'Steam name fixture failed'}
