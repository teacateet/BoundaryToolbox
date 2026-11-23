[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Win64,
    [ValidateRange(1024, 65535)] [int] $Port = 18777,
    [ValidateRange(20, 300)] [int] $TimeoutSeconds = 100,
    [ValidateSet('PvP', 'PvE')] [string] $Mode = 'PvP',
    [ValidateSet('Warehouse','CircularX','MiniFarm','OSS')] [string] $Map = 'Warehouse',
    [ValidateSet('Normal','Easy','Hard')] [string] $Difficulty = 'Normal',
    [string] $LauncherPath,
    [switch] $WithGameData,
    [switch] $InstalledDirectory,
    [switch] $GameSibling,
    [switch] $JoinClient,
    [ValidateSet('MatchFlag','EngineUrl')] [string] $ClientEntry = 'MatchFlag',
    [string] $ProfileLog,
    [ValidateRange(30, 300)] [int] $ClientSeconds = 100,
    [switch] $Direct
)
$ErrorActionPreference = 'Stop'
$binaryRoot = (Resolve-Path -LiteralPath $Win64).Path
$gameExe = Join-Path $binaryRoot 'ProjectBoundarySteam-Win64-Shipping.exe'
if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw "Game executable missing at $gameExe" }
$qaRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../.workflow/qa'))
$run = New-Item -ItemType Directory -Path (Join-Path $qaRoot ('fixed-' + [Guid]::NewGuid().ToString('N')))
$stdoutPath = Join-Path $run.FullName 'host.out'
$stderrPath = Join-Path $run.FullName 'host.err'
$enginePath = Join-Path $run.FullName 'engine.log'
$receiptPath = Join-Path $run.FullName 'result.json'
$modePath = if ($Mode -eq 'PvE') { "/Game/Online/GameMode/BP_PBGameMode_Rush_PVE_$Difficulty.BP_PBGameMode_Rush_PVE_${Difficulty}_C" } else { '/Game/Online/GameMode/PBGameMode_Rush_BP.PBGameMode_Rush_BP_C' }
$argv = @('-server', '-log', '-unattended', '-nullrhi', '-nosound', '-nosplash', '-NoWindow',
          "-map=$Map", "-mode=$modePath", "-port=$Port", "-external=$Port", '-MULTIHOME=127.0.0.1',
          '-servername=OrbitalLocalQA', '-serverregion=LOCAL', ('"-userdir=' + $run.FullName + '"'),
          ('"-abslog=' + $enginePath + '"'))
