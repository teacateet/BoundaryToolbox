param([Parameter(Mandatory)][string]$Directory)
$ErrorActionPreference='Stop'
if (-not (Get-Command lib.exe -ErrorAction SilentlyContinue)) {
    $builder=Get-ChildItem -Path 'C:\Program Files\Microsoft Visual Studio\2022\*\Common7\Tools\Launch-VsDevShell.ps1' -File | Select-Object -First 1
    & $builder.FullName -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
Push-Location -LiteralPath $Directory
try {
    $lib=Get-Content -LiteralPath 'library.json' -Raw | ConvertFrom-Json
    $expected='?BP_OnHandleBackAction@UUMG_Prompt_C@'
    $symbols=& dumpbin.exe /nologo /linkermember:1 $lib.library
    if ($LASTEXITCODE -or -not ($symbols -cmatch ([regex]::Escape($expected)))) {throw 'Missing prompt definition in actual static library'}
    $without=@($lib.members | Where-Object {$_ -ne 'UMG_Prompt_functions.obj'})
    if ($without.Count -ne $lib.members.Count-1) {throw 'Expected one prompt member'}
    & lib.exe /nologo '/OUT:negative-without-prompt.lib' @without
    if ($LASTEXITCODE) {throw 'Negative archive could not be made'}
    $negative=& dumpbin.exe /nologo /linkermember:1 'negative-without-prompt.lib'
    if ($LASTEXITCODE -or ($negative -cmatch ([regex]::Escape($expected)))) {throw 'Unexpected prompt definition in omission control'}
    [pscustomobject]@{symbol=$expected;positiveMembers=$lib.members.Count;omissionMembers=$without.Count;presentInFull=$true;presentInOmission=$false} |
        ConvertTo-Json | Set-Content -LiteralPath 'export-control.json' -Encoding utf8
} finally {Pop-Location}
