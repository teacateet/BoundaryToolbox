#requires -Version 7.0
param([Parameter(Mandatory)][string]$Executable, [Parameter(Mandatory)][string]$CurrentZip,
      [Parameter(Mandatory)][string]$PreviousZip, [Parameter(Mandatory)][string]$LegacyZip,
      [Parameter(Mandatory)][string]$RetailWin64, [Parameter(Mandatory)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$run=Join-Path $EvidenceRoot ([guid]::NewGuid().ToString('N'))
$app=Join-Path $run '工具箱'
$game=Join-Path $run 'ProjectBoundary/Binaries/Win64'
[void][IO.Directory]::CreateDirectory($game)
[IO.Compression.ZipFile]::ExtractToDirectory($CurrentZip,$app)
$exe=Join-Path $app 'BRT.exe'
Copy-Item -LiteralPath $Executable -Destination $exe -Force
$binary='ProjectBoundarySteam-Win64-Shipping.exe'
Copy-Item -LiteralPath (Join-Path $RetailWin64 $binary) -Destination $game
function Install([string]$label,[int]$count) {
    $out=Join-Path $run "$label.out"
    $err=Join-Path $run "$label.err"
    $p=Start-Process -FilePath $exe -ArgumentList @('--install-community',('"'+$game+'"')) -WorkingDirectory $app -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    if (!$p.WaitForExit(90000)) {$p.Kill();throw 'Installation timeout'}
    $report=Get-Content -Raw -LiteralPath $out
    if ($p.ExitCode -ne 0) {throw (Get-Content -Raw -LiteralPath $err)}
    if (($count -gt 0 -and $report -notmatch "已清理 $count 个旧版组件") -or ($count -eq 0 -and $report -match '已清理|无法检查或清理')) {throw "Unexpected result: $report"}
}
$z=[IO.Compression.ZipFile]::OpenRead($PreviousZip)
try {
    $entries=@($z.Entries | Where-Object FullName -match '^components/gameplay/live\d+(?:-radar)?/Payload\.dll$')
    if ($entries.Count -ne 1) {throw 'Expected one old player DLL'}
    $old=Join-Path $app $entries[0].FullName
    if (Test-Path -LiteralPath $old) {throw 'Expected an obsolete component path'}
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($old))
    [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0],$old)
} finally {$z.Dispose()}
$z=[IO.Compression.ZipFile]::OpenRead($LegacyZip)
try {
    $batch=Join-Path $game 'startgame.bat'
    $wrapper=Join-Path $game 'ProjectReboundServerWrapper.exe'
    [IO.Compression.ZipFileExtensions]::ExtractToFile($z.GetEntry('startgame.bat'),$batch)
    [IO.File]::WriteAllText($wrapper,'player custom wrapper')
    Install 'fresh' 2
    if ((Test-Path -LiteralPath $old) -or (Test-Path -LiteralPath $batch) -or [IO.File]::ReadAllText($wrapper) -ne 'player custom wrapper') {throw 'Incorrect cleanup on fresh install'}
    $cfg=Join-Path $game 'ServerLauncher/serverconfig.json'
    $cfgHash=(Get-FileHash -LiteralPath $cfg).Hash
    [IO.Compression.ZipFileExtensions]::ExtractToFile($z.GetEntry('ProjectReboundServerWrapper.exe'),$wrapper,$true)
    Install 'already-installed' 1
    if ((Test-Path -LiteralPath $wrapper) -or (Get-FileHash -LiteralPath $cfg).Hash -ne $cfgHash) {throw 'Incorrect cleanup on repeat install'}
    Install 'repeat' 0
} finally {$z.Dispose()}
Write-Output "PASS $run"
