param([Parameter(Mandatory=$true)][string]$Image)
. (Join-Path $PSScriptRoot '../scripts/Core.ps1')
$dest=Join-Path $script:ModRoot '.work/disc-native'
Import-ModDisc ([IO.Path]::GetFullPath($Image)) $dest
Get-ModHash (Join-Path $dest 'D3D.EXE')
