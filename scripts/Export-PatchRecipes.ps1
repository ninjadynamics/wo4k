# Maintainer helper: turn locally authored binary differences into reviewable text.
. (Join-Path $PSScriptRoot 'Core.ps1')
$destination=Join-Path $script:ModRoot 'src/patches';[IO.Directory]::CreateDirectory($destination)|Out-Null
foreach($file in Get-ChildItem -LiteralPath (Join-Path $script:ModRoot 'patches') -File -Filter '*.wodiff'){
    $stream=$file.OpenRead();$gzip=New-Object IO.Compression.GZipStream($stream,[IO.Compression.CompressionMode]::Decompress);$reader=New-Object IO.BinaryReader($gzip)
    try{
        if([Text.Encoding]::ASCII.GetString($reader.ReadBytes(8)) -ne "WODIFF1`0"){throw 'Unknown binary patch format.'}
        $recipe=[ordered]@{format=1;source_length=$reader.ReadInt32();target_length=$reader.ReadInt32();source_sha256=[Text.Encoding]::ASCII.GetString($reader.ReadBytes(64));target_sha256=[Text.Encoding]::ASCII.GetString($reader.ReadBytes(64));ranges=@()}
        while(($offset=$reader.ReadInt32()) -ne -1){
            $length=$reader.ReadInt32();if($length -le 0 -or $length -gt $recipe.target_length){throw 'Invalid binary patch range.'}
            $bytes=$reader.ReadBytes($length);if($bytes.Length -ne $length){throw 'Truncated binary patch.'}
            $hex=[BitConverter]::ToString($bytes).Replace('-','').ToLowerInvariant();$chunks=@()
            for($i=0;$i -lt $hex.Length;$i+=256){$chunks+=$hex.Substring($i,[Math]::Min(256,$hex.Length-$i))}
            $recipe.ranges+=@{offset=$offset;xor_hex=$chunks}
        }
        Write-ModJson (Join-Path $destination ($file.BaseName+'.json')) $recipe
    }finally{$reader.Dispose();$gzip.Dispose();$stream.Dispose()}
}
