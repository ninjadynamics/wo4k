param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$Destination,[ValidateSet('Pc','Psx')][string]$Bank='Pc',[string]$WorkRoot,[switch]$AllowMissing)
. (Join-Path $PSScriptRoot 'Disc-Audio.ps1')
$Destination=[IO.Path]::GetFullPath($Destination)
if(-not $WorkRoot){$WorkRoot=Join-Path $script:ModRoot '.work/music'}
$source=Resolve-ModInput $SourcePath $WorkRoot
$folder=Find-ModMusicFolder $source $Bank
if($folder){
    [IO.Directory]::CreateDirectory($Destination)|Out-Null
    foreach($file in Get-ChildItem -LiteralPath $folder -File){if($file.Name -match '^(?i)(track\d{2}\.(wav|mp3)|tracks\.ini)$'){[IO.File]::Copy($file.FullName,(Get-ModChild $Destination $file.Name),$true)}}
    return $Destination
}
$descriptors=if([IO.Directory]::Exists($source)){@(Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object {$_.Extension -in '.mds','.cue'} | Select-Object -ExpandProperty FullName)}else{@($source)}
$wanted=if($Bank -eq 'Pc'){9}else{11};$chosen=@()
foreach($descriptor in $descriptors){$tracks=@(Get-ModDiscTracks $descriptor);if(@($tracks|Where-Object Audio).Count -eq $wanted){$chosen=$tracks;break}}
if(-not $chosen.Count -and $AllowMissing){return $null}
if(-not $chosen.Count){throw "No $Bank CD audio found. Use a matching MDS/MDF or CUE/BIN set, its ZIP, or an existing music folder. A data-only ISO has no CD audio."}
$toc=New-Object Collections.Generic.List[string];$toc.Add('[CD]');$toc.Add('Tracks='+$chosen.Count);$toc.Add('')
foreach($track in $chosen){
    $toc.Add(('[Track{0:D2}]' -f $track.Track));$toc.Add('StartFrame='+$track.StartFrame);$toc.Add('Frames='+$track.Frames);$toc.Add('')
    if($track.Audio){$target=Get-ModChild $Destination ('track{0:D2}.wav' -f $track.Track);[WoBeta.Files]::Audio($track.Image,$track.Offset,$track.Frames,$track.Stride,$target)}
}
[IO.File]::WriteAllLines((Get-ModChild $Destination 'tracks.ini'),$toc,[Text.Encoding]::ASCII)
return $Destination
