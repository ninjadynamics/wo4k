# Synthetic fixtures cover every exported PNG and generic native/page naming.
. (Join-Path $PSScriptRoot '../scripts/Core.ps1')
$root=$script:ModRoot;$work=Join-Path $root ('.work/hd-assets-'+[guid]::NewGuid().ToString('N'))
try{
    foreach($folder in @('scripts','manifests')){Copy-ModTree (Join-Path $root $folder) (Join-Path $work $folder)}
    $art=Join-Path $work 'assets/hd';$game=Join-Path $work 'wobble';$pics=Join-Path $game 'textures/sd/Pics/Hi-res'
    [IO.Directory]::CreateDirectory($art)|Out-Null;[IO.Directory]::CreateDirectory($pics)|Out-Null
    function Make-Png([string]$Name,[int]$Width=256,[int]$Height=256){
        $path=Join-Path $art $Name;[WoBeta.Files]::Parent($path)
        $bitmap=New-Object Drawing.Bitmap($Width,$Height,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics=[Drawing.Graphics]::FromImage($bitmap)
        try{$graphics.Clear([Drawing.Color]::FromArgb(128,220,140,40));$bitmap.Save($path,[Drawing.Imaging.ImageFormat]::Png)}finally{$graphics.Dispose();$bitmap.Dispose()}
    }
    $catalog=Get-Content -LiteralPath (Join-Path $root 'manifests/hd-textures.json') -Raw -Encoding UTF8|ConvertFrom-Json
    foreach($entry in $catalog){
        # PowerShell 5.1 unrolls the JSON array in foreach's expression.
        foreach($item in $entry){if($item.kind -eq 'reference'){continue};if($item.kind -eq 'banner'){Make-Png $item.file 462 64}elseif($item.kind -eq 'region'){Make-Png $item.file ($item.source_width*2) ($item.source_height*2)}else{Make-Png $item.file}}
    }
    foreach($pair in @(@('HUD2',3),@('SKY01',1))){
        $header=New-Object byte[] 18;$header[2]=2;$header[16]=16
        [BitConverter]::GetBytes([uint16](256*$pair[1])).CopyTo($header,12);[BitConverter]::GetBytes([uint16]256).CopyTo($header,14)
        [IO.File]::WriteAllBytes((Join-Path $pics ($pair[0]+'.TGA')),$header)
    }
    foreach($region in @($catalog|Where-Object kind -eq region|ForEach-Object regions)){
        $path=Join-Path $pics $region.native;$pages=[int]$region.page+1
        if(Test-Path -LiteralPath $path){$existing=[IO.File]::ReadAllBytes($path);$pages=[Math]::Max($pages,[BitConverter]::ToUInt16($existing,12)/256)}
        $header=New-Object byte[] 18;$header[2]=2;$header[16]=16
        [BitConverter]::GetBytes([uint16](256*$pages)).CopyTo($header,12);[BitConverter]::GetBytes([uint16]256).CopyTo($header,14)
        [IO.File]::WriteAllBytes($path,$header)
    }
    Make-Png 'SKY01.png' 333 197;Make-Png 'HUD2.png' 997 111;Make-Png 'HUD2.page1.png' 193 275;Make-Png 'Pics/Hi-res/SKY01.png' 512 512
    $hashes=@{};foreach($file in Get-ChildItem -LiteralPath $art -Recurse -File){$hashes[$file.FullName]=Get-ModHash $file.FullName}
    $result=& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game -PassThru
    $expected=@($catalog|Where-Object kind -ne reference).Count+4
    if($result.checked -ne $expected -or $result.ignored.Count -ne 0){throw 'Every exported/native PNG should be discovered.'}
    foreach($file in $hashes.Keys){if((Get-ModHash $file) -ne $hashes[$file]){throw 'Installer rewrote an authored PNG.'}}
    if([IO.Directory]::Exists((Join-Path $game 'textures/hd'))){throw 'Automatic discovery created duplicate artwork.'}
    $index=Join-Path $game 'textures/hd-regions.bin';$bytes=[IO.File]::ReadAllBytes($index)
    $placements=@($catalog|Where-Object kind -eq region|ForEach-Object regions).Count
    if([Text.Encoding]::ASCII.GetString($bytes,0,8) -ne 'WOHDREG1' -or [BitConverter]::ToUInt32($bytes,8) -ne $placements -or $bytes.Length -ne 12+312*$placements){throw 'Runtime region index did not cover the manifest.'}
    $again=& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game -Incremental -PassThru
    if($again.region_index_updated){throw 'Unchanged texture index was rewritten.'}
    # A filename registered after the executable was built must install through
    # metadata alone, including alpha detection and removal back to native.
    $template=($catalog|Where-Object kind -eq region|Select-Object -First 1)|ConvertTo-Json -Depth 12|ConvertFrom-Json
    $template.file='custom-edit.png'
    @($catalog)+@($template)|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $work 'manifests/hd-textures.json') -Encoding UTF8
    $custom=Join-Path $art $template.file
    $bitmap=New-Object Drawing.Bitmap(317,111,[Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try{$bitmap.Save($custom,[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
    $withCustom=& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game -PassThru
    if($withCustom.region_placements -ne $placements+$template.regions.Count){throw 'New manifest filename was not automatically registered.'}
    $customRecord=@($withCustom.files|Where-Object file -eq 'custom-edit.png')[0]
    if($customRecord.width -ne 317 -or $customRecord.height -ne 111){throw 'Arbitrary user dimensions were not retained.'}
    function Read-CustomAlpha {
        $data=[IO.File]::ReadAllBytes($index)
        for($i=0;$i -lt [BitConverter]::ToUInt32($data,8);$i++){
            $at=12+$i*312;$name=[Text.Encoding]::Unicode.GetString($data,$at,256).Trim([char]0)
            if($name -eq 'custom-edit.png'){return [BitConverter]::ToUInt32($data,$at+308)}
        }
        throw 'Custom texture missing from runtime index.'
    }
    if((Read-CustomAlpha) -ne 0){throw 'Opaque RGB texture lost native black-key handling.'}
    Make-Png 'custom-edit.png' 193 275
    $customHash=Get-ModHash $custom
    & (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game | Out-Null
    if((Get-ModHash $custom) -ne $customHash){throw 'Automatic texture install resized the authored PNG.'}
    if((Read-CustomAlpha) -ne 1){throw 'Edited PNG alpha was not detected on refresh.'}
    [IO.File]::Delete($custom)
    $removed=& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game -PassThru
    if($removed.region_placements -ne $placements){throw 'Removed PNG left stale HD regions.'}
    $indexHash=Get-ModHash $index
    Make-Png 'HUD2.page3.png'
    $failed=$false;try{& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game}catch{$failed=$_.Exception.Message -like '*page number*'}
    if(-not $failed){throw 'Out-of-range native page was accepted.'}
    [IO.File]::Delete((Join-Path $art 'HUD2.page3.png'));Make-Png 'unmapped.png'
    $failed=$false;try{& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game}catch{$failed=$_.Exception.Message -like '*Cannot map*unmapped.png*'}
    if(-not $failed){throw 'Unknown PNG should explain its mapping error.'}
    if((Get-ModHash $index) -ne $indexHash){throw 'Invalid input partially replaced the last valid HD index.'}
    Write-Host 'PASS: filename-only manifest registration without a runtime rebuild, arbitrary dimensions/aspect ratios, unchanged authored PNGs, alpha edits, idempotent refresh, PNG removal and invalid-input atomicity.'
    Write-Host 'PASS: UI aliases, individual world regions (including rectangular/non-power-of-two images), and generic native PNG/strip/page/nested names discovered; source PNG bytes unchanged and no duplicate assets generated.'
}finally{
    $full=[IO.Path]::GetFullPath($work);$prefix=[IO.Path]::GetFullPath((Join-Path $root '.work')).TrimEnd('\')+'\hd-assets-'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe HD test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}
