param([Parameter(Mandatory)][int]$Port,[Parameter(Mandatory)][string]$Target)
$ErrorActionPreference='Stop'
$game=(Resolve-Path -LiteralPath (Join-Path $Target 'ProjectBoundarySteam-Win64-Shipping.exe')).Path
$until=[DateTime]::UtcNow.AddSeconds(40)
do {
    $owners=@(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue | Select-Object -ExpandProperty OwningProcess -Unique)
    if ($owners.Count -ne 1 -or (Get-Process -Id $owners[0]).Path -ne $game){throw 'Unexpected dedicated server UDP owner'}
    try {
        $map=[IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting("Local\BRT.RoomCount.v1.$($owners[0])",[IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
        try {
            $view=$map.CreateViewAccessor(0,8,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
            try {$word=$view.ReadUInt64(0)} finally {$view.Dispose()}
        } finally {$map.Dispose()}
        $age=[Environment]::TickCount64-($word -shr 16)
        if ((($word -shr 8) -band 255) -eq 1 -and $age -ge 0 -and $age -lt 6500) {
            @{pid=$owners[0];humans=($word -band 255);sampleAgeMs=$age} | ConvertTo-Json -Compress
            return
        }
    } catch [IO.FileNotFoundException] {}
    Start-Sleep -Milliseconds 300
} while ([DateTime]::UtcNow -lt $until)
throw 'No fresh server population sample'
