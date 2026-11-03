param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$GameRoot,[ValidateSet('Hd','Sd')][string]$Kind='Hd',[switch]$Incremental,[switch]$PassThru)
. (Join-Path $PSScriptRoot 'Core.ps1')
if($Kind -eq 'Sd'){Copy-ModTree $SourcePath (Get-ModChild $GameRoot 'textures/sd');return}
$catalog=Get-Content -LiteralPath (Join-Path $script:ModRoot 'manifests/hd-textures.json') -Encoding UTF8 -Raw|ConvertFrom-Json
$canonical=Join-Path $script:ModRoot 'assets/hd';$inputs=@{};$updated=0
foreach($directory in @($canonical,$SourcePath)){
    if(-not [IO.Directory]::Exists($directory)){continue}
    $base=[IO.Path]::GetFullPath($directory).TrimEnd('\')
    foreach($file in Get-ChildItem -LiteralPath $base -File -Filter '*.png' -Recurse){
        if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked artwork is not supported.'}
        $relative=$file.FullName.Substring($base.Length+1).Replace('\','/')
        $inputs[$relative]=$file.FullName
    }
}
foreach($entry in $catalog|Where-Object required){if(-not $inputs.ContainsKey($entry.file)){throw ('Missing HD artwork: '+$entry.file)}}
$native=@{}
$pics=Join-Path $GameRoot 'textures/sd/Pics'
if([IO.Directory]::Exists($pics)){
    foreach($file in Get-ChildItem -LiteralPath $pics -Recurse -File -Filter '*.tga'){
        if(-not $native.ContainsKey($file.BaseName) -or $file.FullName -match '(?i)hi-res'){
            $stream=[IO.File]::OpenRead($file.FullName);$header=New-Object byte[] 18
            try{$read=$stream.Read($header,0,18)}finally{$stream.Dispose()}
            if($read -eq 18){$w=[BitConverter]::ToUInt16($header,12);$h=[BitConverter]::ToUInt16($header,14);if($h -gt 0 -and $w -ge $h -and $w%$h -eq 0){$native[$file.BaseName]=[pscustomobject]@{pages=[int]($w/$h);width=$w;height=$h}}}
        }
    }
}
$records=New-Object Collections.Generic.List[object]
foreach($relative in @($inputs.Keys|Sort-Object)){
    $source=$inputs[$relative];$entry=@($catalog|Where-Object file -eq $relative|Select-Object -First 1);$kindName='';$pages=1;$page=-1
    if($entry.Count){$kindName=$entry[0].kind}
    else{
        $stem=[IO.Path]::GetFileNameWithoutExtension($relative)
        if($stem -match '^(.*)\.page(\d+)$'){$stem=$Matches[1];$page=[int]$Matches[2]}
        if($stem.EndsWith('.tga',[StringComparison]::OrdinalIgnoreCase)){$stem=$stem.Substring(0,$stem.Length-4)}
        $parent=[IO.Path]::GetDirectoryName($relative).Replace('\','/')
        if(-not $native.ContainsKey($stem) -or ($parent -and $parent -notmatch '^(?i)Pics/(Hi-res|Lo-res)$')){
            throw "Cannot map HD PNG '$relative' to a game texture. Use an exported artwork name, or the original TGA name (for example SKY01.png or HUD2.page0.png)."
        }
        $pages=$native[$stem].pages
        if($page -ge $pages){throw "HD page number exceeds the original texture: $relative"}
        $kindName=if($page -lt 0){'strip'}else{'atlas'}
    }
    $image=New-Object Drawing.Bitmap($source)
    try{
        $width=$image.Width;$height=$image.Height
        if($width -lt 1 -or $height -lt 1 -or $width -gt 16384 -or $height -gt 16384 -or [long]$width*$height*4 -gt 256MB){throw "Invalid artwork dimensions: $relative"}
        switch($kindName){
            atlas {if($width -ne $height -or $width -lt 256 -or $width -gt 8192 -or ($width -band ($width-1))){throw "HD atlas must be square and power-of-two, 256..8192: $relative"}}
            strip {if($width -ne $height*$pages -or $height -lt 256 -or $height -gt 8192 -or ($height -band ($height-1))){throw "HD strip must retain its $pages original horizontal pages: $relative"}}
            banner {if($width*64 -ne $height*462 -or $height -lt 64 -or $height -gt 2048 -or ($height -band ($height-1))){throw "Joined menu banner must retain its 462:64 proportions and power-of-two height: $relative"}}
        }
    }finally{$image.Dispose()}
    $records.Add([pscustomobject]@{file=$relative;kind=$kindName;width=$width;height=$height})
}
foreach($relative in $inputs.Keys){
    $destination=Get-ModChild $canonical $relative;$source=$inputs[$relative]
    if(-not [string]::Equals([IO.Path]::GetFullPath($source),$destination,[StringComparison]::OrdinalIgnoreCase) -and
        (-not [IO.File]::Exists($destination) -or (Get-ModHash $source) -ne (Get-ModHash $destination))){
        [WoBeta.Files]::Parent($destination);[IO.File]::Copy($source,$destination,$true);++$updated
    }
}
Write-Host ("Discovered and verified $($records.Count) HD PNGs; replacements load directly on game startup.")
if($PassThru){[pscustomobject]@{checked=$records.Count;updated=$updated;ignored=@();files=@($records.ToArray())}}