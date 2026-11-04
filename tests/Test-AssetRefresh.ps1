param([Parameter(Mandatory=$true)][string]$OriginalEffectsPath)
. (Join-Path $PSScriptRoot '../scripts/Core.ps1')
$root=$script:ModRoot;$fixture=Join-Path $root ('.work/asset-test-'+[guid]::NewGuid().ToString('N').Substring(0,16));$game=Join-Path $fixture 'wobble'
[IO.Directory]::CreateDirectory($game)|Out-Null
try{
foreach($folder in @('scripts','manifests','assets/hd')){Copy-ModTree (Join-Path $root $folder) (Join-Path $fixture $folder)}
[IO.File]::WriteAllText((Join-Path $fixture 'scripts/Reconcile-Installation.ps1'),'param([switch]$FunctionsOnly,[string]$GameRoot,[switch]$PassThru);function Invoke-ModReconcile { [pscustomobject]@{installed=$true} }')
$processor=Join-Path $fixture 'runtime/audio-clean.exe';[WoBeta.Files]::Parent($processor);[IO.File]::Copy((Join-Path $root 'runtime/audio-clean.exe'),$processor)
[IO.File]::Copy((Join-Path $root 'Play.ps1'),(Join-Path $fixture 'Play.ps1'))
[IO.File]::WriteAllText((Join-Path $game 'Wipeout2097.exe'),'not an executable: RefreshOnly must never launch this')
Write-ModJson (Join-Path $game 'installation.json') @{hd_textures=$true;clean_audio=$true;default_soundtrack='Psx'}
& (Join-Path $fixture 'scripts/Install-Audio.ps1') -SourcePath $OriginalEffectsPath -GameRoot $game
& (Join-Path $fixture 'scripts/Install-Textures.ps1') -SourcePath (Join-Path $fixture 'assets/hd') -GameRoot $game
$sample='ENGINES.WAV';$sound=Join-Path $game ('audio/'+$sample);$cleanHash=Get-ModHash $sound
$source=Join-Path $game ('audio/source/'+$sample);$sourceHash=Get-ModHash $source
if($sourceHash -ne (Get-ModHash (Join-Path $OriginalEffectsPath $sample))){throw 'Pristine audio cache differs from input.'}
$expectedArt=@(Get-ChildItem -LiteralPath (Join-Path $fixture 'assets/hd') -File -Filter '*.png').Count
$art=Join-Path $fixture 'assets/hd/hud-gauges.png';$artHash=Get-ModHash $art
$before=(Get-Item -LiteralPath $sound).LastWriteTimeUtc
foreach($name in @('Wipeout2097.ini','CONFIG.DAT','SAVEGAME.BIN')){[IO.File]::WriteAllText((Join-Path $game $name),'keep my settings and save')}
$result=& (Join-Path $fixture 'scripts/Refresh-Assets.ps1') -GameRoot $game -PassThru
if($result.art_updated -ne 0 -or $result.audio_updated -ne 0 -or (Get-Item -LiteralPath $sound).LastWriteTimeUtc -ne $before){throw 'Unchanged launch recopied or reprocessed assets.'}
[IO.File]::WriteAllBytes($sound,[byte[]](1,2,3))
$result=& (Join-Path $fixture 'scripts/Refresh-Assets.ps1') -GameRoot $game -PassThru
if($result.art_updated -ne 0 -or $result.audio_updated -ne 1 -or (Get-ModHash $sound) -ne $cleanHash -or (Get-ModHash $art) -ne $artHash){throw 'Launch did not repair corrupted outputs.'}
[IO.File]::Copy((Join-Path $fixture 'assets/hd/font-and-numbers.png'),(Join-Path $fixture 'assets/hd/hud-gauges.png'),$true)
$result=& (Join-Path $fixture 'scripts/Refresh-Assets.ps1') -GameRoot $game -PassThru
if($result.art_checked -ne $expectedArt -or [IO.Directory]::Exists((Join-Path $game 'textures/hd'))){throw 'HD artwork should be read directly with no duplicates.'}
Write-ModJson (Join-Path $game 'installation.json') @{hd_textures=$true;clean_audio=$false;default_soundtrack='Psx'}
$result=& (Join-Path $fixture 'scripts/Refresh-Assets.ps1') -GameRoot $game -PassThru
if($result.audio_updated -ne 39 -or (Get-ModHash $sound) -ne $sourceHash){throw 'Original-audio preference was not respected.'}
Write-ModJson (Join-Path $game 'installation.json') @{hd_textures=$true;clean_audio=$true;default_soundtrack='Psx'}
$result=& (Join-Path $fixture 'scripts/Refresh-Assets.ps1') -GameRoot $game -PassThru
if((Get-ModHash $sound) -ne $cleanHash -or (Get-ModHash $source) -ne $sourceHash){throw 'Repeated refresh changed pristine inputs or double-denoised effects.'}
foreach($name in @('Wipeout2097.ini','CONFIG.DAT','SAVEGAME.BIN')){if([IO.File]::ReadAllText((Join-Path $game $name)) -ne 'keep my settings and save'){throw 'Launch refresh changed saves or preferences.'}}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixture 'Play.ps1') -RefreshOnly
if($LASTEXITCODE){throw 'Play did not run the refresh path without launching the executable.'}
Write-Host 'PASS: Play runs installers; unchanged assets skipped, modified artwork and corrupt outputs repaired, audio preferences honored, pristine inputs/saves/settings preserved.'

}finally{
    $full=[IO.Path]::GetFullPath($fixture)
    $allowed=[IO.Path]::GetFullPath((Join-Path $root '.work')).TrimEnd('\')+'\asset-test-'
    if(-not $full.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe asset test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
