param([Parameter(Mandatory=$true)][string]$GameSource,[Parameter(Mandatory=$true)][string]$PsxData)
. (Join-Path $PSScriptRoot '../scripts/Configuration.ps1')
$root=$script:ModRoot;$work=Join-Path $root ('.work/reconciliation-'+[guid]::NewGuid().ToString('N'))
$mod=Join-Path $work 'mod';$game=Join-Path $mod 'wobble';$psx=Join-Path $work 'ps1 source'
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Play-Check {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $mod 'Play.ps1') -RefreshOnly -NoErrorDialog
    Assert ($LASTEXITCODE -eq 0) 'Play reconciliation failed.'
}
try{
    foreach($dir in @('scripts','runtime','assets','patches','manifests')){Copy-ModTree (Join-Path $root $dir) (Join-Path $mod $dir)}
    foreach($file in @('VERSION','Play.ps1')){[IO.File]::Copy((Join-Path $root $file),(Join-Path $mod $file))}
    [IO.Directory]::CreateDirectory($psx)|Out-Null
    [IO.File]::Copy($PsxData,(Join-Path $psx 'game.bin'))
    # Small valid PCM tracks exercise the exact installed-bank path without
    # redistributing or repeatedly copying an entire commercial soundtrack.
    $music=Join-Path $psx 'music/psx';[IO.Directory]::CreateDirectory($music)|Out-Null
    [IO.File]::WriteAllText((Join-Path $music 'tracks.ini'),"[CD]`r`nTracks=12`r`n")
    $wav=[Convert]::FromBase64String('UklGRigAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQQAAAAAAAAA')
    for($i=2;$i -le 12;++$i){[IO.File]::WriteAllBytes((Join-Path $music ('track{0:D2}.wav' -f $i)),$wav)}
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $mod 'scripts/Install-Mod.ps1') -GameSource $GameSource -DefaultSoundtrack None -NoErrorDialog
    Assert ($LASTEXITCODE -eq 0) 'Initial PC-only install failed.'
    $config=Get-ModConfiguration $mod;$config.PsxMusicSource=$psx;$config.DefaultSoundtrack='Psx';$null=Save-ModConfiguration $config $mod
    $atlas=Join-Path $game 'textures/sd/Pics/Hi-res/NRACE01.TGA';$pcHash=Get-ModHash $atlas
    Play-Check
    $receipt=Get-Content -LiteralPath (Join-Path $game 'installation.json') -Raw|ConvertFrom-Json
    Assert ($receipt.playstation_textures -and $receipt.playstation_available -and $receipt.default_soundtrack -eq 'Psx') 'Configured PS1 textures/music were not injected on launch.'
    $ps1Hash=Get-ModHash $atlas
    $selection=Get-Content -LiteralPath (Join-Path $game 'textures/selection.json') -Raw|ConvertFrom-Json
    Assert ($selection.ps1_regions -eq 402 -and $selection.pending_ps1_regions -eq 0) 'Configured PS1 regions were not fully applied.'
    $track=Join-Path $game 'music/psx/track02.wav';$trackHash=Get-ModHash $track
    $time=(Get-Item -LiteralPath $atlas).LastWriteTimeUtc;$trackTime=(Get-Item -LiteralPath $track).LastWriteTimeUtc
    Play-Check
    Assert ((Get-Item -LiteralPath $atlas).LastWriteTimeUtc -eq $time -and (Get-Item -LiteralPath $track).LastWriteTimeUtc -eq $trackTime) 'Healthy files were unnecessarily rewritten.'
    [IO.File]::WriteAllBytes($atlas,[byte[]](1,2,3));[IO.File]::Delete($track)
    [IO.File]::WriteAllText((Join-Path $game 'wohr.dll'),'outdated DLL')
    Play-Check
    Assert ((Get-ModHash $atlas) -eq $ps1Hash -and (Get-ModHash $track) -eq $trackHash) 'Damaged/missing PS1 outputs were not repaired.'
    Assert ((Get-ModHash (Join-Path $game 'wohr.dll')) -eq (Get-ModHash (Join-Path $mod 'runtime/wohr.dll'))) 'Manually updated runtime was not synchronized.'
    # An installed, verified bank and texture cache work with the disc offline.
    [IO.Directory]::Move($psx,($psx+' offline'))
    Play-Check
    [IO.File]::WriteAllBytes($atlas,[byte[]](1,2,3));Play-Check
    Assert ((Get-ModHash $atlas) -eq $ps1Hash) 'Cached PS1 data could not repair an atlas while the disc was offline.'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $mod 'scripts/Reconcile-Installation.ps1') -ForceRepair -NoErrorDialog
    Assert ($LASTEXITCODE -eq 0) 'Forced repair could not reuse an offline disc cache.'
    [IO.Directory]::Move(($psx+' offline'),$psx)
    # Both a manual version overwrite and the Update fail-safe preserve saves.
    $save=Join-Path $game 'SAVEGAME.BIN';[IO.File]::WriteAllText($save,'my saved race')
    $graphics=Join-Path $game 'DDrawCompat-Wipeout2097.ini';[IO.File]::AppendAllText($graphics,"`r`nVSync = on`r`n")
    [IO.File]::WriteAllText((Join-Path $mod 'VERSION'),'0.1.0-test-next');Play-Check
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $mod 'scripts/Reconcile-Installation.ps1') -ForceRepair -NoErrorDialog
    Assert ($LASTEXITCODE -eq 0) 'Update install/repair fail-safe failed.'
    Assert ([IO.File]::ReadAllText($save) -eq 'my saved race') 'Repair changed a save.'
    Assert ([IO.File]::ReadAllText($graphics).Contains('VSync = on')) 'Repair discarded graphics preferences.'
    [IO.File]::Delete((Join-Path $game 'Wipeout2097.exe'));Play-Check
    Assert ([IO.File]::Exists((Join-Path $game 'Wipeout2097.exe'))) 'Play could not repair a missing executable.'
    [IO.File]::Delete((Join-Path $game 'audio/source/ENGINES.WAV'));Play-Check
    Assert ([IO.File]::Exists((Join-Path $game 'audio/source/ENGINES.WAV'))) 'Play could not repair a missing original effect.'
    $sky=Join-Path $game 'textures/sd/Pics/Hi-res/SKY01.TGA';$skyHash=Get-ModHash $sky
    [IO.File]::Delete($sky);Play-Check
    Assert ((Get-ModHash $sky) -eq $skyHash) 'Play could not repair a missing native sky texture.'
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $mod '.work') -Directory -Filter 'ensure-*').Count -eq 0) 'Successful reconciliation left temporary extraction directories.'
    Write-Host 'PASS: Play injects PS1 artwork/OST from saved configuration, skips healthy files, repairs damaged/missing assets and runtime, tolerates an offline disc, repairs manual updates/missing EXE, and preserves saves/settings during forced repair.'
}finally{
    $full=[IO.Path]::GetFullPath($work);$prefix=[IO.Path]::GetFullPath((Join-Path $root '.work')).TrimEnd('\')+'\reconciliation-'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe reconciliation test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
