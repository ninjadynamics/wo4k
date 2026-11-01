param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[switch]$PassThru)
. (Join-Path $PSScriptRoot 'Core.ps1')
$GameRoot=[IO.Path]::GetFullPath($GameRoot).TrimEnd('\')
foreach($process in @(Get-Process Wipeout2097 -ErrorAction SilentlyContinue)){
    if(-not $process.Path -or $process.Path.StartsWith($GameRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Close the game before refreshing its artwork or sound effects.'}
}
$layout=& (Join-Path $PSScriptRoot 'Repair-Layout.ps1') -GameRoot $GameRoot -PassThru
$receiptPath=Join-Path $GameRoot 'installation.json'
if(-not [IO.File]::Exists($receiptPath)){throw 'Run Setup / Install / repair before playing.'}
$receipt=Get-Content -LiteralPath $receiptPath -Encoding UTF8 -Raw|ConvertFrom-Json
$videos=& (Join-Path $PSScriptRoot 'Install-Videos.ps1') -GameRoot $GameRoot -PassThru
$source=Join-Path $GameRoot 'audio/source'
if(-not [IO.Directory]::Exists($source)){throw 'Run Setup / Install / repair once to retain the original effects for automatic audio refresh.'}
$art=[pscustomobject]@{checked=0;updated=0}
if($receipt.hd_textures){$art=& (Join-Path $PSScriptRoot 'Install-Textures.ps1') -SourcePath (Join-Path $script:ModRoot 'assets/hd') -GameRoot $GameRoot -Incremental -PassThru}
$audio=& (Join-Path $PSScriptRoot 'Install-Audio.ps1') -SourcePath $source -GameRoot $GameRoot -Original:(-not $receipt.clean_audio) -Incremental -PassThru
$result=[pscustomobject]@{at=(Get-Date -Format o);state='complete';layout=$layout.layout;obsolete_files_removed=$layout.removed;videos_converted=$videos.converted;art_checked=$art.checked;art_updated=$art.updated;audio_checked=$audio.checked;audio_updated=$audio.updated}
Write-ModJson (Join-Path $script:ModRoot 'logs/asset-refresh.json') $result
if($PassThru){$result}
