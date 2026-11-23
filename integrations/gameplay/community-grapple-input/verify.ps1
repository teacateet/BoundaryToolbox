param(
    [Parameter(Mandatory)][string]$OutputDir,
    [string]$CombinedUi,
    [string]$OriginalModule,
    [ValidateSet('v084','split')][string]$Layout='v084'
)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $dev = Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/Common7/Tools/Launch-VsDevShell.ps1' -File | Select-Object -First 1
    if (-not $dev) { throw 'MSVC x64 is required' }
    & $dev.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$work = Join-Path ([IO.Path]::GetFullPath($OutputDir)) ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
if (-not $CombinedUi) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'GrappleInput.hpp') -Destination $work
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests/MarkedHeadRadar.hpp') -Destination $work
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests/sequence.cpp') -Destination $work
    $binary = Join-Path $work 'sequence.exe'
    & cl.exe /nologo /std:c++20 /EHsc /MD /utf-8 ("/Fe"+$binary) ("/Fo"+(Join-Path $work 'sequence.obj')) (Join-Path $work 'sequence.cpp')
    if ($LASTEXITCODE) { throw 'Unable to build sequence fixture' }
    & $binary $work
    if ($LASTEXITCODE) { throw "Sequence check failed ($LASTEXITCODE)" }
    return
}
if (-not $OriginalModule) { throw 'Pass the original community directory with SDK and adjacent headers' }
$module = if ($Layout -eq 'v084') { 'Payload' } else { 'ProjectReboundMainDLL' }
$unit = if ($Layout -eq 'v084') { 'Utility/UIFix.cpp' } else { 'Client/UIShake.cpp' }
$dest = Join-Path (Join-Path $work $module) $unit
$dir = Split-Path -Parent $dest
New-Item -ItemType Directory -Path $dir -Force | Out-Null
Copy-Item -LiteralPath $CombinedUi -Destination $dest
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'GrappleInput.hpp') -Destination $dir
$combinedDir = Split-Path -Parent (Resolve-Path -LiteralPath $CombinedUi).Path
$originalDir = Split-Path -Parent (Join-Path (Resolve-Path -LiteralPath $OriginalModule).Path $unit)
$flags = @('/nologo','/std:c++20','/EHsc','/MD','/utf-8','/bigobj','/W0','/c',"/I$combinedDir","/I$originalDir")
git -C $work init -q
if ($LASTEXITCODE) { throw 'Cannot create isolated checkout' }
$patch = Join-Path $PSScriptRoot ($Layout+'.patch')
$baseline = Join-Path $work 'before.obj'
& cl.exe @flags ("/Fo"+$baseline) $dest
if ($LASTEXITCODE) { throw 'Original UI does not compile' }
git -C $work apply --check $patch
if ($LASTEXITCODE) { throw 'Increment does not apply' }
git -C $work apply --whitespace=nowarn $patch
if ($LASTEXITCODE) { throw 'Cannot apply increment' }
git -C $work apply --reverse --check $patch
if ($LASTEXITCODE) { throw 'Cannot reverse increment' }
$candidate = Join-Path $work 'after.obj'
& cl.exe @flags ("/Fo"+$candidate) $dest
if ($LASTEXITCODE) { throw 'Candidate UI does not compile' }
$before = (& dumpbin.exe /nologo /symbols $baseline) -join "`n"
if ($LASTEXITCODE) { throw 'Cannot read original object' }
$after = (& dumpbin.exe /nologo /symbols $candidate) -join "`n"
if ($LASTEXITCODE) { throw 'Cannot read candidate object' }
$getters = @('GetKeysFromActionName','WasInputKeyJustPressed','WasInputKeyJustReleased','GetCurrentMobilityModule','GetIsAllowedCatch','GetHasCatchingCooledDown')
foreach ($getter in $getters) {
    if ($before.Contains('?' + $getter + '@') -or -not $after.Contains('?' + $getter + '@')) { throw "Unexpected read path for $getter" }
}
foreach ($write in 'ServerLaunchHook','LaunchHook','K2_LaunchHook','BeginCD','SetIsAllowedCatch','ServerResetHook','ResetHook') {
    if ($after.Contains('?' + $write + '@') -and -not $before.Contains('?' + $write + '@')) { throw "New call to $write" }
}
$receipt = [ordered]@{ layout=$Layout; source=(Get-FileHash -LiteralPath $CombinedUi).Hash
    observer=(Get-FileHash -LiteralPath (Join-Path $dir 'GrappleInput.hpp')).Hash
    uiBeforeAddsObserver=$before.Contains('BrtGrappleInput'); uiAfterAddsObserver=$after.Contains('BrtGrappleInput')
    readSymbolsOnly=$getters; newLaunchOrResetCalls=$false; retailSession=$false }
$receipt | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $work 'receipt.json') -Encoding utf8
Write-Output "$Layout compiled, input readers present only in candidate: $work"