$receipt = [ordered]@{ map = $Map; mode = $Mode; difficulty = $Difficulty; port = $Port; ready = $false; joined = $null; withGameData = $WithGameData.IsPresent }
$work = $run.FullName
$server = $null
$client = $null
$priorLogs = @()
try {
    if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "UDP $Port is occupied" }
    if ($JoinClient) {
        if ($Mode -ne 'PvP' -or $Direct) { throw 'Client acceptance requires a wrapped PvP room' }
        if (Get-Process -Name 'ProjectBoundarySteam-Win64-Shipping' -ErrorAction SilentlyContinue) { throw 'Another game instance is already running' }
        if (!(Get-NetTCPConnection -LocalAddress '127.0.0.1' -State Listen -LocalPort 8000 -ErrorAction SilentlyContinue)) { throw 'Start the isolated loopback profile first' }
        if ($ProfileLog) {
            $profilePath = (Resolve-Path -LiteralPath $ProfileLog).Path
            if (!$profilePath.StartsWith($qaRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Profile log must be inside the isolated QA directory' }
            $profileStart = (Get-Item -LiteralPath $profilePath).Length
        }
    }
    if ($Direct) {
        $server = Start-Process -FilePath $gameExe -WorkingDirectory $binaryRoot -ArgumentList $argv -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    } else {
        $wrapper = if ($LauncherPath) { (Resolve-Path -LiteralPath $LauncherPath).Path } else { Join-Path $binaryRoot 'ServerLauncher/ProjectReboundServerLauncher.exe' }
        $receipt.wrapperSha256 = (Get-FileHash -LiteralPath $wrapper).Hash
        if ($GameSibling) {
            if ($WithGameData -or $InstalledDirectory) { throw 'Select one working directory layout' }
            $work = (New-Item -ItemType Directory -Path (Join-Path $binaryRoot ('.orbital-qa-' + $run.Name))).FullName
            $receipt.gameSibling = $true
        }
        if ($WithGameData) {
            $work = (New-Item -ItemType Directory -Path (Join-Path $run.FullName 'ServerLauncher')).FullName
            foreach ($name in @('DT_ItemType.json','steam_appid.txt')) {
                Copy-Item -LiteralPath (Join-Path $binaryRoot $name) -Destination (Join-Path $run.FullName $name)
            }
        }
        if ($InstalledDirectory) {
            if ($WithGameData -or $LauncherPath) { throw 'InstalledDirectory uses the installed wrapper and settings' }
            $work = Join-Path $binaryRoot 'ServerLauncher'
            $config = Get-Content -LiteralPath (Join-Path $work 'serverconfig.json') -Raw | ConvertFrom-Json
            if ($config.port -ne $Port -or $config.mode -ne $Mode -or $config.map -ne $Map -or $config.offline -ne $true -or ($Mode -eq 'PvE' -and $config.difficulty -ne $Difficulty)) { throw 'Installed configuration differs from the requested offline test' }
            $priorLogs = @(Get-ChildItem -LiteralPath (Join-Path $work 'logs') -File -Filter 'log-*.txt' -ErrorAction SilentlyContinue | ForEach-Object FullName)
            $wrapperArgs = @('-cli')
        } else {
            $config = [ordered]@{ map=$Map; mode=$Mode.ToLowerInvariant(); difficulty=$Difficulty.ToLowerInvariant(); serverName='BRT-QA'; serverRegion='LOCAL'; offline=$true; port=$Port; externalPort=$Port }
            $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'serverconfig.json') -Encoding utf8NoBOM
            $wrapperArgs = if ($GameSibling) { @('-cli') } else { @('-cli', ('"-gameexe=' + $gameExe + '"'), "-port=$Port") }
        }
        $receipt.installedDirectory = $InstalledDirectory.IsPresent
        $server = Start-Process -FilePath $wrapper -WorkingDirectory $work -ArgumentList $wrapperArgs -WindowStyle Hidden -PassThru
    }
    $receipt.pid = $server.Id
    try { $server.PriorityClass = 'BelowNormal' } catch { Write-Verbose $_ }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        if ($server.HasExited) { throw "Server exited with code $($server.ExitCode)" }
        $logs = @($stdoutPath, $enginePath) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
        $logDir = Join-Path $work 'logs'
        if (Test-Path -LiteralPath $logDir -PathType Container) {
            $logs = @($logs) + @(Get-ChildItem -LiteralPath $logDir -Filter 'log-*.txt' -File | ForEach-Object FullName | Where-Object { $_ -notin $priorLogs })
        }
        $body = ($logs | ForEach-Object { [string](Get-Content -LiteralPath $_ -Raw -ErrorAction SilentlyContinue) }) -join "`n"
        if ($body.Contains('[Launcher] Server exited unexpectedly.')) { throw 'Game process exited before the requested readiness checks passed' }
        if ($body.Contains('[SERVER] Server is now listening') -and $body.Contains("[SERVER] Map loaded: $Map") -and $body.Contains($modePath)) {
            $gamePids = @($server.Id)
            if (!$Direct) {
                $gamePids = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($server.Id)" | Where-Object { $_.ExecutablePath -eq $gameExe } | ForEach-Object ProcessId)
            }
            $sockets = @(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue | Where-Object { $gamePids -contains $_.OwningProcess })
            if ($sockets.Count -gt 0) {
                $receipt.ready = $true
                $receipt.addresses = @($sockets | ForEach-Object LocalAddress)
                $receipt.seconds = [math]::Round($clock.Elapsed.TotalSeconds, 1)
                break
            }
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $receipt.ready) { throw "No matching game log and owned UDP socket within $TimeoutSeconds seconds" }
    if ($JoinClient) {
        $userDir = [IO.Directory]::CreateDirectory((Join-Path $run.FullName 'client')).FullName
        $joinArg = if ($ClientEntry -eq 'EngineUrl') { "127.0.0.1:$Port" } else { "-match=127.0.0.1:$Port" }
        $clientArgs = @($joinArg, '-LogicServerURL=http://127.0.0.1:8000', '-debuglog', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash', '-nosound', ('"-userdir=' + $userDir + '"'))
        $client = Start-Process -FilePath $gameExe -WorkingDirectory $binaryRoot -ArgumentList $clientArgs -WindowStyle Hidden -PassThru
        $receipt.clientEntry = $ClientEntry
        $receipt.clientPid = $client.Id
        $receipt.joined = $false
        $clock.Restart()
        while ($clock.Elapsed.TotalSeconds -lt $ClientSeconds) {
            if ($client.HasExited) { throw "Client exited with code $($client.ExitCode)" }
            if ($server.HasExited) { throw 'Server exited while waiting for the client' }
            $body = (($logs | ForEach-Object { Get-Content -LiteralPath $_ -Raw -ErrorAction SilentlyContinue }) -join "`n")
            if ($body -match '\[HEARTBEAT\]\s*PlayerCount\s*=\s*([1-9]\d*)\b') {
                $receipt.joined = $true
                $receipt.playerCount = [int]$Matches[1]
                $receipt.joinSeconds = [math]::Round($clock.Elapsed.TotalSeconds, 1)
                break
            }
            Start-Sleep -Milliseconds 500
        }
        if (!$receipt.joined) { throw 'No positive player count from the live PvP server before the client deadline' }
    }
} catch {
    $receipt.error = $_.Exception.Message
    throw
} finally {
    if ($client -and !$client.HasExited) {
        [void]$client.CloseMainWindow()
        if (!$client.WaitForExit(4000)) { $client.Kill($true); [void]$client.WaitForExit(10000) }
    }
    if ($client -and $profilePath) {
        try {
            $stream = [IO.File]::Open($profilePath,'Open','Read','ReadWrite')
            try {
                $null = $stream.Seek($profileStart,'Begin')
                $r = [IO.StreamReader]::new($stream)
                try { $observed = $r.ReadToEnd() } finally { $r.Dispose() }
            } finally { $stream.Dispose() }
            $receipt.profileLogBytes = [Text.Encoding]::UTF8.GetByteCount($observed)
            $receipt.profileArchiveRead = $observed -match 'GetPlayerArchiveV2'
            $receipt.profileMatchRequest = $observed -match 'StartUnityMatchmaking'
        } catch { $receipt.profileLogError = 'Isolated profile log unavailable' }
    }
    if ($server -and -not $server.HasExited) {
        $server.Kill($true)
        [void]$server.WaitForExit(10000)
    }
    $receipt.stopped = !$server -or $server.HasExited
    if ($GameSibling -and $receipt.gameSibling) {
        $expected = [IO.Path]::GetFullPath((Join-Path $binaryRoot ('.orbital-qa-' + $run.Name)))
        $actual = (Resolve-Path -LiteralPath $work).Path
        if ($actual -ne $expected -or (Get-Item -LiteralPath $actual).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) { throw 'Unrecognized diagnostic directory' }
        Copy-Item -LiteralPath (Join-Path $actual 'logs') -Destination (Join-Path $run.FullName 'logs') -Recurse -ErrorAction SilentlyContinue
        $children = @(Get-ChildItem -LiteralPath $actual -Force -Recurse)
        if (!($children | Where-Object { $_.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint) -or $_.Name -notmatch '^(logs|log-[\d_]+\.txt|serverId\.json|serverconfig\.json)$' })) {
            Remove-Item -LiteralPath $actual -Recurse -Force
        } else { $receipt.work = $actual }
    }
    $receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $receiptPath -Encoding utf8
    Write-Output $receiptPath
}
