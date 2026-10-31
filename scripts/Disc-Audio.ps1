. (Join-Path $PSScriptRoot 'Core.ps1')
function Get-ModMdsTracks([string]$Path) {
    $b=[IO.File]::ReadAllBytes($Path)
    if($b.Length -lt 88 -or [Text.Encoding]::ASCII.GetString($b,0,16) -ne 'MEDIA DESCRIPTOR' -or $b[16] -ne 1){throw 'Only MDS version1 images are supported.'}
    $image=[IO.Path]::ChangeExtension($Path,'.mdf');if(-not [IO.File]::Exists($image)){throw 'The matching MDF file is missing.'}
    $sessions=[BitConverter]::ToUInt16($b,20);$table=[BitConverter]::ToUInt32($b,80)
    for($s=0;$s -lt $sessions;$s++) {
        $session=$table+$s*24;if($session+24 -gt $b.Length){throw 'Truncated MDS session.'}
        $count=$b[$session+10];$start=[BitConverter]::ToUInt32($b,$session+20)
        for($i=0;$i -lt $count;$i++) {
            $at=$start+$i*80;if($at+80 -gt $b.Length){throw 'Truncated MDS track.'};$track=$b[$at+4];if($track -lt 1 -or $track -gt 99){continue}
            $extra=[BitConverter]::ToUInt32($b,$at+12);if($extra+8 -gt $b.Length){throw 'Truncated MDS length.'}
            [pscustomobject]@{Track=[int]$track;Audio=(($b[$at] -band 7) -eq 1);Image=$image;Stride=[int][BitConverter]::ToUInt16($b,$at+16);Offset=[long][BitConverter]::ToUInt64($b,$at+40);Frames=[long][BitConverter]::ToUInt32($b,$extra+4);StartFrame=[long](($b[$at+9]*60+$b[$at+10])*75+$b[$at+11])}
        }
    }
}
function Convert-ModMsf([string]$Value) {$v=$Value -split ':';if($v.Count -ne 3){throw 'Bad CUE index.'};return ([int]$v[0]*60+[int]$v[1])*75+[int]$v[2]}
function Get-ModCueTracks([string]$Path) {
    $base=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Path));$records=New-Object Collections.Generic.List[object];$image='';$current=$null
    foreach($line in [IO.File]::ReadAllLines($Path)) {
        if($line -match '^\s*FILE\s+"([^"]+)"\s+BINARY') {$image=Get-ModChild $base $Matches[1];if(-not [IO.File]::Exists($image)){throw "Missing CUE file: $($Matches[1])"}}
        elseif($line -match '^\s*TRACK\s+(\d+)\s+(\S+)') {
            if(-not $image){throw 'CUE track without a file.'}
            $kind=$Matches[2];$stride=2352;if($kind -match '/(2048|2352|2448)$'){$stride=[int]$Matches[1]}
            $current=[pscustomobject]@{Track=[int]([regex]::Match($line,'TRACK\s+(\d+)').Groups[1].Value);Audio=($kind -eq 'AUDIO');Image=$image;Stride=$stride;Index0=-1L;Index1=-1L;Offset=0L;Frames=0L;StartFrame=0L};$records.Add($current)
        } elseif($line -match '^\s*INDEX\s+(00|01)\s+(\d+:\d+:\d+)') {
            if($null -eq $current){throw 'CUE index without track.'};$frame=Convert-ModMsf $Matches[2];if($Matches[1] -eq '00'){$current.Index0=$frame}else{$current.Index1=$frame}
        }
    }
    [long]$baseFrame=150;$last='';[long]$previousSize=0
    for($i=0;$i -lt $records.Count;$i++) {
        $r=$records[$i];if($r.Index1 -lt 0){throw 'CUE track lacks INDEX01.'}
        if($r.Image -ne $last){$baseFrame+=$previousSize;$last=$r.Image;$previousSize=[long]((Get-Item -LiteralPath $r.Image).Length/$r.Stride)}
        [long]$end=$previousSize
        if($i+1 -lt $records.Count -and $records[$i+1].Image -eq $r.Image){$next=$records[$i+1];$end=if($next.Index0 -ge 0){$next.Index0}else{$next.Index1}}
        $r.Offset=$r.Index1*$r.Stride;$r.Frames=$end-$r.Index1;$r.StartFrame=$baseFrame+$r.Index1
        if($r.Frames -le 0){throw 'Bad CUE track length.'};$r
    }
}
function Get-ModDiscTracks([string]$Path) {
    $ext=[IO.Path]::GetExtension($Path).ToLowerInvariant()
    if($ext -eq '.mdf'){$Path=[IO.Path]::ChangeExtension($Path,'.mds');$ext='.mds'}
    if($ext -eq '.bin'){$Path=[IO.Path]::ChangeExtension($Path,'.cue');$ext='.cue'}
    if($ext -eq '.mds'){return @(Get-ModMdsTracks $Path)}
    if($ext -eq '.cue'){return @(Get-ModCueTracks $Path)}
    return @()
}
function Resolve-ModInput([string]$Path,[string]$Work) {
    $full=[IO.Path]::GetFullPath($Path)
    if([IO.Path]::GetExtension($full) -ieq '.zip'){$to=Join-Path $Work ([guid]::NewGuid().ToString('N').Substring(0,16));Expand-ModZip $full $to;return $to}
    if(-not (Test-Path -LiteralPath $full)){throw "Source does not exist: $full"};return $full
}
function Find-ModMusicFolder([string]$Source,[string]$Bank) {
    if(-not [IO.Directory]::Exists($Source)){return $null}
    $choices=@($Source,(Join-Path $Source 'music/pc'),(Join-Path $Source 'music/psx'),(Join-Path $Source 'Music/PS1'),(Join-Path $Source 'Music'))
    $expected=if($Bank -eq 'Pc'){10}else{12}
    foreach($dir in $choices){$toc=Join-Path $dir 'tracks.ini';if([IO.File]::Exists($toc) -and ([IO.File]::ReadAllText($toc) -match ('(?im)^Tracks\s*=\s*'+$expected+'\s*$'))){return [IO.Path]::GetFullPath($dir)}}
    return $null
}
