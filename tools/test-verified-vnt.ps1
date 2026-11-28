#requires -Version 7.0
param([Parameter(Mandatory)][string]$Tests,[Parameter(Mandatory)][string]$Relay,[Parameter(Mandatory)][string]$Evidence)
$ErrorActionPreference='Stop'
$lab=Join-Path $Evidence ([guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($lab)
$socket=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
$socket.Start();$port=$socket.LocalEndpoint.Port;$socket.Stop()
Set-Content -LiteralPath (Join-Path $lab 'server.toml') -Value @("tcp_bind = `"127.0.0.1:$port`"",'network = "10.193.43.0/24"','lease_duration = 300','persistence = false')
$server=Start-Process $Relay -ArgumentList '-c','server.toml' -WorkingDirectory $lab -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $lab 'server.out') -RedirectStandardError (Join-Path $lab 'server.err')
try {
    $end=[DateTime]::UtcNow.AddSeconds(15)
    $pem=Join-Path $lab 'cert.pem'
    while (!(Test-Path -LiteralPath $pem) -or !(Get-NetTCPConnection -State Listen -LocalAddress '127.0.0.1' -LocalPort $port -OwningProcess $server.Id -ErrorAction SilentlyContinue)){
        if ($server.HasExited -or [DateTime]::UtcNow -gt $end){throw 'Local relay did not start'}
        Start-Sleep -Milliseconds 150
    }
    # 信任来源为本次创建的本地服务端文件，不从首次网络连接取得指纹。
    $cert=[Security.Cryptography.X509Certificates.X509Certificate2]::CreateFromPem([IO.File]::ReadAllText($pem))
    $envs=@{BRT_ROOM_RELAY="127.0.0.1:$port";BRT_TEST_FRIEND_SERVER="127.0.0.1:$port";BRT_ROOM_RELAY_SHA256=$cert.GetCertHashString([Security.Cryptography.HashAlgorithmName]::SHA256)}
    $cert.Dispose()
    $out=Join-Path $lab 'network.out';$err=Join-Path $lab 'network.err'
    $proc=Start-Process $Tests -ArgumentList 'two_real_clients_exchange_game_udp_through_an_invite','--ignored','--nocapture' -Environment $envs -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    if (!$proc.WaitForExit(110000)) {$proc.Kill();throw 'Network regression timed out'}
    if ($proc.ExitCode -or (Get-Content -Raw -LiteralPath $err) -notmatch 'after synchronous revoke'){throw "Network regression failed: $lab"}
    Write-Output "PASS $lab"
} finally {if(!$server.HasExited){$server.Kill();$server.WaitForExit()};$server.Dispose()}
