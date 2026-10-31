. (Join-Path $PSScriptRoot 'Disc-Audio.ps1')
function Get-ModNativeRoot([string]$Source,[string]$Work) {
    $input=Resolve-ModInput $Source $Work
    if(-not [IO.Directory]::Exists($input)) {
        $image=$input;[long]$offset=0;$ext=[IO.Path]::GetExtension($input).ToLowerInvariant()
        if($ext -in '.mds','.mdf','.cue','.bin'){
            $tracks=@(Get-ModDiscTracks $input);$data=@($tracks | Where-Object {-not $_.Audio})
            if(-not $data.Count){throw 'This image has no PC game data track.'};$image=$data[0].Image;$offset=$data[0].Offset
        }
        $input=Join-Path $Work 'native-disc';Import-ModDisc $image $input $offset
    }
    $manifest=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $script:ModRoot 'manifests/patches.json') -Raw | ConvertFrom-Json
    $supported=@($manifest.executables | ForEach-Object {$_.source_sha256})+@($manifest.output_sha256)
    foreach($file in Get-ChildItem -LiteralPath $input -File -Recurse | Where-Object {$_.Name -match '^(?i)(Wipeout2(?:_HighTest)?|Wipeout2097|D3D)\.exe$'}) {
        if((Get-ModHash $file.FullName) -in $supported -and [IO.Directory]::Exists((Join-Path $file.DirectoryName 'Common')) -and [IO.Directory]::Exists((Join-Path $file.DirectoryName 'Track01'))){return $file.DirectoryName}
    }
    foreach($image in Get-ChildItem -LiteralPath $input -File -Recurse | Where-Object {$_.Extension -in '.mds','.cue','.iso'}) {
        try{return Get-ModNativeRoot $image.FullName (Join-Path $Work ([guid]::NewGuid().ToString('N')))}catch{Write-Verbose $_.Exception.Message}
    }
    throw 'No supported PC executable found. Supply the original Windows game disc or a supported Wobble folder; a PlayStation disc supplies music only.'
}
function Copy-ModGameData([string]$Source,[string]$Destination) {
    foreach($dir in Get-ChildItem -LiteralPath $Source -Directory | Where-Object {$_.Name -match '^(?i)(Common|Track\d{2})$'}) {
        foreach($file in Get-ChildItem -LiteralPath $dir.FullName -File -Recurse) {
            if($file.Extension -in '.cmp','.tim','.tex'){continue}
            $target=Get-ModChild $Destination ($dir.Name+'/'+$file.FullName.Substring($dir.FullName.Length+1));[WoBeta.Files]::Parent($target);[IO.File]::Copy($file.FullName,$target,$true)
        }
    }
    foreach($name in @('CONFIG.DAT','SAVEGAME.BIN','Wipeout2097.ini')) {
        $p=Join-Path $Source $name;if([IO.File]::Exists($p)){$target=Get-ModChild $Destination $name;[WoBeta.Files]::Parent($target);[IO.File]::Copy($p,$target,$true)}
    }
}
function Install-ModExecutable([string]$Source,[string]$Destination) {
    $manifest=Get-Content -Encoding UTF8 -LiteralPath (Join-Path $script:ModRoot 'manifests/patches.json') -Raw | ConvertFrom-Json
    $target=Get-ModChild $Destination 'Wipeout2097.exe'
    foreach($file in Get-ChildItem -LiteralPath $Source -File | Where-Object {$_.Name -match '^(?i)(Wipeout2(?:_HighTest)?|Wipeout2097|D3D)\.exe$'}) {
        $hash=Get-ModHash $file.FullName
        if($hash -eq $manifest.output_sha256){[WoBeta.Files]::Parent($target);[IO.File]::Copy($file.FullName,$target,$true);return}
        $patch=@($manifest.executables | Where-Object {$_.source_sha256 -eq $hash})
        if($patch.Count){[WoBeta.Files]::Apply($file.FullName,(Get-ModChild $script:ModRoot $patch[0].patch),$target);return}
    }
    throw 'Unsupported executable checksum; source files were left unchanged.'
}
function Add-ModRegionalAliases([string]$SdRoot) {
    foreach($resolution in @('Hi-res','Lo-res')) {
        foreach($pair in @(@('Blitsurf','RSTART97.PPM','RSTARTXL.PPM'),@('Blitsurf','2097COPY.PPM','WOXLCOPY.PPM'),@('Pics','2097LOGO.TGA','XLLOGO.TGA'))) {
            $a=Get-ModChild $SdRoot ($pair[0]+'/'+$resolution+'/'+$pair[1]);$b=Get-ModChild $SdRoot ($pair[0]+'/'+$resolution+'/'+$pair[2])
            if(-not [IO.File]::Exists($b) -and [IO.File]::Exists($a)){[IO.File]::Copy($a,$b)}
        }
    }
}
