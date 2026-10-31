param([Parameter(Mandatory=$true)][string]$OutputFile,[string[]]$SearchRoot,[int]$MaxSeconds=12)
. (Join-Path $PSScriptRoot 'Sources.ps1')
function Get-ModSourceInfo([string]$Path) {
    $result=[ordered]@{Game='';Pc='';Psx=''}
    try {
        $full=[IO.Path]::GetFullPath($Path)
        $manifest=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $script:ModRoot 'manifests/patches.json') -Raw|ConvertFrom-Json
        $hashes=@($manifest.executables|ForEach-Object {$_.source_sha256})+@($manifest.output_sha256)
        if([IO.Directory]::Exists($full)) {
            foreach($name in @('Wipeout2.exe','Wipeout2097.exe','D3D.EXE','Wipeout2_HighTest.exe')){$exe=Join-Path $full $name;if([IO.File]::Exists($exe) -and (Get-ModHash $exe) -in $hashes){$result.Game=$full;break}}
            $pc=Find-ModMusicFolder $full Pc;if($pc){$result.Pc=$pc}
            $psx=Find-ModMusicFolder $full Psx;if($psx){$result.Psx=$psx}
        } elseif([IO.File]::Exists($full)) {
            $ext=[IO.Path]::GetExtension($full).ToLowerInvariant()
            if($ext -eq '.zip') {
                $zip=[IO.Compression.ZipFile]::OpenRead($full)
                try {
                    foreach($entry in $zip.Entries) {
                        if($entry.Name -match '^(?i)(Wipeout2|D3D)\.exe$' -and $entry.Length -lt 3MB){$s=$entry.Open();$m=New-Object IO.MemoryStream;try{$s.CopyTo($m);if([WoBeta.Files]::Hash($m.ToArray()) -in $hashes){$result.Game=$full}}finally{$s.Dispose();$m.Dispose()}}
                        if($entry.Name -like '*.cue' -and $entry.Length -lt 1MB){$s=$entry.Open();$reader=New-Object IO.StreamReader($s);try{$cue=$reader.ReadToEnd();$audio=[regex]::Matches($cue,'(?im)^\s*TRACK\s+\d+\s+AUDIO').Count;if($audio -eq 11){$result.Psx=$full}elseif($audio -eq 9){$result.Pc=$full;$result.Game=$full}}finally{$reader.Dispose();$s.Dispose()}}
                        if($entry.Name -like '*.mds' -and $entry.Length -lt 1MB){$s=$entry.Open();$m=New-Object IO.MemoryStream;try{$s.CopyTo($m);if([WoBeta.Files]::Hash($m.ToArray()) -eq $manifest.pc_mds_sha256){$result.Game=$full;$result.Pc=$full}}finally{$s.Dispose();$m.Dispose()}}
                    }
                }finally{$zip.Dispose()}
            } elseif($ext -in '.mds','.mdf','.cue','.bin','.iso') {
                $tracks=@(Get-ModDiscTracks $full);$audio=@($tracks|Where-Object Audio).Count
                if($audio -eq 9){$result.Pc=$full}elseif($audio -eq 11){$result.Psx=$full}
                $data=@($tracks|Where-Object {-not $_.Audio});$image=$full;[long]$offset=0
                if($data.Count){$image=$data[0].Image;$offset=$data[0].Offset}
                if($ext -eq '.iso' -or $data.Count){$disc=New-Object WoBeta.Disc($image,$offset);try{foreach($entry in $disc.Entries){if(-not $entry.Directory -and $entry.Path -match '^(?i)(D3D|Wipeout2)\.exe$' -and $entry.Length -lt 3MB){if([WoBeta.Files]::Hash($disc.Read($entry.Lba,$entry.Length)) -in $hashes){$result.Game=$full}}}}finally{$disc.Dispose()}}
            }
        }
    }catch{ } # Discovery is deliberately silent, including inaccessible paths.
    return [pscustomobject]$result
}
# Dot-source this file to reuse the cheap single-path probe in the GUI.
if($MyInvocation.InvocationName -eq '.'){return}
$found=[ordered]@{Game='';Pc='';Psx=''}
if(-not $SearchRoot){$SearchRoot=@([Environment]::GetFolderPath('Desktop'),(Join-Path $env:USERPROFILE 'Downloads'),[Environment]::GetFolderPath('MyDocuments'),[IO.Path]::GetDirectoryName($script:ModRoot))}
$queue=New-Object Collections.Generic.Queue[object];$seen=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach($dir in $SearchRoot){if($dir -and [IO.Directory]::Exists($dir)){$queue.Enqueue([pscustomobject]@{Path=[IO.Path]::GetFullPath($dir);Depth=0})}}
$clock=[Diagnostics.Stopwatch]::StartNew();$visited=0
while($queue.Count -and $clock.Elapsed.TotalSeconds -lt $MaxSeconds -and $visited -lt 1000) {
    $node=$queue.Dequeue();if(-not $seen.Add($node.Path)){continue};$visited++
    if($node.Path -like '*\.work\*' -or $node.Path -like '*\backups\*' -or $node.Path -like '*\.git\*'){continue}
    $info=Get-ModSourceInfo $node.Path
    foreach($kind in @('Game','Pc','Psx')){if(-not $found[$kind] -and $info.$kind){$found[$kind]=$info.$kind}}
    foreach($file in @(Get-ChildItem -LiteralPath $node.Path -File -ErrorAction SilentlyContinue | Where-Object {$_.Name -match '(?i)(wipeout|wobble|wo2097)' -and $_.Extension -in '.zip','.iso','.mds','.cue'})) {
        $info=Get-ModSourceInfo $file.FullName;foreach($kind in @('Game','Pc','Psx')){if(-not $found[$kind] -and $info.$kind){$found[$kind]=$info.$kind}}
        if($found.Game -and $found.Pc -and $found.Psx){break}
    }
    if($found.Game -and $found.Pc -and $found.Psx){break}
    if($node.Depth -lt 5){foreach($dir in @(Get-ChildItem -LiteralPath $node.Path -Directory -ErrorAction SilentlyContinue | Where-Object {-not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)} | Sort-Object @{Expression={if($_.Name -match '(?i)(wipeout|wobble|wo2097|music|source)'){0}else{1}}},Name)){$queue.Enqueue([pscustomobject]@{Path=$dir.FullName;Depth=$node.Depth+1})}}
}
Write-ModJson $OutputFile $found
