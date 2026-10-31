param([Parameter(Mandatory=$true)][string]$SdlDirectory,[string]$Compiler='C:/mingw64/bin/gcc.exe',[string]$LibraryDirectory='C:/msys64/mingw64/lib')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$libraries=(Get-Content -Encoding UTF8 -LiteralPath (Join-Path $SdlDirectory 'libs.txt') -Raw).Trim() -split '\s+'
& $Compiler -O2 -s -static-libgcc -mwindows -L $LibraryDirectory -I (Join-Path $SdlDirectory 'include') (Join-Path $root 'src/runtime/controller_host.c') (Join-Path $SdlDirectory 'lib/libSDL3.a') @libraries -o (Join-Path $root 'runtime/wo2097-controller.exe')
if($LASTEXITCODE){throw 'SDL3 controller build failed.'}
$iconv=Join-Path ([IO.Path]::GetDirectoryName($LibraryDirectory)) 'bin/libiconv-2.dll'
if(Test-Path -LiteralPath $iconv){Copy-Item -LiteralPath $iconv -Destination (Join-Path $root 'runtime/libiconv-2.dll') -Force}
