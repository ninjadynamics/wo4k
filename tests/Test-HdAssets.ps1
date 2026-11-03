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
    foreach($entry in @(Get-Content -LiteralPath (Join-Path $root 'manifests/hd-textures.json') -Raw -Encoding UTF8|ConvertFrom-Json)){
        # PowerShell 5.1 unrolls the JSON array in foreach's expression.
        foreach($item in $entry){if($item.kind -eq 'banner'){Make-Png $item.file 462 64}else{Make-Png $item.file}}
    }
    foreach($pair in @(@('HUD2',3),@('SKY01',1))){
        $header=New-Object byte[] 18;$header[2]=2;$header[16]=16
        [BitConverter]::GetBytes([uint16](256*$pair[1])).CopyTo($header,12);[BitConverter]::GetBytes([uint16]256).CopyTo($header,14)
        [IO.File]::WriteAllBytes((Join-Path $pics ($pair[0]+'.TGA')),$header)
    }
    Make-Png 'SKY01.png';Make-Png 'HUD2.png' 768 256;Make-Png 'HUD2.page1.png' 512 512;Make-Png 'Pics/Hi-res/SKY01.png' 512 512
    $hashes=@{};foreach($file in Get-ChildItem -LiteralPath $art -Recurse -File){$hashes[$file.FullName]=Get-ModHash $file.FullName}
    $result=& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game -PassThru
    if($result.checked -ne 17 -or $result.ignored.Count -ne 0){throw 'Every exported/native PNG should be discovered.'}
    foreach($file in $hashes.Keys){if((Get-ModHash $file) -ne $hashes[$file]){throw 'Installer rewrote an authored PNG.'}}
    if([IO.Directory]::Exists((Join-Path $game 'textures/hd'))){throw 'Automatic discovery created duplicate artwork.'}
    Make-Png 'HUD2.page3.png'
    $failed=$false;try{& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game}catch{$failed=$_.Exception.Message -like '*page number*'}
    if(-not $failed){throw 'Out-of-range native page was accepted.'}
    [IO.File]::Delete((Join-Path $art 'HUD2.page3.png'));Make-Png 'unmapped.png'
    $failed=$false;try{& (Join-Path $work 'scripts/Install-Textures.ps1') -SourcePath $art -GameRoot $game}catch{$failed=$_.Exception.Message -like '*Cannot map*unmapped.png*'}
    if(-not $failed){throw 'Unknown PNG should explain its mapping error.'}
    Write-Host 'PASS: all eleven exported UI files plus notice/title and generic native PNG/strip/page/nested names discovered, unknown/invalid pages explained; source PNG bytes unchanged and no duplicate assets generated.'
}finally{
    $full=[IO.Path]::GetFullPath($work);$prefix=[IO.Path]::GetFullPath((Join-Path $root '.work')).TrimEnd('\')+'\hd-assets-'
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe HD test cleanup.'}
    if([IO.Directory]::Exists($full)){[IO.Directory]::Delete($full,$true)}
}