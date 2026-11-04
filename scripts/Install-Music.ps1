param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$GameRoot,[ValidateSet('Pc','Psx')][string]$Bank='Pc')
. (Join-Path $PSScriptRoot 'Disc-Audio.ps1')
$folder=Find-ModMusicFolder ([IO.Path]::GetFullPath($SourcePath)) $Bank
if(-not $folder){throw 'Extract music first; a matching tracks.ini is required.'}
$destination=Get-ModChild $GameRoot ('music/'+$Bank.ToLowerInvariant())
$toc=[IO.File]::ReadAllText((Join-Path $folder 'tracks.ini'));$count=if($Bank -eq 'Pc'){10}else{12}
for($track=2;$track -le $count;$track++) {
    $stem='track{0:D2}' -f $track;$path=Join-Path $folder ($stem+'.wav');if(-not [IO.File]::Exists($path)){$path=Join-Path $folder ($stem+'.mp3')}
    if(-not [IO.File]::Exists($path)){throw "Missing music: $stem"}
    $target=Get-ModChild $destination ([IO.Path]::GetFileName($path));[WoBeta.Files]::Parent($target)
    if(-not [IO.File]::Exists($target) -or (Get-ModHash $path) -ne (Get-ModHash $target)){[IO.File]::Copy($path,$target,$true)}
    # The runtime prefers WAV. Remove a superseded encoding only after its
    # replacement is installed, otherwise a stale WAV can hide a new MP3.
    $alternate=Join-Path $destination ($stem+$(if([IO.Path]::GetExtension($path) -ieq '.wav'){'.mp3'}else{'.wav'}))
    if([IO.File]::Exists($alternate)){[IO.File]::Delete($alternate)}
}
$targetToc=Get-ModChild $destination 'tracks.ini'
if(-not [IO.File]::Exists($targetToc) -or [IO.File]::ReadAllText($targetToc) -cne $toc){[IO.File]::WriteAllText($targetToc,$toc,[Text.Encoding]::ASCII)}
Write-Host "Installed $Bank soundtrack ($($count-1) songs)."
