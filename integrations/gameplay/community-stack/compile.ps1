param(
    [Parameter(Mandatory)][string]$Run,
    [Parameter(Mandatory)][ValidateSet('main','v084')][string]$Version
)
$ErrorActionPreference='Stop'
$matrix=Get-Content -LiteralPath (Join-Path $Run 'matrix.json') -Raw | ConvertFrom-Json
$v=$matrix | Where-Object tag -eq $Version
if (-not $v) { throw "Missing version $Version" }
$builder=Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\Tools\Launch-VsDevShell.ps1' -File | Select-Object -First 1
& $builder.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$objdir=(New-Item -ItemType Directory -Path (Join-Path $Run ("objects-"+$Version)) -Force).FullName
$sdk=$v.sdk
$json=Join-Path (($matrix | Where-Object tag -eq 'main').sdk) 'Libs'
$directories=@($v.top,$sdk,(Join-Path $sdk 'SDK'),(Join-Path $v.top 'BrtInventory'),$json)
$directories+=@($v.parts.PSObject.Properties.Value | ForEach-Object {Join-Path $v.compiledRoot (Split-Path -Parent $_)})
$arguments=@('/nologo','/std:c++latest','/EHsc','/MD','/utf-8','/W0','/bigobj','/Od','/Ob0','/MP2')
foreach ($dir in ($directories | Select-Object -Unique)) {$arguments+="/I$dir"}
$sources=@($v.entries | ForEach-Object {if ($_.StartsWith('SDK/')){Join-Path $sdk $_} else {Join-Path $v.top $_}})
Push-Location -LiteralPath $objdir
try {
    $compiled=& cl.exe @arguments /c @sources 2>&1
    $compiled | Out-File -Encoding utf8 'compile.log'
    if ($LASTEXITCODE) {throw ($compiled | Select-String -Pattern 'error [A-Z]+\d+' | Select-Object -First 14 | Out-String)}
    $objects=@($sources | ForEach-Object {[IO.Path]::GetFileNameWithoutExtension($_)+'.obj'})
    foreach ($object in $objects) {
        if (-not (Test-Path -LiteralPath $object)) {throw "Missing $object"}
        $dump=& dumpbin.exe /nologo /symbols $object
        if ($LASTEXITCODE) {throw "Cannot read $object"}
        $dump | Where-Object {$_ -match '\bExternal\s+\|\s+\?'} | Set-Content -LiteralPath ($object+'.symbols') -Encoding utf8
    }
    $name="community-business-$Version.lib"
    & lib.exe /nologo ("/OUT:"+$name) @objects
    if ($LASTEXITCODE) {throw 'Static library failed'}
    $members=@(& lib.exe /nologo /list $name)
    if ($LASTEXITCODE -or (@(Compare-Object ($members | Sort-Object) ($objects | Sort-Object))).Count -ne 0) {throw 'Static library membership mismatch'}
    $result=[pscustomobject]@{version=$Version;library=$name;bytes=(Get-Item -LiteralPath $name).Length;sha256=(Get-FileHash -LiteralPath $name).Hash.ToLowerInvariant();members=$members}
    $result | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath 'library.json' -Encoding utf8
    & (Join-Path $PSScriptRoot 'check-exports.ps1') -Directory $objdir
    Write-Output "PASS $Version compiled $($sources.Count) selected business and UMG sources into $name"
} finally {Pop-Location}
