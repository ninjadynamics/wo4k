param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$GameRoot,[ValidateSet('Hd','Sd')][string]$Kind='Hd',[switch]$Incremental,[switch]$PassThru)
. (Join-Path $PSScriptRoot 'Core.ps1')
if($Kind -eq 'Sd'){Copy-ModTree $SourcePath (Get-ModChild $GameRoot 'textures/sd');return}
$checked=0;$updated=0;$inputs=@{};$canonical=Join-Path $script:ModRoot 'assets/hd'
foreach($name in @('font-and-numbers','hud-gauges','notice','title')){
    $source=Join-Path $SourcePath ($name+'.png')
    if(-not [IO.File]::Exists($source)){$source=Join-Path $canonical ($name+'.png')}
    if(-not [IO.File]::Exists($source)){throw "Missing HD artwork: $name.png"}
    $inputs[$name]=[IO.Path]::GetFullPath($source)
    $image=New-Object Drawing.Bitmap($source)
    try{
        if($image.Width -le 0 -or $image.Height -le 0 -or $image.Width -gt 16384 -or $image.Height -gt 16384){throw "Invalid artwork dimensions: $name"}
        if($name -in 'font-and-numbers','hud-gauges' -and ($image.Width -ne $image.Height -or $image.Width -lt 256 -or $image.Width -gt 8192 -or ($image.Width -band ($image.Width-1)))){throw "Invalid UI atlas dimensions: $name"}
    }finally{$image.Dispose()}
    ++$checked
}
foreach($name in $inputs.Keys){
    $destination=Get-ModChild $canonical ($name+'.png');$source=$inputs[$name]
    if(-not [string]::Equals($source,$destination,[StringComparison]::OrdinalIgnoreCase) -and
        (-not [IO.File]::Exists($destination) -or (Get-ModHash $source) -ne (Get-ModHash $destination))){
        [WoBeta.Files]::Parent($destination);[IO.File]::Copy($source,$destination,$true);++$updated
    }
}
Write-Host "Verified $checked HD PNGs; the renderer reads assets/hd directly."
if($PassThru){[pscustomobject]@{checked=$checked;updated=$updated}}
