param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[string]$PsxSource)
. (Join-Path $PSScriptRoot '../scripts/Native-Textures.ps1')
. (Join-Path $PSScriptRoot '../scripts/Discover-Sources.ps1') -OutputFile 'unused'
$root=$script:ModRoot;$work=Join-Path $root ('.work/native-textures-'+[guid]::NewGuid().ToString('N'))
$fixture=Join-Path $work 'wobble'
[IO.Directory]::CreateDirectory($fixture)|Out-Null
Copy-ModTree (Join-Path $GameRoot 'textures/sd') (Join-Path $fixture 'textures/sd')
foreach($wad in Get-ChildItem -LiteralPath $GameRoot -Filter 'TRACK*' -Directory){
    $source=Join-Path $wad.FullName 'TRACK.WAD';if(-not [IO.File]::Exists($source)){continue}
    $to=Join-Path $fixture ($wad.Name+'/TRACK.WAD');[WoBeta.Files]::Parent($to);[IO.File]::Copy($source,$to)
}
$before=Get-ModHash (Join-Path $GameRoot 'textures/sd/Pics/Hi-res/NRACE01.TGA')
$result=Install-ModNativeTextures -GameRoot $fixture
if($result.pc_regions -lt 222 -or $result.ps1_regions -ne 0 -or $result.pending_ps1_regions -lt 1){throw 'PC-only selection did not recover roads and leave disc-only regions pending.'}
$sheet=Join-Path $fixture 'textures/sd/Pics/Hi-res/NRACE01.TGA'
$original=Join-Path $fixture 'textures/native-source/Pics/Hi-res/NRACE01.TGA'
if((Get-ModHash $sheet) -eq (Get-ModHash $original)){throw 'Recovered road colours did not reach the packed texture sheet.'}
$plan=Get-Content -LiteralPath (Join-Path $root 'manifests/texture-selection.json') -Raw -Encoding UTF8|ConvertFrom-Json
$decoder=New-Object WoBeta.TextureSources($fixture,(Join-Path $fixture 'textures/ps1-source'))
$road=$decoder.Road('TRACK01/TRACK.WAD','library.cmp','library.ttf',2,128)
$pixels=[IO.File]::ReadAllBytes($sheet);$width=[BitConverter]::ToUInt16($pixels,12)
for($y=0;$y -lt 128;$y++){for($x=0;$x -lt 128;$x++){
    $expected=$road.Pixels[$y*128+$x];$actual=[BitConverter]::ToUInt16($pixels,18+($y*$width+128+$x)*2)
    $expected=(($expected -band 31) -shl 10) -bor ($expected -band 33760) -bor (($expected -shr 10) -band 31)
    if($actual -ne $expected){throw "Recovered PS1 road pixel differs at $x,$y"}
}}
$time=(Get-Item -LiteralPath $sheet).LastWriteTimeUtc
$cached=Install-ModNativeTextures -GameRoot $fixture -Incremental
if((Get-Item -LiteralPath $sheet).LastWriteTimeUtc -ne $time){throw 'Unchanged native assets were rewritten on launch.'}
$once=Get-ModHash $sheet;$null=Install-ModNativeTextures -GameRoot $fixture
if((Get-ModHash $sheet) -ne $once){throw 'Repeated rebuild patches an already-patched sheet.'}
$invalid=$false;try{[WoBeta.TextureSources]::Cmp([byte[]](1,0,0,0,255,255,255,127))|Out-Null}catch{$invalid=$true}
if(-not $invalid){throw 'Oversized CMP declaration was accepted.'}
$invalid=$false;try{[WoBeta.TextureSources]::PatchTga($pixels,$road,$width-1,0,128,128)}catch{$invalid=$true}
if(-not $invalid){throw 'Out-of-bounds texture placement was accepted.'}
if($PsxSource){
    $info=Get-ModSourceInfo $PsxSource
    if(-not $info.PsxData){throw 'PlayStation data image was not discovered.'}
    $result=Install-ModNativeTextures -GameRoot $fixture -PsxSource $PsxSource -WorkRoot (Join-Path $work 'disc')
    if(-not $result.ps1_textures -or $result.ps1_regions -lt 1 -or $result.pending_ps1_regions -ne 0){throw 'Optional disc artwork did not fill every mapped PS1 region.'}
    if([IO.File]::Exists((Join-Path $fixture 'textures/ps1-source/TEXTURES/EXPLODE.CMP'))){throw 'Byte-identical explosion archive was redundantly extracted.'}
    foreach($op in @($plan.operations|Where-Object {$_.source.PSObject.Properties['ps1']})){
        $data=[IO.File]::ReadAllBytes((Join-Path $fixture ('textures/sd/'+$op.atlas)))
        $image=$decoder.Frame($op.source.ps1,'','',$true,[int]$op.source.frame)
        # Read a fresh decoder after the disc cache has been populated.
        $at=18+[int]$data[0]+(([int]$op.y*[BitConverter]::ToUInt16($data,12)+[int]$op.x)*2)
        $c=$image.Pixels[0];$expected=(($c -band 31) -shl 10) -bor ($c -band 33760) -bor (($c -shr 10) -band 31)
        if(([BitConverter]::ToUInt16($data,$at) -band 32767) -ne ($expected -band 32767)){throw ('PS1 source did not reach native atlas: '+$op.label)}
    }
    $first=Get-ModHash $sheet;$null=Install-ModNativeTextures -GameRoot $fixture
    if((Get-ModHash $sheet) -ne $first){throw 'Disc artwork was lost during a source-free rebuild.'}
    $disc=Find-ModPs1Disc $PsxSource
    $dataOnly=Join-Path $work 'ps1-data-only.bin';[IO.File]::Copy($disc.Image,$dataOnly)
    $info=Get-ModSourceInfo $dataOnly
    if(-not $info.PsxData -or $info.PsxMusic){throw 'Data-only PS1 artwork incorrectly enabled CD audio.'}
    $music=& (Join-Path $root 'scripts/Extract-Music.ps1') -SourcePath $dataOnly -Destination (Join-Path $work 'no-audio') -Bank Psx -AllowMissing
    if($music){throw 'Data-only PS1 image invented a music bank.'}
    $imported=Install-ModNativeTextures -GameRoot $fixture -PsxSource $dataOnly -WorkRoot (Join-Path $work 'data-only')
    if(-not $imported.ps1_textures -or $imported.pending_ps1_regions -ne 0){throw 'Data-only PS1 image could not supply artwork.'}
}
if((Get-ModHash (Join-Path $GameRoot 'textures/sd/Pics/Hi-res/NRACE01.TGA')) -ne $before){throw 'Test changed the actual game installation.'}
Write-Host ('PASS: fixed PC/PS1 selection, recovered 128x128 road pixels, both native resolutions, source retention, incremental launch, repeat rebuild and malformed-input bounds. Fixture: '+$fixture)
Write-Output $fixture
