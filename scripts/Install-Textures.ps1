param([Parameter(Mandatory=$true)][string]$SourcePath,[Parameter(Mandatory=$true)][string]$GameRoot,[ValidateSet('Hd','Sd')][string]$Kind='Hd',[switch]$Incremental,[switch]$PassThru)
. (Join-Path $PSScriptRoot 'Core.ps1')
$destination=Get-ModChild $GameRoot ('textures/'+$Kind.ToLowerInvariant())
if($Kind -eq 'Sd'){Copy-ModTree $SourcePath $destination;return}
$stateFile=Get-ModChild $destination 'install-state.json';$previous=@{};$next=New-Object Collections.Generic.List[object];$updated=0
if($Incremental -and [IO.File]::Exists($stateFile)){try{$state=Get-Content -LiteralPath $stateFile -Encoding UTF8 -Raw|ConvertFrom-Json;foreach($entry in $state.files){if($entry.PSObject.Properties['source_sha256'] -and $entry.PSObject.Properties['packer'] -and $entry.PSObject.Properties['output_sha256']){$previous[$entry.name]=$entry}}}catch{$previous=@{}}}
$packer=Get-ModHash (Join-Path $PSScriptRoot 'Core.ps1')
foreach($name in @('font-and-numbers','hud-gauges','notice','title')) {
    $source=Join-Path $SourcePath ($name+'.png');if(-not [IO.File]::Exists($source)){continue}
    $hash=Get-ModHash $source;$png=Get-ModChild $destination ($name+'.png')
    $output=Get-ModChild $destination ($name+$(if($name -in 'notice','title'){'.bmp'}else{'.rgba'}))
    $old=$previous[$name]
    if($Incremental -and $old -and $old.source_sha256 -eq $hash -and $old.packer -eq $packer -and [IO.File]::Exists($png) -and [IO.File]::Exists($output) -and (Get-ModHash $png) -eq $hash -and (Get-ModHash $output) -eq $old.output_sha256){$next.Add($old);continue}
    [WoBeta.Files]::Parent($png)
    if(-not [string]::Equals([IO.Path]::GetFullPath($source),$png,[StringComparison]::OrdinalIgnoreCase)){[IO.File]::Copy($source,$png,$true)}
    if($name -in 'notice','title'){[WoBeta.Files]::PackArt($source,$output)}
    else{[WoBeta.Files]::PackUi($source,$output)}
    $next.Add([pscustomobject]@{name=$name;source_sha256=$hash;packer=$packer;output_sha256=(Get-ModHash $output)});++$updated
}
Write-ModJson $stateFile @{files=@($next.ToArray())}
Write-Host "Verified HD artwork; refreshed $updated images."
if($PassThru){[pscustomobject]@{checked=$next.Count;updated=$updated}}
