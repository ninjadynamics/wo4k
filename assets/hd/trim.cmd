@echo off
setlocal
set "WO4K_TRIM_SCRIPT=%~f0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$s=[IO.File]::ReadAllText($env:WO4K_TRIM_SCRIPT); & ([ScriptBlock]::Create($s.Substring($s.LastIndexOf('# POWERSHELL START'))))"
set "trim_exit=%errorlevel%"
if /i not "%~1"=="--no-pause" pause
exit /b %trim_exit%
# POWERSHELL START
# Double-click to optimize PNGs beside this script. Requires ffmpeg on PATH.
# Pixels, alpha, bit depth and color-profile chunks are preserved; editing
# metadata (including Photoshop/XMP data) is discarded. Originals are replaced
# only after pixel verification, and only if the result is smaller.
$ErrorActionPreference='Stop'
try {
    $ffmpeg=(Get-Command ffmpeg.exe -CommandType Application -ErrorAction Stop).Source
    $folder=[IO.Path]::GetDirectoryName($env:WO4K_TRIM_SCRIPT)
    $files=@(Get-ChildItem -LiteralPath $folder -Filter '*.png' -File)
    $saved=[long]0; $changed=0; $failed=0

    function Get-PixelHash([string]$path) {
        # Normalize both decodes to lossless 16-bit RGBA, including hidden RGB.
        $result=@(& $ffmpeg -hide_banner -loglevel error -nostdin -i $path -map 0:v:0 -frames:v 1 -pix_fmt rgba64be -c:v rawvideo -f hash -hash sha256 -)
        if($LASTEXITCODE -ne 0 -or $result.Count -ne 1 -or $result[0] -notmatch '^SHA256=') { throw 'Pixel verification failed.' }
        return $result[0]
    }

    function Get-PngChunks([byte[]]$bytes) {
        $offset=8
        while($offset -lt $bytes.Length) {
            if($offset+12 -gt $bytes.Length) { throw 'Truncated PNG chunk.' }
            $length=([long]$bytes[$offset]*16777216)+([long]$bytes[$offset+1]*65536)+([long]$bytes[$offset+2]*256)+$bytes[$offset+3]
            $size=$length+12
            if($offset+$size -gt $bytes.Length) { throw 'Invalid PNG chunk length.' }
            [pscustomobject]@{Name=[Text.Encoding]::ASCII.GetString($bytes,$offset+4,4);Offset=$offset;Size=[int]$size}
            $offset+=[int]$size
        }
    }

    function Restore-ColorChunks([byte[]]$source,[string]$target) {
        # FFmpeg can drop ICC/gamma information. Copy original color chunks
        # verbatim, including their CRCs, before the encoded pixel data.
        $colorNames=@('cHRM','gAMA','iCCP','sBIT','sRGB','cICP','mDCv','cLLi')
        $colors=@(Get-PngChunks $source | Where-Object { $_.Name -in $colorNames })
        $encoded=[IO.File]::ReadAllBytes($target)
        $stream=New-Object IO.MemoryStream
        try {
            $stream.Write($encoded,0,8)
            foreach($chunk in @(Get-PngChunks $encoded)) {
                if($chunk.Name -in $colorNames) { continue }
                $stream.Write($encoded,$chunk.Offset,$chunk.Size)
                if($chunk.Name -eq 'IHDR') {
                    foreach($color in $colors) { $stream.Write($source,$color.Offset,$color.Size) }
                }
            }
            [IO.File]::WriteAllBytes($target,$stream.ToArray())
        } finally { $stream.Dispose() }
    }

    foreach($file in $files) {
        $temp=Join-Path $folder ('.trim-'+[Guid]::NewGuid().ToString('N')+'.tmp')
        try {
            $original=[IO.File]::ReadAllBytes($file.FullName)
            $chunks=@(Get-PngChunks $original)
            if(@($chunks | Where-Object Name -eq 'acTL').Count) { throw 'Animated PNG skipped to preserve all frames.' }
            $hash=Get-PixelHash $file.FullName
            & $ffmpeg -hide_banner -loglevel error -nostdin -n -i $file.FullName -map 0:v:0 -map_metadata -1 -frames:v 1 -c:v png -compression_level 9 -pred mixed -f image2 -update 1 $temp
            if($LASTEXITCODE -ne 0 -or -not [IO.File]::Exists($temp)) { throw 'FFmpeg encoding failed.' }
            # Check the PNG bit depth as well as decoded RGBA values.
            $encoded=[IO.File]::ReadAllBytes($temp)
            if($original[24] -ne $encoded[24]) { throw 'Bit depth changed; original retained.' }
            Restore-ColorChunks $original $temp
            if((Get-PixelHash $temp) -ne $hash) { throw 'Pixels changed; original retained.' }
            $newLength=(Get-Item -LiteralPath $temp).Length
            if($newLength -ge $original.Length) {
                Write-Host ('Unchanged: '+$file.Name)
                continue
            }
            # Do not overwrite an image saved by Photoshop during compression.
            $current=[IO.File]::ReadAllBytes($file.FullName)
            $sha=[Security.Cryptography.SHA256]::Create()
            try {
                if([Convert]::ToBase64String($sha.ComputeHash($current)) -ne [Convert]::ToBase64String($sha.ComputeHash($original))) { throw 'File was edited during compression; original retained.' }
            } finally { $sha.Dispose() }
            [IO.File]::Replace($temp,$file.FullName,[NullString]::Value)
            $saved+=$original.Length-$newLength; $changed++
            Write-Host ('Trimmed: {0} ({1:N0} -> {2:N0} bytes)' -f $file.Name,$original.Length,$newLength)
        } catch {
            $failed++
            Write-Host ('Skipped: {0}: {1}' -f $file.Name,$_.Exception.Message) -ForegroundColor Yellow
        } finally {
            if([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
        }
    }
    Write-Host ('Done. {0}/{1} PNGs smaller; {2:N2} MiB saved; {3} failed/skipped.' -f $changed,$files.Count,($saved/1MB),$failed)
    if($failed) { exit 1 }
    exit 0
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host 'Install FFmpeg and add it to PATH if it was not found.'
    exit 1
}
