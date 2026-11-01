param([Parameter(Mandatory=$true)][string]$GameSource,[switch]$SkipPlaybackProbe,[string]$Mp4Fixture)
. (Join-Path $PSScriptRoot '../scripts/Sources.ps1')
$work=Join-Path $script:ModRoot ('.work/movies-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$native=Get-ModNativeRoot $GameSource (Join-Path $work 'source')
$target=Join-Path $work 'game';Copy-ModGameData $native $target
$files=@(Get-ChildItem (Join-Path $target 'movies') -File -ErrorAction SilentlyContinue)
if($files.Count -ne 5){throw 'Movie fixture must include the intro and four endings.'}
foreach($file in $files){$original=Join-Path $native ('MPEG/'+$file.Name);if(-not (Test-Path -LiteralPath $original)){$original=Join-Path $native ('movies/'+$file.Name)};if((Get-ModHash $file.FullName) -ne (Get-ModHash $original)){throw 'Movie bytes changed.'}}
# Missing video must return without opening a player or requiring codecs.
& (Join-Path $script:ModRoot 'scripts/Show-Intro.ps1') -GameRoot (Join-Path $work 'no-movie')
$player=Join-Path $script:ModRoot 'scripts/Show-Video.ps1'
if(-not $SkipPlaybackProbe){
    foreach($clip in @('intro','xtro1','xtro2','xtro3','xtro4')){& $player -GameRoot $target -Movie $clip -Probe}
    # Invalid HD replacement must fall back to the original, with no hang.
    $replacement=Join-Path $target 'movies/intro.mp4'
    [IO.File]::WriteAllText($replacement,'invalid media')
    & $player -GameRoot $target -Movie intro -Probe
    [IO.File]::Delete($replacement)
    if($Mp4Fixture){
        [IO.File]::Copy([IO.Path]::GetFullPath($Mp4Fixture),$replacement,$true)
        if((& $player -GameRoot $target -Movie intro -ResolveOnly) -ne $replacement){throw 'MP4 priority failed.'}
        # Remove original temporarily: this probe must decode the actual MP4.
        $original=Join-Path $target 'movies/WXLINTR.MPG';$saved=$original+'.saved'
        [IO.File]::Move($original,$saved)
        try{& $player -GameRoot $target -Movie intro -Probe}finally{[IO.File]::Move($saved,$original);[IO.File]::Delete($replacement)}
    }
}
Write-Host 'PASS: original movie extraction, byte integrity, missing-file fallback and requested playback probe.'