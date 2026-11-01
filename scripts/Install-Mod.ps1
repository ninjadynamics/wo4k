param([string]$GameSource,[string]$PcMusicSource,[string]$PsxMusicSource,[ValidateSet('Pc','Psx','None')][string]$DefaultSoundtrack='Pc',[string]$RequestFile,[string]$StatusFile,[switch]$OriginalAudio,[switch]$SdOnly,[switch]$NoErrorDialog)
. (Join-Path $PSScriptRoot 'Diagnostics.ps1')
$null=Start-WoLog 'install' (Join-Path $PSScriptRoot '..')
try {
. (Join-Path $PSScriptRoot 'Sources.ps1')
if($RequestFile){$request=Get-Content -Encoding UTF8 -LiteralPath $RequestFile -Raw|ConvertFrom-Json;$GameSource=$request.GameSource;$PcMusicSource=$request.PcMusicSource;$PsxMusicSource=$request.PsxMusicSource;$DefaultSoundtrack=$request.DefaultSoundtrack;$OriginalAudio=-not $request.CleanAudio;$SdOnly=-not $request.HdTextures}
function Set-InstallStatus([int]$Percent,[string]$Message,[string]$State='busy') {
    Write-Host $Message
    Write-WoLog 'install_stage' @{percent=$Percent;message=$Message;state=$State}
    if($StatusFile){Write-ModJson $StatusFile ([ordered]@{percent=$Percent;message=$Message;state=$State})}
}
$session=Join-Path $script:ModRoot ('.work/i-'+[guid]::NewGuid().ToString('N').Substring(0,16))
$stage=Join-Path $session 'wobble';$target=Join-Path $script:ModRoot 'wobble';$backup=$null
    if(-not $GameSource){throw 'Select the Windows game source.'}
    foreach($p in @(Get-Process -Name Wipeout2097 -ErrorAction SilentlyContinue)){if($p.Path -and $p.Path.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Close this mod installation before updating it.'}}
    Set-InstallStatus 3 'Verifying sources'
    [IO.Directory]::CreateDirectory($stage)|Out-Null
    $native=Get-ModNativeRoot $GameSource (Join-Path $session 'game-source')
    Set-InstallStatus 12 'Copying game data and applying the executable patch'
    Copy-ModGameData $native $stage;Install-ModExecutable $native $stage
    # Existing drop-in MP4s belong to the user; repair carries them forward.
    foreach($name in @('intro','WXLINTR','XTRO1','XTRO2','XTRO3','XTRO4')){
        $oldMovie=Join-Path $target ('movies/'+$name+'.mp4')
        if([IO.File]::Exists($oldMovie)){
            $newMovie=Get-ModChild $stage ('movies/'+$name+'.mp4');[WoBeta.Files]::Parent($newMovie);[IO.File]::Copy($oldMovie,$newMovie,$true)
            $oldCopy=Get-ModChild $stage ('movies/'+$(if($name -eq 'intro'){'WXLINTR'}else{$name})+'.MPG')
            if([IO.File]::Exists($oldCopy)){[IO.File]::Delete($oldCopy)}
        }
    }
    & (Join-Path $PSScriptRoot 'Install-Videos.ps1') -GameRoot $stage
    Set-InstallStatus 24 'Extracting SD textures'
    $sd=Get-ModChild $stage 'textures/sd'
    & (Join-Path $PSScriptRoot 'Extract-Textures.ps1') -SourcePath $native -Destination $sd -NoPng | Out-Null
    Add-ModRegionalAliases $sd
    Set-InstallStatus 34 'Processing sound effects locally'
    $audio=Join-Path $session 'audio-original'
    & (Join-Path $PSScriptRoot 'Extract-Audio.ps1') -SourcePath $native -Destination $audio | Out-Null
    & (Join-Path $PSScriptRoot 'Install-Audio.ps1') -SourcePath $audio -GameRoot $stage -Original:$OriginalAudio
    Set-InstallStatus 46 'Extracting the PC soundtrack'
    $pcSource=if($PcMusicSource){$PcMusicSource}elseif(Find-ModMusicFolder $native Pc){$native}else{$GameSource}
    $pc=Join-Path $session 'pc-music'
    & (Join-Path $PSScriptRoot 'Extract-Music.ps1') -SourcePath $pcSource -Destination $pc -Bank Pc -WorkRoot (Join-Path $session 'pc-source') -AllowMissing:(-not [bool]$PcMusicSource) | Out-Null
    $pcAvailable=Test-Path -LiteralPath (Join-Path $pc 'tracks.ini')
    if($pcAvailable){& (Join-Path $PSScriptRoot 'Install-Music.ps1') -SourcePath $pc -GameRoot $stage -Bank Pc}
    else{Set-InstallStatus 58 'No PC music included; continuing without it'}
    $psxSource=$PsxMusicSource
    if(-not $psxSource -and (Find-ModMusicFolder $native Psx)){$psxSource=$native}
    if(-not $psxSource -and (Find-ModMusicFolder $target Psx)){$psxSource=$target}
    if($psxSource){
        Set-InstallStatus 64 'Extracting the optional PlayStation soundtrack'
        $psx=Join-Path $session 'psx-music'
        & (Join-Path $PSScriptRoot 'Extract-Music.ps1') -SourcePath $psxSource -Destination $psx -Bank Psx -WorkRoot (Join-Path $session 'psx-source') | Out-Null
        & (Join-Path $PSScriptRoot 'Install-Music.ps1') -SourcePath $psx -GameRoot $stage -Bank Psx
    } elseif($DefaultSoundtrack -eq 'Psx'){throw 'PlayStation default selected, but no PlayStation soundtrack source was provided.'}
    if($DefaultSoundtrack -eq 'Pc' -and -not $pcAvailable){$DefaultSoundtrack=if($psxSource){'Psx'}else{'None'}}
    Set-InstallStatus 82 'Installing the mod runtime and artwork'
    foreach($name in @('wohr.dll','ddraw.dll','wo2097-controller.exe','DDrawCompat-Wipeout2097.ini')) {
        [IO.File]::Copy((Join-Path $script:ModRoot ('runtime/'+$name)),(Get-ModChild $stage $name),$true)
    }
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $script:ModRoot 'runtime') -File -Filter 'lib*.dll'){[IO.File]::Copy($file.FullName,(Get-ModChild $stage $file.Name),$true)}
    foreach($name in @('w2097-hd.cg','w2097-hd.cgp')){$out=Get-ModChild $stage ('common-shaders-master/w2097/'+$name);[WoBeta.Files]::Parent($out);[IO.File]::Copy((Join-Path $script:ModRoot ('runtime/'+$name)),$out,$true)}
    if(-not $SdOnly){& (Join-Path $PSScriptRoot 'Install-Textures.ps1') -SourcePath (Join-Path $script:ModRoot 'assets/hd') -GameRoot $stage}
    $settings=Get-ModChild $stage 'Wipeout2097.ini'
    if(-not [IO.File]::Exists($settings)){[IO.File]::Copy((Join-Path $script:ModRoot 'runtime/Wipeout2097.ini'),$settings)}
    # Existing saves/preferences belong to the user; repair preserves them.
    foreach($name in @('CONFIG.DAT','SAVEGAME.BIN','Wipeout2097.ini')){$old=Get-ModChild $target $name;if([IO.File]::Exists($old)){[IO.File]::Copy($old,(Get-ModChild $stage $name),$true)}}
    $ini=[IO.File]::ReadAllText($settings)
    if($ini -match '(?im)^Soundtrack\s*='){$ini=[regex]::Replace($ini,'(?im)^Soundtrack\s*=.*$',('Soundtrack='+[int]($DefaultSoundtrack -eq 'Psx')))}else{$ini+="`r`n[Audio]`r`nSoundtrack=$([int]($DefaultSoundtrack -eq 'Psx'))`r`n"}
    if($DefaultSoundtrack -eq 'None'){$ini=[regex]::Replace($ini,'(?im)^MusicVolume\s*=.*$','MusicVolume=0')}
    elseif(Test-Path -LiteralPath (Join-Path $target 'installation.json')){
        $prior=Get-Content -LiteralPath (Join-Path $target 'installation.json') -Encoding UTF8 -Raw|ConvertFrom-Json
        if($prior.default_soundtrack -eq 'None'){$ini=[regex]::Replace($ini,'(?im)^MusicVolume\s*=.*$','MusicVolume=100')}
    }
    [IO.File]::WriteAllText($settings,$ini,[Text.Encoding]::ASCII)
    Set-InstallStatus 93 'Verifying the completed installation'
    $required=@('Wipeout2097.exe','wohr.dll','ddraw.dll','Common/MENU.DAT','Track01/TRACK.WAD','textures/sd/Pics/Hi-res/HUD2.tga','audio/ENGINES.WAV')
    foreach($name in $required){if(-not [IO.File]::Exists((Get-ModChild $stage $name))){throw "Installation is incomplete: $name"}}
    $manifest=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $script:ModRoot 'manifests/patches.json') -Raw|ConvertFrom-Json
    if((Get-ModHash (Join-Path $stage 'Wipeout2097.exe')) -ne $manifest.output_sha256){throw 'Final executable checksum failed.'}
    $receipt=[ordered]@{version=([IO.File]::ReadAllText((Join-Path $script:ModRoot 'VERSION')).Trim());installed_at=(Get-Date -Format o);default_soundtrack=$DefaultSoundtrack;pc_music_available=[bool]$pcAvailable;playstation_available=[bool]$psxSource;hd_textures=(-not $SdOnly);clean_audio=(-not $OriginalAudio);executable_sha256=$manifest.output_sha256}
    Write-ModJson (Join-Path $stage 'installation.json') $receipt
    if([IO.Directory]::Exists($target)){
        $backup=Get-ModChild $script:ModRoot ('backups/wobble-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'));[WoBeta.Files]::Parent($backup)
        # Both resolved paths are inside modroot; never move a supplied source.
        if(-not $target.StartsWith($script:ModRoot+'\') -or -not $backup.StartsWith($script:ModRoot+'\')){throw 'Unsafe install move.'}
        [IO.Directory]::Move($target,$backup)
    }
    try{[IO.Directory]::Move($stage,$target)}catch{if($backup -and -not [IO.Directory]::Exists($target)){[IO.Directory]::Move($backup,$target)};throw}
    # Only this generated session is eligible for automatic recursive cleanup.
    $workBase=[IO.Path]::GetFullPath((Join-Path $script:ModRoot '.work')).TrimEnd('\')
    $ownedSession=[IO.Path]::GetFullPath($session)
    if(-not $ownedSession.StartsWith($workBase+'\i-',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe cleanup target.'}
    try{[IO.Directory]::Delete($ownedSession,$true)}catch{Write-Verbose ('Temporary extraction retained: '+$_.Exception.Message)}
    Set-InstallStatus 100 'Ready to race' 'complete'
} catch {
    Complete-WoFailure $_ 'Installation failed' -StatusFile $StatusFile -NoDialog:$NoErrorDialog
    throw
}
