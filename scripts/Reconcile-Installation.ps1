param([string]$GameRoot,[switch]$ForceRepair,[string]$StatusFile,[switch]$FunctionsOnly,[switch]$PassThru,[switch]$NoErrorDialog)
if(-not (Get-Command Write-WoLog -ErrorAction SilentlyContinue)){. (Join-Path $PSScriptRoot 'Diagnostics.ps1')}
. (Join-Path $PSScriptRoot 'Configuration.ps1')
. (Join-Path $PSScriptRoot 'Cleanup-ManagedFiles.ps1')
. (Join-Path $PSScriptRoot 'Music-State.ps1')
. (Join-Path $PSScriptRoot 'Native-Textures.ps1')
function Test-ModPcInstallation([string]$Root,[string]$GameRoot,$Receipt){
    $manifest=Get-Content -LiteralPath (Join-Path $Root 'manifests/game-files.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $patches=Get-Content -LiteralPath (Join-Path $Root 'manifests/patches.json') -Raw -Encoding UTF8|ConvertFrom-Json
    foreach($relative in $manifest.required){
        $file=Get-ModChild $GameRoot $relative
        if(-not [IO.File]::Exists($file)){return $false}
        $record=@((Get-ModProperty $Receipt 'pc_files' @())|Where-Object file -eq $relative)
        if($record.Count -and (Get-ModHash $file) -ne $record[0].sha256){return $false}
    }
    if((Get-ModHash (Join-Path $GameRoot 'Wipeout2097.exe')) -ne $patches.output_sha256){return $false}
    $plan=Get-Content -LiteralPath (Join-Path $Root 'manifests/texture-selection.json') -Raw -Encoding UTF8|ConvertFrom-Json
    foreach($atlas in @($plan.operations.atlas|Select-Object -Unique)){
        if(-not [IO.File]::Exists((Get-ModChild $GameRoot ('textures/sd/'+$atlas)))){return $false}
    }
    $sources=Join-Path $GameRoot 'textures/sd/sources.json'
    if([IO.File]::Exists($sources)){
        $patched=@($plan.operations.atlas|Select-Object -Unique)
        try{$records=Get-Content -LiteralPath $sources -Raw -Encoding UTF8|ConvertFrom-Json}catch{return $false}
        foreach($record in $records){
            $relative=([string]$record.file).Replace('\','/')
            if($relative -in $patched -or [IO.Path]::GetExtension($relative) -notin '.tga','.cmp','.tim','.tex'){continue}
            $file=Get-ModChild (Join-Path $GameRoot 'textures/sd') $relative
            if(-not [IO.File]::Exists($file) -or (Get-ModHash $file) -ne $record.sha256){return $false}
        }
    }
    foreach($entry in $patches.audio){
        $path=Join-Path $GameRoot ('audio/source/'+$entry.name)
        if(-not [IO.File]::Exists($path)){return $false}
        $hash=Get-ModHash $path
        $record=@((Get-ModProperty $Receipt 'pc_files' @())|Where-Object file -eq ('audio/source/'+$entry.name))
        if($record.Count){if($hash -ne $record[0].sha256){return $false}}
        elseif((Get-ModProperty $Receipt 'clean_audio' $true) -and $hash -notin @($entry.source_hashes) -and $hash -ne $entry.output_sha256){return $false}
    }
    return $true
}
function Sync-ModRuntime([string]$Root,[string]$GameRoot){
    $updated=0
    $files=@('wohr.dll','ddraw.dll','wo2097-controller.exe')+@(Get-ChildItem -LiteralPath (Join-Path $Root 'runtime') -File -Filter 'lib*.dll'|Select-Object -ExpandProperty Name)
    foreach($name in $files){
        $source=Join-Path $Root ('runtime/'+$name);$target=Get-ModChild $GameRoot $name
        if(-not [IO.File]::Exists($source)){throw ('The mod download is incomplete: runtime/'+$name)}
        if(-not [IO.File]::Exists($target) -or (Get-ModHash $source) -ne (Get-ModHash $target)){[WoBeta.Files]::Parent($target);[IO.File]::Copy($source,$target,$true);++$updated}
    }
    foreach($name in @('w2097-hd.cg','w2097-hd.cgp')){
        $source=Join-Path $Root ('runtime/'+$name);$target=Get-ModChild $GameRoot ('common-shaders-master/w2097/'+$name)
        if([IO.File]::Exists($source) -and (-not [IO.File]::Exists($target) -or (Get-ModHash $source) -ne (Get-ModHash $target))){[WoBeta.Files]::Parent($target);[IO.File]::Copy($source,$target,$true);++$updated}
    }
    $ini=Join-Path $GameRoot 'Wipeout2097.ini'
    if(-not [IO.File]::Exists($ini)){[IO.File]::Copy((Join-Path $Root 'runtime/Wipeout2097.ini'),$ini);++$updated}
    # Merge new sectioned runtime defaults without resetting tuned lighting,
    # volume, track selection or other existing preferences.
    $text=[IO.File]::ReadAllText($ini);$original=$text;$section=''
    foreach($line in [IO.File]::ReadAllLines((Join-Path $Root 'runtime/Wipeout2097.ini'))){
        if($line -match '^\[([^]]+)\]'){$section=$Matches[1];continue}
        if($line -notmatch '^([^=;#]+)='){continue};$key=$Matches[1].Trim()
        $sectionPattern='(?ims)(^\['+[regex]::Escape($section)+'\]\s*\r?\n)(.*?)(?=^\[|\z)'
        $block=[regex]::Match($text,$sectionPattern)
        if($block.Success){
            if($block.Groups[2].Value -notmatch ('(?im)^\s*'+[regex]::Escape($key)+'\s*=')){$offset=$block.Index+$block.Length;$text=$text.Insert($offset,"`r`n"+$line+"`r`n")}
        }else{$text+="`r`n["+$section+"]`r`n"+$line+"`r`n"}
    }
    if($text -cne $original){[IO.File]::WriteAllText($ini,$text,[Text.Encoding]::ASCII);++$updated}
    # Add newly introduced defaults; keep all existing graphics preferences.
    $target=Join-Path $GameRoot 'DDrawCompat-Wipeout2097.ini';$text=if([IO.File]::Exists($target)){[IO.File]::ReadAllText($target)}else{''};$original=$text
    foreach($line in [IO.File]::ReadAllLines((Join-Path $Root 'runtime/DDrawCompat-Wipeout2097.ini'))){
        if($line -match '^\s*([A-Za-z][A-Za-z0-9]*)\s*='){
            $key=$Matches[1];if($text -notmatch ('(?im)^\s*'+[regex]::Escape($key)+'\s*=')){$text+="`r`n"+$line+"`r`n"}
        }
    }
    if($text -cne $original){[IO.File]::WriteAllText($target,$text,[Text.Encoding]::ASCII);++$updated}
    return $updated
}
function Invoke-ModReconcile([string]$Root=$script:ModRoot,[string]$GameRoot,[switch]$ForceRepair,[string]$StatusFile){
    if(-not $GameRoot){$GameRoot=Join-Path $Root 'wobble'};$GameRoot=[IO.Path]::GetFullPath($GameRoot).TrimEnd('\')
    foreach($process in @(Get-Process Wipeout2097 -ErrorAction SilentlyContinue)){
        if(-not $process.Path -or $process.Path.StartsWith($GameRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Close the game before reconciling its installation.'}
    }
    $config=Get-ModConfiguration $Root;$receiptPath=Join-Path $GameRoot 'installation.json';$receipt=$null
    if([IO.File]::Exists($receiptPath)){$receipt=Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8|ConvertFrom-Json}
    if(-not $config.GameSource -and -not $receipt){return [pscustomobject]@{installed=$false;repaired=$false;runtime_updated=0}}
    $config=Save-ModConfiguration $config $Root
    $version=[IO.File]::ReadAllText((Join-Path $Root 'VERSION')).Trim()
    $healthy=Test-ModPcInstallation $Root $GameRoot $receipt
    $previous=Get-ModProperty $receipt 'configuration';$oldGame=Resolve-ModConfiguredPath $Root ([string](Get-ModProperty $previous 'GameSource' ''))
    $repair=$ForceRepair -or -not $healthy -or (Get-ModProperty $receipt 'version' '') -ne $version -or ($oldGame -and $oldGame -ne $config.GameSource)
    if($repair){
        $source=$config.GameSource
        if(-not $source -or -not (Test-Path -LiteralPath $source)){if($healthy -and (-not $oldGame -or $oldGame -eq $source)){$source=$GameRoot}else{throw 'The Windows game needs repair and its source is unavailable. Restore its path in Setup.'}}
        if($source -eq $GameRoot -and -not [IO.Directory]::Exists((Join-Path $GameRoot 'audio/source'))){throw 'The original sound-effect cache is missing. Select your original PC game source in Setup to repair it.'}
        $request=Get-ModChild $Root ('.work/ensure-request-'+[guid]::NewGuid().ToString('N')+'.json')
        $arguments=[ordered]@{};foreach($name in @('GameSource','PcMusicSource','PsxMusicSource','DefaultSoundtrack','HdTextures','CleanAudio','SoundMode','FullscreenMode')){$arguments[$name]=$config.$name};$arguments.GameSource=$source
        if($config.PsxMusicSource -and -not (Test-Path -LiteralPath $config.PsxMusicSource)){
            $priorPsx=Resolve-ModConfiguredPath $Root ([string](Get-ModProperty $previous 'PsxMusicSource' ''))
            $import=Join-Path $Root '.state/ps1-import.json'
            if([IO.File]::Exists($import)){$priorPsx=[string](Get-Content -LiteralPath $import -Raw -Encoding UTF8|ConvertFrom-Json).source}
            if($priorPsx -eq $config.PsxMusicSource -and ([IO.File]::Exists((Join-Path $GameRoot 'textures/ps1-source/source.json')) -or @(Get-ModMusicFiles $GameRoot Psx).Count)){$arguments.PsxMusicSource=''}
        }
        if($config.PcMusicSource -and -not (Test-Path -LiteralPath $config.PcMusicSource)){
            $priorPc=Resolve-ModConfiguredPath $Root ([string](Get-ModProperty $previous 'PcMusicSource' ''))
            if((-not $previous -or $priorPc -eq $config.PcMusicSource) -and (Find-ModMusicFolder $GameRoot Pc)){$arguments.PcMusicSource=Find-ModMusicFolder $GameRoot Pc}
        }
        $arguments['PreserveConfiguration']=$true;Write-ModJson $request $arguments
        try{
            $argsList=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $Root 'scripts/Install-Mod.ps1'),'-RequestFile',$request,'-NoErrorDialog')
            if($StatusFile){$argsList+=@('-StatusFile',$StatusFile)}
            & $script:ModPowerShell @argsList|Out-Host
            if($LASTEXITCODE){throw 'Automatic Install / repair failed. See the installation log and restore unavailable source paths in Setup.'}
        }finally{if([IO.File]::Exists($request)){[IO.File]::Delete($request)}}
        $receipt=Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8|ConvertFrom-Json;$config=Get-ModConfiguration $Root
    }
    $settingsMissing=-not [IO.File]::Exists((Join-Path $GameRoot 'Wipeout2097.ini')) -or -not [IO.File]::Exists((Join-Path $GameRoot 'DDrawCompat-Wipeout2097.ini'))
    $runtime=Sync-ModRuntime $Root $GameRoot
    $plan=Get-Content -LiteralPath (Join-Path $Root 'manifests/texture-selection.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $origin=Join-Path $GameRoot 'textures/ps1-source/source.json'
    $requirePs1=[bool](Get-ModProperty $receipt 'playstation_textures' $false) -or [IO.File]::Exists($origin)
    $nativeHealthy=Test-ModNativeTextures $GameRoot $plan -RequirePs1:$requirePs1
    $importFile=Join-Path $Root '.state/ps1-import.json';$importState=$null
    if([IO.File]::Exists($importFile)){$importState=Get-Content -LiteralPath $importFile -Raw -Encoding UTF8|ConvertFrom-Json}
    $source=$config.PsxMusicSource;$stamp=Get-ModSourceStamp $source
    $sourceChanged=$source -and (-not $importState -or $importState.source -ne $source -or ($importState.source_stamp -ne $stamp -and -not $stamp.EndsWith('|unavailable')))
    $needsSource=$source -and ($sourceChanged -or -not $nativeHealthy)
    if($needsSource -and -not (Test-Path -LiteralPath $source)){
        if((-not $nativeHealthy -and -not [IO.File]::Exists($origin)) -or ($importState -and $importState.source -ne $source)){throw 'Configured PlayStation artwork needs its disc source. Restore the path in Setup.'}
        $needsSource=$false
    }
    $session=Join-Path $Root ('.work/ensure-textures-'+[guid]::NewGuid().ToString('N'))
    try{
        $native=Install-ModNativeTextures -GameRoot $GameRoot -PsxSource $(if($needsSource){$source}else{''}) -WorkRoot $session -Incremental -RequirePs1:$requirePs1
        if($source){[WoBeta.ConfigurationFile]::Write($importFile,(ConvertTo-Json -InputObject @{source=$source;source_stamp=$stamp;ps1_textures=$native.ps1_textures}))}
    }finally{
        $full=[IO.Path]::GetFullPath($session);$base=[IO.Path]::GetFullPath((Join-Path $Root '.work')).TrimEnd('\')+'\ensure-textures-'
        if(-not $full.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe texture extraction cleanup.'}
        if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
    }
    $music=Ensure-ModMusic $Root $GameRoot $config
    if($config.DefaultSoundtrack -eq 'Pc' -and -not $music.pc_available){$config.DefaultSoundtrack=if($music.psx_available){'Psx'}else{'None'};$config=Save-ModConfiguration $config $Root}
    $changed=$settingsMissing -or [int](Get-ModProperty $receipt 'configuration_revision' -1) -ne $config.revision
    if($changed){
        $ini=Join-Path $GameRoot 'Wipeout2097.ini';$text=[IO.File]::ReadAllText($ini)
        if($text -match '(?im)^Soundtrack\s*='){$text=[regex]::Replace($text,'(?im)^Soundtrack\s*=.*$',('Soundtrack='+[int]($config.DefaultSoundtrack -eq 'Psx')))}else{$text+="`r`n[Audio]`r`nSoundtrack=$([int]($config.DefaultSoundtrack -eq 'Psx'))`r`n"}
        if($config.DefaultSoundtrack -eq 'None'){$text=[regex]::Replace($text,'(?im)^MusicVolume\s*=.*$','MusicVolume=0')}
        elseif((Get-ModProperty $receipt 'default_soundtrack') -eq 'None'){$text=[regex]::Replace($text,'(?im)^MusicVolume\s*=.*$','MusicVolume=100')}
        [IO.File]::WriteAllText($ini,$text,[Text.Encoding]::ASCII)
        Set-ModSoundMode -SettingsPath $ini -SoundMode $config.SoundMode
        Set-ModFullscreenMode -SettingsPath (Join-Path $GameRoot 'DDrawCompat-Wipeout2097.ini') -FullscreenMode $config.FullscreenMode
    }
    foreach($pair in @(@('configuration_revision',$config.revision),@('hd_textures',$config.HdTextures),@('clean_audio',$config.CleanAudio),@('pc_music_available',$music.pc_available),@('playstation_available',$music.psx_available),@('playstation_textures',$native.ps1_textures),@('default_soundtrack',$config.DefaultSoundtrack))){$receipt|Add-Member -NotePropertyName $pair[0] -NotePropertyValue $pair[1] -Force}
    if(-not $receipt.PSObject.Properties['pc_files']){$receipt|Add-Member -NotePropertyName pc_files -NotePropertyValue @(Get-ModPcFileReceipt $GameRoot $Root)}
    Write-ModJson $receiptPath $receipt
    $cleanup=Remove-ModRetiredFiles $Root
    return [pscustomobject]@{installed=$true;repaired=[bool]$repair;runtime_updated=$runtime;ps1_textures=$native.ps1_textures;music_updated=$music.updated;obsolete_files_removed=$cleanup.removed}
}
if($FunctionsOnly){return}
$null=Start-WoLog 'reconcile' $script:ModRoot
try{
    $result=Invoke-ModReconcile -Root $script:ModRoot -GameRoot $GameRoot -ForceRepair:$ForceRepair -StatusFile $StatusFile
    if($result.installed){$null=& (Join-Path $PSScriptRoot 'Refresh-Assets.ps1') -GameRoot $(if($GameRoot){$GameRoot}else{Join-Path $script:ModRoot 'wobble'}) -SkipReconcile}
    if($StatusFile){Write-ModJson $StatusFile @{percent=100;message='Installation checked and ready.';state='complete'}}
    if($PassThru){$result}
}catch{Complete-WoFailure $_ 'Checking the configured installation failed' -StatusFile $StatusFile -NoDialog:$NoErrorDialog;throw}
