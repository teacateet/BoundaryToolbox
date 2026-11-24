#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceRoot,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('v084', 'main')][string]$Version = 'main',
    [switch]$IncludeBirthTrace,
    [switch]$IncludePreorderReady,
    [switch]$IncludeOwnerGameplay,
    [switch]$IncludeNativeArchive,
    [switch]$IncludeFirstLoadout,
    [switch]$IncludePveLayers,
    [switch]$IncludeDevRig,
    [ValidatePattern('^[a-zA-Z0-9_-]{1,32}$')][string]$BuildLabel = 'live34'
)
$ErrorActionPreference = 'Stop'
if ($IncludePreorderReady -and $Version -ne 'main') {
    throw 'Preorder readiness currently supports the main source.'
}
if ($IncludeOwnerGameplay -and !($Version -eq 'main' -and $IncludeBirthTrace -and $IncludePreorderReady)) {
    throw 'Owner gameplay requires main, birth trace and guarded preorders.'
}
if ($IncludeNativeArchive -and !$IncludeOwnerGameplay) {
    throw "Native archive requires owner gameplay."
}
if ($IncludePveLayers -and !$IncludeNativeArchive) {
    throw 'The local PvE integration requires the native archive and owner gameplay hooks.'
}
$kits = Split-Path -Parent $PSScriptRoot
$module = if ($Version -eq 'v084') { 'Payload' } else { 'ProjectReboundMainDLL' }
$layout = if ($Version -eq 'v084') { 'v084' } else { 'split' }
$commit = if ($Version -eq 'v084') { 'b3d7f83f3789111d4a460b9662a8dc4e06e6a934' } else { '74c68ac3e6007c36248c442a281c2b7b831cc53a' }
$expectedProject = if ($Version -eq 'v084') { 'ae80fa85d92465b277a81da740aa014a25bd9e38' } else { 'b80ba707d944f2d65d6d1684652452b928b023ec' }
$original = Join-Path (Resolve-Path -LiteralPath $SourceRoot).Path $module
$projectText = [IO.File]::ReadAllText((Join-Path $original 'Payload.vcxproj')).Replace("`r`n", "`n")
$projectBytes = [Text.Encoding]::UTF8.GetBytes($projectText)
$blob = [Text.Encoding]::UTF8.GetBytes("blob $($projectBytes.Length)`0") + $projectBytes
$projectHash = [Convert]::ToHexString([Security.Cryptography.SHA1]::HashData($blob)).ToLowerInvariant()
if ($projectHash -ne $expectedProject) { throw 'Unexpected upstream project; use the pinned clean source archive.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory already exists; use a new isolated directory.' }
$out = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$source = (New-Item -ItemType Directory -Path (Join-Path $out 'source')).FullName
Copy-Item -LiteralPath $original -Destination (Join-Path $source $module) -Recurse
$top = Join-Path $source $module
$project = Join-Path $top 'Payload.vcxproj'
# Git 的 Windows 导出可能使用 CRLF；补丁基线统一为相同的 LF 文本。
Get-ChildItem -LiteralPath $top -File -Recurse | Where-Object { $_.Extension -in @('.cpp', '.h', '.hpp', '.c', '.inl', '.vcxproj') } | ForEach-Object {
    $text = [IO.File]::ReadAllText($_.FullName).Replace("`r`n", "`n")
    [IO.File]::WriteAllText($_.FullName, $text, [Text.UTF8Encoding]::new($false))
}
& git -C $source init --quiet
if ($LASTEXITCODE) { throw 'Cannot initialize isolated patch workspace.' }
$patches = @(
    "community-inventory/$Version-community.patch",
    "community-weapon-birth/$Version-birth.patch",
    "community-grapple/$Version-community.patch",
    "inventory-reconnect/$Version-reconnect.patch",
    "first-life/$Version.patch",
    "community-presentation/$layout.patch",
    "community-reticle/$layout.patch",
    "community-radar/$layout.patch",
    "community-death-menu/$layout.patch",
    "community-death-menu/sdk-$layout.patch",
    "community-pod-view/$layout.patch",
    "community-grapple-input/$layout.patch"
)
$manifest = [ordered]@{ version = $Version; upstreamCommit = $commit; patches = @(); headers = @(); runtime = 'Not installed or run in a game.' }
foreach ($relative in $patches) {
    $patch = Join-Path $kits $relative
    & git -C $source -c core.autocrlf=false apply --check $patch
    if ($LASTEXITCODE) { throw "Patch does not apply: $relative" }
    & git -C $source -c core.autocrlf=false apply --whitespace=nowarn $patch
    if ($LASTEXITCODE) { throw "Patch failed: $relative" }
    $manifest.patches += [ordered]@{ path = $relative; sha256 = (Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant() }
}
$headers = @(
    'community-reticle/SparrowReticle.hpp',
    'community-radar/MarkedHeadRadar.hpp',
    'community-radar/UiJournal.hpp',
    'community-death-menu/DeathMenu.hpp',
    'community-death-menu/DeathMenu.sdk.hpp',
    'community-pod-view/PodView.hpp',
    'community-pod-view/ShoulderSlots.hpp',
    'community-grapple-input/GrappleInput.hpp'
)
if ($Version -eq 'v084') { $headers += @('community-death-menu/DeathMenuTravel.hpp', 'community-death-menu/DeathMenu.v084.hpp') }
$uiDirectory = Join-Path $top $(if ($Version -eq 'v084') { 'Utility' } else { 'Client' })
foreach ($relative in $headers) {
    $header = Join-Path $kits $relative
    Copy-Item -LiteralPath $header -Destination (Join-Path $uiDirectory (Split-Path -Leaf $header))
    $manifest.headers += [ordered]@{ path = $relative; sha256 = (Get-FileHash -LiteralPath $header).Hash.ToLowerInvariant() }
}
if ($IncludeBirthTrace) {
    $tracePatch = Join-Path $kits "community-birth-trace/$Version.patch"
    $traceHeader = Join-Path $kits 'community-birth-trace/BirthTrace.hpp'
    & git -C $source -c core.autocrlf=false apply --check $tracePatch
    if ($LASTEXITCODE) { throw 'Birth trace patch does not apply.' }
    & git -C $source -c core.autocrlf=false apply --whitespace=nowarn $tracePatch
    if ($LASTEXITCODE) { throw 'Birth trace patch failed.' }
    Copy-Item -LiteralPath $traceHeader -Destination (Join-Path $top 'BrtInventory/BirthTrace.hpp')
    $manifest.birthTrace = [ordered]@{ patchSha256 = (Get-FileHash -LiteralPath $tracePatch).Hash.ToLowerInvariant(); headerSha256 = (Get-FileHash -LiteralPath $traceHeader).Hash.ToLowerInvariant() }
}
$ownerModules = @()
if ($IncludePreorderReady) { $ownerModules += 'community-preorder-ready' }
if ($IncludeOwnerGameplay) { $ownerModules += @('community-owner-menu', 'community-owner-birth') }
if ($IncludeNativeArchive) { $ownerModules += @('community-native-archive', 'community-archive-readback') }
if ($Version -eq 'main') { $ownerModules += 'community-local-login' }
if ($IncludePveLayers) { $ownerModules += @('community-pve-layers','community-world-driver','community-airlock','community-grapple-delivery','community-disconnected-player','community-redeploy') }
if ($IncludeFirstLoadout) { $ownerModules += 'community-first-loadout' }
# 开发探针只用于本机联调，带探针的构建不得安装给玩家。
if ($IncludeDevRig) { $ownerModules += 'dev-rig' }
foreach ($ownerModule in $ownerModules) {
    $moduleDirectory = Join-Path $kits $ownerModule
    $modulePatch = Join-Path $moduleDirectory 'main.patch'
    & git -C $source -c core.autocrlf=false apply --check $modulePatch
    if ($LASTEXITCODE) { throw "Patch does not apply: $ownerModule/main.patch" }
    & git -C $source -c core.autocrlf=false apply --whitespace=nowarn $modulePatch
    if ($LASTEXITCODE) { throw "Patch failed: $ownerModule/main.patch" }
    $manifest.patches += [ordered]@{ path = "$ownerModule/main.patch"; sha256 = (Get-FileHash -LiteralPath $modulePatch).Hash.ToLowerInvariant() }
    foreach ($header in Get-ChildItem -LiteralPath $moduleDirectory -Filter '*.hpp' -File) {
        Copy-Item -LiteralPath $header.FullName -Destination (Join-Path $top ('BrtInventory/' + $header.Name))
        $manifest.headers += [ordered]@{ path = "$ownerModule/" + $header.Name; sha256 = (Get-FileHash -LiteralPath $header.FullName).Hash.ToLowerInvariant() }
    }
}
# 在已有服务器及客户端日志中记录真实加载的补丁集合。
$entry = Join-Path $top $(if ($Version -eq 'v084') { 'dllmain.cpp' } else { 'Core/Entry.cpp' })
$code = [IO.File]::ReadAllText($entry)
$marker = "[BRT-PAYLOAD] $BuildLabel source=$commit patches=$($manifest.patches.Count)"
if ($IncludeBirthTrace) { $marker += ' birth-trace=1' }
if ($IncludePreorderReady) { $marker += ' preorder-ready=1' }
if ($IncludeOwnerGameplay) { $marker += ' owner-gameplay=1' }
if ($IncludeFirstLoadout) { $marker += ' first-loadout=1' }
if ($IncludePveLayers) { $marker += ' pve-layers=1' }
if ($IncludeNativeArchive) { $marker += ' archive=1' }
if ($IncludeDevRig) { $marker += ' dev-rig=1' }
$serverPoint = 'ServerLog("[SERVER] Hooks installed.");'
$clientPoint = 'InitClientHook();'
if ($code.Split($serverPoint).Count -ne 2 -or $code.Split($clientPoint).Count -ne 2) { throw 'Unexpected initialization source.' }
$code = $code.Replace($serverPoint, $serverPoint + "`n            ServerLog(`"$marker role=server`" );")
$code = $code.Replace($clientPoint, $clientPoint + "`n            ClientLog(`"$marker role=client`" );`n            if (clientLogFile.is_open()) { clientLogFile << `"$marker role=client\n`"; clientLogFile.flush(); }")
[IO.File]::WriteAllText($entry, $code, [Text.UTF8Encoding]::new($false))
$manifest.marker = $marker
$manifest.entry = [ordered]@{ path = $entry; sha256 = (Get-FileHash -LiteralPath $entry).Hash.ToLowerInvariant() }
# 编译设置只写入隔离副本。补齐新业务头文件搜索路径与大 SDK 所需的编译参数。
[xml]$xml = [IO.File]::ReadAllText($project)
$ns = $xml.DocumentElement.NamespaceURI
$group = $xml.CreateElement('ItemDefinitionGroup', $ns)
$compile = $xml.CreateElement('ClCompile', $ns)
foreach ($setting in @(
    @('AdditionalOptions', '/utf-8 /bigobj %(AdditionalOptions)'),
    @('AdditionalIncludeDirectories', '$(ProjectDir);$(ProjectDir)Libs;%(AdditionalIncludeDirectories)'),
    @('MultiProcessorCompilation', 'true')
)) {
    $node = $xml.CreateElement($setting[0], $ns)
    $node.InnerText = $setting[1]
    [void]$compile.AppendChild($node)
}
[void]$group.AppendChild($compile)
$targets = @($xml.Project.Import | Where-Object { $_.Project -like '*Microsoft.Cpp.targets' })[0]
[void]$xml.DocumentElement.InsertBefore($group, $targets)
$xml.Save($project)
$inputs = @($xml.Project.ItemGroup.ClCompile | Where-Object { $_.Include } | ForEach-Object { $_.Include })
foreach ($file in $inputs) { if (!(Test-Path -LiteralPath (Join-Path $top $file))) { throw "Missing translation unit: $file" } }
$manifest.translationUnits = $inputs
$manifestPath = Join-Path $out 'build-manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild)) { throw 'MSBuild is unavailable.' }
$binaryDirectory = Join-Path $out 'bin'
$intermediateDirectory = Join-Path $out 'obj'
$arguments = @($project, '/nologo', '/m:2', '/t:Build', '/p:Configuration=Release', '/p:Platform=x64', "/p:OutDir=$binaryDirectory\", "/p:IntDir=$intermediateDirectory\", '/v:minimal', '/clp:ErrorsOnly', '/fl', "/flp:logfile=$out\msbuild.log;verbosity=normal;encoding=UTF-8")
$manifest.command = @($msbuild) + $arguments
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
& $msbuild @arguments
$manifest.exitCode = $LASTEXITCODE
if ($LASTEXITCODE -eq 0) {
    $dll = Get-Item -LiteralPath (Join-Path $binaryDirectory 'Payload.dll')
    $manifest.dll = [ordered]@{ path = $dll.FullName; bytes = $dll.Length; sha256 = (Get-FileHash -LiteralPath $dll.FullName).Hash.ToLowerInvariant() }
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
if ($manifest.exitCode) { throw "Payload build failed; see $out\msbuild.log" }
$manifest.dll | ConvertTo-Json
