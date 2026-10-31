param([Parameter(Mandatory=$true)][string]$GameSource,[string]$PsxMusicSource,[string]$PcImage)
. (Join-Path $PSScriptRoot '../scripts/Sources.ps1')
$work=Join-Path $script:ModRoot ('.work/optional-music-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$fixture=Join-Path $work 'mod';[IO.Directory]::CreateDirectory($fixture)|Out-Null
foreach($name in @('scripts','runtime','assets','patches','manifests')){Copy-ModTree (Join-Path $script:ModRoot $name) (Join-Path $fixture $name)}
[IO.File]::Copy((Join-Path $script:ModRoot 'VERSION'),(Join-Path $fixture 'VERSION'))
function Install-Case([string]$Bank,[string]$Psx=''){
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $fixture 'scripts/Install-Mod.ps1'),'-GameSource',$GameSource,'-DefaultSoundtrack',$Bank)
    if($Psx){$arguments+=@('-PsxMusicSource',$Psx)}
    & powershell.exe @arguments | Out-Host
    if($LASTEXITCODE){throw "Optional-music installation failed: $Bank"}
    $receipt=Get-Content (Join-Path $fixture 'wobble/installation.json') -Encoding UTF8 -Raw|ConvertFrom-Json
    if($receipt.pc_music_available){throw 'Fixture must use a data-only PC source.'}
    if(-not (Test-Path (Join-Path $fixture 'wobble/audio/ENGINES.WAV'))){throw 'Sound effects must remain available without music.'}
    return $receipt
}
$receipt=Install-Case Pc
if($receipt.default_soundtrack -ne 'None'){throw 'Missing PC music must fall back to No music.'}
$ini=[IO.File]::ReadAllText((Join-Path $fixture 'wobble/Wipeout2097.ini'))
if($ini -notmatch '(?m)^MusicVolume=0\s*$'){throw 'No-music installation must start silently.'}
if($PsxMusicSource){
    $receipt=Install-Case Psx $PsxMusicSource
    if($receipt.default_soundtrack -ne 'Psx' -or -not $receipt.playstation_available){throw 'PS1-only music must work without PC music.'}
    $ini=[IO.File]::ReadAllText((Join-Path $fixture 'wobble/Wipeout2097.ini'))
    if($ini -notmatch '(?m)^MusicVolume=100\s*$'){throw 'Switching from No music to PS1 must restore audibility.'}
}
if($PcImage){
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixture 'scripts/Install-Mod.ps1') -GameSource $PcImage -DefaultSoundtrack Pc | Out-Host
    if($LASTEXITCODE){throw 'Single-source full-PC-disc install failed.'}
    $receipt=Get-Content (Join-Path $fixture 'wobble/installation.json') -Encoding UTF8 -Raw|ConvertFrom-Json
    if(-not $receipt.pc_music_available -or $receipt.default_soundtrack -ne 'Pc'){throw 'Full PC source must supply its own music automatically.'}
}
Write-Host 'PASS: data-only PC, PS1-only music, full PC single-source music, effect retention and silent-to-music repair.'
