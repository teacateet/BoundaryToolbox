#requires -Version 7.0
param([Parameter(Mandatory)][string]$UpstreamDirectory,[Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
# 从固定官方归档逐项取安装器使用的文件。上游有示例玩家档案，不能将原始 ZIP 原样转发。
$groups=@(
    @('rebound-base-stable.zip','75b023aa985adb3156108fcaa9921f5f4c29e4660a49f63720562604c419e0d6','base.zip'),
    @('rebound-0.8.4.zip','fff143b06c2782d559a19c86d32979c09fdeaa29afcccc99a16637c70fc16094','server.zip'),
    @('node-v24.22.3-win-x64.zip','e612af99f626ab415ea225af6bce5c971feb448cb5cae250bb1c3fa50c5f7d9f','node.zip')
)
function Selected([int]$group,[string]$name) {
    switch($group){
        0 { return $name -cin @('Release/dxgi.dll','Release/DT_ItemType.json','Release/steam_appid.txt') }
        1 { return $name -cin @('Payload.dll','project_rebound_version.txt','ServerLauncher/ProjectReboundServerLauncher.exe','ServerLauncher/slint_cpp.dll','ServerLauncher/serverconfig.json','BoundaryMetaServer-main/index.js','BoundaryMetaServer-main/package.json','BoundaryMetaServer-main/package-lock.json','BoundaryMetaServer-main/LICENSE.txt') -or $name.StartsWith('BoundaryMetaServer-main/game/',[StringComparison]::Ordinal) -or $name.StartsWith('BoundaryMetaServer-main/node_modules/',[StringComparison]::Ordinal) }
        2 { return $name -cin @('node-v24.22.3-win-x64/node.exe','node-v24.22.3-win-x64/LICENSE') }
    }
}
$dest=[IO.Directory]::CreateDirectory($OutputDirectory).FullName
for ($i=0;$i -lt $groups.Count;$i++) {
    $inputZip=Join-Path $UpstreamDirectory $groups[$i][0]
    if ((Get-FileHash -LiteralPath $inputZip).Hash -ine $groups[$i][1]) {throw "Incorrect upstream archive: $($groups[$i][0])"}
    $file=Join-Path $dest $groups[$i][2]
    if (Test-Path -LiteralPath $file) {throw "Offline component already exists: $file"}
    $source=[IO.Compression.ZipFile]::OpenRead($inputZip)
    try{
        $selected=[Collections.Generic.SortedDictionary[string,object]]::new([StringComparer]::Ordinal)
        foreach($entry in $source.Entries){
            $name=$entry.FullName
            if(!$entry.Name -or !(Selected $i $name)){continue}
            if($name -match '\\|:|(^|/)(\.\.?|)(/|$)'){throw 'Unexpected entry name'}
            $selected.Add($name,$entry)
        }
        $out=[IO.Compression.ZipFile]::Open($file,[IO.Compression.ZipArchiveMode]::Create)
        try{
            foreach($row in $selected.GetEnumerator()){
                # 内包不二次压缩；固定时间与顺序，内容和 SHA 可从上游重复生成。外层 ZIP 再压缩。
                $part=$out.CreateEntry($row.Key,[IO.Compression.CompressionLevel]::NoCompression)
                $part.LastWriteTime=[datetimeoffset]::new(2000,1,1,0,0,0,[timespan]::Zero);$part.ExternalAttributes=0
                $read=$row.Value.Open();$write=$part.Open()
                try{$read.CopyTo($write)}finally{$write.Dispose();$read.Dispose()}
            }
        }finally{$out.Dispose()}
    }finally{$source.Dispose()}
    [pscustomobject]@{name=$groups[$i][2];files=$selected.Count;sha256=(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $file).Length}
}
