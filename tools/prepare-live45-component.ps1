#requires -Version 7.0
param([Parameter(Mandatory)][string]$Payload,[Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $Payload).Path
if ((Get-FileHash -LiteralPath $source).Hash -ne '38b24830120df106b636e7ce02163efdc1bd60ae9ab6134936ce515409685ac3') {
    throw 'Expected the live45 release build, without the development probe.'
}
# 唯一改动是 CodeView 调试记录内的 PDB 路径；玩家包不应带维护者机器上的路径。
$bytes = [IO.File]::ReadAllBytes($source)
$latin = [Text.Encoding]::Latin1.GetString($bytes)
$match = [regex]::Matches($latin, '(?<path>[A-Za-z]:\\[^\x00-\x1f]*\\Payload\.pdb)\x00')
if ($match.Count -ne 1) { throw 'Unexpected CodeView path count' }
$offset = $match[0].Index
if ($offset -lt 24 -or $latin.Substring($offset-24,4) -cne 'RSDS') { throw 'Invalid CodeView entry' }
[Array]::Clear($bytes,$offset,$match[0].Length)
$name = [Text.Encoding]::ASCII.GetBytes('Payload.pdb')
[Array]::Copy($name,0,$bytes,$offset,$name.Length)
$out = [IO.Path]::GetFullPath($Destination)
if ($out -eq $source) { throw 'Output must be separate' }
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($out))
[IO.File]::WriteAllBytes($out,$bytes)
Get-FileHash -LiteralPath $out | Select-Object Hash,Path
