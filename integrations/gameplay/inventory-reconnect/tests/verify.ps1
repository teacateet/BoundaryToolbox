param(
    [Parameter(Mandatory=$true)][string]$CommunityModule,
    [Parameter(Mandatory=$true)][string]$ArchiveSource,
    [string]$JsonInclude,
    [string]$OutDir=(Join-Path ([IO.Path]::GetTempPath()) ('BrtReconnect-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference='Stop'
$sdk=(Resolve-Path -LiteralPath $CommunityModule).Path
$parts=Join-Path $sdk 'BrtInventory'
if(!$JsonInclude) { $JsonInclude=Join-Path $sdk 'Libs' }
$output=(New-Item -ItemType Directory -Path $OutDir -Force).FullName
$implementation=Join-Path $PSScriptRoot '..\..\..\loadout\room-spawn.cjs'
$case=([IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\session-menu.case')) -replace "`r`n","`n").TrimEnd()+"`n"
$source=[IO.File]::ReadAllText($implementation) -replace "`r`n","`n"
if(!$source.Contains($case)) {throw 'Expected the current reconnect route in the toolbox working tree'}
$original=Join-Path $output 'original-room.cjs'
[IO.File]::WriteAllText($original,$source.Replace($case,''),[Text.UTF8Encoding]::new($false))
if(!(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $setup=Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\Tools\Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if(!$setup){throw 'The Visual Studio 2022 x64 C++ tools are required'}
    & $setup.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
Push-Location -LiteralPath $output
try {
    $flags=@('/nologo','/EHsc','/MD','/std:c++latest','/utf-8','/W0',"/I$parts","/I$JsonInclude")
    & cl @flags /c (Join-Path $parts 'RoomHttp.cpp')
    if($LASTEXITCODE){throw 'RoomHttp failed to compile'}
    & cl @flags (Join-Path $PSScriptRoot 'check.cpp') RoomHttp.obj '/Fe:read.exe'
    if($LASTEXITCODE){throw 'HTTP client failed to link'}
    & cl @flags "/I$PSScriptRoot\fixture-sdk" (Join-Path $PSScriptRoot 'connection-flow.cpp') RoomHttp.obj '/Fe:flow.exe'
    if($LASTEXITCODE){throw 'Connection fixture failed to link'}
    & .\read.exe
    if($LASTEXITCODE){throw 'Connection state checks failed'}
    & node (Join-Path $PSScriptRoot 'verify-http.cjs') $output (Join-Path $output 'read.exe') $ArchiveSource $original
    if($LASTEXITCODE){throw 'Archive integration failed'}
} finally {Pop-Location}
