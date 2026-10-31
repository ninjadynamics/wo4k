. (Join-Path $PSScriptRoot 'Core.ps1')
foreach($file in Get-ChildItem -LiteralPath (Join-Path $script:ModRoot 'src/patches') -File -Filter '*.json'){
    $recipe=Get-Content -LiteralPath $file.FullName -Encoding UTF8 -Raw|ConvertFrom-Json
    if($recipe.format -ne 1 -or $recipe.source_sha256 -notmatch '^[a-f0-9]{64}$' -or $recipe.target_sha256 -notmatch '^[a-f0-9]{64}$'){throw 'Invalid patch recipe.'}
    $target=Join-Path $script:ModRoot ('patches/'+$file.BaseName+'.wodiff');[WoBeta.Files]::Parent($target)
    $stream=[IO.File]::Create($target);$gzip=New-Object IO.Compression.GZipStream($stream,[IO.Compression.CompressionMode]::Compress);$writer=New-Object IO.BinaryWriter($gzip)
    try{
        $writer.Write([Text.Encoding]::ASCII.GetBytes("WODIFF1`0"));$writer.Write([int]$recipe.source_length);$writer.Write([int]$recipe.target_length)
        $writer.Write([Text.Encoding]::ASCII.GetBytes($recipe.source_sha256));$writer.Write([Text.Encoding]::ASCII.GetBytes($recipe.target_sha256))
        $end=0
        foreach($range in $recipe.ranges){
            $hex=$range.xor_hex -join ''
            if($hex -notmatch '^(?:[a-f0-9]{2})+$' -or $range.offset -lt $end -or $range.offset+$hex.Length/2 -gt $recipe.target_length){throw 'Invalid patch range.'}
            $bytes=New-Object byte[] ($hex.Length/2);for($i=0;$i -lt $bytes.Length;$i++){$bytes[$i]=[Convert]::ToByte($hex.Substring($i*2,2),16)}
            $writer.Write([int]$range.offset);$writer.Write([int]$bytes.Length);$writer.Write($bytes);$end=$range.offset+$bytes.Length
        }
        $writer.Write([int]-1)
    }finally{$writer.Dispose();$gzip.Dispose();$stream.Dispose()}
    Write-Host "Generated $($file.BaseName).wodiff from text patch source."
}
