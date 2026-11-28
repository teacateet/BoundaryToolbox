#requires -Version 7.0
param([Parameter(Mandatory)][string]$TestBinary, [Parameter(Mandatory)][string]$RetailWin64)
$ErrorActionPreference='Stop'
$scope=[guid]::NewGuid().ToString()
function Start-Probe([string]$op) {
    $p=[Diagnostics.Process]::new()
    $i=$p.StartInfo
    $i.FileName=(Resolve-Path -LiteralPath $TestBinary).Path
    $i.WorkingDirectory=(Resolve-Path -LiteralPath $RetailWin64).Path
    $i.UseShellExecute=$false
    $i.CreateNoWindow=$true
    $i.RedirectStandardOutput=$true
    $i.RedirectStandardInput=$true
    $i.RedirectStandardError=$true
    $i.Environment['BRT_QA_DIRECTORY']=$scope
    $i.Environment['BRT_QA_OP']=$op
    $i.Environment['SteamAppId']='1364020'
    $i.Environment['SteamGameId']='1364020'
    foreach($arg in '--ignored','--exact','steam_directory::tests::qa_helper','--nocapture') {$i.ArgumentList.Add($arg)}
    [void]$p.Start()
    return $p
}
function Count-Rooms {
    $p=Start-Probe 'list'
    try {
        if (!$p.WaitForExit(30000)) {throw 'List timed out'}
        if ($p.ExitCode) {throw $p.StandardError.ReadToEnd()}
        $rows=$p.StandardOutput.ReadToEnd().Split([char]10) | Where-Object { $_.StartsWith('QA-ROWS ') } | Select-Object -First 1
        if ($null -eq $rows) {throw 'Missing list result'}
        $result=ConvertFrom-Json -NoEnumerate -InputObject $rows.Substring(8)
        foreach($room in $result) {if ($room.host -ne 'BRT QA' -or $room.invite -notmatch '^brt://room/[0-9a-f]{32}/10\.193\.24\.5:18777$') {throw 'Invalid metadata'} }
        return $result.Count
    } finally {if (!$p.HasExited){$p.Kill()};$p.Dispose()}
}
function Expect-Count([int]$n) {
    $end=[DateTime]::UtcNow.AddSeconds(45)
    do {if ((Count-Rooms) -eq $n) {return}; Start-Sleep -Milliseconds 900} while([DateTime]::UtcNow -lt $end)
    throw "Expected $n public rooms"
}
Expect-Count 0
$p=Start-Probe 'host'
try {
    $end=[DateTime]::UtcNow.AddSeconds(30)
    do {
        $line=$p.StandardOutput.ReadLineAsync()
        if (!$line.Wait([Math]::Max(1,($end-[DateTime]::UtcNow).TotalMilliseconds))) {throw 'Host timed out'}
        if ($null -eq $line.Result) {throw ('Host failed: '+$p.StandardError.ReadToEnd())}
    } while ($line.Result -ne 'BRT-DIRECTORY READY')
    Expect-Count 1
    $p.StandardInput.Close()
    if (!$p.WaitForExit(10000) -or $p.ExitCode -ne 0) {throw 'Host did not close cleanly'}
    Expect-Count 0
    Write-Output 'PASS: independent Steam processes; before=0, listed=1, withdrawn=0; same Steam account; no live game endpoint.'
} finally {if (!$p.HasExited){$p.StandardInput.Close(); if (!$p.WaitForExit(5000)) {$p.Kill()}};$p.Dispose()}
