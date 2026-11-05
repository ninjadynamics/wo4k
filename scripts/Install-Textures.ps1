param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$GameRoot,[ValidateSet('Hd','Sd')][string]$Kind='Hd',[switch]$Incremental,[switch]$PassThru)
. (Join-Path $PSScriptRoot 'Core.ps1')
if($Kind -eq 'Sd'){Copy-ModTree $SourcePath (Get-ModChild $GameRoot 'textures/sd');return}
if(-not ('WoBeta.HdRegionIndex' -as [type])){Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot 'HdRegionIndex.cs')}
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
$regionIndex=New-Object WoBeta.HdRegionIndex
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
    if($kindName -eq 'reference'){throw "HD image '$relative' is a reference screenshot, not a game texture. $($entry[0].reason)"}
    $image=New-Object Drawing.Bitmap($source)
    try{
        $width=$image.Width;$height=$image.Height
        if($width -lt 1 -or $height -lt 1 -or $width -gt 16384 -or $height -gt 16384 -or [long]$width*$height*4 -gt 256MB){throw "Invalid artwork dimensions: $relative"}
        switch($kindName){
            region {
                if($entry[0].file -notmatch '^[a-z0-9-]+\.png$' -or -not @($entry[0].regions).Count){throw "HD texture has no valid region mapping: $relative"}
                $sw=[int]$entry[0].source_width;$sh=[int]$entry[0].source_height
                if($sw -lt 1 -or $sh -lt 1){throw "Invalid source rectangle in the HD texture manifest: $relative"}
                $alphaMode=if($entry[0].PSObject.Properties['alpha_mode']){[string]$entry[0].alpha_mode}else{'auto'}
                if($alphaMode -notin 'auto','png','black-key'){throw "Unknown HD transparency mode for: $relative"}
                $alpha=if($alphaMode -eq 'png'){$true}elseif($alphaMode -eq 'black-key'){$false}else{[WoBeta.HdRegionIndex]::HasTransparency($image)}
                foreach($region in $entry[0].regions){
                    $stem=[IO.Path]::GetFileNameWithoutExtension($region.native)
                    if(-not $native.ContainsKey($stem) -or $region.page -ge $native[$stem].pages -or
                        $region.width -ne $sw -or $region.height -ne $sh -or $region.x -lt 0 -or $region.y -lt 0 -or
                        $region.x+$sw -gt 256 -or $region.y+$sh -gt 256){throw "HD region placement is invalid: $relative"}
                    $regionIndex.Add($relative,[string]$region.native,[int]$region.page,[int]$region.x,[int]$region.y,$sw,$sh,$alpha)
                }
            }
            # Source resolution/aspect ratio never selects a texture or rejects
            # a valid image. GPU page storage is managed at load time.
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
$indexChanged=$regionIndex.Save((Get-ModChild $GameRoot 'textures/hd-regions.bin'))
if($indexChanged){++$updated}
Write-Host ("Discovered and verified $($records.Count) HD PNGs; replacements load directly on game startup.")
if($PassThru){[pscustomobject]@{checked=$records.Count;updated=$updated;region_placements=$regionIndex.Count;region_index_updated=$indexChanged;ignored=@();files=@($records.ToArray())}}
