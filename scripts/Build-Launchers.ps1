param([string]$Compiler='C:/mingw64/bin/gcc.exe',[string]$ResourceCompiler='')
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $ResourceCompiler){$ResourceCompiler=Join-Path (Split-Path $Compiler) 'windres.exe'}
Add-Type -AssemblyName System.Drawing
$work=Join-Path $root '.work/build-launchers';[IO.Directory]::CreateDirectory($work)|Out-Null
# PNG-backed frames retain the supplied transparent artwork at Windows shell sizes.
function Convert-LauncherIcon([string]$Source,[string]$Destination) {
    $frames=New-Object Collections.Generic.List[byte[]];$sizes=@(16,24,32,48,64,128,256)
    $image=[Drawing.Image]::FromFile($Source)
    try{foreach($size in $sizes){
        $bitmap=New-Object Drawing.Bitmap($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics=[Drawing.Graphics]::FromImage($bitmap);$stream=New-Object IO.MemoryStream
        try{
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($image,(New-Object Drawing.Rectangle(0,0,$size,$size)))
            $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png);$frames.Add($stream.ToArray())
        }finally{$graphics.Dispose();$bitmap.Dispose();$stream.Dispose()}
    }}finally{$image.Dispose()}
    $stream=[IO.File]::Create($Destination);$writer=New-Object IO.BinaryWriter($stream)
    try{
        $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$sizes.Count)
        $offset=6+16*$sizes.Count
        for($i=0;$i -lt $sizes.Count;$i++){
            $dimension=if($sizes[$i] -eq 256){0}else{$sizes[$i]}
            $writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([uint16]0)
            $writer.Write([uint16]1);$writer.Write([uint16]32);$writer.Write([uint32]$frames[$i].Length);$writer.Write([uint32]$offset)
            $offset+=$frames[$i].Length
        }
        foreach($frame in $frames){$writer.Write([byte[]]$frame)}
    }finally{$writer.Dispose();$stream.Dispose()}
}
foreach($name in @('Setup','Play')) {
    $icon=Join-Path $root ('assets/icons/'+$name.ToLowerInvariant()+'.ico')
    Convert-LauncherIcon (Join-Path $root ('assets/icons/'+$name.ToLowerInvariant()+'.png')) $icon
    $resource=Join-Path $work ($name+'.rc');$object=Join-Path $work ($name+'.o')
    [IO.File]::WriteAllText($resource,('1 ICON "'+$icon.Replace('\','/')+'"'),[Text.Encoding]::ASCII)
    & $ResourceCompiler -i $resource -o $object -O coff
    if($LASTEXITCODE){throw "Launcher icon compilation failed: $name"}
    $defines=@();if($name -eq 'Play'){$defines=@('-DWO_PLAY')}
    & $Compiler -O2 -s -static -municode -mwindows @defines (Join-Path $root 'src/launcher.c') $object -o (Join-Path $root ($name+'.exe'))
    if($LASTEXITCODE){throw "Launcher build failed: $name"}
}