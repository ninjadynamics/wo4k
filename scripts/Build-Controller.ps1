param([Parameter(Mandatory=$true)][string]$SdlDirectory,[string]$Compiler='C:/mingw64/bin/gcc.exe',[string]$LibraryDirectory='C:/msys64/mingw64/lib')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$legacy=Join-Path $SdlDirectory 'libs.txt'
if(Test-Path -LiteralPath $legacy){$libraries=([IO.File]::ReadAllText($legacy)).Trim() -split '\s+'}
else{
    $metadata=[IO.File]::ReadAllText((Join-Path $SdlDirectory 'lib/pkgconfig/sdl3.pc'))
    if($metadata -notmatch '(?m)^Libs.private:\s*(.+)$'){throw 'SDL3 static link flags are missing.'}
    $libraries=$Matches[1].Trim() -split '\s+'
}
& $Compiler -O2 -s -static-libgcc -mwindows -L $LibraryDirectory -I (Join-Path $SdlDirectory 'include') (Join-Path $root 'src/runtime/controller_host.c') (Join-Path $SdlDirectory 'lib/libSDL3.a') @libraries -o (Join-Path $root 'runtime/wo2097-controller.exe')
if($LASTEXITCODE){throw 'SDL3 controller build failed.'}
$iconv=Join-Path ([IO.Path]::GetDirectoryName($LibraryDirectory)) 'bin/libiconv-2.dll'
if(Test-Path -LiteralPath $iconv){Copy-Item -LiteralPath $iconv -Destination (Join-Path $root 'runtime/libiconv-2.dll') -Force}
