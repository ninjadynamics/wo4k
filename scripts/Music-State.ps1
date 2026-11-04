. (Join-Path $PSScriptRoot 'Configuration.ps1')
. (Join-Path $PSScriptRoot 'Disc-Audio.ps1')
function Get-ModMusicFiles([string]$GameRoot,[ValidateSet('Pc','Psx')][string]$Bank){
    $folder=Join-Path $GameRoot ('music/'+$Bank.ToLowerInvariant());$toc=Join-Path $folder 'tracks.ini'
    $count=if($Bank -eq 'Pc'){10}else{12}
    if(-not [IO.File]::Exists($toc) -or [IO.File]::ReadAllText($toc) -notmatch ('(?im)^Tracks\s*=\s*'+$count+'\s*$')){return @()}
    $files=New-Object Collections.Generic.List[string];$files.Add($toc)
    for($i=2;$i -le $count;++$i){
        $stem='track{0:D2}' -f $i;$path=Join-Path $folder ($stem+'.wav')
        if(-not [IO.File]::Exists($path)){$path=Join-Path $folder ($stem+'.mp3')}
        if(-not [IO.File]::Exists($path) -or (Get-Item -LiteralPath $path).Length -lt 4){return @()}
        $stream=[IO.File]::OpenRead($path)
        try{$header=New-Object byte[] 12;$read=$stream.Read($header,0,12)}finally{$stream.Dispose()}
        if([IO.Path]::GetExtension($path) -ieq '.wav'){
            if($read -lt 12 -or [Text.Encoding]::ASCII.GetString($header,0,4) -ne 'RIFF' -or [Text.Encoding]::ASCII.GetString($header,8,4) -ne 'WAVE'){return @()}
        }elseif($read -lt 3 -or ([Text.Encoding]::ASCII.GetString($header,0,3) -ne 'ID3' -and -not ($header[0] -eq 255 -and ($header[1] -band 224) -eq 224))){return @()}
        $files.Add($path)
    }
    return $files.ToArray()
}
function Test-ModMusicState([string]$GameRoot,[string]$Bank,$State){
    $files=@(Get-ModMusicFiles $GameRoot $Bank);if(-not $files.Count){return $false}
    if(-not $State -or -not $State.available -or -not $State.PSObject.Properties['files']){return $false}
    foreach($file in $files){
        $relative=$file.Substring($GameRoot.TrimEnd('\').Length+1).Replace('\','/')
        $record=@($State.files|Where-Object file -eq $relative)
        if($record.Count -ne 1 -or (Get-ModHash $file) -ne $record[0].sha256){return $false}
    }
    return $true
}
function New-ModMusicState([string]$GameRoot,[string]$Bank,[string]$Source,[string]$Stamp){
    $records=@(foreach($file in @(Get-ModMusicFiles $GameRoot $Bank)){
        [pscustomobject]@{file=$file.Substring($GameRoot.TrimEnd('\').Length+1).Replace('\','/');sha256=(Get-ModHash $file)}
    })
    return [pscustomobject]@{source=$Source;source_stamp=$Stamp;available=($records.Count -gt 0);files=$records}
}
function Ensure-ModMusic([string]$Root,[string]$GameRoot,$Configuration){
    $path=Join-Path $Root '.state/music-sources.json';$previous=$null
    if([IO.File]::Exists($path)){$previous=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json}
    $installed=$null;$receipt=Join-Path $GameRoot 'installation.json'
    if([IO.File]::Exists($receipt)){$installed=Get-Content -LiteralPath $receipt -Raw -Encoding UTF8|ConvertFrom-Json}
    $states=[ordered]@{};$updated=0
    foreach($bank in @('Pc','Psx')){
        $source=if($bank -eq 'Psx'){$Configuration.PsxMusicSource}elseif($Configuration.PcMusicSource){$Configuration.PcMusicSource}else{$Configuration.GameSource}
        $old=Get-ModProperty $previous $bank;$musicFolder=Find-ModMusicFolder $source $bank
        $stamp=Get-ModSourceStamp $(if($musicFolder){$musicFolder}else{$source})
        $files=@(Get-ModMusicFiles $GameRoot $bank);$complete=$files.Count -gt 0
        $same=$old -and $old.source -eq $source
        if(-not $old -and $complete -and (Get-ModProperty $installed 'configuration_revision' -1) -eq $Configuration.revision){
            $states[$bank]=New-ModMusicState $GameRoot $bank $source $stamp;continue
        }
        $healthy=Test-ModMusicState $GameRoot $bank $old
        if($same -and (($healthy -and ($old.source_stamp -eq $stamp -or $stamp.EndsWith('|unavailable'))) -or (-not $old.available -and -not $complete -and $old.source_stamp -eq $stamp))){$states[$bank]=$old;continue}
        if(-not $source -or -not (Test-Path -LiteralPath $source)){
            if($complete -and -not $old){$states[$bank]=New-ModMusicState $GameRoot $bank $source $stamp;continue}
            if($old -and -not $old.available -and $same){$states[$bank]=$old;continue}
            if(($old -and $old.available) -or $Configuration.DefaultSoundtrack -eq $bank){throw "The configured $bank soundtrack needs its source. Restore its path in Setup."}
            $states[$bank]=New-ModMusicState $GameRoot $bank $source $stamp;continue
        }
        $session=Join-Path $Root ('.work/ensure-music-'+[guid]::NewGuid().ToString('N'))
        try{
            $out=Join-Path $session 'bank'
            $null=& (Join-Path $PSScriptRoot 'Extract-Music.ps1') -SourcePath $source -Destination $out -Bank $bank -WorkRoot (Join-Path $session 'source') -AllowMissing
            if([IO.File]::Exists((Join-Path $out 'tracks.ini'))){
                & (Join-Path $PSScriptRoot 'Install-Music.ps1') -SourcePath $out -GameRoot $GameRoot -Bank $bank | Out-Host;++$updated
            }elseif($complete -and -not $old){
                # Adopt a complete legacy bank when the original disc is no
                # longer configured. Its installed files become the receipt.
            }elseif($Configuration.DefaultSoundtrack -eq $bank){throw "The selected $bank source has no CD audio. Choose No music or a complete soundtrack source in Setup."}
            $states[$bank]=New-ModMusicState $GameRoot $bank $source $stamp
        }finally{
            $full=[IO.Path]::GetFullPath($session);$base=[IO.Path]::GetFullPath((Join-Path $Root '.work')).TrimEnd('\')+'\ensure-music-'
            if(-not $full.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe music extraction cleanup.'}
            if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
        }
    }
    [WoBeta.ConfigurationFile]::Write($path,(ConvertTo-Json -InputObject $states -Depth 8))
    return [pscustomobject]@{pc_available=[bool]$states.Pc.available;psx_available=[bool]$states.Psx.available;updated=$updated}
}
